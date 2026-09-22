/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Keship推进器插件实现 */
/// ZhangYu 创建 2026.5.11

#include "KSPlugin_Thrust.hh"

#include <gz/sim/Model.hh>
#include <gz/sim/Link.hh>
#include <gz/sim/components/Name.hh>
#include <gz/sim/components/Pose.hh>
#include <gz/sim/components/Inertial.hh>
#include <gz/plugin/Register.hh>
#include <gz/msgs/double.pb.h>

#include "../utils/DiagnosticLog.hh"
#include "../utils/ForceAccumulator.hh"
#include "keship/utils/KeshipPaths.hh"

/////////////////////////////////////////////////
keship::KSPlugin_Thrust::KSPlugin_Thrust()
    : System(), dataPtr(gz::utils::MakeUniqueImpl<Implementation>())
{
  gzmsg << "KSPlugin_Thrust: 初始化" << std::endl;
}

/////////////////////////////////////////////////
void keship::KSPlugin_Thrust::Configure(
    const gz::sim::Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    gz::sim::EntityComponentManager &_ecm,
    gz::sim::EventManager &_eventMgr)
{
  gzmsg << "KSPlugin_Thrust: 配置实体 " << _entity << std::endl;

  this->entity_ = _entity;

  /// 获取模型名称
  auto nameComp = _ecm.Component<gz::sim::components::Name>(_entity);
  this->dataPtr->modelName = nameComp ? nameComp->Data() : "unknown";
  gzmsg << "KSPlugin_Thrust: model=" << this->dataPtr->modelName << std::endl;

  /// 解析 link_name
  auto [linkName, linkOk] = _sdf->Get<std::string>("link_name", "");
  if (linkOk) this->linkName_ = linkName;

  auto model = gz::sim::Model(_entity);
  if (!model.Valid(_ecm))
  {
    gzerr << "KSPlugin_Thrust: 无效的模型实体" << std::endl;
    return;
  }

  this->linkEntity_ = model.LinkByName(_ecm, this->linkName_);
  if (this->linkEntity_ == gz::sim::kNullEntity)
  {
    gzerr << "KSPlugin_Thrust: 找不到 link '" << this->linkName_ << "'" << std::endl;
    return;
  }

  /// 打印 link 实际加载的惯性/质心（用于核对 KSM 配置是否生效）
  {
    /// 诊断日志：输出到 data/log/<时间戳>/，文件名 thrust_<model>_<link>.csv
    DiagnosticLog::init(keship::KeshipLogDir());
    this->dataPtr->logPrefix = this->dataPtr->modelName + "_" + this->linkName_;

    auto inertialComp =
        _ecm.Component<gz::sim::components::Inertial>(this->linkEntity_);
    if (inertialComp)
    {
      const auto &in = inertialComp->Data();
      const auto &mm = in.MassMatrix();
      gzmsg << "KSPlugin_Thrust: link[" << this->linkName_
            << "] 实际惯性 mass=" << mm.Mass()
            << " CoG(link系)=" << in.Pose().Pos()
            << " Ixx=" << mm.Ixx() << " Iyy=" << mm.Iyy()
            << " Izz=" << mm.Izz()
            << " 完整pose=" << in.Pose() << std::endl;
    }
    else
    {
      gzmsg << "KSPlugin_Thrust: link[" << this->linkName_
            << "] 无 Inertial 组件（质心可能在原点）" << std::endl;
    }
  }

  /// 解析 <thrust> 子块
  if (!_sdf->HasElement("thrust"))
  {
    gzmsg << "KSPlugin_Thrust: 无 <thrust> 子块，跳过" << std::endl;
    return;
  }

  auto ptr = const_cast<sdf::Element *>(_sdf.get());
  int count = 0;

  for (sdf::ElementPtr elem = ptr->GetElement("thrust");
       elem;
       elem = elem->GetNextElement("thrust"))
  {
    auto body = std::make_unique<ThrusterBody>();

    auto [name, nameOk] = elem->Get<std::string>("name", "");
    body->name = nameOk ? name : ("thrust_" + std::to_string(count));

    auto [pose, poseOk] = elem->Get<gz::math::Pose3d>("pose", gz::math::Pose3d::Zero);
    if (poseOk) body->poseOffset = pose;

    auto [maxForce, forceOk] = elem->Get<double>("max_force", 1000.0);
    if (forceOk) body->maxForce = maxForce;

    ThrusterBody *bodyPtr = body.get();
    std::string topic = "/keship/thrust/" + this->dataPtr->modelName + "/" + bodyPtr->name;
    if (!this->dataPtr->node.Subscribe(topic,
          std::function<void(const gz::msgs::Double&)>(
            [bodyPtr](const gz::msgs::Double &_msg) {
              bodyPtr->currentLevel.store(
                  std::clamp(_msg.data(), -bodyPtr->maxForce, bodyPtr->maxForce),
                  std::memory_order_relaxed);
            })))
    {
      gzwarn << "KSPlugin_Thrust: 订阅 " << topic << " 失败" << std::endl;
    }
    else
    {
      this->dataPtr->thrusterTopics.push_back(topic);
    }

    gzmsg << "KSPlugin_Thrust: 推进器[" << count << "] name=" << bodyPtr->name
          << " maxForce=" << bodyPtr->maxForce
          << " topic=" << topic << std::endl;

    this->dataPtr->bodies.push_back(std::move(body));
    count++;
  }

  gzmsg << "KSPlugin_Thrust: 配置完成 — " << count << " 个推进器" << std::endl;
}

/////////////////////////////////////////////////
void keship::KSPlugin_Thrust::PreUpdate(
    const gz::sim::UpdateInfo &_info,
    gz::sim::EntityComponentManager &_ecm)
{
  if (_info.paused)
    return;
  if (this->linkEntity_ == gz::sim::kNullEntity)
    return;
  if (this->dataPtr->bodies.empty())
    return;

  auto poseComp = _ecm.Component<gz::sim::components::Pose>(this->entity_);
  if (!poseComp)
    return;

  gz::math::Pose3d modelPose = poseComp->Data();
  auto linkPose = gz::sim::Link(this->linkEntity_).WorldPose(_ecm);
  gz::math::Vector3d linkWorldPos =
      linkPose ? linkPose->Pos() : modelPose.Pos();
  gz::math::Vector3d linkEuler =
      linkPose ? linkPose->Rot().Euler() : modelPose.Rot().Euler();

  /// 质心世界坐标（**仅用于诊断日志**；力矩参考点已于 2026-09-22 改为 link 原点，见下方 ★）
  auto linkInertialPose = gz::sim::Link(this->linkEntity_).WorldInertialPose(_ecm);
  gz::math::Vector3d linkCoMWorldPos =
      linkInertialPose ? linkInertialPose->Pos() : linkWorldPos;

  /// ★ 受力总线（2026-09-22）：力矩参考点（link 原点）在 ForceAccumulator 里**唯一确定**。
  ///   本插件原先按**质心**算力矩，而引擎把力作用于 **link 原点** ⇒ 推进器即便置于质心
  ///   也会产生寄生俯仰（实测 α = 0.009991 rad/s²，正确值应为 0）。
  ///   见 commit 756acd0 与 doc/audit/force_ref_calibration_2026-09-22.md §5b/§7。
  keship::ForceAccumulator acc(linkWorldPos);

  /// 前 10 帧全量打印（无推力基线）；之后仅当推力非零时打印，
  /// 以便采集施加推力后的 totalForce/totalTorque 而不刷屏。
  const bool diagLimit = (this->dataPtr->diagCount < 10);

  for (auto &body : this->dataPtr->bodies)
  {
    double thrustMag = body->currentLevel.load(std::memory_order_relaxed);
    if (std::abs(thrustMag) < 1e-9)
      continue;

    gz::math::Pose3d bodyWorldPose = modelPose * body->poseOffset;

    /// 推力方向 = body 的 local +X 在世界系中的方向
    gz::math::Vector3d thrustDir = bodyWorldPose.Rot() * gz::math::Vector3d::UnitX;
    /// 推力方向 = 箭头（螺旋桨朝向）的反方向
    gz::math::Vector3d thrustForce = thrustDir * (-thrustMag);

    /// 如实声明"力作用在**推进器位置**"—— 绕 link 原点的力矩由 ForceAccumulator
    /// 唯一折算（τ = (推进器 − link原点) × F）。⚠ linkCoMWorldPos 仍用于下方诊断日志。
    acc.Add({"thrust/" + body->name, thrustForce, bodyWorldPose.Pos()});

    if (diagLimit)
      gzmsg << "KSPlugin_Thrust::PreUpdate body[" << body->name
            << "] thrust=" << thrustMag << " force=" << thrustForce << std::endl;
  }

  /// 合计结果（由受力总线统一给出；后面的施加与诊断都读它）
  const gz::math::Vector3d totalForce = acc.TotalForce();
  const gz::math::Vector3d totalTorque = acc.TotalTorque();

  /// 循环外单次施加：合力 + 合力矩写入同一 ExternalWorldWrenchCmd（力施于 link 原点）。
  /// 注意：该组件每帧只消费一次且后写覆盖先写，绝不能在推进器循环内多次调用
  /// AddWorldWrench / SetComponentData，否则前一个推进器会被覆盖丢失。
  if (std::abs(totalForce.Length()) > 1e-9 ||
      std::abs(totalTorque.Length()) > 1e-9)
  {
    gz::sim::Link(this->linkEntity_)
        .AddWorldWrench(_ecm, totalForce, totalTorque);
  }

  const bool diagActive =
      diagLimit ||
      std::abs(totalForce.X()) > 1e-6 || std::abs(totalForce.Y()) > 1e-6 ||
      std::abs(totalForce.Z()) > 1e-6 ||
      std::abs(totalTorque.X()) > 1e-6 || std::abs(totalTorque.Y()) > 1e-6 ||
      std::abs(totalTorque.Z()) > 1e-6;

  if (diagActive)
  {
    /// 记录合力/合力矩（totalTorque 为绕质心的计算值，仅诊断；
    /// 实际施加为 link 原点合力 + 该合力矩）及参考点位姿（写入日志文件，非终端）
    this->dataPtr->logWriteCounter++;
    DiagnosticLog::write(
        "thrust_" + this->dataPtr->logPrefix + ".csv",
        "frame,simTimeSec,linkX,linkY,linkZ,roll,pitch,yaw,comX,comY,comZ,totalFx,totalFy,totalFz,totalTx,totalTy,totalTz",
        std::to_string(this->dataPtr->logWriteCounter) + ","
        + std::to_string(std::chrono::duration<double>(_info.simTime).count()) + ","
        + std::to_string(linkWorldPos.X()) + "," + std::to_string(linkWorldPos.Y()) + "," + std::to_string(linkWorldPos.Z()) + ","
        + std::to_string(linkEuler.X()) + "," + std::to_string(linkEuler.Y()) + "," + std::to_string(linkEuler.Z()) + ","
        + std::to_string(linkCoMWorldPos.X()) + "," + std::to_string(linkCoMWorldPos.Y()) + "," + std::to_string(linkCoMWorldPos.Z()) + ","
        + std::to_string(totalForce.X()) + "," + std::to_string(totalForce.Y()) + "," + std::to_string(totalForce.Z()) + ","
        + std::to_string(totalTorque.X()) + "," + std::to_string(totalTorque.Y()) + "," + std::to_string(totalTorque.Z()));
    this->dataPtr->diagCount++;
  }
}

/////////////////////////////////////////////////
keship::KSPlugin_Thrust::Implementation::~Implementation()
{
  for (const auto &t : thrusterTopics)
    node.Unsubscribe(t);
}

/**************************************************************************************************************/
GZ_ADD_PLUGIN(keship::KSPlugin_Thrust,
              gz::sim::System,
              gz::sim::ISystemConfigure,
              gz::sim::ISystemPreUpdate)

GZ_ADD_PLUGIN_ALIAS(keship::KSPlugin_Thrust,
                    "KSPlugin_Thrust")
