/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L2 结构建模→稳性链 —— 纯计算内核独立测试（不依赖 Qt / gz / 仿真进程）
 *
 * 覆盖：GZ(0)=0、矩形驳船 wall-sided 解析对照、初稳性高、单峰性、
 *       IMO 五项衡准阈值、负 GM 反例、站数收敛；破舱：μ=0 退化、
 *       对称/不对称横倾、μ 单调性、进水量守恒、全破负例。
 * 全部判据是数值判据：失败即 return 1，可直接接 CI。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "DamageStabilityCore.hh"
#include "StabilityGzCore.hh"

namespace {

int gFail = 0;
int gPass = 0;

void Check(bool _cond, const std::string &_msg) {
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (_cond) {
    ++gPass;
  } else {
    ++gFail;
  }
}

void Title(const char *_t) { std::printf("\n── %s ─────────────────────────────────────\n", _t); }

double RelErr(double _a, double _b) {
  return std::abs(_a - _b) / std::max(1.0e-12, std::abs(_b));
}

/// HS-08 教学驳船：矩形剖面，站表半宽恒为 B/2
keship::GzInput Barge(double _kg) {
  keship::GzInput in;
  in.L = 6.0;
  in.B = 1.5;
  in.D = 1.0;
  in.draft = 0.3;
  in.KG = _kg;
  in.rhoWater = 1000.0;
  in.boxSection = true;
  in.stations = {{0.0, 0.75}, {3.0, 0.75}, {6.0, 0.75}};
  return in;
}

/// 非矩形（变半宽）教学船：抛物线型平面形状，用于站数收敛与一般路径校核
keship::GzInput ShipHull(int _n, double _kg) {
  keship::GzInput in;
  in.L = 6.0;
  in.B = 1.5;
  in.D = 1.0;
  in.draft = 0.3;
  in.KG = _kg;
  in.rhoWater = 1000.0;
  in.boxSection = false;
  for (int i = 0; i < _n; ++i) {
    const double x = in.L * static_cast<double>(i) / static_cast<double>(_n - 1);
    const double u = 2.0 * x / in.L - 1.0;
    in.stations.push_back({x, 0.735 * (1.0 - 0.55 * u * u)});
  }
  return in;
}

/// wall-sided 解析式（仅在"水线同时切到两舷"时成立）
double WallSided(double _phiDeg, double _B, double _T, double _KG) {
  const double bm = _B * _B / (12.0 * _T);
  const double gm = 0.5 * _T + bm - _KG;
  const double r = _phiDeg * M_PI / 180.0;
  return std::sin(r) * (gm + 0.5 * bm * std::tan(r) * std::tan(r));
}

// ── ① GZ(0) 严格为 0 ───────────────────────────────────────────────────────
void TestGzAtZero() {
  Title("T1.1  GZ(0) == 0");
  const double box = keship::GZ(0.0, Barge(0.5));
  const double ship = keship::GZ(0.0, ShipHull(21, 0.5));
  const double negZero = keship::GZ(-0.0, Barge(0.5));

  std::printf("        box GZ(0)=%.3e   ship GZ(0)=%.3e   GZ(-0)=%.3e\n", box, ship,
              negZero);
  Check(std::abs(box) < 1.0e-9, "① 矩形驳船 GZ(0) 严格为 0（|GZ|<1e-9）");
  Check(std::abs(ship) < 1.0e-9, "① 变半宽船 GZ(0) 严格为 0（|GZ|<1e-9）");
  Check(std::abs(negZero) < 1.0e-9, "① GZ(-0.0) 严格为 0（奇函数在 0 处无符号跳变）");
}

// ── ② wall-sided 解析对照（含有效性边界）────────────────────────────────────
void TestWallSided() {
  Title("T1.2  矩形驳船 vs wall-sided 解析式");
  const keship::GzInput in = Barge(0.5);
  const double B = in.B, T = in.draft, D = in.D, KG = in.KG;

  // 两个边界：舭部出水（高舷侧）与甲板边缘入水（低舷侧）。取先发生者。
  const double phiBilge = std::atan(2.0 * T / B) * 180.0 / M_PI;
  const double phiDeck = std::atan(2.0 * (D - T) / B) * 180.0 / M_PI;
  const double phiValid = std::min(phiBilge, phiDeck);

  std::printf("        φ_舭部出水=%.2f°   φ_甲板入水=%.2f°   ⇒ wall-sided 有效上限=%.2f°\n",
              phiBilge, phiDeck, phiValid);
  std::printf("        %4s  %10s  %10s  %10s  %s\n", "φ[°]", "数值GZ", "解析GZ", "相对误差",
              "wall-sided");

  bool inRangeOk = true;
  double worstInRange = 0.0;
  bool beyondDeviates = true;

  for (int phi = 0; phi <= 30; phi += 5) {
    const double num = keship::GZ(static_cast<double>(phi), in);
    const double ana = WallSided(static_cast<double>(phi), B, T, KG);
    const double rel = (phi == 0) ? 0.0 : RelErr(num, ana);
    const bool valid = (static_cast<double>(phi) <= phiValid + 1e-9);

    if (valid) {
      worstInRange = std::max(worstInRange, rel);
      if (rel > 0.02) inRangeOk = false;
    } else if (rel <= 0.02) {
      beyondDeviates = false;  // 越过边界后若仍"吻合"，说明模型没按实际剖面算
    }

    std::printf("        %4d  %+10.6f  %+10.6f  %10.2e  %s\n", phi, num, ana, rel,
                valid ? "有效" : "× 已失效（按实际剖面）");
  }

  Check(inRangeOk, "② φ ≤ wall-sided 有效上限（21.8°）时相对误差 ≤2%（实测 "
                   + std::to_string(worstInRange) + "）");
  Check(beyondDeviates,
        "② 25°/30° 已越过舭部出水角，数值解必须偏离解析式 >2%（证明未继续套公式）");
  Check(keship::GZ(20.0, in) > 0.0 && keship::GZ(30.0, in) > 0.0,
        "② 大倾角 GZ 仍为正（剖面截断未产生非物理负值）");
}

// ── ③ 初稳性高 = dGZ/dφ|0 ──────────────────────────────────────────────────
void TestGm0() {
  Title("T1.3  GM0 数值差分 vs KB+BM−KG");
  const keship::GzInput in = Barge(0.5);
  const double kb = 0.5 * in.draft;
  const double bm = in.B * in.B / (12.0 * in.draft);
  const double gmAnalytic = kb + bm - in.KG;

  const double hRad = 1.0e-3;
  const double hDeg = hRad * 180.0 / M_PI;
  const double gmNum = (keship::GZ(hDeg, in) - keship::GZ(-hDeg, in)) / (2.0 * hRad);

  std::printf("        KB=%.6f  BM=%.6f  GM=KB+BM−KG=%.6f\n", kb, bm, gmAnalytic);
  std::printf("        dGZ/dφ|0（中心差分 h=1e-3 rad）= %.9f  相对误差=%.2e\n", gmNum,
              RelErr(gmNum, gmAnalytic));
  Check(RelErr(gmNum, gmAnalytic) <= 0.02,
        "③ dGZ/dφ|0 与 KB+BM−KG 相对误差 ≤2%");
  Check(std::abs(gmNum - gmAnalytic) < 1.0e-6,
        "③ 二者实际吻合到 1e-6 以内（远优于 2% 门槛）");
}

// ── ④ 0~60° 单峰 ──────────────────────────────────────────────────────────
void TestSinglePeak() {
  Title("T1.4  GZ 曲线 0~60° 单峰");
  const auto curve = keship::GzCurve(Barge(0.5), 60.0, 1.0);
  Check(curve.size() == 61, "④ GzCurve(60°,1°) 返回 61 点（含两端）");

  int upDown = 0;  // 由升转降的次数
  int downUp = 0;  // 由降转升的次数
  double best = -1.0e9;
  double bestPhi = 0.0;
  for (std::size_t i = 0; i < curve.size(); ++i) {
    if (curve[i].gzM > best) {
      best = curve[i].gzM;
      bestPhi = curve[i].phiDeg;
    }
    if (i > 0 && i + 1 < curve.size()) {
      const double d0 = curve[i].gzM - curve[i - 1].gzM;
      const double d1 = curve[i + 1].gzM - curve[i].gzM;
      if (d0 > 0.0 && d1 < 0.0) ++upDown;
      if (d0 < 0.0 && d1 > 0.0) ++downUp;
    }
  }

  const keship::GzSummary s = keship::Summarize(Barge(0.5));
  std::printf("        曲线峰值=%.6f m @ %.1f°（1° 采样）；汇总峰值=%.6f m @ %.2f°\n",
              best, bestPhi, s.gzMaxM, s.angleGzMaxDeg);

  Check(upDown == 1 && downUp == 0, "④ 0~60° 内恰好一次由升转降、无二次上升（单峰）");
  Check(s.angleGzMaxDeg > 0.0 && s.angleGzMaxDeg < 60.0,
        "④ 最大 GZ 对应横倾角落在 (0,60) 内");
  Check(std::abs(s.gzMaxM - best) < 0.005,
        "④ 汇总峰值与 1° 采样峰值一致（精修差 <0.005 m）");
}

// ── ⑤ IMO 五项衡准阈值 ────────────────────────────────────────────────────
void TestImo() {
  Title("T1.5  IMO A.749(18) 五项衡准阈值");

  const keship::GzSummary good = keship::Summarize(Barge(0.38));
  const keship::GzSummary base = keship::Summarize(Barge(0.5));

  auto dump = [](const char *_name, const keship::GzSummary &_s) {
    std::printf("        %s：GZmax=%.4f m @%.1f°  GM0=%.4f m\n", _name, _s.gzMaxM,
                _s.angleGzMaxDeg, _s.gm0M);
    std::printf("              A(0-30)=%.5f [%s≥0.055]  A(30-40)=%.5f [%s≥0.030]\n",
                _s.area0to30, _s.passArea0to30 ? "PASS" : "FAIL", _s.area30to40,
                _s.passArea30to40 ? "PASS" : "FAIL");
    std::printf("              A(40-φv)=%.5f [%s≥0.090]  φv=%.2f°  allPass=%s\n",
                _s.area40toVanishing, _s.passArea40toVan ? "PASS" : "FAIL",
                _s.vanishingAngleDeg, _s.allPass ? "true" : "false");
  };
  dump("KG=0.38（可满足）", good);
  dump("KG=0.50（默认驳船）", base);

  // 阈值逐条核对：布尔量必须**恰好**等于对应比较式，不能是恒真/恒假
  Check(good.passArea0to30 == (good.area0to30 >= 0.055), "⑤ 0~30° 阈值 = 0.055");
  Check(good.passArea30to40 == (good.area30to40 >= 0.030), "⑤ 30~40° 阈值 = 0.030");
  Check(good.passArea40toVan == (good.area40toVanishing >= 0.090),
        "⑤ 40~消失角 阈值 = 0.090");
  Check(good.passGzMax == (good.gzMaxM >= 0.20 && good.angleGzMaxDeg >= 30.0),
        "⑤ GZmax 阈值 = 0.20 m 且对应角 ≥30°");
  Check(good.passGm0 == (good.gm0M >= 0.15), "⑤ GM0 阈值 = 0.15 m");
  Check(good.allPass == (good.passArea0to30 && good.passArea30to40 &&
                         good.passArea40toVan && good.passGzMax && good.passGm0),
        "⑤ allPass 为五项的逻辑与");
  Check(good.allPass, "⑤ 正例：KG=0.38 时五项全部满足（判据可达，不是恒假）");
  Check(!base.allPass, "⑤ 反例：KG=0.50 时 GZmax=0.177<0.2，allPass=false（判据非恒真）");
}

// ── ⑥ 负 GM 反例 ─────────────────────────────────────────────────────────
void TestNegativeGm() {
  Title("T1.6  KG 抬到 KB+BM 以上");
  const keship::GzInput in = Barge(0.9);  // KB+BM = 0.775
  const keship::GzSummary s = keship::Summarize(in);
  std::printf("        KG=0.90 > KB+BM=0.775 ⇒ GM0=%.6f  allPass=%s  GZmax=%.6f\n",
              s.gm0M, s.allPass ? "true" : "false", s.gzMaxM);
  Check(s.gm0M < 0.0, "⑥ 负例：gm0M < 0");
  Check(!s.allPass, "⑥ 负例：allPass == false");
  Check(s.gm0M < -0.1 && s.gm0M > -0.15, "⑥ 负例量值 ≈ KB+BM−KG = −0.125（不是随意发散）");
}

// ── ⑦ 站数收敛 ───────────────────────────────────────────────────────────
void TestConvergence() {
  Title("T1.7  站数 21 → 41 收敛");
  const auto s21 = keship::Summarize(ShipHull(21, 0.5));
  const auto s41 = keship::Summarize(ShipHull(41, 0.5));
  const double rel = RelErr(s21.gzMaxM, s41.gzMaxM);

  std::printf("        N=21: GZmax=%.8f m @%.2f°  GM0=%.6f\n", s21.gzMaxM,
              s21.angleGzMaxDeg, s21.gm0M);
  std::printf("        N=41: GZmax=%.8f m @%.2f°  GM0=%.6f\n", s41.gzMaxM,
              s41.angleGzMaxDeg, s41.gm0M);
  std::printf("        相对变化=%.4f%%（门槛 0.5%%）\n", 100.0 * rel);

  Check(rel <= 0.005, "⑦ 21→41 站 gzMaxM 相对变化 ≤0.5%");
  Check(std::abs(keship::GZ(0.0, ShipHull(21, 0.5))) < 1.0e-9,
        "⑦ 变半宽船 GZ(0) 仍严格为 0");

  // 一般路径的初稳性高：BM = ∫(2/3)b³dx / ∫2bT dx（直立舷侧、变半宽时仍成立）
  double vol = 0.0, ix = 0.0;
  const int n = 4000;
  for (int i = 0; i < n; ++i) {
    const double x0 = 6.0 * i / n, x1 = 6.0 * (i + 1) / n;
    const double u0 = 2.0 * x0 / 6.0 - 1.0, u1 = 2.0 * x1 / 6.0 - 1.0;
    const double b0 = 0.735 * (1.0 - 0.55 * u0 * u0);
    const double b1 = 0.735 * (1.0 - 0.55 * u1 * u1);
    vol += 0.5 * (2.0 * b0 * 0.3 + 2.0 * b1 * 0.3) * (x1 - x0);
    ix += 0.5 * ((2.0 / 3.0) * b0 * b0 * b0 + (2.0 / 3.0) * b1 * b1 * b1) * (x1 - x0);
  }
  const double gmAnalytic = 0.15 + ix / vol - 0.5;
  const double gmRel = RelErr(s41.gm0M, gmAnalytic);
  std::printf("        变半宽船 GM0 解析=%.6f  数值=%.6f  相对误差=%.3e\n", gmAnalytic,
              s41.gm0M, gmRel);
  Check(gmRel <= 0.02, "⑦ 变半宽船 GM0 与 BM=I/∇ 解析值相对误差 ≤2%（一般路径正确）");
  Check(std::abs(keship::GZ(20.0, ShipHull(21, 0.5)) +
                 keship::GZ(-20.0, ShipHull(21, 0.5))) < 1.0e-9,
        "⑦ 对称船 GZ(−φ) = −GZ(φ)（20° 处 ≤1e-9）");
}

// ── 破舱稳性 ─────────────────────────────────────────────────────────────
keship::Compartment SymComp(double _mu) {
  return {"对称舱", 2.0, 4.0, -0.7, 0.7, 0.0, 0.6, _mu};
}
keship::Compartment StarboardComp(double _mu) {
  return {"右舷舱", 2.0, 4.0, 0.1, 0.7, 0.0, 0.6, _mu};
}
keship::Compartment PortComp(double _mu) {
  return {"左舷舱", 2.0, 4.0, -0.7, -0.1, 0.0, 0.6, _mu};
}

void TestDamageDegenerate() {
  Title("D1  μ=0 退化：必须与完整船逐位一致");
  const keship::GzInput intact = Barge(0.5);
  const keship::GzSummary s = keship::Summarize(intact);
  const auto r = keship::FloodOne(intact, {SymComp(0.0)}, 0);

  std::printf("        intact: GM0=%.12f  GZmax=%.12f\n", s.gm0M, s.gzMaxM);
  std::printf("        μ=0   : GM0=%.12f  GZmax=%.12f  heel=%.3e  Vf=%.3e\n",
              r.gmResidual, r.gzMaxResidual, r.heelDeg, r.floodedVolume);

  Check(std::abs(r.gmResidual - s.gm0M) <= 1.0e-9,
        "D1 μ=0 时 gmResidual 与完整船 GM0 差 ≤1e-9");
  Check(std::abs(r.gzMaxResidual - s.gzMaxM) <= 1.0e-9,
        "D1 μ=0 时 gzMaxResidual 与完整船 GZmax 差 ≤1e-9");
  Check(std::abs(r.heelDeg) <= 1.0e-9, "D1 μ=0 时无横倾（≤1e-9°）");
  Check(std::abs(r.floodedVolume) <= 1.0e-12, "D1 μ=0 时进水量为 0");
  Check(r.canFloat, "D1 μ=0 时仍可漂浮");
}

void TestDamageSymmetry() {
  Title("D2 对称舱破损：不产生横倾");
  const auto r = keship::FloodOne(Barge(0.5), {SymComp(0.95)}, 0);
  std::printf("        μ=0.95 对称舱：heel=%.3e°  newDraft=%.5f m  GM=%.6f m  Vf=%.6f m³\n",
              r.heelDeg, r.newDraft, r.gmResidual, r.floodedVolume);
  std::printf("        note: %s\n", r.note.c_str());
  Check(std::abs(r.heelDeg) < 1.0e-6, "D2 对称舱 |heelDeg| < 1e-6");
  Check(r.canFloat, "D2 对称舱破损后仍能漂浮");
  Check(r.oneCompartmentStandard, "D2 满足一舱不沉（残存 GM>0 且未丧失干舷）");
  Check(r.gmResidual > 0.0 && r.gmResidual < 0.275,
        "D2 残存 GM 为正且小于完整船（0 < GM < 0.275）");
}

void TestDamageAsymmetry() {
  Title("D3 不对称舱破损：横倾指向破损侧");
  const auto rs = keship::FloodOne(Barge(0.5), {StarboardComp(0.95)}, 0);
  const auto rp = keship::FloodOne(Barge(0.5), {PortComp(0.95)}, 0);

  std::printf("        右舷舱：heel=%+.3f°  trim=%+.3f°  GM=%.6f  Vf=%.6f  canFloat=%d\n",
              rs.heelDeg, rs.trimDeg, rs.gmResidual, rs.floodedVolume, rs.canFloat ? 1 : 0);
  std::printf("        左舷舱：heel=%+.3f°  trim=%+.3f°  GM=%.6f  Vf=%.6f  canFloat=%d\n",
              rp.heelDeg, rp.trimDeg, rp.gmResidual, rp.floodedVolume, rp.canFloat ? 1 : 0);

  Check(rs.heelDeg > 0.0 && rs.heelDeg < 30.0,
        "D3 右舷舱破损 → 向右舷横倾，量值 ∈ (0,30)");
  Check(rp.heelDeg < 0.0 && rp.heelDeg > -30.0,
        "D3 左舷舱破损 → 向左舷横倾（符号镜像，量值 ∈ (−30,0)）");
  Check(rs.canFloat && rp.canFloat, "D3 单舱破损后仍能漂浮");
  Check(std::abs(rs.trimDeg) < 1.0 && std::abs(rp.trimDeg) < 1.0,
        "D3 舱室位于船中，纵倾接近 0（|trim|<1°）");
}

void TestDamagePermeability() {
  Title("D4 渗透率 μ 扫描：残存 GM 单调不增");
  std::printf("        %6s  %12s  %12s\n", "μ", "gmResidual", "Vf(m³)");
  double prev = 1.0e9;
  bool mono = true;
  for (double mu : {0.0, 0.2, 0.4, 0.6, 0.8, 0.95}) {
    const auto r = keship::FloodOne(Barge(0.5), {SymComp(mu)}, 0);
    std::printf("        %6.2f  %12.6f  %12.6f\n", mu, r.gmResidual, r.floodedVolume);
    if (r.gmResidual > prev + 1.0e-12) mono = false;
    prev = r.gmResidual;
  }
  Check(mono, "D4 μ 从 0→0.95，gmResidual 单调不增");
  Check(prev < 0.275 - 0.05, "D4 μ=0.95 时 GM 明显下降（不是常数假象）");
}

void TestDamageVolume() {
  Title("D5  进水量 = μ × 舱室体积（全浸工况）");
  keship::Compartment c{"全浸舱", 2.0, 4.0, 0.1, 0.7, 0.0, 0.15, 0.90};
  const double vc = (c.x2 - c.x1) * (c.y2 - c.y1) * (c.z2 - c.z1);
  const auto r = keship::FloodOne(Barge(0.5), {c}, 0);
  const double expect = 0.90 * vc;
  const double rel = RelErr(r.floodedVolume, expect);
  std::printf("        舱室体积=%.6f m³  期望进水量=μV=%.6f m³  实测=%.6f m³  误差=%.3e\n",
              vc, expect, r.floodedVolume, rel);
  std::printf("        newDraft=%.5f m（> z2=0.15 ⇒ 舱室确实全浸）  heel=%.3f°\n",
              r.newDraft, r.heelDeg);
  Check(r.newDraft > c.z2, "D5 最终水线高于舱顶（全浸前提成立）");
  Check(rel <= 0.05, "D5 floodedVolume 与 μ·V 相对误差 ≤5%");
  Check(rel < 1.0e-6, "D5 实测为解析级一致（误差 <1e-6，远优于 5%）");
}

void TestDamageAllFlooded() {
  Title("D6  负例：全部舱室破损（μ=1）");
  std::vector<keship::Compartment> all = {
      {"艉舱", 0.0, 2.0, -0.75, 0.75, 0.0, 1.0, 1.0},
      {"中舱", 2.0, 4.0, -0.75, 0.75, 0.0, 1.0, 1.0},
      {"艏舱", 4.0, 6.0, -0.75, 0.75, 0.0, 1.0, 1.0},
  };
  const auto r = keship::FloodOne(Barge(0.5), all, -1);
  std::printf("        canFloat=%d  newDraft=%.3f  GM=%.3f  heel=%.3f\n",
              r.canFloat ? 1 : 0, r.newDraft, r.gmResidual, r.heelDeg);
  std::printf("        note: %s\n", r.note.c_str());
  Check(!r.canFloat, "D6 全破后 canFloat == false");
  Check(r.note.find("浮力不足") != std::string::npos, "D6 note 明确说明剩余浮力不足");
  Check(r.newDraft == 0.0 && r.gmResidual == 0.0 && r.heelDeg == 0.0,
        "D6 不可漂浮时不返回'看起来正常'的水线/GM/横倾（全置 0）");
  Check(!r.oneCompartmentStandard, "D6 不满足一舱不沉");
}

void TestDamageTrim() {
  Title("D7  纵倾方向检查（附加油密性）");
  // 注意坐标系：x=0 是艉、x=6 是艏
  const keship::Compartment stern{"艉部舱", 0.0, 2.0, -0.7, 0.7, 0.0, 0.4, 0.90};
  const keship::Compartment bow{"艏部舱", 4.0, 6.0, -0.7, 0.7, 0.0, 0.4, 0.90};
  const auto rs = keship::FloodOne(Barge(0.5), {stern}, 0);
  const auto rb = keship::FloodOne(Barge(0.5), {bow}, 0);

  std::printf("        艉舱破损：trim=%+.3f°  heel=%+.3f°  Vf=%.4f  canFloat=%d\n",
              rs.trimDeg, rs.heelDeg, rs.floodedVolume, rs.canFloat ? 1 : 0);
  std::printf("        艏舱破损：trim=%+.3f°  heel=%+.3f°  Vf=%.4f  canFloat=%d\n",
              rb.trimDeg, rb.heelDeg, rb.floodedVolume, rb.canFloat ? 1 : 0);

  Check(rs.trimDeg < -0.5, "D7 艉舱破损 → trimDeg<0（艉沉艏抬）");
  Check(rb.trimDeg > 0.5, "D7 艏舱破损 → trimDeg>0（艏沉艉抬）");
  Check(std::abs(rs.heelDeg) < 1.0e-6 && std::abs(rb.heelDeg) < 1.0e-6,
        "D7 左右对称舱室不产生横倾");
}

}  // namespace

int main() {
  std::printf("================================================================\n");
  std::printf(" KeShip L2 稳性链 · 纯计算内核测试（StabilityGzCore + DamageStabilityCore）\n");
  std::printf("================================================================\n");

  TestGzAtZero();
  TestWallSided();
  TestGm0();
  TestSinglePeak();
  TestImo();
  TestNegativeGm();
  TestConvergence();
  TestDamageDegenerate();
  TestDamageSymmetry();
  TestDamageAsymmetry();
  TestDamagePermeability();
  TestDamageVolume();
  TestDamageAllFlooded();
  TestDamageTrim();

  std::printf("\n================================================================\n");
  std::printf(" 汇总：%d PASS, %d FAIL\n", gPass, gFail);
  std::printf("================================================================\n");
  return (gFail == 0) ? 0 : 1;
}
