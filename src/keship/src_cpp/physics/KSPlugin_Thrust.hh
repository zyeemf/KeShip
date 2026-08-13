/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Keship推进器插件 — 多推进器支持，固定方向推力 */
/// ZhangYu 创建 2026.5.11

#ifndef KESHIP_PLUGIN_THRUST_HH_
#define KESHIP_PLUGIN_THRUST_HH_

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gz/sim/System.hh>
#include <gz/utils/ImplPtr.hh>
#include <gz/transport/Node.hh>
#include <gz/math/Pose3.hh>

namespace keship
{
  /// 单个推进器运行时数据
  struct ThrusterBody
  {
    std::string name;
    gz::math::Pose3d poseOffset{0, 0, 0, 0, 0, 0};
    double maxForce = 1000.0;
    std::atomic<double> currentLevel{0.0};
  };

  class KSPlugin_Thrust : public gz::sim::System,
                          public gz::sim::ISystemConfigure,
                          public gz::sim::ISystemPreUpdate
  {
  public:
    KSPlugin_Thrust();
    ~KSPlugin_Thrust() override = default;

  public:
    void Configure(const gz::sim::Entity &_entity,
                   const std::shared_ptr<const sdf::Element> &_sdf,
                   gz::sim::EntityComponentManager &_ecm,
                   gz::sim::EventManager &_eventMgr) override;

    void PreUpdate(const gz::sim::UpdateInfo &_info,
                   gz::sim::EntityComponentManager &_ecm) override;

  private:
    gz::sim::Entity entity_{gz::sim::kNullEntity};
    gz::sim::Entity linkEntity_{gz::sim::kNullEntity};
    std::string linkName_{"base_link"};

    GZ_UTILS_UNIQUE_IMPL_PTR(dataPtr)
  };

  class KSPlugin_Thrust::Implementation
  {
  public:
    std::vector<std::unique_ptr<ThrusterBody>> bodies;
    std::vector<std::string> thrusterTopics;
    gz::transport::Node node;
    std::string modelName;
    std::string logPrefix;
    uint64_t logWriteCounter = 0;
    int diagCount = 0;

    ~Implementation();
  };

} // namespace keship

#endif // KESHIP_PLUGIN_THRUST_HH_
