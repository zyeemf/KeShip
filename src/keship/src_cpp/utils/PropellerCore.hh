/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— 螺旋桨敞水与船-桨匹配内核（不依赖 Qt / gz）
 *
 * 职责：① 由几何（直径/螺距比/盘面比/叶数）算敞水特性 KT(J)、KQ(J)、η0(J)；
 *       ② 解船桨平衡 T(1−t) = R，给出转速、推力、扭矩、功率与推进质量系数 QPC。
 *
 * 为什么与 ResistanceCore 分开：敞水特性只与螺旋桨几何有关，与船体无关；
 * 匹配层再把两者接起来。这样 RP-03（敞水）可以完全脱离船体单独做实验。
 *
 * 公式与系数出处：
 *   ① 敞水系数定义：J = Va/(nD)、KT = T/(ρn²D⁴)、KQ = Q/(ρn²D⁵)、
 *      η0 = J·KT/(2π·KQ) —— ITTC 敞水试验标准定义
 *   ② KT/KQ 采用 **Wageningen B 系列（Oosterveld & van Oossanen, 1975）** 的
 *      多项式展开形式：X = Σ A_stuv · J^s · (P/D)^t · (Ae/A0)^u · Z^v
 *      本模块保留 B3-50（Z=3, Ae/A0=0.50）的 s≤2 子集，并把系数按该桨的
 *      公开敞水特性锚点标定（锚点见 .cc 内 kKt0/kKq0 注释与报告）。
 *      适用 J 范围：0 ≤ J ≤ 0.9·(P/D)（本实现到 J=1.2 仍连续，但超出零推力点
 *      后 KT<0 属物理外延，不是回归有效区）。
 *   ③ 伴流与推力减额：J 用**实效伴流速度** Va = (1−w)V；平衡式用 T(1−t)=R；
 *      QPC = η0·(1−t)/(1−w) —— 船身效率 ηH = (1−t)/(1−w) 的标准定义
 */

#ifndef KESHIP_PROPELLER_CORE_HH_
#define KESHIP_PROPELLER_CORE_HH_

#include <vector>

#include "ResistanceCore.hh"

namespace keship {

/// 螺旋桨几何。默认值为 RP-03 教学桨（B3-50，D=0.15 m，等螺距 P/D=1.0）
struct PropellerInput {
  double D = 0.15;      ///< 直径 (m)
  double P_over_D = 1.0;///< 螺距比 P/D
  double Ae_A0 = 0.5;   ///< 盘面比（伸张面积/盘面积）
  double Z = 3.0;       ///< 叶数（B 系列回归里按实数幂参与，故用 double）
};

/// 敞水特性单点
struct OpenWaterPoint {
  double J = 0.0;    ///< 进速系数
  double KT = 0.0;   ///< 推力系数
  double KQ = 0.0;   ///< 扭矩系数
  double eta0 = 0.0; ///< 敞水效率 η0 = J·KT/(2π·KQ)
};

/// 单点敞水计算。J<0 时按 0 处理；J 超出回归有效区仍连续返回（见文件头说明）。
OpenWaterPoint OpenWaterAtJ(const PropellerInput &_p, double _J);

/// 敞水曲线 [0, _Jmax]，_n 个等距点（含端点）。
std::vector<OpenWaterPoint> OpenWaterCurve(const PropellerInput &_p, double _Jmax,
                                           int _n);

/// 船-桨平衡结果
struct MatchingResult {
  double n_rps = 0.0;    ///< 螺旋桨转速 (r/s)
  double thrustN = 0.0;  ///< 螺旋桨推力 (N)
  double torqueNm = 0.0; ///< 螺旋桨扭矩 (N·m)
  double powerW = 0.0;   ///< 收到功率 P_D = 2πnQ (W)
  double qpc = 0.0;      ///< 准推进系数 ηD = η0·(1−t)/(1−w)
  bool converged = false;///< 是否真的收敛（不掩盖失败，GUI 需据此提示）
};

/// 解 T(1−t) = R。R 由 ResistanceAtSpeed 给出（含风阻，与 GUI 显示同一口径）。
/// _wakeFraction 伴流分数 w、_thrustDeduction 推力减额 t，均按 [0,0.9] 钳位。
/// 收敛判据 |T(1−t) − R|/R ≤ 1e-4；V 或 R 为 0 时无有效工作点，converged=false。
MatchingResult MatchOperatingPoint(const PropellerInput &_p,
                                   const ResistanceInput &_hull, double _speedMs,
                                   double _wakeFraction, double _thrustDeduction);

}  // namespace keship

#endif  // KESHIP_PROPELLER_CORE_HH_
