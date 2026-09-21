/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 线性阻尼系数换算 —— 供各实验对话框共用（2026-09-19 新增）
 *
 * 背景（doc/plans/headless_ctl_plan.md §7.5 / §14）：
 *   插件 KSPlugin_HydroBuoyancy 的阻尼有两条分支（cc:427-469）：
 *     · pose_based —— **需要 hasCylinderGeometry_**，网格船体 / 长方体走不到；
 *     · fossen     —— 读 SDF 的 <linear_damping>（Zw/Kp/Mq）。
 *   所以网格船体与长方体**不给 <linear_damping> 就是零阻尼** → 横摇长时间不衰减 →
 *   "等横倾角稳定"类判据永不满足（SP-03 Inclining / SP-04 HullStability 因此不可用）。
 *
 *   ⚠️ 另一个坑：插件在 `enable_hydro=true` 而 fossen **无效**时会**整体 return，
 *      连浮力都不施加**（cc:200-203）→ 系数必须有正下限兜底，否则模型会沉底。
 */

#ifndef KESHIP_LINEAR_DAMPING_HH_
#define KESHIP_LINEAR_DAMPING_HH_

#include <algorithm>
#include <cmath>

namespace keship
{
  /// \brief fossen 线性阻尼三系数（对应 SDF 的 <linear_damping><Zw/Kp/Mq>）
  struct LinearDampingCoeffs
  {
    double Zw = 0.0;   ///< 垂荡
    double Kp = 0.0;   ///< 横摇
    double Mq = 0.0;   ///< 纵摇
  };

  /// \brief 由静水恢复刚度与质量/惯量反算线性阻尼系数：c = 2·ζ·√(k·m)
  ///
  /// \param _Awp       水线面面积 [m²]
  ///                   长方体恒为 L·B；数学船型取 d∇/dT（体积曲线求导）
  /// \param _L/_B      船长 / 型宽 [m]，用于水线面惯性矩的矩形近似 I ≈ A_wp·B²/12
  ///                   （长方体时该近似**精确**：A_wp·B²/12 = L·B³/12）
  /// \param _mass      船体质量 [kg]
  /// \param _ixx/_iyy  横摇 / 纵摇惯量 [kg·m²]
  /// \param _zetaHeave 目标垂荡阻尼比（取插件 pose_based 同值 0.15）
  /// \param _zetaRot   目标转动阻尼比（同值 0.10）
  /// \param _rho/_g    流体密度 / 重力加速度 —— **必须与 SDF 的 <fluid_density> 同源**
  ///                   （P3-2 教训：写两处必漏一处）
  inline LinearDampingCoeffs MakeLinearDamping(
      double _Awp, double _L, double _B,
      double _mass, double _ixx, double _iyy,
      double _zetaHeave = 0.15, double _zetaRot = 0.10,
      double _rho = 1000.0, double _g = 9.81)
  {
    LinearDampingCoeffs c;
    /// 正下限 1e-3：防退化参数把 fossen 弄成"无效"，那会连浮力一起丢掉
    c.Zw = std::max(1e-3, 2.0 * _zetaHeave *
        std::sqrt(_rho * _g * _Awp * _mass));
    c.Kp = std::max(1e-3, 2.0 * _zetaRot *
        std::sqrt(_rho * _g * (_Awp * _B * _B / 12.0) * _ixx));
    c.Mq = std::max(1e-3, 2.0 * _zetaRot *
        std::sqrt(_rho * _g * (_Awp * _L * _L / 12.0) * _iyy));
    return c;
  }

}  // namespace keship

#endif  // KESHIP_LINEAR_DAMPING_HH_
