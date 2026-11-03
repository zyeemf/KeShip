/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— 阻力计算内核实现（纯函数，无状态） */

#include "ResistanceCore.hh"

#include <algorithm>
#include <cmath>

namespace keship {

namespace {

constexpr double kGravity = 9.80665;  ///< 标准重力加速度 (m/s²)

/// ITTC-57 在 Re < 1e3 时已无物理意义（(lgRe−2) → 0，Cf 发散）。
/// 故把 Re 下限钳到 1e3：V→0 时 Rf ∝ V² 仍单调趋零，且不会出现 NaN/Inf。
constexpr double kReMin = 1.0e3;

/// 兴波阻力锚点：Cr(Fn_ref) = Cr_ref。
/// 取值依据：Taylor / Series 60 剩余阻力图谱在 Fn≈0.30、Cb≈0.6 处
/// Cr ≈ 0.8e-3 ~ 1.5e-3；取**上限** 1.5e-3 属于偏保守（宁可多算阻力）。
constexpr double kCrRef = 1.50e-3;
constexpr double kFnRef = 0.30;

/// 高 Fn 饱和强度。纯 V⁴ 律在 Fn>0.6 会把兴波阻力抬到非物理量级（教学平台会
/// 出现「3 m/s 之后阻力暴涨十几倍」的假象），故加 1/(1+κu²) 滤波：
///   Cr(u) = Cr_ref·u²·(1+κ)/(1+κu²)，u = Fn/Fn_ref
/// 该式在 u=1 处严格等于 Cr_ref，u→∞ 时收敛到 Cr_ref·(1+κ)/κ = 3.375e-3，
/// 保证 R(V) 全程单调、有界、无负值；代价是 Fn>0.45 段**低估**（见报告）。
constexpr double kCrSat = 0.8;

/// Cb 惩罚中心：Watson & Gilfillan 最优方形系数式 Cb_opt = 1.08 − 1.68·Fn
/// 在设计 Fn = 0.30 处的值（0.576）。**刻意取常量**：若让它随当前 Fn 变化，
/// 在 Cb 固定时会给 R(V) 注入一段虚假的强非线性，污染 R–V 幂律（RP-02 要拟合 k）。
constexpr double kCbOpt = 0.576;
/// 惩罚强度与特征宽度：Cr *= 1 + kCbPenalty·((Cb−Cb_opt)/kCbWidth)²
/// 系数为**标定值**（非文献回归），标定目标：Fn≈0.3 时总阻力最优 Cb 落在
/// 0.55~0.60，与 Watson 式的 0.576 相符。宽度 0.10 表示 Cb 偏离 0.1 时兴波系数
/// 上升约一倍——量级取自 Cb 每增 0.1、同 Fn 下 Cr 约翻倍的图谱趋势。
constexpr double kCbPenalty = 0.9;
constexpr double kCbWidth = 0.10;

/// 空气阻力系数（钝体正面迎风）。Hoerner 钝体系数在 0.8~1.2 之间，取 1.0。
constexpr double kWindCd = 1.0;

double Clamp(double _v, double _lo, double _hi) {
  return std::min(std::max(_v, _lo), _hi);
}

/// ITTC-57 平板摩擦系数。Re 已由调用方钳位。
double Ittc57(double _Re) {
  const double lg = std::log10(_Re) - 2.0;
  return 0.075 / (lg * lg);
}

/// 兴波阻力系数 Cr(Fn, Cb)。
double WaveCoefficient(double _Fn, double _Cb) {
  const double u = std::max(_Fn, 0.0) / kFnRef;
  const double u2 = u * u;
  double cr = kCrRef * u2 * (1.0 + kCrSat) / (1.0 + kCrSat * u2);

  const double dCb = (Clamp(_Cb, 0.30, 0.90) - kCbOpt) / kCbWidth;
  cr *= 1.0 + kCbPenalty * dCb * dCb;

  return std::max(cr, 0.0);
}

/// 空气阻力：只有迎风面积与风速都为正时才有贡献。
double WindResistance(const ResistanceInput &_in) {
  const double v = std::abs(_in.windSpeedMs);
  const double a = std::max(_in.frontalAreaM2, 0.0);
  return 0.5 * std::max(_in.rhoAir, 0.0) * kWindCd * a * v * v;
}

}  // namespace

double FroudeNumber(const ResistanceInput &_in, double _speedMs) {
  const double l = std::max(_in.Lwl, 1.0e-6);
  return std::max(_speedMs, 0.0) / std::sqrt(kGravity * l);
}

double WettedSurfaceM2(const ResistanceInput &_in) {
  const double l = std::max(_in.Lwl, 1.0e-6);
  const double b = std::max(_in.B, 1.0e-6);
  const double t = std::max(_in.T, 1.0e-6);
  const double cm = Clamp(_in.Cm, 0.30, 0.99);
  const double cb = Clamp(_in.Cb, 0.10, 0.95);
  const double cwp = Clamp(_in.Cwp, 0.30, 0.99);
  const double btl = Clamp(b / t, 1.0, 20.0);  ///< B/T 超出 1~20 时回归式失真，钳位

  // Holtrop & Mennen (1982)：S = L(2T+B)√Cm·(0.453 + 0.4425Cb − 0.2862Cm
  //                                        − 0.003467·B/T + 0.3696Cwp) + 2.38·ABT/Cb
  // 本平台无球鼻艏（ABT=0），故末项省略。
  const double bracket = 0.453 + 0.4425 * cb - 0.2862 * cm - 0.003467 * btl +
                         0.3696 * cwp;
  double s = l * (2.0 * t + b) * std::sqrt(cm) * bracket;

  // 极端输入（例如 Cb→0.1 使括号项转负）时的兜底：用同一几何的量级式，
  // 保证 S>0，绝不把负面积带进 Rf/Ct。
  if (!(s > 0.0) || !std::isfinite(s)) {
    s = l * (2.0 * t + b) * std::sqrt(cm) * 0.70;
  }
  return std::max(s, 1.0e-6);
}

double FormFactorOnePlusK(const ResistanceInput &_in) {
  const double l = std::max(_in.Lwl, 1.0e-6);
  const double b = Clamp(_in.B, 1.0e-6, l);
  const double t = Clamp(_in.T, 1.0e-6, b);
  const double cm = Clamp(_in.Cm, 0.30, 0.99);
  const double cb = Clamp(_in.Cb, 0.10, 0.95);
  const double cp = Clamp(cb / cm, 0.10, 0.99);  ///< 棱形系数 Cp = Cb/Cm
  const double vol = std::max(_in.volumeD, 1.0e-9);

  // Holtrop & Mennen (1982)：c14 = 1 + 0.011·Cstern，常规船 Cstern = 0 ⇒ c14 = 1
  const double c14 = 1.0;
  const double onePlusK =
      0.93 + 0.487118 * c14 * std::pow(b / l, 1.06806) *
                 std::pow(t / l, 0.46106) *
                 std::pow(l / std::cbrt(vol), 0.121563) *
                 std::pow(l * l * l / vol, 0.36486) *
                 std::pow(1.0 - cp, -0.604247);

  // 回归式在极端瘦削船型下可能给出 1+k<1（非物理：黏性阻力不可能小于平板）。
  return Clamp(onePlusK, 1.0, 2.5);
}

ResistanceBreakdown ResistanceAtSpeed(const ResistanceInput &_in, double _speedMs) {
  ResistanceBreakdown out;

  const double v = std::max(_speedMs, 0.0);
  const double l = std::max(_in.Lwl, 1.0e-6);
  const double rho = std::max(_in.rhoWater, 0.0);
  const double s = WettedSurfaceM2(_in);
  const double onePlusK = FormFactorOnePlusK(_in);
  const double appendage = Clamp(_in.appendageFactor, 0.1, 5.0);

  out.speedMs = v;
  out.Fn = FroudeNumber(_in, v);
  out.onePlusK = onePlusK;
  out.Re = v * l / std::max(_in.nu, 1.0e-12);
  out.Cf = Ittc57(std::max(out.Re, kReMin));
  out.Rwind = WindResistance(_in);

  if (v <= 1.0e-9) {
    // 静止：无动压 ⇒ 无摩擦/兴波阻力；只剩风（Ct 无定义，置 0）
    out.Rf = 0.0;
    out.Rr = 0.0;
    out.Rtotal = out.Rwind;
    out.Ct = 0.0;
    return out;
  }

  const double q = 0.5 * rho * v * v * s;  ///< 动压×湿面积，三项共用

  // 黏性项 = 平板摩擦 × 形状因子 × 附体修正（(1+k2) 只放大黏性部分，
  // 兴波与船体形状相关、与附体无关）
  out.Rf = q * onePlusK * out.Cf * appendage;
  out.Rr = q * WaveCoefficient(out.Fn, _in.Cb);
  out.Rtotal = out.Rf + out.Rr + out.Rwind;
  out.Ct = (q > 1.0e-12) ? out.Rtotal / q : 0.0;

  return out;
}

std::vector<ResistanceBreakdown> ResistanceCurve(const ResistanceInput &_in,
                                                 double _vMin, double _vMax, int _n) {
  std::vector<ResistanceBreakdown> curve;
  double lo = std::max(_vMin, 0.0);
  double hi = std::max(_vMax, 0.0);
  if (hi < lo) std::swap(lo, hi);
  if (_n < 2) {
    curve.push_back(ResistanceAtSpeed(_in, lo));
    return curve;
  }

  curve.reserve(static_cast<size_t>(_n));
  const double step = (hi - lo) / static_cast<double>(_n - 1);
  for (int i = 0; i < _n; ++i) {
    // 末点直接取 hi，避免累积误差让曲线终点差一点（GUI 上会看出断口）
    const double v = (i == _n - 1) ? hi : lo + step * static_cast<double>(i);
    curve.push_back(ResistanceAtSpeed(_in, v));
  }
  return curve;
}

double CbForMinResistance(const ResistanceInput &_in, double _vMin, double _vMax) {
  constexpr double kCbLo = 0.50;
  constexpr double kCbHi = 0.85;
  constexpr double kCbStep = 0.005;
  constexpr int kSpeedSamples = 11;

  double lo = std::max(_vMin, 0.0);
  double hi = std::max(_vMax, 0.0);
  if (hi < lo) std::swap(lo, hi);

  double bestCb = kCbLo;
  double bestMean = -1.0;

  for (double cb = kCbLo; cb <= kCbHi + 1.0e-9; cb += kCbStep) {
    ResistanceInput probe = _in;
    probe.Cb = cb;
    // Cb 与排水体积必须自洽：∇ = Cb·L·B·T（Cb 的定义式）。
    // 若沿用调用方的 volumeD，(1+k) 里 (L/∇^(1/3)) 一项就与 Cb 脱钩，
    // 扫描出来的「最优 Cb」只是湿面积在变，物理上讲不通。
    probe.volumeD = cb * std::max(_in.Lwl, 1.0e-6) * std::max(_in.B, 1.0e-6) *
                    std::max(_in.T, 1.0e-6);
    // Cm / Cwp 在此保持调用方给定值（本平台教学船型 Cm 基本恒定），
    // 若一并随 Cb 变化会引入两个未标定的自由度，反而降低可辩性。

    double sum = 0.0;
    for (int i = 0; i < kSpeedSamples; ++i) {
      const double v = lo + (hi - lo) * static_cast<double>(i) /
                               static_cast<double>(kSpeedSamples - 1);
      sum += ResistanceAtSpeed(probe, v).Rtotal;
    }
    const double mean = sum / static_cast<double>(kSpeedSamples);
    if (bestMean < 0.0 || mean < bestMean) {
      bestMean = mean;
      bestCb = cb;
    }
  }
  return Clamp(bestCb, kCbLo, kCbHi);
}

double ModelToShipResistance(const ResistanceInput &_model, double _lambda,
                             double _shipSpeedMs) {
  if (!(_lambda > 0.0) || !(_shipSpeedMs > 0.0)) return 0.0;

  const double vs = _shipSpeedMs;
  const double vm = vs / std::sqrt(_lambda);  ///< 同 Fn：Vm = Vs/√λ

  // 拖曳试验无风：先抹掉风项，否则 Ct 里会混进与 Fn 无关的空气阻力
  ResistanceInput model = _model;
  model.windSpeedMs = 0.0;
  model.frontalAreaM2 = 0.0;

  const ResistanceBreakdown bm = ResistanceAtSpeed(model, vm);

  // 实船雷诺数：只关心 Cf，故用缩尺后的水线长重算 Re 与 Cf
  const double ls = std::max(_model.Lwl, 1.0e-6) * _lambda;
  const double res = vs * ls / std::max(_model.nu, 1.0e-12);
  const double cfs = Ittc57(std::max(res, kReMin));

  // 三因次法：剩余阻力系数相同，只把摩擦项从模型雷诺数改到实船雷诺数
  const double cts = bm.Ct - bm.onePlusK * (bm.Cf - cfs);

  // 几何相似 ⇒ 湿面积按 λ² 缩放（Holtrop 式中所有项都是 L 的一次 × 无量纲比值，
  // 故 λ² 关系严格成立，不需要重算 S）
  const double ss = WettedSurfaceM2(model) * _lambda * _lambda;
  const double rho = std::max(_model.rhoWater, 0.0);

  return std::max(cts, 0.0) * 0.5 * rho * ss * vs * vs;
}

}  // namespace keship
