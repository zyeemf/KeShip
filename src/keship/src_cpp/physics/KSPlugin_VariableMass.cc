/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Keship外部力施加器 — 接收质量指令, 修改 InertialComponent */

#include "KSPlugin_VariableMass.hh"

#include <iostream>

#include <gz/plugin/Register.hh>
#include <gz/sim/Model.hh>
#include <gz/sim/components/Name.hh>
#include <gz/sim/components/Pose.hh>
#include <gz/sim/components/Inertial.hh>
#include <gz/msgs/double.pb.h>

keship::KSPlugin_VariableMass::KSPlugin_VariableMass()
    : System(), dataPtr(gz::utils::MakeUniqueImpl<Implementation>())
{
  std::cout << "KSPlugin_VariableMass: 初始化" << std::endl;
}

void keship::KSPlugin_VariableMass::Configure(
    const gz::sim::Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    gz::sim::EntityComponentManager &_ecm,
    gz::sim::EventManager &_eventMgr)
{
  std::cout << "KSPlugin_VariableMass: 配置实体 " << _entity << std::endl;
  this->entity_ = _entity;
  this->ecm_ = &_ecm;
  this->LoadParameters(_entity, _sdf, _ecm);
}

void keship::KSPlugin_VariableMass::PreUpdate(
    const gz::sim::UpdateInfo &_info,
    gz::sim::EntityComponentManager &_ecm)
{
  if (!dataPtr->dirty_) return;
  if (dataPtr->linkEntity_ == gz::sim::kNullEntity) return;

  auto inertialComp = _ecm.Component<gz::sim::components::Inertial>(
      dataPtr->linkEntity_);
  if (!inertialComp) return;

  auto inertial = inertialComp->Data();
  double newMass = dataPtr->baseMass_ + dataPtr->payloadMass_;
  if (newMass <= 0.0) newMass = dataPtr->baseMass_;
  auto mm = inertial.MassMatrix();
  mm.SetMass(newMass);
  inertial.SetMassMatrix(mm);
  _ecm.SetComponentData<gz::sim::components::Inertial>(
      dataPtr->linkEntity_, inertial);

  dataPtr->dirty_ = false;
}

void keship::KSPlugin_VariableMass::LoadParameters(
    const gz::sim::Entity &_entity,
    const std::shared_ptr<const sdf::Element> &_sdf,
    gz::sim::EntityComponentManager &_ecm)
{
  auto [linkName, linkOk] = _sdf->Get<std::string>("link_name", "");
  if (linkOk) this->dataPtr->linkName_ = linkName;

  gz::sim::Model model(_entity);
  this->dataPtr->link_ = gz::sim::Link(
      model.LinkByName(_ecm, this->dataPtr->linkName_));
  this->dataPtr->linkEntity_ = this->dataPtr->link_.Entity();

  if (!this->dataPtr->link_.Valid(_ecm))
  {
    gzerr << "KSPlugin_VariableMass: 找不到 link '"
          << this->dataPtr->linkName_ << "'" << std::endl;
    return;
  }

  auto ic = _ecm.Component<gz::sim::components::Inertial>(
      this->dataPtr->linkEntity_);
  if (ic)
    this->dataPtr->baseMass_ = ic->Data().MassMatrix().Mass();

  std::cout << "KSPlugin_VariableMass: 配置完成 — link="
            << this->dataPtr->linkName_
            << " baseMass=" << this->dataPtr->baseMass_ << std::endl;

  bool enabled = false;
  if (_sdf->HasElement("external_force"))
    enabled = true;
  else if (_sdf->HasElement("variable_load"))
    enabled = true;

  if (enabled)
  {
    auto ptr = const_cast<sdf::Element *>(_sdf.get());
    auto elem = ptr->HasElement("external_force")
        ? ptr->GetElement("external_force")
        : ptr->GetElement("variable_load");
    auto [vlName, vlOk] = elem->Get<std::string>("link_name", "");
    if (vlOk)
    {
      std::string modelName = model.Name(_ecm);
      dataPtr->topicName = "/keship/sp01/payload/" + modelName;

      std::function<void(const gz::msgs::Double &)> cb =
          [this](const gz::msgs::Double &_msg) {
            std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
            if (!this->dataPtr->alive_) return;
            this->dataPtr->payloadMass_ = _msg.data();
            this->dataPtr->dirty_ = true;
          };
      dataPtr->node.Subscribe(dataPtr->topicName, cb);

      std::cout << "KSPlugin_VariableMass: 已启用, topic="
                << dataPtr->topicName << std::endl;
    }
  }
}

keship::KSPlugin_VariableMass::Implementation::~Implementation()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    alive_ = false;
  }
  if (!topicName.empty())
    node.Unsubscribe(topicName);
}

GZ_ADD_PLUGIN(keship::KSPlugin_VariableMass,
              gz::sim::System,
              gz::sim::ISystemConfigure,
              gz::sim::ISystemPreUpdate)

GZ_ADD_PLUGIN_ALIAS(keship::KSPlugin_VariableMass,
                    "KSPlugin_VariableMass")
