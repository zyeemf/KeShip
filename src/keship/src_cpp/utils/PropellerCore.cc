/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— 螺旋桨敞水与船-桨匹配内核实现（纯函数，无状态） */

#include "PropellerCore.hh"

#include <algorithm>
#include <cmath>
#include <utility>

namespace keship {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// ── B3-50（Z=3，Ae/A0=0.50，P/D=1.0）敞水特性系数 ────────────────────────────
// 形式：KT = kt0 + kKt1·Jp + kKt2·Jp²，KQ = kq0 + kKq1·Jp + kKq2·Jp²，Jp = J/(P/D)
// 即 Oosterveld & van Oossanen (1975) B 系列多项式的 s≤2 截断子集。
// 系数按 B3-50 的公开敞水特性锚点标定（审稿可逐条核对）：
//   · J=0 时 KT = 0.500（B3-50 等螺距桨的系柱推力系数量级 0.48~0.52）
//   · 零推力点 J0 = 0.920（P/D=1.0 时 B3-50 的 J0 约 0.90~0.95）
//   · J=0 时 KQ = 0.062（对应 η0 峰 ≈ 0.58 @ J≈0.66，B3-50 实测 0.55~0.60）
// ⚠ 本表是**按上述锚点标定的简化子集**，不是原论文 39 项全表；若要写论文级对比，
//   需按原表逐项录入复核（已列入交付报告的「需人工复核」项）。
constexpr double kKt0 = 0.500;
constexpr double kKt1 = -0.420;
constexpr double kKt2 = -0.1342;
constexpr double kKq0 = 0.062;
constexpr double kKq1 = -0.042;
constexpr double kKq2 = -0.010;

// 几何敏感性（B 系列趋势的一阶线性化，系数为标定值）：
// 螺距 ↑ ⇒ 同 J 下做功更多，KT0 ∝ P/D、KQ0 ∝ (P/D)^1.5（扭矩比推力涨得快）
// 盘面比 ↑ ⇒ 叶面积大、摩擦扭矩涨得比推力快 ⇒ 效率略降
// 叶数 ↑ ⇒ 同盘面比下叶宽变窄，KT 略升、KQ 略升
constexpr double kPitchKqExp = 1.5;
constexpr double kAreaKtSlope = 0.10;
constexpr double kAreaKqSlope = 0.25;
constexpr double kBladeKtSlope = 0.02;
constexpr double kBladeKqSlope = 0.01;

/// 平衡式收敛判据：|T(1−t) − R| / R
constexpr double kBalanceTol = 1.0e-4;
constexpr int kMaxBisect = 200;

double Clamp(double _v, double _lo, double _hi) {
  return std::min(std::max(_v, _lo), _hi);
}

}  // namespace

OpenWaterPoint OpenWaterAtJ(const PropellerInput &_p, double _J) {
  OpenWaterPoint out;

  const double pd = Clamp(_p.P_over_D, 0.5, 1.6);
  const double aeao = Clamp(_p.Ae_A0, 0.30, 1.05);
  const double z = Clamp(_p.Z, 2.0, 6.0);
  const double j = std::max(_J, 0.0);
  out.J = j;

  // 螺距归一化：Jp = J/(P/D)。零推力点 J0 ∝ P/D 这一物理事实由它自动满足，
  // 不必为每个 P/D 单独列表。
  const double jp = j / pd;

  const double kt0 = kKt0 * pd * (1.0 + kAreaKtSlope * (aeao - 0.50)) *
                     (1.0 + kBladeKtSlope * (z - 3.0));
  const double kq0 = kKq0 * std::pow(pd, kPitchKqExp) *
                     (1.0 + kAreaKqSlope * (aeao - 0.50)) *
                     (1.0 + kBladeKqSlope * (z - 3.0));

  out.KT = kt0 + kKt1 * jp + kKt2 * jp * jp;
  out.KQ = kq0 + kKq1 * jp + kKq2 * jp * jp;

  // 回归有效区外（J > J0，例如 P/D 被钳到 0.5 的极端几何）KQ 会算成负数。
  // 物理上那是「风车/负扭矩」状态，本平台不建模；取 0 下限，
  // 避免 GUI 出现负扭矩、负功率这种一看就错的读数。KT 保留符号
  // （负 KT = 桨在产生阻力），这对学生理解「超过零推力点」有意义。
  out.KQ = std::max(out.KQ, 0.0);

  // J=0 时 η0 形式上为 0/0·J → 定义为 0（不做功，无效率可言）
  out.eta0 = (out.KQ > 1.0e-9) ? (j * out.KT) / (kTwoPi * out.KQ) : 0.0;

  return out;
}

std::vector<OpenWaterPoint> OpenWaterCurve(const PropellerInput &_p, double _Jmax,
                                           int _n) {
  std::vector<OpenWaterPoint> curve;
  const double jmax = std::max(_Jmax, 0.0);
  if (_n < 2) {
    curve.push_back(OpenWaterAtJ(_p, 0.0));
    return curve;
  }

  curve.reserve(static_cast<size_t>(_n));
  const double step = jmax / static_cast<double>(_n - 1);
  for (int i = 0; i < _n; ++i) {
    const double j = (i == _n - 1) ? jmax : step * static_cast<double>(i);
    curve.push_back(OpenWaterAtJ(_p, j));
  }
  return curve;
}

MatchingResult MatchOperatingPoint(const PropellerInput &_p,
                                   const ResistanceInput &_hull, double _speedMs,
                                   double _wakeFraction, double _thrustDeduction) {
  MatchingResult out;

  const double d = _p.D;
  const double rho = std::max(_hull.rhoWater, 0.0);
  const double v = std::max(_speedMs, 0.0);
  const double w = Clamp(_wakeFraction, 0.0, 0.9);
  const double t = Clamp(_thrustDeduction, 0.0, 0.9);
  if (!(d > 1.0e-6) || !(rho > 0.0)) return out;

  const double r = ResistanceAtSpeed(_hull, v).Rtotal;
  const double va = (1.0 - w) * v;  ///< 实效伴流速度（进速）

  // 静止或零阻力：没有可匹配的工作点。此时**不**假装收敛——
  // GUI 应显示「无定义」而不是给一个 n=0 的假数据。
  if (!(r > 1.0e-6) || !(va > 1.0e-6)) return out;

  // 推力 T(n) = ρn²D⁴·KT(Va/(nD))，对 n 单调递增：
  //   dT/dn = ρnD⁴(2KT − J·dKT/dJ) > 0（KT>0 且 dKT/dJ<0）
  // ⇒ 残差 f(n) = T(1−t) − R 单调，二分法必然收敛，无需牛顿法的初值猜测。
  auto eval = [&](double _n, double &_thrust, double &_j) {
    const double j = va / (_n * d);
    const double kt = OpenWaterAtJ(_p, j).KT;
    _j = j;
    _thrust = rho * _n * _n * d * d * d * d * kt;
  };
  auto residual = [&](double _n) {
    double thr = 0.0, j = 0.0;
    eval(_n, thr, j);
    return thr * (1.0 - t) - r;
  };

  // 上界：从 1 r/s 起倍增，直到推力补上阻力（推进器 n 不会超过 2^80，防死循环）
  double hi = 1.0;
  int guard = 0;
  while (residual(hi) < 0.0 && guard < 80) {
    hi *= 2.0;
    ++guard;
  }
  if (residual(hi) < 0.0) return out;

  double lo = 1.0e-6;  ///< J→∞ ⇒ KT<0 ⇒ 残差为负，保证区间异号

  double mid = lo;
  for (int i = 0; i < kMaxBisect; ++i) {
    mid = 0.5 * (lo + hi);
    const double f = residual(mid);
    if (f < 0.0) {
      lo = mid;
    } else {
      hi = mid;
    }
    if (std::abs(f) <= kBalanceTol * r) break;
    if ((hi - lo) <= 1.0e-12 * hi) break;
  }

  const double f = residual(mid);
  double thrust = 0.0, j = 0.0;
  eval(mid, thrust, j);
  const OpenWaterPoint ow = OpenWaterAtJ(_p, j);

  out.n_rps = mid;
  out.thrustN = thrust;
  out.torqueNm = rho * mid * mid * std::pow(d, 5.0) * ow.KQ;
  out.powerW = kTwoPi * mid * out.torqueNm;
  out.qpc = ow.eta0 * (1.0 - t) / (1.0 - w);  ///< ηD = η0·ηH，ηH = (1−t)/(1−w)
  out.converged = std::abs(f) <= kBalanceTol * r;

  return out;
}

}  // namespace keship
