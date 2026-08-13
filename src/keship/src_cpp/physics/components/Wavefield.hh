/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 波浪场 ECM 组件 — 全局波浪场共享（gz-waves 模式）
 *
 * 世界级 KSPlugin_WavesModel 为每个水区创建波浪场对象，
 * 以 weak_ptr 存入本组件（挂在水区波浪场实体上）；
 * 模型浮力插件按水区名查实体 → 读组件 → 拿引用采样波高。
 *
 * 参考 notes/waves_global_refactor.md
 */

#ifndef KESHIP_COMPONENTS_WAVEFIELD_HH_
#define KESHIP_COMPONENTS_WAVEFIELD_HH_

#include <memory>

#include <gz/sim/components/Component.hh>
#include <gz/sim/components/Factory.hh>
#include <gz/sim/components/Serialization.hh>
#include <gz/sim/config.hh>

#include "../Wavefield.hh"

namespace keship
{
namespace components
{
  /// \brief 波浪场组件：持有全局波浪场对象的弱引用
  /// （强引用由 KSPlugin_WavesModel 私有持有，保证生命周期）。
  using Wavefield = gz::sim::components::Component<
      std::weak_ptr<keship::Wavefield>, class WavefieldTag>;
  GZ_SIM_REGISTER_COMPONENT("keship_components.Wavefield", Wavefield)
}
}

#endif // KESHIP_COMPONENTS_WAVEFIELD_HH_
