/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 波浪场世界级系统（gz-waves 模式）— 全局波浪参数唯一所有权
 *
 * 为每个水区创建一个波浪场对象 + 实体 "wavefield_{水区名}"，
 * 波浪场以 weak_ptr 写入 ECM 组件（keship::components::Wavefield），
 * 模型浮力插件按水区名查实体取引用采样波高。
 *
 * 参数来源：
 *  - data/keship_water_areas.json（GUI 写入，每水区 waveParams；mtime 轮询 1s）
 *  - /keship/wavefield/parameters_set（PMS，GUI/实验对话框；回调仅存副本，
 *    仿真线程 PreUpdate 应用；带 area 键只更新指定水区，无 area 更新全部）
 *
 * 参考 notes/waves_global_refactor.md
 */

#ifndef KESHIP_PLUGIN_WAVES_MODEL_HH_
#define KESHIP_PLUGIN_WAVES_MODEL_HH_

#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <gz/msgs/param.pb.h>
#include <gz/sim/System.hh>
#include <gz/transport/Node.hh>

#include "Wavefield.hh"

namespace keship
{
  class KSPlugin_WavesModelPrivate;

  class KSPlugin_WavesModel : public gz::sim::System,
                              public gz::sim::ISystemConfigure,
                              public gz::sim::ISystemPreUpdate
  {
    public: KSPlugin_WavesModel();
    public: ~KSPlugin_WavesModel() override;

    public: void Configure(const gz::sim::Entity &_entity,
                           const std::shared_ptr<const sdf::Element> &_sdf,
                           gz::sim::EntityComponentManager &_ecm,
                           gz::sim::EventManager &_eventMgr) override;

    public: void PreUpdate(const gz::sim::UpdateInfo &_info,
                           gz::sim::EntityComponentManager &_ecm) override;

    private: std::unique_ptr<KSPlugin_WavesModelPrivate> dataPtr;
  };
}

#endif // KESHIP_PLUGIN_WAVES_MODEL_HH_
