/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Keship外部力施加器 — 接收外部质量指令, 修改惯性组件 */
/// ZhangYu 创建 2026.3.2
/// ZhaoYu 重构 2026.7.16: Controller 拆分, VariableLoad → ExternalForce

#ifndef KESHIP_PLUGIN_EXTERNAL_FORCE_HH_
#define KESHIP_PLUGIN_EXTERNAL_FORCE_HH_

#include <string>

#include <gz/sim/System.hh>
#include <gz/utils/ImplPtr.hh>
#include <gz/transport/Node.hh>

#include <gz/sim/Link.hh>

namespace keship
{

  class KSPlugin_VariableMass : public gz::sim::System,
                                 public gz::sim::ISystemConfigure,
                                 public gz::sim::ISystemPreUpdate
  {
  public:
    KSPlugin_VariableMass();
    ~KSPlugin_VariableMass() override = default;

  public:
    void Configure(const gz::sim::Entity &_entity,
                   const std::shared_ptr<const sdf::Element> &_sdf,
                   gz::sim::EntityComponentManager &_ecm,
                   gz::sim::EventManager &_eventMgr) override;

    void PreUpdate(const gz::sim::UpdateInfo &_info,
                   gz::sim::EntityComponentManager &_ecm) override;

  private:
    void LoadParameters(const gz::sim::Entity &_entity,
                        const std::shared_ptr<const sdf::Element> &_sdf,
                        gz::sim::EntityComponentManager &_ecm);

    gz::sim::Entity entity_{gz::sim::kNullEntity};
    gz::sim::EntityComponentManager *ecm_{nullptr};

    GZ_UTILS_UNIQUE_IMPL_PTR(dataPtr)
  };

  class KSPlugin_VariableMass::Implementation
  {
  public:
    std::string linkName_{"base_link"};
    gz::sim::Entity linkEntity_{gz::sim::kNullEntity};
    gz::sim::Link link_{gz::sim::kNullEntity};

    gz::transport::Node node;
    std::mutex mutex;

    double baseMass_ = 0.0;
    double payloadMass_ = 0.0;
    bool dirty_ = false;
    std::string topicName;

    ~Implementation();
    bool alive_ = true;
  };

} // namespace keship

#endif // KESHIP_PLUGIN_EXTERNAL_FORCE_HH_
