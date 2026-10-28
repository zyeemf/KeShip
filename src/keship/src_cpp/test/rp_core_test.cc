/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— 纯计算内核独立测试（不依赖 Qt / gz / 仿真进程）
 *
 * 覆盖：ITTC-57 手算对照、阻力分解恒等式、R–V 单调性与幂律、敞水特性形状、
 *       船-桨平衡、三因次实船外推、最优 Cb 扫描。
 * 全部判据都是**数值判据**（不是「看起来合理」）：失败即 return 1，可接 CI。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "PropellerCore.hh"
#include "ResistanceCore.hh"

namespace {

int gFail = 0;

void Check(bool _cond, const std::string &_msg) {
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (!_cond) ++gFail;
}

void Note(const std::string &_msg) {
  std::printf("[NOTE]  %s\n", _msg.c_str());
}

bool Near(double _a, double _b, double _rel) {
  const double scale = std::max({1.0, std::abs(_a), std::abs(_b)});
  return std::abs(_a - _b) <= _rel * scale;
}

/// Cb 与排水体积必须自洽：∇ = Cb·L·B·T（与 CbForMinResistance 内部同口径）
keship::ResistanceInput WithCb(const keship::ResistanceInput &_in, double _cb) {
  keship::ResistanceInput s = _in;
  s.Cb = _cb;
  s.volumeD = _cb * _in.Lwl * _in.B * _in.T;
  return s;
}

/// [_vMin,_vMax] 上等距 11 点总阻力的平均值（Cb 扫描的目标函数）
double MeanResistance(const keship::ResistanceInput &_in, double _vMin,
                      double _vMax) {
  const auto curve = keship::ResistanceCurve(_in, _vMin, _vMax, 11);
  double sum = 0.0;
  for (const auto &b : curve) sum += b.Rtotal;
  return curve.empty() ? 0.0 : sum / static_cast<double>(curve.size());
}

// ── ① ITTC-57 手算对照 ──────────────────────────────────────────────────────
void TestIttc57() {
  keship::ResistanceInput in;
  const double re = 1.0e7;
  const double v = re * in.nu / in.Lwl;  ///< 反解出恰好 Re=1e7 的航速

  const auto b = keship::ResistanceAtSpeed(in, v);
  const double cfHand = 0.075 / std::pow(std::log10(re) - 2.0, 2.0);  // 0.075/25 = 0.003
  const double rel = std::abs(b.Cf - cfHand) / cfHand;

  std::printf("        Re=%.6e  Cf=%.12f  手算=%.12f  相对误差=%.3e\n", b.Re,
              b.Cf, cfHand, rel);
  Check(rel <= 1.0e-9, "① ITTC-57：Re=1e7 时 Cf 与手算值一致（相对误差 ≤1e-9）");
  Check(Near(cfHand, 0.003, 1.0e-12), "① ITTC-57：0.075/25 = 0.0030 基准值正确");
}

// ── ② 阻力分解自洽 ─────────────────────────────────────────────────────────
void TestDecomposition() {
  keship::ResistanceInput in;
  in.windSpeedMs = 2.5;      ///< 有风工况：否则 Rwind≡0，恒等式退化成两项
  in.frontalAreaM2 = 3.0;

  bool allOk = true;
  double worst = 0.0;
  for (double v : {0.5, 1.0, 2.0, 3.0, 4.0}) {
    const auto b = keship::ResistanceAtSpeed(in, v);
    const double sum = b.Rf + b.Rr + b.Rwind;
    const double err = std::abs(b.Rtotal - sum) / std::max(1.0, sum);
    worst = std::max(worst, err);
    if (err > 1.0e-9) allOk = false;
  }
  std::printf("        最大相对偏差=%.3e（含风阻 %.3f N @2.5 m/s）\n", worst,
              0.5 * in.rhoAir * 3.0 * 2.5 * 2.5);
  Check(allOk, "② 分解自洽：Rtotal == Rf + Rr + Rwind（≤1e-9，5 个航速）");
}

// ── ③ R–V 单调、无 NaN / 负值 ──────────────────────────────────────────────
void TestCurveMonotonic() {
  keship::ResistanceInput in;
  const auto curve = keship::ResistanceCurve(in, 0.2, 4.0, 41);

  bool increasing = true;
  bool finiteNonNeg = true;
  for (size_t i = 0; i < curve.size(); ++i) {
    const auto &b = curve[i];
    for (double x : {b.speedMs, b.Fn, b.Re, b.Cf, b.onePlusK, b.Rf, b.Rr,
                     b.Rwind, b.Rtotal, b.Ct}) {
      if (!std::isfinite(x) || x < 0.0) finiteNonNeg = false;
    }
    if (i > 0 && !(b.Rtotal > curve[i - 1].Rtotal)) increasing = false;
  }
  std::printf("        点数=%zu  R(0.2)=%.3f N  R(4.0)=%.1f N\n", curve.size(),
              curve.front().Rtotal, curve.back().Rtotal);
  Check(finiteNonNeg, "③a R–V 曲线全部字段有限且非负（无 NaN/负值）");
  Check(increasing, "③b R–V 曲线在 0.2~4.0 m/s 严格单调递增");
}

// ── ④ 兴波段 R ∝ V^k ───────────────────────────────────────────────────────
void TestPowerLaw() {
  keship::ResistanceInput in;
  const auto curve = keship::ResistanceCurve(in, 2.0, 3.0, 11);

  // ln R = ln C + k·ln V 的最小二乘斜率
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  const double n = static_cast<double>(curve.size());
  for (const auto &b : curve) {
    const double x = std::log(b.speedMs);
    const double y = std::log(b.Rtotal);
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  const double k = (n * sxy - sx * sy) / (n * sxx - sx * sx);

  const auto b2 = keship::ResistanceAtSpeed(in, 2.0);
  const double waveShare = b2.Rr / std::max(b2.Rtotal, 1.0e-9);
  std::printf("        拟合 k=%.4f（区间要求 [1.8,2.6]）  Rr/Rtotal@2m/s=%.1f%%\n",
              k, 100.0 * waveShare);
  Check(k >= 1.8 && k <= 2.6, "④ 2.0~3.0 m/s 段 R∝V^k，k 落在 [1.8,2.6]");

  Note("Cr 采用傅汝德 V⁴ 律（Cr∝Fn²）+ 高 Fn 饱和，故 k≈2.2；"
       "此标定下 Fn≈0.3 的兴波份额约 25%，『兴波主导』是教学口径，非数值事实。");
}

// ── ⑤ 螺旋桨敞水特性 ───────────────────────────────────────────────────────
void TestPropellerOpenWater() {
  keship::PropellerInput p;  ///< B3-50：D=0.15, P/D=1.0, Ae/A0=0.5, Z=3

  const auto at0 = keship::OpenWaterAtJ(p, 0.0);
  std::printf("        J=0: KT=%.4f KQ=%.5f η0=%.4f\n", at0.KT, at0.KQ, at0.eta0);
  Check(at0.KT > 0.0, "⑤a J=0 时 KT > 0（系柱推力为正）");

  bool kqDec = true;
  for (int i = 1; i <= 200; ++i) {
    const double j0 = 1.0 * static_cast<double>(i - 1) / 200.0;
    const double j1 = 1.0 * static_cast<double>(i) / 200.0;
    if (!(keship::OpenWaterAtJ(p, j1).KQ < keship::OpenWaterAtJ(p, j0).KQ)) {
      kqDec = false;
    }
  }
  Check(kqDec, "⑤b KQ 随 J 单调递减（0≤J≤1，201 点）");

  double bestEta = -1.0, bestJ = -1.0;
  for (int i = 0; i <= 2400; ++i) {
    const double j = 1.2 * static_cast<double>(i) / 2400.0;
    const double eta = keship::OpenWaterAtJ(p, j).eta0;
    if (eta > bestEta) {
      bestEta = eta;
      bestJ = j;
    }
  }
  std::printf("        η0 峰值=%.4f @ J=%.3f\n", bestEta, bestJ);
  Check(bestJ >= 0.40 && bestJ <= 0.75,
        "⑤c η0 峰值进速系数落在 [0.40,0.75]");
  Check(bestEta > 0.40 && bestEta < 0.85, "⑤d η0 峰值量级合理（0.40~0.85）");

  // 曲线接口：端点与点数
  const auto curve = keship::OpenWaterCurve(p, 1.0, 21);
  Check(curve.size() == 21 && Near(curve.front().J, 0.0, 1e-12) &&
            Near(curve.back().J, 1.0, 1e-12),
        "⑤e OpenWaterCurve 端点与点数正确（21 点，[0,1]）");
}

// ── ⑥ 船-桨平衡 ────────────────────────────────────────────────────────────
void TestMatching() {
  keship::ResistanceInput hull;
  keship::PropellerInput prop;

  const double v = 2.0;
  const double w = 0.15;
  const double t = 0.10;

  const auto m = keship::MatchOperatingPoint(prop, hull, v, w, t);
  const double r = keship::ResistanceAtSpeed(hull, v).Rtotal;
  const double bal = std::abs(m.thrustN * (1.0 - t) - r) / r;

  std::printf("        n=%.2f r/s (%.0f rpm)  T=%.2f N  Q=%.3f N·m  P_D=%.1f W  "
              "QPC=%.3f  残差=%.2e\n",
              m.n_rps, m.n_rps * 60.0, m.thrustN, m.torqueNm, m.powerW, m.qpc,
              bal);
  Check(m.converged, "⑥a 匹配收敛 converged=true");
  Check(bal <= 1.0e-3, "⑥b |T(1−t)−R|/R ≤ 1e-3（解确实满足平衡）");
  Check(m.qpc >= 0.30 && m.qpc <= 0.85, "⑥c QPC ∈ [0.30,0.85]");
  Check(m.n_rps > 0.0 && m.powerW > 0.0 && m.torqueNm > 0.0,
        "⑥d 转速/扭矩/功率均为正");

  // 负例：零航速无有效工作点，不得假装收敛
  const auto m0 = keship::MatchOperatingPoint(prop, hull, 0.0, w, t);
  Check(!m0.converged && m0.n_rps <= 0.0,
        "⑥e 负例：V=0 时无工作点，converged 如实为 false");
}

// ── ⑦ 实船外推（三因次法） ─────────────────────────────────────────────────
void TestShipExtrapolation() {
  keship::ResistanceInput model;
  const double lambda = 10.0;
  const double vm = 2.0;
  const double vs = vm * std::sqrt(lambda);  ///< 同 Fn

  const auto bm = keship::ResistanceAtSpeed(model, vm);
  const double rModel = bm.Rtotal;
  const double rShip = keship::ModelToShipResistance(model, lambda, vs);

  const double sModel = keship::WettedSurfaceM2(model);
  const double cts = rShip / (0.5 * model.rhoWater * sModel * lambda * lambda * vs * vs);

  std::printf("        Fn=%.4f  Rm=%.2f N  Rs=%.1f N  Rm/Rs=%.1e  "
              "Ctm=%.6f  Cts=%.6f\n",
              bm.Fn, rModel, rShip, rModel / rShip, bm.Ct, cts);

  // 剩余阻力系数在两尺度上必须相同：Cts − (1+k)Cfs == Ctm − (1+k)Cfm
  const double ls = model.Lwl * lambda;
  const double res = vs * ls / model.nu;
  const double cfs = 0.075 / std::pow(std::log10(res) - 2.0, 2.0);
  const double lhs = cts - bm.onePlusK * cfs;
  const double rhs = bm.Ct - bm.onePlusK * bm.Cf;
  Check(Near(lhs, rhs, 1.0e-9),
        "⑦a 三因次法自洽：Cts−(1+k)Cfs == Ctm−(1+k)Cfm（剩余项守恒）");

  Check(rShip > 100.0 * rModel, "⑦b 实船总阻力远大于模型（≈λ³ 量级）");

  // ⚠ 需求原文写的是「Cts > Ctm」。这与三因次法**方向相反**：
  //   同 Fn 下实船 Re 大 3 个量级 ⇒ Cfs < Cfm ⇒ Cts = Ctm −(1+k)(Cfm−Cfs) < Ctm。
  //   照抄会让物理出错，故此处断言**正确方向**，并在报告里标注该需求条目笔误。
  Check(cts < bm.Ct, "⑦c 同 Fn 下 Cts < Ctm（实船 Re 更大、摩擦系数更小）");
  Check(cts / bm.Ct > 0.4 && cts / bm.Ct < 1.0, "⑦d Cts/Ctm 落在 (0.4,1.0)");
  Note("需求第 7 条「Cts > Ctm」与三因次法互斥（见报告第 6 节）；本测试按物理正确方向断言。");
}

// ── ⑧ 扫 Cb 找真极小 ───────────────────────────────────────────────────────
void TestOptimalCb() {
  keship::ResistanceInput in;
  const double vMin = 1.0, vMax = 3.0;
  const double cb = keship::CbForMinResistance(in, vMin, vMax);

  const double mEnd = MeanResistance(WithCb(in, 0.50), vMin, vMax);
  const double mGhi = MeanResistance(WithCb(in, 0.85), vMin, vMax);
  const double mMin = MeanResistance(WithCb(in, cb), vMin, vMax);

  std::printf("        Cb*=%.3f  平均阻力: Cb=0.50→%.2f N, Cb*=%.3f→%.2f N, "
              "Cb=0.85→%.2f N\n",
              cb, mEnd, cb, mMin, mGhi);

  Check(cb >= 0.50 && cb <= 0.85, "⑧a CbForMinResistance ∈ [0.5,0.85]");
  Check(mMin < mEnd * 0.995 && mMin < mGhi * 0.995,
        "⑧b 该 Cb 的平均阻力严格小于两端点（真极小，非端点解）");
  Check(std::abs(cb - 0.576) < 0.10,
        "⑧c 最优 Cb 与 Watson 式 Cb_opt=1.08−1.68Fn(Fn=0.3)=0.576 相符（±0.10）");
}

}  // namespace

int main() {
  std::printf("==== L8 阻力→推进链 核心层测试 ====\n");
  TestIttc57();
  TestDecomposition();
  TestCurveMonotonic();
  TestPowerLaw();
  TestPropellerOpenWater();
  TestMatching();
  TestShipExtrapolation();
  TestOptimalCb();

  std::printf("==== 失败 %d 项 ====\n", gFail);
  return gFail == 0 ? 0 : 1;
}
