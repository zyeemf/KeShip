/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* KeShip 浮力-水动力合并插件 — 统一浮力(Fossen恢复力矩)与6-DOF水动力 */

#ifndef KESHIP_PLUGIN_HYDRO_BUOYANCY_HH_
#define KESHIP_PLUGIN_HYDRO_BUOYANCY_HH_

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <gz/sim/System.hh>
#include <gz/utils/ImplPtr.hh>
#include <gz/transport/Node.hh>

#include <gz/sim/Link.hh>
#include <gz/math/Pose3.hh>
#include <gz/math/Matrix6.hh>
#include <gz/msgs/param.pb.h>

#include "Wavefield.hh"
#include "keship/utils/WaterArea.hh"
#include "../utils/WaterAreaManager.hh"
#include "components/Wavefield.hh"
#include "components/EnvFlow.hh"
#include "../utils/PlanarMooring.hh"

namespace keship
{
  class Buoyancy;
  class FossenModel;
  class ForceAccumulator;
  class ForceAccumulator;

  /// 单个浮力体描述
  struct BuoyancyBody
  {
    std::string name;
    std::shared_ptr<Buoyancy> buoyancy;
    gz::math::Pose3d poseOffset{0, 0, 0, 0, 0, 0};
    double totalVolume = 0.0;
    double characteristicLengthSq = 1.0;

    /// ★ T5（2026-09-22）：浮力体在**自身坐标系**下的包围盒，
    /// 由网格顶点算出（Configure 时一次）。供"迎流/迎风面积"与作用点估算用：
    ///   型宽 B = bboxMax.Y − bboxMin.Y
    ///   型深 D = bboxMax.Z − bboxMin.Z
    /// bboxValid 为 false 时表示未取到几何（此时流力/风压力按 0 处理）。
    gz::math::Vector3d bboxMin{0, 0, 0};
    gz::math::Vector3d bboxMax{0, 0, 0};
    bool bboxValid = false;

    /// ★ S3（2026-10-07）：浮力体几何 —— 供「按几何计算 M_A」用（Configure 时一次）。
    /// 目前只对 **圆柱** 启用（解析式只对圆柱成立）；Box / Mesh 保持 false。
    bool hasCylinder = false;
    double cylRadius = 0.0;
    double cylLength = 0.0;
  };

  class KSPlugin_HydroBuoyancy : public gz::sim::System,
                                  public gz::sim::ISystemConfigure,
                                  public gz::sim::ISystemPreUpdate
  {
  public:
    KSPlugin_HydroBuoyancy();
    ~KSPlugin_HydroBuoyancy() override = default;

  public:
    void Configure(const gz::sim::Entity &_entity,
                   const std::shared_ptr<const sdf::Element> &_sdf,
                   gz::sim::EntityComponentManager &_ecm,
                   gz::sim::EventManager &_eventMgr) override;

    void PreUpdate(const gz::sim::UpdateInfo &_info,
                   gz::sim::EntityComponentManager &_ecm) override;

  private:
    void LoadParameters(const std::shared_ptr<const sdf::Element> &_sdf);
    void LoadSingleMeshBuoyancy(gz::sim::EntityComponentManager &_ecm);
    void LoadMultiBuoyancy(const std::shared_ptr<const sdf::Element> &_sdf,
                           gz::sim::EntityComponentManager &_ecm);
    void computePoseBasedDamping(
        gz::sim::EntityComponentManager &_ecm,
        const gz::math::Pose3d &_modelPose,
        double _totalSubVol,
        double _effectiveFluidLevel,
        gz::math::Vector3d &_force,
        gz::math::Vector3d &_torque,
        double _bodyMass);

    void ApplyForces(gz::sim::EntityComponentManager &_ecm,
                     const gz::math::Vector3d &_force,
                     const gz::math::Vector3d &_torque);

    /// 软系泊（MS-02/03/06）：仅约束水平位置与世界航向，
    /// 保留 roll/pitch/heave 自由。力作用于质心、力矩为世界 Z 纯力矩。
    void ApplyMooring(gz::sim::EntityComponentManager &_ecm,
                      const gz::sim::UpdateInfo &_info,
                      const gz::math::Pose3d &_linkWorldPose,
                      keship::ForceAccumulator &_acc);


  private:
    gz::sim::Entity entity_{gz::sim::kNullEntity};

    std::string linkName_{"base_link"};
    gz::sim::Entity linkEntity_{gz::sim::kNullEntity};
    gz::sim::Link linkObj_{gz::sim::kNullEntity};

    /// \brief 模型实例名（日志文件名前缀用，多模型避免交错）
    std::string modelName_{""};
    /// \brief 诊断日志前缀 "模型名_link名"
    std::string logPrefix_{""};
    /// \brief 完整 scoped 名 "world::model"（软系泊目标身份）
    std::string scopedName_{""};
    /// \brief 完整 scoped 名 "world::model"（软系泊目标身份）

    std::string meshPath_{};

    bool enableBuoyancy_{true};
    bool enableHydro_{false};

    double fluidLevel_{0.0};
    double waterThreshold_{0.5};

    GZ_UTILS_UNIQUE_IMPL_PTR(dataPtr)
  };

  class KSPlugin_HydroBuoyancy::Implementation
  {
  public:
    double fluidDensity_{1000.0};
    gz::math::Vector3d gravity_{0.0, 0.0, -9.81};

    /// 多浮力体列表
    std::vector<BuoyancyBody> bodies;

    /// 单网格模式参数
    double cylinderRadius_{0.0};
    double cylinderLength_{0.0};
    int cylinderSegments_{32};
    bool hasCylinderGeometry_{false};

    std::string dampingMode_{"fossen"};

    /// 目标阻尼比 (pose_based 模式用)
    double dampingRatio_{0.1};

    /// Fossen 水动力模型
    std::unique_ptr<FossenModel> fossen;

    gz::transport::Node node;
    std::mutex mutex;

    /// ── 软系泊（MS-02/03/06）──────────────────────────────────────
    /// 命令由实验核心（GUI/CLI）发布，本插件按完整 scoped 名过滤。
    /// 只约束水平位置与 yaw；roll/pitch/heave 不受直接约束。
    PlanarMooringMailbox mooringMailbox;
    gz::transport::Node::Publisher mooringStatusPub;
    bool mooringStatusPubReady = false;
    uint64_t mooringSessionId = 0;
    PlanarMooringReference mooringReference;
    bool mooringApplied = false;
    int mooringStatusCounter = 0;
    gz::math::Vector3d mooringLastForce{0.0, 0.0, 0.0};
    double mooringLastTorque = 0.0;
    bool mooringForceSaturated = false;
    bool mooringYawSaturated = false;

    /// 浸水体积对外发布（opt-in：SDF 写 <publish_subvolume>true</publish_subvolume>）
    /// 默认关闭 —— 仅供排水量实验（HS-02）作为"实测 ∇"数据源；
    /// 未开启时零开销、行为与原先完全一致
    bool publishSubVol_ = false;
    std::string subVolTopic_;
    gz::transport::Node::Publisher subVolPub_;
    /// 发布降频计数（逐帧发布会淹没 transport，固定每 N 帧发一次）
    int subVolPubCounter_ = 0;

    /// 全局波浪场引用（gz-waves 模式：从水区波浪场实体 ECM 组件获取，
    /// 见 notes/waves_global_refactor.md）
    std::weak_ptr<Wavefield> activeWavefield_;

    /// 仿真时间
    std::chrono::steady_clock::duration simTime_{0};

    ~Implementation();

    bool alive_ = true;

    /// 日志写入降频计数器（非核心逻辑）
    int logWriteCounter{0};

    /// 每 body 浮力计算诊断限频计数器（成员变量，避免多实例共享 static）
    int diagBodyCount_{0};

    /// 多水区支持
    WaterAreaManager waterAreaMgr_;
    bool waterAreasConfigured_ = false;
    std::string currentAreaName_;
    int waterAreaReloadCounter_ = 0;
    /// \brief 波浪场引用缺失时的重试节流计数（每 30 帧重查一次）
    int wfRetryCounter_ = 0;
    std::filesystem::file_time_type lastWaterAreasMtime_;

    /// 阻尼分量诊断缓存
    struct HydroDiag {
        double tauD[6]{};   // -D·ν
        bool hydroActive = false;
        int waterFrameCount = 0;
    } hydroDiag;
    double lastWaveHeight_ = 0.0;

    /// ★ T5（2026-09-22）：环境流动（流速 / 风速）。
    /// 由 KSPlugin_WavesModel 写入 `wavefield_{水区}` 实体上的 EnvFlow 组件，
    /// 本插件按当前水区读取（与 Wavefield 组件同处、同一把锁保护的查找路径）。
    /// 速度全为 0 时两项力**精确为 0** ⇒ 静水无风场景逐位不变（即软开关）。
    components::EnvFlowData envFlow_;

    /// ★ T5（2026-09-22）流速 / 风速外力系数（全部 SDF 可调，便于按船型标定）
    double currentDragCoeff_{1.0};    ///< <current_drag_coeff>  流阻力系数 C_d,w（钝体侧向）
    double windDragCoeff_{1.2};       ///< <wind_drag_coeff>     风阻力系数 C_d,a（船体水上部分）
    double airDensity_{1.225};        ///< <air_density>         空气密度 kg/m³
    double currentAreaScale_{1.0};    ///< <current_area_scale>  迎流面积缩放（标定用）
    double windAreaScale_{1.0};       ///< <wind_area_scale>     迎风面积缩放（标定用）

    /// \brief 当前水区的波浪场实体 ID（缓存在此，供**每帧**读 EnvFlow 组件用；
    /// 流速/风速可能被环境编辑器即时改动，不能只在"水区切换时"读一次）
    gz::sim::Entity activeWfEntity_{gz::sim::kNullEntity};

    /// \brief 该 link 是否有**非零**流体附加质量（Configure 时判定一次）。
    /// 为 false 时"出水取消 M_A"整段不生效 ⇒ 其他模型完全不受影响。
    bool hasAddedMass_ = false;

    /// \brief 当前是否处于"附加质量已取消"状态（用于**只在翻转时**下发命令，
    /// 避免每帧调用 Link::SetFluidAddedMass 触发引擎重算惯性）。
    bool maCancelled_ = false;

    /// ★ S3（2026-10-07） 按吃水、逐浮体的 M_A 参数化。
    ///   KESHIP_MA_DRAFT=1 ⇒ 启用（每帧按各浮体浸没比 τ_i 计算并合成整船 6×6）；
    ///   未设 / =0 ⇒ 现状（S2 二值），行为逐位不变。
    bool maDraftEnabled_ = false;
    /// 逐浮体浸没比 τ_i = V_sub,i / V_i（每帧更新；水外或未算 ⇒ 0）。
    std::vector<double> bodySubFrac_;
    /// 下发门控：上次实际下发的 M_A 与其有效性。
    gz::math::Matrix6d lastMaSent_{gz::math::Matrix6d::Zero};
    bool maSentValid_ = false;

    /// ★ S3′（2026-10-07）：解析平衡浸没比 tau0 = (m/ρ) / ΣV（Configure 时一次算）。
    ///   ≤0 ⇒ 不可用（运行时退回读 ECM 原值）。S3′ 用**固定 tau0**（不随帧变），
    ///   只在首次入水下发一次 ⇒ 不每帧改惯性、不破坏动量守恒。
    double maTau0_ = 0.0;
    /// ★ S3′：跟踪"是否已下发过归零"（完全出水置位、重新入水清位）。
    bool maZeroed_ = false;

    /// ★ S3′（2026-10-07）：静止门控用 —— 上一步世界位姿（差分求速度）与其有效性。
    gz::math::Vector3d lastPosePos_{gz::math::Vector3d::Zero};
    bool lastPoseValid_ = false;
    /// 最近一次算得的线速度（诊断用）。
    double maLastVn_ = 0.0;
  };

} // namespace keship

#endif // KESHIP_PLUGIN_HYDRO_BUOYANCY_HH_
