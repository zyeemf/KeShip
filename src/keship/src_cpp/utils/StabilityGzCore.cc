/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-08 完整稳性综合校核 —— 静稳性曲线内核实现（纯函数，无状态、无线程、无 IO） */

#include "StabilityGzCore.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace keship {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;

/// 二分法迭代次数：区间宽度约 4 m，60 次后 < 4e-18，已在 double 分辨率以下。
/// 不再增加——多加的迭代只会重复同一个浮点数。
constexpr int kBisectIters = 60;

/// 线性插值取站表半宽（_kx 已升序，覆盖 [0,L]）
double BAt(const std::vector<double> &_kx, const std::vector<double> &_kb, double _x) {
  if (_kx.empty()) return 0.0;
  if (_x <= _kx.front()) return _kb.front();
  if (_x >= _kx.back()) return _kb.back();
  const auto it = std::upper_bound(_kx.begin(), _kx.end(), _x);
  const std::size_t i = static_cast<std::size_t>(it - _kx.begin());
  const double x0 = _kx[i - 1], x1 = _kx[i];
  const double w = (_x - x0) / (x1 - x0);
  return _kb[i - 1] + w * (_kb[i] - _kb[i - 1]);
}

}  // namespace

namespace detail {

SectionProps SectionBelowWaterline(double _halfB, double _depth, double _tanPhi, double _z0) {
  SectionProps p;
  if (!(_halfB > 0.0) || !(_depth > 0.0) || !std::isfinite(_z0) || !std::isfinite(_tanPhi)) {
    return p;
  }

  // 水深 d(y) = clamp(z0 + t·y, 0, D) 是分段线性函数：在两条等深线处切开后，
  // 每一段要么是常数 D，要么是线性段。分段解析积分 → 无离散误差（GZ(0) 严格 0）。
  double cuts[4] = {-_halfB, _halfB, 0.0, 0.0};
  int n = 2;
  if (std::abs(_tanPhi) > 1e-14) {
    const double yZero = -_z0 / _tanPhi;             // 水线与龙骨的交点
    const double yDeck = (_depth - _z0) / _tanPhi;   // 水线与甲板的交点
    if (yZero > -_halfB && yZero < _halfB) cuts[n++] = yZero;
    if (yDeck > -_halfB && yDeck < _halfB) cuts[n++] = yDeck;
  }
  std::sort(cuts, cuts + n);

  for (int i = 0; i + 1 < n; ++i) {
    const double u0 = cuts[i], u1 = cuts[i + 1];
    const double dm = _z0 + _tanPhi * 0.5 * (u0 + u1);
    if (dm <= 0.0) continue;

    const double len = u1 - u0;
    const double s1 = u1 * u1 - u0 * u0;              // ∫ y dy 的 2 倍
    const double s2 = u1 * u1 * u1 - u0 * u0 * u0;    // ∫ y² dy 的 3 倍

    if (dm >= _depth) {
      // 全浸段：深度恒为 D
      p.area += _depth * len;
      p.yMoment += 0.5 * _depth * s1;
      p.zMoment += 0.5 * _depth * _depth * len;
    } else {
      // 部分浸段：d(y) = a + c·y
      const double a = _z0, c = _tanPhi;
      p.area += a * len + 0.5 * c * s1;
      p.yMoment += 0.5 * a * s1 + c * s2 / 3.0;
      // 0.5·∫(a+cy)² dy = 0.5·(a²·len + a·c·s1 + c²·s2/3)
      p.zMoment += 0.5 * (a * a * len + a * c * s1 + c * c * s2 / 3.0);
    }
  }
  return p;
}

HullGeom BuildHullGeom(const GzInput &_in) {
  HullGeom g;
  g.length = std::max(_in.L, 1e-6);
  g.depth = std::max(_in.D, 1e-6);
  g.draft = std::min(std::max(_in.draft, 0.0), g.depth);

  // 站表 → 折线。boxSection=true 时强制 b=B/2：矩形驳船由 L/B/D/T 完全确定，
  // 站表内容被忽略；这样"解析可校核"的口径不会被学生随手改的站表破坏。
  std::vector<double> kx, kb;
  if (_in.boxSection && _in.B > 0.0) {
    kx = {0.0, g.length};
    kb = {0.5 * _in.B, 0.5 * _in.B};
  } else {
    std::vector<HullStation> st;
    st.reserve(_in.stations.size());
    for (const auto &s : _in.stations) {
      if (!std::isfinite(s.x) || !std::isfinite(s.halfBreadth)) continue;
      st.push_back({s.x, std::max(s.halfBreadth, 0.0)});
    }
    if (st.empty()) return g;  // valid=false：无几何
    std::sort(st.begin(), st.end(),
              [](const HullStation &_a, const HullStation &_b) { return _a.x < _b.x; });

    const double x0 = st.front().x;
    const double x1 = st.back().x;
    const double span = x1 - x0;
    if (!(span > 1e-9)) {
      // 只有一站（或全部重合）：按等剖面棱柱处理，长度取 L
      kx = {0.0, g.length};
      kb = {st.front().halfBreadth, st.front().halfBreadth};
    } else {
      // 站表 x 跨度归一到 [0,L]（表本身覆盖 0..L 时为恒等变换）
      kx.reserve(st.size());
      kb.reserve(st.size());
      for (const auto &s : st) {
        kx.push_back((s.x - x0) / span * g.length);
        kb.push_back(s.halfBreadth);
      }
    }
  }

  // 站数不足时按"均匀网格 + 原始节点"加密：任一 x 处 b 仍由原表线性插值，
  // 加密不改变船型，只让梯形积分在截面突变（舭部/甲板截断）处更贴近真值。
  const int nk = static_cast<int>(kx.size());
  const int nUniform = std::max(21, 2 * nk - 1);
  std::vector<double> gx;
  gx.reserve(static_cast<std::size_t>(nUniform + nk));
  for (int i = 0; i < nUniform; ++i) {
    gx.push_back(g.length * static_cast<double>(i) / static_cast<double>(nUniform - 1));
  }
  for (double x : kx) gx.push_back(x);
  std::sort(gx.begin(), gx.end());
  gx.erase(std::unique(gx.begin(), gx.end(),
                       [](double _a, double _b) { return std::abs(_a - _b) < 1e-9; }),
           gx.end());

  g.x = gx;
  g.halfB.resize(gx.size());
  for (std::size_t i = 0; i < gx.size(); ++i) g.halfB[i] = BAt(kx, kb, gx[i]);

  g.maxHalfB = *std::max_element(g.halfB.begin(), g.halfB.end());
  g.minHalfB = *std::min_element(g.halfB.begin(), g.halfB.end());

  // 排水体积、浮心纵向位置、水线面面积/惯性矩：统一梯形法（与 GZ 曲线同一口径）
  double vol = 0.0, mx = 0.0, aw = 0.0, ax = 0.0, ix = 0.0;
  for (std::size_t i = 0; i + 1 < g.x.size(); ++i) {
    const double dx = g.x[i + 1] - g.x[i];
    const double a0 = 2.0 * g.halfB[i] * g.draft;
    const double a1 = 2.0 * g.halfB[i + 1] * g.draft;
    const double w0 = 2.0 * g.halfB[i];
    const double w1 = 2.0 * g.halfB[i + 1];
    vol += 0.5 * (a0 + a1) * dx;
    mx += 0.5 * (g.x[i] * a0 + g.x[i + 1] * a1) * dx;
    aw += 0.5 * (w0 + w1) * dx;
    ax += 0.5 * (g.x[i] * w0 + g.x[i + 1] * w1) * dx;
    ix += 0.5 * ((2.0 / 3.0) * g.halfB[i] * g.halfB[i] * g.halfB[i] +
                 (2.0 / 3.0) * g.halfB[i + 1] * g.halfB[i + 1] * g.halfB[i + 1]) * dx;
  }

  if (!(vol > 1e-12) || !(g.maxHalfB > 1e-12)) {
    g.valid = false;
    return g;
  }

  g.displacement = vol;
  g.lcbX = mx / vol;
  g.waterplaneArea = aw;
  g.waterplaneIx = ix;
  g.lcfX = (aw > 1e-12) ? ax / aw : 0.0;

  double il = 0.0;
  for (std::size_t i = 0; i + 1 < g.x.size(); ++i) {
    const double dx = g.x[i + 1] - g.x[i];
    const double w0 = 2.0 * g.halfB[i];
    const double w1 = 2.0 * g.halfB[i + 1];
    const double d0 = g.x[i] - g.lcfX;
    const double d1 = g.x[i + 1] - g.lcfX;
    il += 0.5 * (w0 * d0 * d0 + w1 * d1 * d1) * dx;
  }
  g.waterplaneIl = il;
  g.valid = true;
  return g;
}

HullProps HullAt(const HullGeom &_g, double _tanPhi, double _z0) {
  HullProps h;
  if (!_g.valid || _g.x.size() < 2) return h;
  for (std::size_t i = 0; i + 1 < _g.x.size(); ++i) {
    const double dx = _g.x[i + 1] - _g.x[i];
    const SectionProps p0 = SectionBelowWaterline(_g.halfB[i], _g.depth, _tanPhi, _z0);
    const SectionProps p1 = SectionBelowWaterline(_g.halfB[i + 1], _g.depth, _tanPhi, _z0);
    h.volume += 0.5 * (p0.area + p1.area) * dx;
    h.yMoment += 0.5 * (p0.yMoment + p1.yMoment) * dx;
    h.zMoment += 0.5 * (p0.zMoment + p1.zMoment) * dx;
    h.xMoment += 0.5 * (_g.x[i] * p0.area + _g.x[i + 1] * p1.area) * dx;
  }
  return h;
}

double SolveWaterlineZ0(const HullGeom &_g, double _phiDeg, double _targetVolume) {
  if (!_g.valid) return 0.0;
  const double a = std::min(std::abs(_phiDeg), 89.999);
  const double t = std::tan(a * kDeg2Rad);
  // 括区间取对称安全界：t 可正可负，用 |t| 保证两端分别"全空/全浸"
  const double span = std::abs(t) * _g.maxHalfB;
  double lo = -span - 1.0;
  double hi = _g.depth + span + 1.0;
  for (int i = 0; i < kBisectIters; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (HullAt(_g, t, mid).volume < _targetVolume) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  return 0.5 * (lo + hi);
}

double GzAt(const HullGeom &_g, double _phiDeg, double _KG) {
  if (!_g.valid || !std::isfinite(_phiDeg) || !std::isfinite(_KG)) return 0.0;

  const double s = (_phiDeg < 0.0) ? -1.0 : 1.0;
  const double a = std::min(std::abs(_phiDeg), 90.0);

  // 正浮：对称剖面的一阶矩严格为 0，直接返回 0，避免求根残差污染 |GZ(0)|<1e-9 判据
  if (a < 1e-12) return 0.0;

  // 90°：水线在船体坐标里竖直，浸水部分是一个"等深切片"，z_B ≡ D/2，
  // 且 y_B·cos90° = 0 ⇒ GZ = D/2 − KG。此式与站表形状无关（棱柱解析极限）。
  if (a >= 90.0 - 1e-9) return s * (0.5 * _g.depth - _KG);

  const double t = std::tan(a * kDeg2Rad);
  const double z0 = SolveWaterlineZ0(_g, a, _g.displacement);
  const HullProps h = HullAt(_g, t, z0);
  if (!(h.volume > 1e-12)) return 0.0;

  const double r = a * kDeg2Rad;
  const double yB = h.yMoment / h.volume;
  const double zB = h.zMoment / h.volume;
  const double kn = yB * std::cos(r) + zB * std::sin(r);  // 浮心到龙骨基线的横向距离
  return s * (kn - _KG * std::sin(r));
}

}  // namespace detail

namespace {

/// 区间 [_a,_b] 上的 GZ 面积（m·rad）。步长 0.05°，端点精确落入网格，
/// 避免 30°/40° 落在网格外造成系统偏差。
double AreaBetween(const detail::HullGeom &_g, double _KG, double _a, double _b) {
  if (!(_b > _a)) return 0.0;
  const double span = _b - _a;
  const int n = std::max(4, static_cast<int>(std::ceil(span / 0.05)));
  const double h = span / static_cast<double>(n);
  double sum = 0.0;
  double prev = detail::GzAt(_g, _a, _KG);
  for (int i = 1; i <= n; ++i) {
    const double cur = detail::GzAt(_g, _a + static_cast<double>(i) * h, _KG);
    sum += 0.5 * (prev + cur) * h * kDeg2Rad;
    prev = cur;
  }
  return sum;
}

/// 峰值精修：0.1° 网格只能定位到 ±0.1°，用黄金分割在 ±0.2° 内收敛到 ~1e-12°。
/// 只用于"最大 GZ 及其对应角"，不改动任何面积积分。
double RefinePeak(const detail::HullGeom &_g, double _KG, double _lo, double _hi) {
  constexpr double kInvPhi = 0.6180339887498949;
  double a = _lo, b = _hi;
  double c = b - kInvPhi * (b - a);
  double d = a + kInvPhi * (b - a);
  double fc = detail::GzAt(_g, c, _KG);
  double fd = detail::GzAt(_g, d, _KG);
  for (int i = 0; i < 60; ++i) {
    if (fc > fd) {
      b = d;
      d = c;
      fd = fc;
      c = b - kInvPhi * (b - a);
      fc = detail::GzAt(_g, c, _KG);
    } else {
      a = c;
      c = d;
      fc = fd;
      d = a + kInvPhi * (b - a);
      fd = detail::GzAt(_g, d, _KG);
    }
  }
  return 0.5 * (a + b);
}

}  // namespace

double GZ(double _phiDeg, const GzInput &_in) {
  const detail::HullGeom g = detail::BuildHullGeom(_in);
  return detail::GzAt(g, _phiDeg, _in.KG);
}

std::vector<GzPoint> GzCurve(const GzInput &_in, double _maxDeg, double _stepDeg) {
  std::vector<GzPoint> out;
  const detail::HullGeom g = detail::BuildHullGeom(_in);
  if (!g.valid) return out;

  // 定义域封顶 90°：超过 90° 的"GZ"要另建一套剖面翻转模型，本内核不做。
  const double maxD = std::min(std::max(_maxDeg, 0.0), 90.0);
  const double step = (_stepDeg > 1e-9) ? _stepDeg : 1.0;
  int n = static_cast<int>(std::floor(maxD / step + 1e-9));
  n = std::min(n, 200000);

  out.reserve(static_cast<std::size_t>(n + 2));
  for (int i = 0; i <= n; ++i) {
    const double phi = static_cast<double>(i) * step;
    out.push_back({phi, detail::GzAt(g, phi, _in.KG)});
  }
  if (maxD - out.back().phiDeg > 1e-9) {
    out.push_back({maxD, detail::GzAt(g, maxD, _in.KG)});
  }
  return out;
}

GzSummary Summarize(const GzInput &_in) {
  GzSummary s;
  const detail::HullGeom g = detail::BuildHullGeom(_in);
  if (!g.valid) return s;

  // 0.1° 稠密曲线：定位峰值与消失角。901 点 × 60 次二分 ≈ 5e6 次剖面积分，毫秒级。
  constexpr double kStep = 0.1;
  constexpr int kLast = 900;  // 0 .. 90.0
  std::vector<double> gz(static_cast<std::size_t>(kLast) + 1);
  for (int i = 0; i <= kLast; ++i) {
    gz[static_cast<std::size_t>(i)] = detail::GzAt(g, static_cast<double>(i) * kStep, _in.KG);
  }

  int imax = 0;
  for (int i = 1; i <= kLast; ++i) {
    if (gz[static_cast<std::size_t>(i)] > gz[static_cast<std::size_t>(imax)]) imax = i;
  }

  if (imax == 0 || imax == kLast) {
    // 峰值落在定义域端点（例如 GM<0 时最大值就是 0 处的 0）
    s.angleGzMaxDeg = static_cast<double>(imax) * kStep;
    s.gzMaxM = gz[static_cast<std::size_t>(imax)];
  } else {
    const double center = static_cast<double>(imax) * kStep;
    const double lo = std::max(0.0, center - 0.2);
    const double hi = std::min(90.0, center + 0.2);
    s.angleGzMaxDeg = RefinePeak(g, _in.KG, lo, hi);
    s.gzMaxM = detail::GzAt(g, s.angleGzMaxDeg, _in.KG);
  }

  // 消失角：峰值之后第一次 GZ ≤ 0；线性插值到零点。
  // 若到 90° 仍未回零（例如 KG < D/2 的封闭箱体），按 90° 计并在报告里注明。
  s.vanishingAngleDeg = 90.0;
  for (int i = imax + 1; i <= kLast; ++i) {
    if (gz[static_cast<std::size_t>(i)] <= 0.0) {
      const double p0 = static_cast<double>(i - 1) * kStep;
      const double p1 = static_cast<double>(i) * kStep;
      const double g0 = gz[static_cast<std::size_t>(i - 1)];
      const double g1 = gz[static_cast<std::size_t>(i)];
      s.vanishingAngleDeg =
          (std::abs(g1 - g0) > 1e-15) ? p0 + (p1 - p0) * (g0 / (g0 - g1)) : p1;
      break;
    }
  }

  // GM0 = dGZ/dφ|₀（弧度制）。GZ 是奇函数，中心差分误差 O(h²)，h=1e-3 rad 足够。
  constexpr double kGmDeltaRad = 1.0e-3;
  const double hDeg = kGmDeltaRad * kRad2Deg;
  s.gm0M = (detail::GzAt(g, hDeg, _in.KG) - detail::GzAt(g, -hDeg, _in.KG)) /
           (2.0 * kGmDeltaRad);

  s.area0to30 = AreaBetween(g, _in.KG, 0.0, 30.0);
  s.area30to40 = AreaBetween(g, _in.KG, 30.0, 40.0);
  s.area0to40 = s.area0to30 + s.area30to40;  // 同一条曲线，避免两次数值积分产生不一致
  s.area40toVanishing =
      AreaBetween(g, _in.KG, 40.0, std::max(40.0, s.vanishingAngleDeg));

  // IMO A.749(18) / IS Code 2008 五项衡准（阈值见 .hh 注释）
  s.passArea0to30 = s.area0to30 >= 0.055;
  s.passArea30to40 = s.area30to40 >= 0.030;
  s.passArea40toVan = s.area40toVanishing >= 0.090;
  s.passGzMax = s.gzMaxM >= 0.20 && s.angleGzMaxDeg >= 30.0;
  s.passGm0 = s.gm0M >= 0.15;
  s.allPass = s.passArea0to30 && s.passArea30to40 && s.passArea40toVan &&
              s.passGzMax && s.passGm0;
  return s;
}

}  // namespace keship
