/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* KeShip 浮力-水动力合并插件实现 — 统一 PreUpdate 计算并一次性写入 ECM */

#include "KSPlugin_HydroBuoyancy.hh"

#include <gz/sim/Model.hh>
#include <gz/sim/components/Pose.hh>
#include <gz/sim/components/Geometry.hh>
#include <gz/sim/components/ExternalWorldWrenchCmd.hh>
#include <gz/sim/components/AngularVelocity.hh>
#include <gz/sim/components/LinearVelocity.hh>
#include <gz/sim/components/Inertial.hh>
#include <gz/plugin/Register.hh>
#include <gz/common/Util.hh>

#include "../utils/Buoyancy.hh"
#include "../utils/ForceAccumulator.hh"
#include "../utils/FossenModel.hh"
#include "components/EnvFlow.hh"
#include "../utils/DiagnosticLog.hh"
#include "../utils/MeshUtils.hh"
#include "../utils/CylinderMeshGenerator.hh"
#include "../utils/PrimitiveMeshGenerator.hh"
#include "../utils/WaterAreaSerializer.hh"
#include "keship/utils/KeshipPaths.hh"
#include <gz/msgs/double.pb.h>
#include <gz/sim/components/Name.hh>
#include <cmath>
#include <fstream>

using namespace keship;

/////////////////////////////////////////////////
KSPlugin_HydroBuoyancy::KSPlugin_HydroBuoyancy()
    : System(), dataPtr(gz::utils::MakeUniqueImpl<Implementation>())
{
  gzmsg << "KSPlugin_HydroBuoyancy: 初始化" << std::endl;
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::Configure(
    const gz::sim::Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    gz::sim::EntityComponentManager &_ecm,
    gz::sim::EventManager &_eventMgr)
{
  gzmsg << "KSPlugin_HydroBuoyancy: 配置实体 " << _entity << std::endl;

  DiagnosticLog::init(keship::KeshipLogDir());

  this->entity_ = _entity;

  /// 模型实例名（日志文件名前缀，多模型避免交错写入）
  {
    auto nameComp = _ecm.Component<gz::sim::components::Name>(_entity);
    if (nameComp)
      this->modelName_ = nameComp->Data();
  }

  this->LoadParameters(_sdf);

  /// 日志前缀 "模型名_link名"（linkName_ 已由 LoadParameters 解析）
  this->logPrefix_ = this->modelName_.empty()
      ? this->linkName_
      : this->modelName_ + "_" + this->linkName_;

  /// 浸水体积发布（opt-in）：仅当 SDF 显式写 <publish_subvolume>true</publish_subvolume>
  /// 才建话题并发布。默认关闭 —— 其他模型零开销、行为与原先完全一致。
  {
    auto [pv, pvOk] = _sdf->Get<bool>("publish_subvolume", false);
    if (pvOk && pv)
    {
      this->dataPtr->publishSubVol_ = true;
      this->dataPtr->subVolTopic_ = "/keship/sp01/subvol/" + this->modelName_;
      this->dataPtr->subVolPub_ = this->dataPtr->node.Advertise<gz::msgs::Double>(
          this->dataPtr->subVolTopic_);
      gzmsg << "KSPlugin_HydroBuoyancy: 浸水体积发布已开启, topic="
            << this->dataPtr->subVolTopic_ << std::endl;
    }
  }

  /// 加载水区配置（波浪场引用按水区从 ECM 实体获取，不订阅 topic）
  {
    std::string path = keship::KeshipWaterAreasFile();
    std::ifstream f(path);
    if (f.is_open())
    {
      std::string content((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
      auto areas = DeserializeWaterAreas(content);
      if (!areas.empty())
      {
        for (auto &a : areas)
          this->dataPtr->waterAreaMgr_.AddArea(std::move(a));
        this->dataPtr->waterAreasConfigured_ = true;
        gzmsg << "KSPlugin_HydroBuoyancy: 从文件加载水域 — "
              << this->dataPtr->waterAreaMgr_.Count() << " 片水区" << std::endl;
      }
      try {
        this->dataPtr->lastWaterAreasMtime_ =
            std::filesystem::last_write_time(path);
      } catch (...) {}
    }
  }

  /// 解析 link
  auto model = gz::sim::Model(_entity);
  if (!model.Valid(_ecm))
  {
    gzerr << "KSPlugin_HydroBuoyancy: 无效的模型实体" << std::endl;
    return;
  }

  this->linkEntity_ = model.LinkByName(_ecm, this->linkName_);
  if (this->linkEntity_ == gz::sim::kNullEntity)
  {
    gzerr << "KSPlugin_HydroBuoyancy: 找不到 link '" << this->linkName_ << "'" << std::endl;
    return;
  }
  this->linkObj_ = gz::sim::Link(this->linkEntity_);
  this->linkObj_.EnableVelocityChecks(_ecm, true);

  /// 初始化 Fossen 模型（如已配置）
  if (this->enableHydro_)
  {
    this->dataPtr->fossen = std::make_unique<FossenModel>();



    /// 读取阻尼
    if (_sdf->HasElement("damping"))
    {
      auto sdfPtr = const_cast<sdf::Element *>(_sdf.get());
      auto damp = sdfPtr->GetElement("damping");
      std::vector<DampingTerm> terms;
      for (auto te = damp->GetElement("term"); te;
           te = te->GetNextElement("term"))
      {
        DampingTerm t;
        auto [tv, tok] = te->Get<int>("target", 0);
        t.target = tok ? tv : 0;
        {
          auto [cv, cok] = te->Get<double>("coefficient", 0.0);
          if (cok) t.coefficient = cv;
        }
        for (auto fe = te->GetElement("factor"); fe;
             fe = fe->GetNextElement("factor"))
        {
          auto [dv, dok] = fe->Get<int>("dof", 0);
          auto [ov, ook] = fe->Get<int>("order", 1);
          t.factors.push_back({dok ? dv : 0, ook ? ov : 1});
        }
        terms.push_back(t);
      }
      this->dataPtr->fossen->setDampingTerms(terms);
    }
    else if (_sdf->HasElement("linear_damping"))
    {
      auto sdfPtr = const_cast<sdf::Element *>(_sdf.get());
      auto ld = sdfPtr->GetElement("linear_damping");
      std::array<double, 8> coeffs{};
      auto get = [&](const char *_key, int _idx) {
        auto [v, ok] = ld->Get<double>(_key, 0.0);
        if (ok) coeffs[_idx] = v;
      };
      get("Xu",0); get("Yv",1); get("Zw",2); get("Kp",3);
      get("Mq",4); get("Nr",5); get("Yr",6); get("Nv",7);
      this->dataPtr->fossen->setDampingCoeffs(coeffs);
    }


  }

  /// 分流：多浮力体 vs 单网格
  if (_sdf->HasElement("buoyancy"))
    this->LoadMultiBuoyancy(_sdf, _ecm);
  else
    this->LoadSingleMeshBuoyancy(_ecm);

  gzmsg << "KSPlugin_HydroBuoyancy: 配置完成 — "
        << "buoyancy=" << (enableBuoyancy_?"on":"off")
        << ", hydro=" << (enableHydro_?"on":"off")
        << ", density=" << this->dataPtr->fluidDensity_
        << ", fluidLevel=" << this->fluidLevel_
        << ", bodies=" << this->dataPtr->bodies.size()
        << std::endl;
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::PreUpdate(
    const gz::sim::UpdateInfo &_info,
    gz::sim::EntityComponentManager &_ecm)
{
  if (_info.paused)
    return;
  if (this->linkEntity_ == gz::sim::kNullEntity)
    return;
  if (!this->enableBuoyancy_ && !this->enableHydro_)
    return;
  if (this->enableHydro_
      && this->dataPtr->dampingMode_ != "pose_based"
      && (!this->dataPtr->fossen || !this->dataPtr->fossen->valid()))
    return;

  /// 1. 读取模型和 link 的世界位姿
  auto poseComp = _ecm.Component<gz::sim::components::Pose>(this->entity_);
  if (!poseComp)
    return;

  gz::math::Pose3d modelPose = poseComp->Data();
  auto linkPoseOpt = this->linkObj_.WorldPose(_ecm);
  if (!linkPoseOpt)
    return;
  gz::math::Pose3d linkWorldPose = *linkPoseOpt;
  gz::math::Vector3d linkWorldPos = linkWorldPose.Pos();

  /// 2. 读取总质量
  double totalMass = 0.0;
  auto inertialComp = _ecm.Component<gz::sim::components::Inertial>(this->linkEntity_);
  if (inertialComp)
    totalMass = inertialComp->Data().MassMatrix().Mass();

  /// 3. 水区热更新
  if (++this->dataPtr->waterAreaReloadCounter_ >= 250)
  {
    this->dataPtr->waterAreaReloadCounter_ = 0;
    try
    {
      auto mtime = std::filesystem::last_write_time(
          keship::KeshipWaterAreasFile());
      if (mtime != this->dataPtr->lastWaterAreasMtime_)
      {
        this->dataPtr->lastWaterAreasMtime_ = mtime;
        std::ifstream f(keship::KeshipWaterAreasFile());
        if (f.is_open())
        {
          std::string content((std::istreambuf_iterator<char>(f)),
                               std::istreambuf_iterator<char>());
          auto areas = DeserializeWaterAreas(content);
          std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
          this->dataPtr->waterAreaMgr_.Clear();
          for (auto &a : areas)
            this->dataPtr->waterAreaMgr_.AddArea(std::move(a));
          this->dataPtr->waterAreasConfigured_ =
              !this->dataPtr->waterAreaMgr_.Areas().empty();
          this->dataPtr->currentAreaName_.clear();
        }
      }
    }
    catch (...) {}
  }

  /// 4. 多水区查找 — 确定有效水位高度和水中标志
  double effectiveFluidLevel = this->fluidLevel_;
  bool inWater = true;

  {
    std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
    if (!this->dataPtr->waterAreasConfigured_)
    {
      inWater = false;
    }
    else
    {
      const WaterArea *area = this->dataPtr->waterAreaMgr_.Lookup(
          linkWorldPos.X(), linkWorldPos.Y());
      if (!area)
      {
        inWater = false;
      }
      else
      {
        effectiveFluidLevel = area->baseHeight;
        /// 水区变化或波浪场引用缺失（每 30 帧重试一次）时重新查找实体。
        /// 载入世界时 WavesModel 在暂停期/1s 内重建波浪场，船"进入水区"的
        /// 首次查找可能早于波浪场实体创建 → 静水；重试保证竞态窗口自愈。
        /// 日志仅在状态翻转时输出，避免静水场景刷屏（2026-08-12 修复）
        const bool areaChanged =
            (area->name != this->dataPtr->currentAreaName_);
        const bool wfAlive = !this->dataPtr->activeWavefield_.expired();
        if (areaChanged
            || (!wfAlive && (++this->dataPtr->wfRetryCounter_ % 30) == 1))
        {
          this->dataPtr->currentAreaName_ = area->name;
          /// 按水区名查实体 "wavefield_{name}" → 读 ECM 组件 weak_ptr
          auto wfEntity = _ecm.EntityByComponents(
              gz::sim::components::Name("wavefield_" + area->name));
          this->dataPtr->activeWfEntity_ = wfEntity;   // ★ T5：供每帧读 EnvFlow
          auto wfComp = _ecm.Component<keship::components::Wavefield>(wfEntity);
          this->dataPtr->activeWavefield_ =
              wfComp ? wfComp->Data() : std::weak_ptr<Wavefield>();
          const bool nowAlive = !this->dataPtr->activeWavefield_.expired();
          if (areaChanged || nowAlive != wfAlive)
            gzmsg << "KSPlugin_HydroBuoyancy: 进入水区 [" << area->name
                  << "] 波浪场引用 " << (nowAlive ? "有效" : "缺失(静水)")
                  << std::endl;
        }
      }
    }
  }

  /// ★ T5（2026-09-22）：每帧读当前水区的环境流动（流速 / 风速）。
  /// 不能只在"水区切换时"读一次 —— 环境编辑器可在运行中即时改风/流。
  /// 实体或组件缺失时保持上一次的值（默认全 0 ⇒ 两项力为 0）。
  if (this->dataPtr->activeWfEntity_ != gz::sim::kNullEntity)
  {
    auto efComp = _ecm.Component<keship::components::EnvFlow>(
        this->dataPtr->activeWfEntity_);
    if (efComp)
      this->dataPtr->envFlow_ = efComp->Data();
  }

  /// 水面检测（给水动力用）
  double linkZ = linkWorldPos.Z();
  bool aboveWater = (linkZ > effectiveFluidLevel + this->waterThreshold_);

  const double rhoG = this->dataPtr->fluidDensity_
                    * std::abs(this->dataPtr->gravity_.Z());

  /// ★ 受力总线（2026-09-22）：本插件施加的每一份力都经 ForceAccumulator 累加，
  ///   力矩参考点（link 原点）在构造函数里**唯一确定**，插件只需如实声明力的作用点。
  ///   设计说明见 src_cpp/utils/ForceAccumulator.hh；
  ///   为什么必须是 link 原点见 doc/audit/force_ref_calibration_2026-09-22.md §5b。
  keship::ForceAccumulator acc(linkWorldPos);
  double totalSubVol = 0.0;

  /// 时间记录（波浪时间全局统一，无 per-model 缓起）
  this->dataPtr->simTime_ = _info.simTime;

  /// ═══════════════════════════════════════════════
  /// 5. 浮力计算 (if enableBuoyancy)
  /// ═══════════════════════════════════════════════
  if (this->enableBuoyancy_ && !this->dataPtr->bodies.empty() && inWater)
  {
    int bodyIdx = 0;
    for (auto &body : this->dataPtr->bodies)
    {
      /// 浮力体世界位姿 = link 世界位姿 * 相对 link 偏移（与恢复力矩参考 linkWorldPos 同源）
      gz::math::Pose3d bodyWorldPose = linkWorldPose * body.poseOffset;

      /// 波高 + **解析**梯度（一次遍历，不再三点采样差分），构造倾斜裁剪面
      double h0 = 0.0, dhdx = 0.0, dhdy = 0.0;
      {
        std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
        auto wf = this->dataPtr->activeWavefield_.lock();
        if (wf)
        {
          double t  = std::chrono::duration<double>(this->dataPtr->simTime_).count();
          double t0 = 0.0;   // 波浪从世界 0s 缓起，所有模型一致
          /// 原先是三点采样（中心 + x/y 各 +1 m）+ 前向差分：每个分量会引入
          /// 幅值 ×2sin(k·ds/2)/(k·ds) 与**相位滞后 k·ds/2**（默认参数下 11°~30°，
          /// λ<2 m 时甚至反号）。改解析式后与 ComputeDepthSimply 的高度逐位一致。
          /// 见 doc/audit/water_wave_audit_2026-09-21.md §W-7。
          wf->ComputeHeightAndGradient(
              bodyWorldPose.Pos(), t, t0, &h0, &dhdx, &dhdy);
        }
        this->dataPtr->lastWaveHeight_ = h0;
      }

      /// ★ 波面裁剪平面必须**通过采样点** (x0, y0, h0 + level)。
      /// gz::math::Plane 的语义是「n·p = d」（gz/math7/gz/math/Plane.hh:67/127），
      /// 由 n·p = d 反解平面在 (x0,y0) 处的高度 = N·d + dhdx·x0 + dhdy·y0，
      /// 故 d 必须取 (h0 + level − dhdx·x0 − dhdy·y0) / N；
      /// 若写成 d = h0 + level，水面高度会平白多出 dhdx·x0 + dhdy·y0，
      /// 误差随船离世界原点的距离**线性放大**（静水时 dhdx=dhdy=0 ⇒ 恒为 0，
      /// 这正是此前所有实验都测不出来的原因）。
      /// 见 doc/audit/water_wave_audit_2026-09-21.md §2（缺陷 W-1）。
      gz::math::Vector3d n0(-dhdx, -dhdy, 1.0);
      const double n0Len = n0.Length();
      gz::math::Vector3d waveNormal = n0 / n0Len;
      double waveOffset =
          (h0 + effectiveFluidLevel
           - dhdx * bodyWorldPose.Pos().X()
           - dhdy * bodyWorldPose.Pos().Y()) / n0Len;

      gz::math::Plane<double> waterPlane(waveNormal, waveOffset);
      double subVol = body.buoyancy->calculateTotalVolumeContribution(
          bodyWorldPose, waterPlane);

      /// [DIAG-D1] 每 body 浮力计算诊断（写日志文件，限频）
      {
        if (this->dataPtr->diagBodyCount_ < 600)
        {
          this->dataPtr->diagBodyCount_++;
          auto euler = body.poseOffset.Rot().Euler();
          auto we = bodyWorldPose.Rot().Euler();
          DiagnosticLog::write(this->logPrefix_ + "_diag_buoy.log",
              "[KSBuoy] tag,body,poseX,poseY,poseZ,poseRoll,posePitch,poseYaw,worldX,worldY,worldZ,worldRoll,worldPitch,worldYaw,totalVol,subVol",
              std::string("[diag1],") + body.name
              + "," + std::to_string(body.poseOffset.Pos().X())
              + "," + std::to_string(body.poseOffset.Pos().Y())
              + "," + std::to_string(body.poseOffset.Pos().Z())
              + "," + std::to_string(euler.X())
              + "," + std::to_string(euler.Y())
              + "," + std::to_string(euler.Z())
              + "," + std::to_string(bodyWorldPose.Pos().X())
              + "," + std::to_string(bodyWorldPose.Pos().Y())
              + "," + std::to_string(bodyWorldPose.Pos().Z())
              + "," + std::to_string(we.X())
              + "," + std::to_string(we.Y())
              + "," + std::to_string(we.Z())
              + "," + std::to_string(body.totalVolume)
              + "," + std::to_string(subVol));
        }
      }

      if (subVol < 1e-9)
      {
        DiagnosticLog::write(this->logPrefix_ + "_diag_buoy.log", "",
            std::string("[skip],") + body.name + ",subVol=" + std::to_string(subVol));
        bodyIdx++;
        continue;
      }

      totalSubVol += subVol;

      double Fmag = rhoG * subVol;
      gz::math::Vector3d buoyancyForce(0, 0, Fmag);

      /// 浮力 + 恢复力矩：**如实声明"力作用在浮心"**即可 ——
      /// 绕 link 原点的力矩由 ForceAccumulator 唯一折算（τ = (CoB − link原点) × F）。
      /// 该折算与引擎"力作用于 link 原点"的语义互补后，物理上等价于绕质心的惯性恢复力矩。
      gz::math::Vector3d cobLocal = body.buoyancy->getCenterOfBuoyancy();
      gz::math::Vector3d cobWorld = bodyWorldPose.Rot() * cobLocal + bodyWorldPose.Pos();
      acc.Add({"buoyancy/" + body.name, buoyancyForce, cobWorld});

      bodyIdx++;
    }

    /// 浸水体积发布（opt-in，降频每 6 帧一次；供排水量实验取"实测 ∇"）
    /// 2026-09-19 复核修正：原先写在本 if 之外 —— 模型不在任何水区（或未启用浮力、
    /// 无浮力体）时浮力循环整段跳过、totalSubVol 保持 0，却照样把 0 发出去，
    /// 订阅端无法区分"实测为 0"与"根本没算"，会把"没测到"伪装成"测到了"。
    /// 移入本 if：只有真的算过才发布。
    if (this->dataPtr->publishSubVol_)
    {
      if (++this->dataPtr->subVolPubCounter_ >= 6)
      {
        this->dataPtr->subVolPubCounter_ = 0;
        gz::msgs::Double subVolMsg;
        subVolMsg.set_data(totalSubVol);
        this->dataPtr->subVolPub_.Publish(subVolMsg);
      }
    }
  }

  /// ═══════════════════════════════════════════════
  /// 6. 阻尼（水中，M_A 通过 <fluid_added_mass> 注入 DART 隐式处理）
  /// ═══════════════════════════════════════════════
  {
    if (inWater && this->enableHydro_)
    {
      if (this->dataPtr->dampingMode_ == "pose_based"
          && this->dataPtr->hasCylinderGeometry_
          && !this->dataPtr->bodies.empty())
      {
        gz::math::Vector3d dampForce(0, 0, 0);
        gz::math::Vector3d dampTorque(0, 0, 0);
        computePoseBasedDamping(
            _ecm, modelPose, totalSubVol, effectiveFluidLevel,
            dampForce, dampTorque, totalMass);
        /// 阻尼的**力项无力臂**（既有口径：隐含作用于 link 原点）⇒ applyPoint 填 linkWorldPos，
        /// 叉乘恒为 0，力矩全部来自 pureTorque。**本次重构不改这一口径、不改数值**。
        acc.Add({"damping/pose_based", dampForce, linkWorldPos, dampTorque});
      }
      else if (this->dataPtr->fossen && this->dataPtr->fossen->valid())
      {
        auto linVelWorld = this->linkObj_.WorldLinearVelocity(_ecm);
        auto angVelWorld = this->linkObj_.WorldAngularVelocity(_ecm);
        if (linVelWorld && angVelWorld)
        {
          gz::math::Quaterniond R_bw = modelPose.Rot().Inverse();
          gz::math::Vector3d vBody = R_bw * (*linVelWorld);
          gz::math::Vector3d wBody = R_bw * (*angVelWorld);

          Eigen::Vector<double, 6> nu;
          nu << vBody.X(), vBody.Y(), vBody.Z(),
                wBody.X(), wBody.Y(), wBody.Z();

          Eigen::Vector<double, 6> tauBody;
          tauBody.setZero();
          this->dataPtr->fossen->computeDamping(nu, tauBody);

          gz::math::Vector3d forceWorld = modelPose.Rot() * gz::math::Vector3d(
              tauBody(0), tauBody(1), tauBody(2));
          gz::math::Vector3d torqueWorld = modelPose.Rot() * gz::math::Vector3d(
              tauBody(3), tauBody(4), tauBody(5));

          /// 同上：Fossen 阻尼的力项无力臂（隐含作用于 link 原点）
          /// ⇒ applyPoint 填 linkWorldPos，叉乘恒为 0，力矩全部来自 pureTorque。
          acc.Add({"damping/fossen", forceWorld, linkWorldPos, torqueWorld});

          auto &d = this->dataPtr->hydroDiag;
          for (int i = 0; i < 6; ++i) d.tauD[i] = tauBody(i);
          d.hydroActive = true;
          d.waterFrameCount++;
        }
      }
    }
    else
    {
      this->dataPtr->hydroDiag.hydroActive = false;
      this->dataPtr->hydroDiag.waterFrameCount = 0;
    }
  }

  /// ═══════════════════════════════════════════════
  /// 6b. 流速 / 风速外力（T5，2026-09-22）
  ///
  ///   模型：F = ½·ρ·C_d·A·|v_rel|·v_rel      （v_rel = v_env − v_ship，世界系）
  ///   面积（由浮力体包围盒 + 当前吃水估算，方案 A）：
  ///     A_sub   = B · T          水下侧向投影
  ///     A_above = B · (D − T)    水线以上侧向投影
  ///   作用点：水下 = 水位 − T/2；水上 = 水位 + (D−T)/2
  ///           （风力作用点高于重心 ⇒ 产生倾覆力矩，教学上有意义）
  ///
  ///   ★ 软开关：流速与风速均为 0 时两项力**精确为 0**
  ///     ⇒ 静水无风场景与改动前逐位一致（这是本项的首要判据）。
  ///   ★ 静水力与阻尼**一行未动**：本段只向受力总线追加两项。
  /// ═══════════════════════════════════════════════
  if (inWater)
  {
    const components::EnvFlowData &ef = this->dataPtr->envFlow_;
    const bool hasFlow = std::abs(ef.currentSpeed) > 1e-9;
    const bool hasWind = std::abs(ef.windSpeed) > 1e-9;

    if (hasFlow || hasWind)
    {
      auto linVel = this->linkObj_.WorldLinearVelocity(_ecm);
      if (linVel)
      {
        /// 环境速度矢量（世界系）。
        ///
        /// ★★ 2026-09-23 方向约定统一（张老师："以船舶为主题"）：
        ///   本项目**统一采用 NED 风格**（与 Fossen 6-DOF、波浪方向、船体几何一致）：
        ///     **0° = +X（北），顺时针** ⇒ 单位方向向量 = (cos(dir), sin(dir), 0)
        ///       dir = 0    → (+1, 0, 0) = 正北
        ///       dir = π/2  → (0, +1, 0) = 正东
        ///   ⚠ 注意：这与 Gazebo 世界系的 ENU 惯例（+X 东、+Y 北）**不同**。
        ///     此前本处用 `(sin, cos)`（ENU）⇒ 与波浪方向差 90°，已修正。
        ///   ⚠ 改动任一处方向约定，都要同步核对：
        ///     Wavefield.cc（波浪）、WaveVisual.cc（流向视觉）、
        ///     GerstnerWaves_vs_330.glsl（shader）、EnvFlow.hh（注释）。
        const gz::math::Vector3d vCurrent(
            ef.currentSpeed * std::cos(ef.currentDirection),
            ef.currentSpeed * std::sin(ef.currentDirection), 0.0);
        const gz::math::Vector3d vWind(
            ef.windSpeed * std::cos(ef.windDirection),
            ef.windSpeed * std::sin(ef.windDirection), 0.0);

        /// 所有浮力体包围盒的**并集**（世界系；8 角点变换后取极值，兼顾倾斜）
        double bMinZ = 1e30, bMaxZ = -1e30, bMinY = 1e30, bMaxY = -1e30;
        for (auto &body : this->dataPtr->bodies)
        {
          if (!body.bboxValid)
            continue;
          const gz::math::Pose3d bp = linkWorldPose * body.poseOffset;
          for (int ix = 0; ix < 2; ++ix)
            for (int iy = 0; iy < 2; ++iy)
              for (int iz = 0; iz < 2; ++iz)
              {
                const gz::math::Vector3d corner(
                    ix ? body.bboxMax.X() : body.bboxMin.X(),
                    iy ? body.bboxMax.Y() : body.bboxMin.Y(),
                    iz ? body.bboxMax.Z() : body.bboxMin.Z());
                const gz::math::Vector3d w = bp.Rot() * corner + bp.Pos();
                bMinZ = std::min(bMinZ, w.Z());
                bMaxZ = std::max(bMaxZ, w.Z());
                bMinY = std::min(bMinY, w.Y());
                bMaxY = std::max(bMaxY, w.Y());
              }
        }

        if (bMaxZ > bMinZ)
        {
          const double D = bMaxZ - bMinZ;    ///< 型深（世界竖直向跨度）
          const double B = bMaxY - bMinY;    ///< 型宽

          /// ★ 作用点的**水平位置取质心**（水下侧面积形心的近似）。
          /// ⚠ 绝不能用 link 原点的 x,y —— 二者可相差数米
          ///   （如 ke0812_1.ksm：link 原点 vs CoM 差 (1.6, -0.99) m），
          ///   会产生**虚假的偏航力矩**。实测（2026-09-23）：流速 1 m/s 时
          ///   船 30 s 内 yaw 从 -1° 转到 +47°（原地打转），改用 CoM 后消除。
          const gz::math::Vector3d linkCoMWorldPos =
              this->linkObj_.WorldInertialPose(_ecm)
                  ? this->linkObj_.WorldInertialPose(_ecm)->Pos()
                  : linkWorldPos;
          const double T = std::clamp(effectiveFluidLevel - bMinZ, 0.0, D);  ///< 吃水
          const double A_sub = B * T * this->dataPtr->currentAreaScale_;
          const double A_above = B * (D - T) * this->dataPtr->windAreaScale_;

          /// 流力（水流对船体）
          if (hasFlow)
          {
            const gz::math::Vector3d vr = vCurrent - *linVel;
            const double mag = vr.Length();
            if (mag > 1e-9)
            {
              const gz::math::Vector3d F = vr * (0.5 * this->dataPtr->fluidDensity_
                  * this->dataPtr->currentDragCoeff_ * A_sub * mag);
              const gz::math::Vector3d P(linkCoMWorldPos.X(), linkCoMWorldPos.Y(),
                  effectiveFluidLevel - 0.5 * T);
              acc.Add({"env/current", F, P});
            }
          }

          /// 风压力（风对船体水上部分）
          if (hasWind)
          {
            const gz::math::Vector3d vr = vWind - *linVel;
            const double mag = vr.Length();
            if (mag > 1e-9)
            {
              const gz::math::Vector3d F = vr * (0.5 * this->dataPtr->airDensity_
                  * this->dataPtr->windDragCoeff_ * A_above * mag);
              const gz::math::Vector3d P(linkCoMWorldPos.X(), linkCoMWorldPos.Y(),
                  effectiveFluidLevel + 0.5 * (D - T));
              acc.Add({"env/wind", F, P});
            }
          }
        }
      }
    }
  }

  /// 7. 诊断日志
  double simTimeSec = std::chrono::duration<double>(
      this->dataPtr->simTime_).count();

  if (this->dataPtr->logWriteCounter % 50 == 0)
  {
    auto pc = _ecm.Component<gz::sim::components::Pose>(this->entity_);
    gz::math::Pose3d wp = pc ? pc->Data() : gz::math::Pose3d::Zero;

    auto lv = this->linkObj_.WorldLinearVelocity(_ecm);
    auto av = this->linkObj_.WorldAngularVelocity(_ecm);

    DiagnosticLog::write("hydrobuoyancy_" + this->logPrefix_ + ".csv",
      "frame,px,py,pz,roll,pitch,yaw,"
      "vx,vy,vz,wx,wy,wz,"
      "subVol,Fx,Fy,Fz,Tx,Ty,Tz,mass,inWater,aboveWater,"
      "simTimeSec,areaName,waveHeight,effectiveFluidLevel",
      std::to_string(this->dataPtr->logWriteCounter) + ","
      + std::to_string(wp.Pos().X()) + "," + std::to_string(wp.Pos().Y()) + "," + std::to_string(wp.Pos().Z()) + ","
      + std::to_string(wp.Rot().Euler().X()) + "," + std::to_string(wp.Rot().Euler().Y()) + "," + std::to_string(wp.Rot().Euler().Z()) + ","
      + (lv ? std::to_string(lv->X()) + "," + std::to_string(lv->Y()) + "," + std::to_string(lv->Z()) : "0,0,0") + ","
      + (av ? std::to_string(av->X()) + "," + std::to_string(av->Y()) + "," + std::to_string(av->Z()) : "0,0,0") + ","
      + std::to_string(totalSubVol) + ","
      + std::to_string(acc.TotalForce().X()) + "," + std::to_string(acc.TotalForce().Y()) + "," + std::to_string(acc.TotalForce().Z()) + ","
      + std::to_string(acc.TotalTorque().X()) + "," + std::to_string(acc.TotalTorque().Y()) + "," + std::to_string(acc.TotalTorque().Z()) + ","
      + std::to_string(totalMass) + ","
      + std::to_string(inWater) + "," + std::to_string(aboveWater) + ","
      + std::to_string(simTimeSec) + ","
      + this->dataPtr->currentAreaName_ + ","
      + std::to_string(this->dataPtr->lastWaveHeight_) + ","
      + std::to_string(effectiveFluidLevel));
  }

  /// 高频阻尼分量日志（水中每帧，前 1000 个水帧）
  if (this->dataPtr->hydroDiag.hydroActive
      && this->dataPtr->hydroDiag.waterFrameCount <= 1000)
  {
    auto &d = this->dataPtr->hydroDiag;
    DiagnosticLog::write("hydro_detail_" + this->logPrefix_ + ".csv",
      "frame,wfCount,simTimeSec,"
      "tauDfx,tauDfy,tauDfz,tauDtx,tauDty,tauDtz",
      std::to_string(this->dataPtr->logWriteCounter) + ","
      + std::to_string(d.waterFrameCount) + ","
      + std::to_string(simTimeSec) + ","
      + std::to_string(d.tauD[0]) + "," + std::to_string(d.tauD[1]) + "," + std::to_string(d.tauD[2]) + ","
      + std::to_string(d.tauD[3]) + "," + std::to_string(d.tauD[4]) + "," + std::to_string(d.tauD[5]));
  }
  this->dataPtr->logWriteCounter++;

  /// 8. 汇总写入 ECM（wrench 每步被物理系统 Clear，多插件共存无顺序依赖）
  this->ApplyForces(_ecm, acc.TotalForce(), acc.TotalTorque());
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::LoadParameters(
    const std::shared_ptr<const sdf::Element> &_sdf)
{
  auto [density, ok1] = _sdf->Get<double>("fluid_density", this->dataPtr->fluidDensity_);
  if (ok1) this->dataPtr->fluidDensity_ = density;

  auto [level, ok2] = _sdf->Get<double>("fluid_level", this->fluidLevel_);
  if (ok2) this->fluidLevel_ = level;

  auto [linkName, linkOk] = _sdf->Get<std::string>("link_name", "");
  if (linkOk)
    this->linkName_ = linkName;

  auto [meshPath, meshOk] = _sdf->Get<std::string>("mesh_path", "");
  if (meshOk)
    this->meshPath_ = meshPath;

  /// 功能开关
  auto [enb, enbOk] = _sdf->Get<bool>("enable_buoyancy", true);
  if (enbOk) this->enableBuoyancy_ = enb;

  auto [enh, enhOk] = _sdf->Get<bool>("enable_hydro", false);
  if (enhOk) this->enableHydro_ = enh;
  else
  {
    /// 向后兼容：若 SDF 中有阻尼配置，自动启用 hydro
    if (_sdf->HasElement("linear_damping") || _sdf->HasElement("damping"))
      this->enableHydro_ = true;
  }

  auto [thr, thrOk] = _sdf->Get<double>("water_threshold", 0.5);
  if (thrOk) this->waterThreshold_ = thr;

  auto [cylRadius, cylROk] = _sdf->Get<double>("cylinder_radius", 0.0);
  if (cylROk) {
    this->dataPtr->cylinderRadius_ = cylRadius;
    this->dataPtr->hasCylinderGeometry_ = true;
  }

  auto [cylLength, cylLOk] = _sdf->Get<double>("cylinder_length", 0.0);
  if (cylLOk) this->dataPtr->cylinderLength_ = cylLength;

  auto [cylSegs, cylSOk] = _sdf->Get<int>("cylinder_segments", 32);
  if (cylSOk) this->dataPtr->cylinderSegments_ = cylSegs;

  auto [dm, dmOk] = _sdf->Get<std::string>("damping_mode", "fossen");
  if (dmOk) this->dataPtr->dampingMode_ = dm;

  auto [zeta, zetaOk] = _sdf->Get<double>("damping_ratio", 0.1);
  if (zetaOk) this->dataPtr->dampingRatio_ = zeta;

  /// ★ T5（2026-09-22）流速 / 风速外力系数（全部可选，缺省见 .hh）
  /// 软开关：环境流速与风速均为 0 时两项力**精确为 0** ⇒ 静水无风场景逐位不变。
  auto [cdw, cdwOk] =
      _sdf->Get<double>("current_drag_coeff", this->dataPtr->currentDragCoeff_);
  if (cdwOk) this->dataPtr->currentDragCoeff_ = cdw;

  auto [cda, cdaOk] =
      _sdf->Get<double>("wind_drag_coeff", this->dataPtr->windDragCoeff_);
  if (cdaOk) this->dataPtr->windDragCoeff_ = cda;

  auto [rhoAir, rhoAirOk] =
      _sdf->Get<double>("air_density", this->dataPtr->airDensity_);
  if (rhoAirOk) this->dataPtr->airDensity_ = rhoAir;

  auto [cScale, cScaleOk] =
      _sdf->Get<double>("current_area_scale", this->dataPtr->currentAreaScale_);
  if (cScaleOk) this->dataPtr->currentAreaScale_ = cScale;

  auto [wScale, wScaleOk] =
      _sdf->Get<double>("wind_area_scale", this->dataPtr->windAreaScale_);
  if (wScaleOk) this->dataPtr->windAreaScale_ = wScale;
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::LoadSingleMeshBuoyancy(
    gz::sim::EntityComponentManager &_ecm)
{
  MeshDataSimple meshData;

  if (this->dataPtr->hasCylinderGeometry_)
  {
    CylinderParams p{this->dataPtr->cylinderRadius_,
                     this->dataPtr->cylinderLength_,
                     this->dataPtr->cylinderSegments_};
    meshData = GeneratePrimitiveMesh(PrimitiveType::Cylinder, nullptr, &p);
  }
  else
  {
    if (this->meshPath_.empty())
    {
      auto collisions = this->linkObj_.Collisions(_ecm);
      for (auto colEntity : collisions)
      {
        auto geomComp = _ecm.Component<gz::sim::components::Geometry>(colEntity);
        if (!geomComp) continue;
        const auto &geom = geomComp->Data();
        if (geom.Type() == sdf::GeometryType::MESH)
        {
          auto meshPtr = geom.MeshShape();
          if (meshPtr)
          {
            this->meshPath_ = gz::common::findFile(meshPtr->Uri());
            break;
          }
        }
      }
    }

    if (this->meshPath_.empty())
    {
      gzwarn << "KSPlugin_HydroBuoyancy: 未找到 mesh 路径且无浮力体配置" << std::endl;
      return;
    }

    meshData = LoadMeshFromFile(this->meshPath_);
  }

  if (meshData.vertices.empty())
  {
    gzerr << "KSPlugin_HydroBuoyancy: 加载网格失败" << std::endl;
    return;
  }

  this->dataPtr->bodies.clear();

  BuoyancyBody body;
  body.name = "default";
  body.poseOffset = gz::math::Pose3d(0, 0, 0, 0, 0, 0);
  body.buoyancy = std::make_shared<Buoyancy>();
  body.buoyancy->setDiagLogPrefix(this->logPrefix_);
  body.buoyancy->loadMeshData(meshData);

  body.totalVolume = 0.0;
  for (const auto &tri : body.buoyancy->triangles_)
    body.totalVolume += std::abs(tri.volume_);

  /// 特征长度平方
  if (!meshData.vertices.empty())
  {
    auto &v0 = meshData.vertices[0];
    double minX = v0.X(), maxX = v0.X();
    double minY = v0.Y(), maxY = v0.Y();
    double minZ = v0.Z(), maxZ = v0.Z();
    for (const auto &v : meshData.vertices)
    {
      minX = std::min(minX, v.X()); maxX = std::max(maxX, v.X());
      minY = std::min(minY, v.Y()); maxY = std::max(maxY, v.Y());
      minZ = std::min(minZ, v.Z()); maxZ = std::max(maxZ, v.Z());
    }
    double dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    body.characteristicLengthSq = dx*dx + dy*dy + dz*dz;

    /// ★ T5（2026-09-22）：顺便记下包围盒 —— 迎流/迎风面积与作用点估算用
    /// （型宽 B = bboxMax.Y − bboxMin.Y，型深 D = bboxMax.Z − bboxMin.Z）
    body.bboxMin = {minX, minY, minZ};
    body.bboxMax = {maxX, maxY, maxZ};
    body.bboxValid = true;
  }

  this->dataPtr->bodies.push_back(std::move(body));

  gzmsg << "KSPlugin_HydroBuoyancy: 单网格加载 — "
        << meshData.vertices.size() << " 顶点, "
        << meshData.triangles.size() << " 三角形"
        << ", 总容积=" << this->dataPtr->bodies.back().totalVolume << " m³" << std::endl;
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::LoadMultiBuoyancy(
    const std::shared_ptr<const sdf::Element> &_sdf,
    gz::sim::EntityComponentManager &_ecm)
{
  this->dataPtr->bodies.clear();
  auto ptr = const_cast<sdf::Element *>(_sdf.get());
  int count = 0;

  for (sdf::ElementPtr elem = ptr->GetElement("buoyancy");
       elem;
       elem = elem->GetNextElement("buoyancy"))
  {
    BuoyancyBody body;

    auto [name, nameOk] = elem->Get<std::string>("name", "");
    body.name = nameOk ? name : ("body_" + std::to_string(count));

    auto [pose, poseOk] = elem->Get<gz::math::Pose3d>("pose", gz::math::Pose3d::Zero);
    if (poseOk) body.poseOffset = pose;

    auto geomElem = elem->GetElement("geometry");
    if (!geomElem)
    {
      gzerr << "KSPlugin_HydroBuoyancy: buoyancy '" << body.name << "' 缺少 <geometry>" << std::endl;
      continue;
    }

    MeshDataSimple meshData;
    if (geomElem->HasElement("box"))
    {
      auto boxElem = geomElem->GetElement("box");
      auto sizeElem = boxElem->GetElement("size");
      if (sizeElem)
      {
        auto size = sizeElem->Get<gz::math::Vector3d>();
        BoxParams bp{size.X(), size.Y(), size.Z()};
        meshData = GeneratePrimitiveMesh(PrimitiveType::Box, &bp, nullptr);
      }
    }
    else if (geomElem->HasElement("cylinder"))
    {
      auto cylElem = geomElem->GetElement("cylinder");
      auto [r, rOk] = cylElem->Get<double>("radius", 0.5);
      auto [l, lOk] = cylElem->Get<double>("length", 1.0);
      CylinderParams cp{r, l, 32};
      meshData = GeneratePrimitiveMesh(PrimitiveType::Cylinder, nullptr, &cp);
      this->dataPtr->cylinderRadius_ = r;
      this->dataPtr->cylinderLength_ = l;
      this->dataPtr->hasCylinderGeometry_ = true;
    }
    else if (geomElem->HasElement("mesh"))
    {
      auto meshElem = geomElem->GetElement("mesh");
      auto uriElem = meshElem->GetElement("uri");
      std::string uri;
      if (uriElem)
      {
        uri = uriElem->GetValue()->GetAsString();
        auto path = gz::common::findFile(uri);
        if (!path.empty())
          meshData = LoadMeshFromFile(path);
      }
      if (meshData.vertices.empty())
      {
        gzerr << "KSPlugin_HydroBuoyancy: 无法加载 mesh 几何体, uri="
              << uri << std::endl;
        continue;
      }
    }
    else
    {
      gzerr << "KSPlugin_HydroBuoyancy: 不支持的几何体类型" << std::endl;
      continue;
    }

    if (meshData.vertices.empty())
      continue;

    body.buoyancy = std::make_shared<Buoyancy>();
    body.buoyancy->setDiagLogPrefix(this->logPrefix_);
    body.buoyancy->loadMeshData(meshData);

    body.totalVolume = 0.0;
    for (const auto &tri : body.buoyancy->triangles_)
      body.totalVolume += std::abs(tri.volume_);

    {
      auto &v0 = meshData.vertices[0];
      double minX = v0.X(), maxX = v0.X();
      double minY = v0.Y(), maxY = v0.Y();
      double minZ = v0.Z(), maxZ = v0.Z();
      for (const auto &v : meshData.vertices)
      {
        minX = std::min(minX, v.X()); maxX = std::max(maxX, v.X());
        minY = std::min(minY, v.Y()); maxY = std::max(maxY, v.Y());
        minZ = std::min(minZ, v.Z()); maxZ = std::max(maxZ, v.Z());
      }
      double dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
      body.characteristicLengthSq = dx*dx + dy*dy + dz*dz;

      /// ★ T5（2026-09-22）：顺便记下包围盒 —— 迎流/迎风面积与作用点估算用
      body.bboxMin = {minX, minY, minZ};
      body.bboxMax = {maxX, maxY, maxZ};
      body.bboxValid = true;
    }

    this->dataPtr->bodies.push_back(std::move(body));

    gzmsg << "KSPlugin_HydroBuoyancy: 浮力体[" << count << "] name=" << body.name
          << " volume=" << this->dataPtr->bodies.back().totalVolume
          << " lenSq=" << this->dataPtr->bodies.back().characteristicLengthSq << std::endl;

    count++;
  }

  gzmsg << "KSPlugin_HydroBuoyancy: 多浮力体加载完成 — " << count << " 个体" << std::endl;
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::ApplyForces(
    gz::sim::EntityComponentManager &_ecm,
    const gz::math::Vector3d &_force,
    const gz::math::Vector3d &_torque)
{
  auto wrenchComp = _ecm.Component<gz::sim::components::ExternalWorldWrenchCmd>(
      this->linkEntity_);

  gz::msgs::Wrench wrenchMsg;
  if (wrenchComp)
    wrenchMsg = wrenchComp->Data();

  /// 累加模式（wrench 每步被物理系统 Clear()，多插件共存无顺序依赖）
  wrenchMsg.mutable_force()->set_x(wrenchMsg.force().x() + _force.X());
  wrenchMsg.mutable_force()->set_y(wrenchMsg.force().y() + _force.Y());
  wrenchMsg.mutable_force()->set_z(wrenchMsg.force().z() + _force.Z());
  wrenchMsg.mutable_torque()->set_x(wrenchMsg.torque().x() + _torque.X());
  wrenchMsg.mutable_torque()->set_y(wrenchMsg.torque().y() + _torque.Y());
  wrenchMsg.mutable_torque()->set_z(wrenchMsg.torque().z() + _torque.Z());

  if (wrenchComp)
    _ecm.SetComponentData<gz::sim::components::ExternalWorldWrenchCmd>(
        this->linkEntity_, wrenchMsg);
  else
    _ecm.CreateComponent(this->linkEntity_,
                         gz::sim::components::ExternalWorldWrenchCmd(wrenchMsg));
}

/////////////////////////////////////////////////
KSPlugin_HydroBuoyancy::Implementation::~Implementation()
{
  std::lock_guard<std::mutex> lock(mutex);
  alive_ = false;
}

/////////////////////////////////////////////////
void KSPlugin_HydroBuoyancy::computePoseBasedDamping(
    gz::sim::EntityComponentManager &_ecm,
    const gz::math::Pose3d &_modelPose,
    double _totalSubVol,
    double _effectiveFluidLevel,
    gz::math::Vector3d &_force,
    gz::math::Vector3d &_torque,
    double _bodyMass)
{
  if (_totalSubVol <= 1e-6) return;

  auto linVelWorld = this->linkObj_.WorldLinearVelocity(_ecm);
  auto angVelWorld = this->linkObj_.WorldAngularVelocity(_ecm);
  if (!linVelWorld || !angVelWorld) return;

  gz::math::Quaterniond R_bw = _modelPose.Rot().Inverse();
  gz::math::Vector3d vBody = R_bw * (*linVelWorld);
  gz::math::Vector3d wBody = R_bw * (*angVelWorld);

  double r = this->dataPtr->cylinderRadius_;
  double L = this->dataPtr->cylinderLength_;
  if (r <= 0.0 || L <= 0.0) return;

  double rho = this->dataPtr->fluidDensity_;
  double totalVol = M_PI * r * r * L;
  double m_sub = rho * std::min(_totalSubVol, totalVol);

  /// 水面在 body 帧的法向: n = R_bw * (0,0,1) = 世界 Z 在 body 帧的表示
  gz::math::Vector3d n = R_bw * gz::math::Vector3d(0, 0, 1);
  double waterZ = _effectiveFluidLevel + this->dataPtr->lastWaveHeight_;
  double planeD = waterZ - _modelPose.Pos().Z();

  /// ── 湿面积数值积分 ──
  /// 对圆柱侧面 (r·cosθ, r·sinθ, z) 和两端分别积分，
  /// 计算各自由度对应的湿投影面积 A_proj[0..2] (m²)
  /// 和湿力矩面积 A_rot[0..2] (m⁴)

  double A_proj[3] = {0.0, 0.0, 0.0};
  double A_rot[3]  = {0.0, 0.0, 0.0};

  const int Ntheta = 16;
  const int Nz = 32;
  const int Nrho = 8;

  /// 侧面: p(θ,z) = (r·cosθ, r·sinθ, z), 法向 n_s = (cosθ, sinθ, 0)
  {
    double dtheta = 2.0 * M_PI / Ntheta;
    double dz = L / Nz;

    for (int it = 0; it < Ntheta; ++it)
    {
      double theta = (it + 0.5) * dtheta;
      double ct = std::cos(theta);
      double st = std::sin(theta);
      double dA_dz = r * dtheta;

      for (int iz = 0; iz < Nz; ++iz)
      {
        double z = -0.5 * L + (iz + 0.5) * dz;

        /// 浸没检测
        if (n.X() * r * ct + n.Y() * r * st + n.Z() * z >= planeD)
          continue;

        double dA = dA_dz * dz;

        /// 平移投影 (标准化至标准迎流面积 2rL)
        /// 侧面: |n_s · (1,0,0)| = |cosθ|, 积分 = 4rL → 归一化因子 0.5
        A_proj[0] += 0.5 * std::abs(ct) * dA;
        A_proj[1] += 0.5 * std::abs(st) * dA;

        /// 旋转力矩面积
        /// 绕 body X (roll): 切向 e_t = (0,-z,y)/√(y²+z²)
        /// A_roll = ∫ r⊥³ × |n_s·e_t| dA, 其中 r⊥² = y²+z²
        {
          double y = r * st;
          double rp2 = y * y + z * z;
          double rp = std::sqrt(rp2);
          if (rp > 1e-12)
            A_rot[0] += rp2 * rp * std::abs(z * st) / rp * dA;
        }

        /// 绕 body Y (pitch): 切向 e_t = (z,0,-x)/√(x²+z²)
        {
          double x = r * ct;
          double rp2 = x * x + z * z;
          double rp = std::sqrt(rp2);
          if (rp > 1e-12)
            A_rot[1] += rp2 * rp * std::abs(z * ct) / rp * dA;
        }

        /// 绕 body Z (yaw): 侧面法向 = 径向, 切向 = 轴向 → 垂直, 投影 = 0
        /// 侧面不贡献 yaw 阻尼
      }
    }
  }

  /// 端面: p(ρ,θ) = (ρ·cosθ, ρ·sinθ, ±L/2), 法向 = (0,0,±1)
  /// 两个端面 (iside=0 下端, iside=1 上端)
  for (int iside = 0; iside < 2; ++iside)
  {
    double z_end = (iside == 0) ? -0.5 * L : 0.5 * L;
    double dtheta = 2.0 * M_PI / Ntheta;
    double drho = r / Nrho;

    for (int it = 0; it < Ntheta; ++it)
    {
      double theta = (it + 0.5) * dtheta;
      double ct = std::cos(theta);
      double st = std::sin(theta);

      for (int ir = 0; ir < Nrho; ++ir)
      {
        double rho_loc = (ir + 0.5) * drho;
        double x = rho_loc * ct;
        double y = rho_loc * st;

        /// 浸没检测
        if (n.X() * x + n.Y() * y + n.Z() * z_end >= planeD)
          continue;

        double dA = rho_loc * drho * dtheta;

        /// body Z 平移: 端面法向 = (0,0,±1), 投影 = 1
        A_proj[2] += dA;

        /// 绕 body X (roll): 端面切向 e_t = (0,-z_end,y)/√(y²+z_end²)
        /// |n_s·e_t| = |(0,0,±1)·(0,-z_end,y)|/r⊥ = |y|/r⊥
        {
          double rp2 = y * y + z_end * z_end;
          double rp = std::sqrt(rp2);
          if (rp > 1e-12)
            A_rot[0] += rp2 * rp * std::abs(y) / rp * dA;
        }

        /// 绕 body Y (pitch): 端面切向 e_t = (z_end,0,-x)/√(x²+z_end²)
        /// |n_s·e_t| = |(0,0,±1)·(z_end,0,-x)|/r⊥ = |x|/r⊥
        {
          double rp2 = x * x + z_end * z_end;
          double rp = std::sqrt(rp2);
          if (rp > 1e-12)
            A_rot[1] += rp2 * rp * std::abs(x) / rp * dA;
        }

        /// 绕 body Z (yaw): 端面法向 = 轴向, 切向 = 轴向 → 垂直, 投影 = 0
      }
    }
  }

  /// ── 纯物理阻尼计算 ──
  /// 阻力系数（Morison 圆桩/圆盘实测范围）
  const double Cd_n = 1.1;    // 侧壁（径向）阻力系数
  const double Cd_a = 0.82;   // 端面（轴向）阻力系数
  const double g_accel = this->dataPtr->gravity_.Length();

  /// 兴波阻尼比（物理值，船舶工程实测范围）：
  /// 垂荡 0.10~0.30 → 取 0.15；横摇/纵摇 0.03~0.15 → 取 0.10
  const double zeta_heave = 0.15;
  const double zeta_rot   = 0.10;

  /// 摩擦线性系数（× 浸没排水质量；仅防数值残漂，远小于兴波项）
  const double frLin = 0.002;

  /// 水线面面积 A_wp: 水平面与圆柱表面的交截面
  ///   n   = 世界 Z 在 body 帧的表示 = cosTilt
  ///   d   = -planeD = 圆柱中心到水面的距离 (正 = 中心在水面下)
  ///   A_wp = 直立时 πr², 横躺时 L·2√(r²-d²), 中间过渡
  double cosTilt = std::abs(n.Z());
  double d_center = -planeD;
  double immFrac = std::max(-1.0, std::min(1.0, d_center / r));
  double halfWidth = std::sqrt(std::max(0.0, 1.0 - immFrac * immFrac));
  double A_horiz = 2.0 * r * L * halfWidth;
  double A_vert  = M_PI * r * r;
  // 当圆柱趋近直立时水线面从矩形过渡为圆形
  double vertWeight = std::min(1.0, std::max(0.0, (cosTilt - 0.5) / 0.5));
  double A_wp = (1.0 - vertWeight) * A_horiz + vertWeight * A_vert;

  /// 水线面惯性矩 I_wp（转动恢复刚度 k = ρg·I_wp）：
  /// 直立 = 圆盘绕直径 πr⁴/4，横躺 = 矩形 L·(2w)³/12，中间过渡
  double I_vert  = M_PI * r * r * r * r / 4.0;
  double I_horiz = L * std::pow(2.0 * halfWidth * r, 3.0) / 12.0;
  double I_wp = (1.0 - vertWeight) * I_horiz + vertWeight * I_vert;

  /// 转动等效惯量：细长圆柱回转半径 ≈ L²/12
  double I_eff = std::max(_bodyMass, 1e-6) * L * L / 12.0;

  /// 兴波线性阻尼系数（单位：平动 N·s/m，转动 N·m·s）
  double B_heave = 2.0 * zeta_heave
      * std::sqrt(rho * g_accel * A_wp * std::max(_bodyMass, 1e-6));
  double B_rot = 2.0 * zeta_rot
      * std::sqrt(rho * g_accel * I_wp * I_eff);

  /// 摩擦线性项（极小，防数值残漂）
  double B_fr    = frLin * m_sub;
  double B_fr_rot = frLin * I_eff;

  /// 浸没湿侧壁面积（yaw 二次阻尼用）
  double L_sub = std::min(_totalSubVol / (M_PI * r * r), L);
  double A_wet_side = 2.0 * M_PI * r * L_sub;

  double nu[6] = {vBody.X(), vBody.Y(), vBody.Z(),
                  wBody.X(), wBody.Y(), wBody.Z()};

  double tauBody[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

  /// surge/sway: 摩擦 + 压差阻力（侧壁投影面积, Cd_n）
  tauBody[0] -= (B_fr
      + 0.5 * rho * Cd_n * A_proj[0] * std::abs(nu[0])) * nu[0];
  tauBody[1] -= (B_fr
      + 0.5 * rho * Cd_n * A_proj[1] * std::abs(nu[1])) * nu[1];

  /// heave: 兴波 + 压差阻力（端面投影面积, Cd_a）
  tauBody[2] -= (B_heave
      + 0.5 * rho * Cd_a * A_proj[2] * std::abs(nu[2])) * nu[2];

  /// roll/pitch: 兴波 + 压差阻力（力矩面积, Cd_n）
  tauBody[3] -= (B_rot
      + 0.5 * rho * Cd_n * A_rot[0] * std::abs(nu[3])) * nu[3];
  tauBody[4] -= (B_rot
      + 0.5 * rho * Cd_n * A_rot[1] * std::abs(nu[4])) * nu[4];

  /// yaw: 摩擦 + 压差阻力（湿侧壁 × 力臂², Cd_n）
  tauBody[5] -= (B_fr_rot
      + 0.5 * rho * Cd_n * A_wet_side * r * r * std::abs(nu[5])) * nu[5];

  _force  += _modelPose.Rot() * gz::math::Vector3d(tauBody[0], tauBody[1], tauBody[2]);
  _torque += _modelPose.Rot() * gz::math::Vector3d(tauBody[3], tauBody[4], tauBody[5]);

  auto &d = this->dataPtr->hydroDiag;
  for (int i = 0; i < 6; ++i) d.tauD[i] = tauBody[i];
  d.hydroActive = true;
  d.waterFrameCount++;
}

/*********************************************/
GZ_ADD_PLUGIN(keship::KSPlugin_HydroBuoyancy,
              gz::sim::System,
              gz::sim::ISystemConfigure,
              gz::sim::ISystemPreUpdate)

GZ_ADD_PLUGIN_ALIAS(keship::KSPlugin_HydroBuoyancy,
                    "KSPlugin_HydroBuoyancy")
