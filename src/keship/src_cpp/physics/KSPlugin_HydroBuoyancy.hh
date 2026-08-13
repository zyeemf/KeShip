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
#include <gz/msgs/param.pb.h>

#include "Wavefield.hh"
#include "keship/utils/WaterArea.hh"
#include "../utils/WaterAreaManager.hh"
#include "components/Wavefield.hh"

namespace keship
{
  class Buoyancy;
  class FossenModel;

  /// 单个浮力体描述
  struct BuoyancyBody
  {
    std::string name;
    std::shared_ptr<Buoyancy> buoyancy;
    gz::math::Pose3d poseOffset{0, 0, 0, 0, 0, 0};
    double totalVolume = 0.0;
    double characteristicLengthSq = 1.0;
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

  private:
    gz::sim::Entity entity_{gz::sim::kNullEntity};

    std::string linkName_{"base_link"};
    gz::sim::Entity linkEntity_{gz::sim::kNullEntity};
    gz::sim::Link linkObj_{gz::sim::kNullEntity};

    /// \brief 模型实例名（日志文件名前缀用，多模型避免交错）
    std::string modelName_{""};
    /// \brief 诊断日志前缀 "模型名_link名"
    std::string logPrefix_{""};

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
    std::filesystem::file_time_type lastWaterAreasMtime_;

    /// 阻尼分量诊断缓存
    struct HydroDiag {
        double tauD[6]{};   // -D·ν
        bool hydroActive = false;
        int waterFrameCount = 0;
    } hydroDiag;
    double lastWaveHeight_ = 0.0;
  };

} // namespace keship

#endif // KESHIP_PLUGIN_HYDRO_BUOYANCY_HH_
