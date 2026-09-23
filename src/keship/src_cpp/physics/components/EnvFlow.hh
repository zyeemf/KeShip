/*
 * 环境流动 ECM 组件 —— 流速 / 风速（2026-09-22，T5）
 *
 * 用途：把「水域的风 / 流参数」从 KSPlugin_WavesModel（world 级，持有 WaterArea）
 *       传给 KSPlugin_HydroBuoyancy（model 级，算受力）。
 *
 * 数据流：
 *   EnvironmentParamsDialog → WaterArea.windParams / currentParams → JSON
 *     → KSPlugin_WavesModel::RebuildFromJson（已有 WaterArea）
 *     → 写入本组件（挂在 wavefield_{水区名} 实体上，与 Wavefield 组件同处）
 *     → KSPlugin_HydroBuoyancy 按水区查实体 → 读本组件 → 算流力 / 风压力
 *
 * 为什么用 ECM 组件而不是话题：HydroBuoyancy 已在 PreUpdate（仿真线程）里
 * 读 ECM（Wavefield 组件）⇒ 同一模式、无跨线程问题；且风/流是"世界状态"
 * 而非"事件"，每帧可读、无需缓存。
 *
 * ★★ 方向约定（2026-09-23 统一为 NED，张老师："以船舶为主题"）：
 *   **0 = +X（北），顺时针** —— 与 Fossen 6-DOF、波浪方向、船体几何一致。
 *   ⇒ 单位方向向量 = (cos(dir), sin(dir), 0)
 *      dir = 0     → (+1, 0, 0) = 正北
 *      dir = π/2   → (0, +1, 0) = 正东
 *
 * ⚠ 这与 Gazebo 世界系的 ENU 惯例（+X 东、+Y 北）**不同** —— 本项目的方向类参数
 *   一律按 NED 解释。此前本文件与流向/风向的计算曾用 (sin, cos)（ENU），
 *   与波浪方向差 90°，已于 2026-09-23 统一修正。
 *
 * ⚠ 改动任一处方向约定，都要同步核对四处实现：
 *   `Wavefield.cc`（波浪）、`WaveVisual.cc`（流向视觉）、
 *   `KSPlugin_HydroBuoyancy.cc`（环境力）、`GerstnerWaves_vs_330.glsl`（shader）。
 */

#ifndef KESHIP_COMPONENTS_ENVFLOW_HH_
#define KESHIP_COMPONENTS_ENVFLOW_HH_

#include <gz/sim/components/Component.hh>
#include <gz/sim/components/Factory.hh>
#include <gz/sim/components/Serialization.hh>
#include <gz/sim/config.hh>

namespace keship
{
namespace components
{
  /// \brief 环境流动参数（流速 / 风速）。
  struct EnvFlowData
  {
    /// \brief 流速（表层），m/s
    double currentSpeed{0.0};

    /// \brief 流向，rad（**0 = +X 北向**，顺时针 —— NED，见文件头说明）
    double currentDirection{0.0};

    /// \brief 风速，m/s
    double windSpeed{0.0};

    /// \brief 风向，rad（**0 = +X 北向**，顺时针 —— NED，见文件头说明）
    double windDirection{0.0};
  };

  /// \brief 环境流动组件（挂在 wavefield_{水区名} 实体上）
  using EnvFlow = gz::sim::components::Component<EnvFlowData, class EnvFlowTag>;
  GZ_SIM_REGISTER_COMPONENT("keship_components.EnvFlow", EnvFlow)
}
}

#endif  // KESHIP_COMPONENTS_ENVFLOW_HH_
