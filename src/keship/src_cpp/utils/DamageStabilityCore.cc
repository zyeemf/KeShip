/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-07 破舱稳性仿真 —— 损失浮力法内核实现（纯函数，无状态） */

#include "DamageStabilityCore.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace keship {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg2Rad = kPi / 180.0;
constexpr double kRad2Deg = 180.0 / kPi;
constexpr int kBisectIters = 60;
constexpr double kGmDeltaRad = 1.0e-3;

/// 渗透率钳到 [0,1]：>1 或 <0 属于输入错误，钳位比外推安全
double Mu(const Compartment &_c) {
  return std::min(std::max(_c.permeability, 0.0), 1.0);
}

/// 轴对齐长方体舱室被水线 z = z0 + tanφ·y 截断后的浸水部分。
/// 与船体剖面积分同构（分段线性 → 解析积分），只是下界是舱底 z1、上界是舱顶 z2。
detail::SectionProps BoxBelowWaterline(double _y1, double _y2, double _z1, double _z2,
                                       double _tanPhi, double _z0) {
  detail::SectionProps p;
  if (!(_y2 > _y1) || !(_z2 > _z1) || !std::isfinite(_z0) || !std::isfinite(_tanPhi)) {
    return p;
  }

  double cuts[4] = {_y1, _y2, 0.0, 0.0};
  int n = 2;
  if (std::abs(_tanPhi) > 1e-14) {
    const double yBottom = (_z1 - _z0) / _tanPhi;
    const double yTop = (_z2 - _z0) / _tanPhi;
    if (yBottom > _y1 && yBottom < _y2) cuts[n++] = yBottom;
    if (yTop > _y1 && yTop < _y2) cuts[n++] = yTop;
  }
  std::sort(cuts, cuts + n);

  for (int i = 0; i + 1 < n; ++i) {
    const double u0 = cuts[i], u1 = cuts[i + 1];
    const double zm = _z0 + _tanPhi * 0.5 * (u0 + u1);
    if (zm <= _z1) continue;

    const double len = u1 - u0;
    const double s1 = u1 * u1 - u0 * u0;
    const double s2 = u1 * u1 * u1 - u0 * u0 * u0;

    if (zm >= _z2) {
      // 该段舱室全浸
      const double d = _z2 - _z1;
      p.area += d * len;
      p.yMoment += 0.5 * d * s1;
      p.zMoment += 0.5 * (_z2 * _z2 - _z1 * _z1) * len;
    } else {
      // 该段部分浸水：顶面 = z1 + a + c·y
      const double a = _z0 - _z1;
      const double c = _tanPhi;
      p.area += a * len + 0.5 * c * s1;
      p.yMoment += 0.5 * a * s1 + c * s2 / 3.0;
      p.zMoment += _z1 * a * len + 0.5 * _z1 * c * s1 + 0.5 * a * a * len +
                   0.5 * a * c * s1 + c * c * s2 / 6.0;
    }
  }
  return p;
}

struct NetProps {
  double volume = 0.0;
  double yMoment = 0.0;
  double zMoment = 0.0;
  double xMoment = 0.0;
};

/// 剩余（完整船浮力 − 各破损舱损失浮力）的一阶矩
NetProps NetAt(const detail::HullGeom &_g, const std::vector<int> &_flooded,
               const std::vector<Compartment> &_comps, double _tanPhi, double _z0) {
  const detail::HullProps h = detail::HullAt(_g, _tanPhi, _z0);
  NetProps n;
  n.volume = h.volume;
  n.yMoment = h.yMoment;
  n.zMoment = h.zMoment;
  n.xMoment = h.xMoment;

  for (int idx : _flooded) {
    const Compartment &c = _comps[static_cast<std::size_t>(idx)];
    const double mu = Mu(c);
    const double len = c.x2 - c.x1;
    const detail::SectionProps s =
        BoxBelowWaterline(c.y1, c.y2, c.z1, c.z2, _tanPhi, _z0);
    n.volume -= mu * len * s.area;
    n.yMoment -= mu * len * s.yMoment;
    n.zMoment -= mu * len * s.zMoment;
    n.xMoment -= mu * len * s.area * 0.5 * (c.x1 + c.x2);
  }
  return n;
}

/// 解正浮水线 z0 使剩余浮力 == 原排水体积。返回 false 表示"剩余浮力根本不够"。
bool SolveNetZ0(const detail::HullGeom &_g, const std::vector<int> &_flooded,
                const std::vector<Compartment> &_comps, double _phiDeg, double _target,
                double &_z0) {
  const double a = std::min(std::abs(_phiDeg), 89.0);  // 扫描上限 89°，避开 tan 奇点
  const double t = std::tan(a * kDeg2Rad) * ((_phiDeg < 0.0) ? -1.0 : 1.0);
  const double span = std::abs(t) * _g.maxHalfB;
  double lo = -span - 1.0;
  double hi = _g.depth + span + 1.0;

  // 上限处若仍托不住重量，说明破舱后浮力不足，直接失败，不返回"近似水线"
  if (NetAt(_g, _flooded, _comps, t, hi).volume < _target) return false;

  for (int i = 0; i < kBisectIters; ++i) {
    const double mid = 0.5 * (lo + hi);
    if (NetAt(_g, _flooded, _comps, t, mid).volume < _target) {
      lo = mid;
    } else {
      hi = mid;
    }
  }
  _z0 = 0.5 * (lo + hi);
  return true;
}

struct GzEval {
  double gz = 0.0;
  bool ok = false;
};

/// 破舱船在给定横倾角下的残存 GZ。**不能**用对称性取负：破损舱左右不对称，
/// GZ(−φ) ≠ −GZ(φ)，故这里始终按带符号的 tanφ 实际求解。
GzEval GzDamaged(const detail::HullGeom &_g, const std::vector<int> &_flooded,
                 const std::vector<Compartment> &_comps, double _phiDeg, double _KG) {
  GzEval r;
  if (!std::isfinite(_phiDeg) || !std::isfinite(_KG)) return r;

  const double a = std::min(std::abs(_phiDeg), 90.0);
  if (a >= 90.0 - 1e-9) return r;  // 破舱船 90° 切片不是 D/2，本内核不外推

  double z0 = 0.0;
  if (!SolveNetZ0(_g, _flooded, _comps, _phiDeg, _g.displacement, z0)) return r;

  // 注意：这里必须用**带符号**的 φ。破损舱左右不对称，GZ(−φ) ≠ −GZ(φ)，
  // 若像完整船那样"按 |φ| 算再乘符号"，等于把舱室也镜像了一遍，物理上错误。
  const double t = std::tan(a * kDeg2Rad) * ((_phiDeg < 0.0) ? -1.0 : 1.0);
  const NetProps n = NetAt(_g, _flooded, _comps, t, z0);
  if (!(n.volume > 1e-12)) return r;

  if (a < 1e-12) {
    r.gz = n.yMoment / n.volume;  // 正浮：cos0=1，sin0=0
    r.ok = true;
    return r;
  }

  const double rr = _phiDeg * kDeg2Rad;  // 带符号，直接进 cos/sin
  const double kn = (n.yMoment / n.volume) * std::cos(rr) +
                    (n.zMoment / n.volume) * std::sin(rr);
  r.gz = kn - _KG * std::sin(rr);
  r.ok = true;
  return r;
}

double GzOrLow(const detail::HullGeom &_g, const std::vector<int> &_flooded,
               const std::vector<Compartment> &_comps, double _phiDeg, double _KG) {
  const GzEval e = GzDamaged(_g, _flooded, _comps, _phiDeg, _KG);
  return e.ok ? e.gz : -1.0e6;
}

/// 残存 GZ 峰值精修（与 StabilityGzCore 的峰值精修同一策略）
double RefinePeakDamaged(const detail::HullGeom &_g, const std::vector<int> &_flooded,
                         const std::vector<Compartment> &_comps, double _KG,
                         double _lo, double _hi) {
  constexpr double kInvPhi = 0.6180339887498949;
  double a = _lo, b = _hi;
  double c = b - kInvPhi * (b - a);
  double d = a + kInvPhi * (b - a);
  double fc = GzOrLow(_g, _flooded, _comps, c, _KG);
  double fd = GzOrLow(_g, _flooded, _comps, d, _KG);
  for (int i = 0; i < 60; ++i) {
    if (fc > fd) {
      b = d;
      d = c;
      fd = fc;
      c = b - kInvPhi * (b - a);
      fc = GzOrLow(_g, _flooded, _comps, c, _KG);
    } else {
      a = c;
      c = d;
      fc = fd;
      d = a + kInvPhi * (b - a);
      fd = GzOrLow(_g, _flooded, _comps, d, _KG);
    }
  }
  return 0.5 * (a + b);
}

}  // namespace

DamageResult FloodOne(const GzInput &_intact, const std::vector<Compartment> &_comps,
                      int _floodedIndex) {
  DamageResult r;
  const detail::HullGeom g = detail::BuildHullGeom(_intact);
  if (!g.valid) {
    r.note = "完整船几何无效（站表为空 / 排水体积为 0），无法计算破舱稳性";
    return r;
  }

  // ① 选定破损舱室：<0 表示全破（负例/极限工况）
  std::vector<int> flooded;
  if (_floodedIndex < 0) {
    flooded.reserve(_comps.size());
    for (int i = 0; i < static_cast<int>(_comps.size()); ++i) flooded.push_back(i);
  } else if (_floodedIndex < static_cast<int>(_comps.size())) {
    flooded.push_back(_floodedIndex);
  } else {
    r.note = "破损舱索引越界";
    return r;
  }
  if (flooded.empty()) {
    r.note = "未指定破损舱室";
    return r;
  }

  // ② 几何合法性：本内核只在"舱室完全落在船体包络内"时使用损失浮力法。
  //    越界说明输入不是一条能自洽的船，宁可判失败也不外推。
  for (int idx : flooded) {
    const Compartment &c = _comps[static_cast<std::size_t>(idx)];
    if (!(c.x2 > c.x1) || !(c.y2 > c.y1) || !(c.z2 > c.z1)) {
      r.note = "舱室几何退化（要求 x2>x1, y2>y1, z2>z1）：" + c.name;
      return r;
    }
    if (c.x1 < -1e-6 || c.x2 > g.length + 1e-6) {
      r.note = "舱室纵向超出 [0,L]：" + c.name;
      return r;
    }
    if (c.z1 < -1e-6 || c.z2 > g.depth + 1e-6) {
      r.note = "舱室垂向超出 [0,D]：" + c.name;
      return r;
    }
    const double need = std::max(std::abs(c.y1), std::abs(c.y2));
    double bMin = g.maxHalfB;
    for (std::size_t i = 0; i < g.x.size(); ++i) {
      if (g.x[i] >= c.x1 - 1e-9 && g.x[i] <= c.x2 + 1e-9) {
        bMin = std::min(bMin, g.halfB[i]);
      }
    }
    if (need > bMin + 1e-6) {
      r.note = "舱室横向超出该段船体半宽包络，损失浮力法不适用：" + c.name;
      return r;
    }
  }

  // ③ 正浮残存 GZ：对称破损应严格为 0
  const GzEval e0 = GzDamaged(g, flooded, _comps, 0.0, _intact.KG);
  if (!e0.ok) {
    const double maxNet = NetAt(g, flooded, _comps, 0.0, g.depth + 1.0).volume;
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "剩余浮力不足：最大可用浮力 %.3f m³ < 排水量 %.3f m³，判定不能继续漂浮",
                  std::max(maxNet, 0.0), g.displacement);
    r.note = buf;
    return r;
  }

  // ④ 平衡横倾角：GZ(0) 的符号决定船往哪边倒，找第一个 GZ 零点（稳定平衡）。
  double heel = 0.0;
  if (std::abs(e0.gz) > 1e-9) {
    const double dir = (e0.gz < 0.0) ? 1.0 : -1.0;  // GZ<0 ⇒ 继续向右舷倾
    double lo = 0.0;
    double gLo = e0.gz;
    double hi = 0.0;
    bool bracketed = false;

    for (int k = 1; k <= 120; ++k) {  // 0.5° 步长，搜索到 60°
      const double phi = dir * 0.5 * static_cast<double>(k);
      const GzEval e = GzDamaged(g, flooded, _comps, phi, _intact.KG);
      if (!e.ok) break;
      if ((gLo < 0.0 && e.gz >= 0.0) || (gLo > 0.0 && e.gz <= 0.0)) {
        hi = phi;
        bracketed = true;
        break;
      }
      lo = phi;
      gLo = e.gz;
    }

    if (!bracketed) {
      r.note = "在 ±60° 内找不到 GZ=0 的稳定平衡横倾（残存稳性不足，判定倾覆）";
      return r;
    }

    for (int i = 0; i < 60; ++i) {
      const double mid = 0.5 * (lo + hi);
      const GzEval em = GzDamaged(g, flooded, _comps, mid, _intact.KG);
      if (!em.ok) {
        r.note = "平衡横倾求解过程中剩余浮力失效，判定不能继续漂浮";
        return r;
      }
      if ((em.gz < 0.0) == (gLo < 0.0)) {
        lo = mid;
        gLo = em.gz;
      } else {
        hi = mid;
      }
    }
    heel = 0.5 * (lo + hi);
  }

  // ⑤ 平衡状态
  double z0 = 0.0;
  if (!SolveNetZ0(g, flooded, _comps, heel, g.displacement, z0)) {
    r.note = "平衡横倾处剩余浮力不足，判定不能继续漂浮";
    return r;
  }
  const double t = std::tan(std::min(std::abs(heel), 89.0) * kDeg2Rad) *
                   ((heel < 0.0) ? -1.0 : 1.0);
  const NetProps net = NetAt(g, flooded, _comps, t, z0);

  double fv = 0.0;
  for (int idx : flooded) {
    const Compartment &c = _comps[static_cast<std::size_t>(idx)];
    const detail::SectionProps sp = BoxBelowWaterline(c.y1, c.y2, c.z1, c.z2, t, z0);
    fv += Mu(c) * (c.x2 - c.x1) * sp.area;
  }

  // ⑥ 残存 GM = 平衡横倾处的 dGZ/dφ
  const double hDeg = kGmDeltaRad * kRad2Deg;
  const GzEval ep = GzDamaged(g, flooded, _comps, heel + hDeg, _intact.KG);
  const GzEval em = GzDamaged(g, flooded, _comps, heel - hDeg, _intact.KG);
  const double gm = (ep.ok && em.ok) ? (ep.gz - em.gz) / (2.0 * kGmDeltaRad) : 0.0;

  // ⑦ 残存 GZ 最大值：左右各扫到 89°（0.1° 与完整船同网格，保证 μ=0 时可逐位复现）
  //    初值取平衡点处的 0：曲线在该点必然过零，最大值不会低于它。
  double best = 0.0;
  double bestPhi = heel;
  for (int side = -1; side <= 1; side += 2) {
    for (int i = 0; i <= 890; ++i) {
      const double phi = side * 0.1 * static_cast<double>(i);
      const GzEval e = GzDamaged(g, flooded, _comps, phi, _intact.KG);
      if (!e.ok) continue;
      if (e.gz > best) {
        best = e.gz;
        bestPhi = phi;
      }
    }
  }
  if (bestPhi > -88.9 && bestPhi < 88.9) {
    const double lo = std::max(-89.0, bestPhi - 0.2);
    const double hi = std::min(89.0, bestPhi + 0.2);
    bestPhi = RefinePeakDamaged(g, flooded, _comps, _intact.KG, lo, hi);
    const GzEval e = GzDamaged(g, flooded, _comps, bestPhi, _intact.KG);
    if (e.ok) best = e.gz;
  }

  // ⑧ 干舷校核：低舷侧甲板边缘一旦没入水线，本模型视为丧失储备浮力。
  const double deckImmersion = z0 + std::abs(t) * g.maxHalfB;
  const bool deckWet = deckImmersion > g.depth + 1e-9;

  // ⑨ 纵倾：把"浮心纵向移动量"折算成小角纵倾（线性估算，见文件头）
  const double kbIntact = 0.5 * g.draft;  // 矩形剖面正浮浮心严格在中吃水
  const double bmL =
      (g.waterplaneIl > 0.0) ? g.waterplaneIl / g.displacement : 0.0;
  const double gmL = bmL + kbIntact - _intact.KG;
  const double xBDamaged = (net.volume > 1e-12) ? net.xMoment / net.volume : g.lcbX;
  const double trim = (std::abs(gmL) > 1e-6) ? (g.lcbX - xBDamaged) / gmL : 0.0;

  r.floodedVolume = fv;
  r.newDraft = z0;
  r.heelDeg = heel;
  r.trimDeg = trim * kRad2Deg;
  r.gmResidual = gm;
  r.gzMaxResidual = best;
  r.canFloat = !deckWet;
  r.oneCompartmentStandard = r.canFloat && gm > 0.0;

  char buf[320];
  if (_floodedIndex < 0) {
    std::snprintf(buf, sizeof(buf),
                  "全部 %zu 个舱室破损：%.1f%% 浮力损失，平衡横倾 %.2f°，残存 GM %.3f m%s",
                  _comps.size(), 100.0 * fv / std::max(g.displacement, 1e-9), heel, gm,
                  deckWet ? "；最终水线淹没甲板边缘，判定不能继续漂浮" : "");
  } else if (deckWet) {
    std::snprintf(buf, sizeof(buf),
                  "单舱破损（%s）：最终水线淹没甲板边缘（干舷丧失），判定不能继续漂浮",
                  _comps[static_cast<std::size_t>(_floodedIndex)].name.c_str());
  } else {
    std::snprintf(buf, sizeof(buf),
                  "单舱破损（%s）：残存浮力充足，平衡横倾 %.2f°，残存 GM %.3f m，纵倾 %.2f°",
                  _comps[static_cast<std::size_t>(_floodedIndex)].name.c_str(), heel, gm,
                  r.trimDeg);
  }
  r.note = buf;
  return r;
}

}  // namespace keship
