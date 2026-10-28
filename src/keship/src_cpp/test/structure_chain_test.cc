/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L3 结构设计链 —— 内核测试 + 交付物链测试（SD-01 → SD-03 → SD-09）
 *
 * 两档编译：
 *   ① **不带 Qt**（判据最严的那档，必须独立跑通）：
 *        g++ -std=c++17 -O2 -I utils test/structure_chain_test.cc \
 *            utils/MidshipSectionCore.cc -o /tmp/structure_chain_test
 *      覆盖 ①~⑤ 的解析对照/平行轴定理/中和轴方向/剖面模数/强度换算，
 *      以及 ⑥ 的"SD-01 结构要素 → 中横剖面 → W"与板厚敏感性。
 *   ② **带 Qt**（加 -DKESHIP_CHAIN_QT 并链 ExpDeliverable.cc + Qt5Core）：
 *      额外跑真正的**文件级**交付物链——把 SD-01 交付物写成 sd01_structure.json、
 *      读回、ValidExpDeliverable(d,"SD-01")、再由 extra.members[] 复原截面；
 *      以及 ⑦ 的串号负例（SD-03 交付物拿 "SD-09" 校验必须被拒）。
 *
 * 为什么 ⑥/⑦ 的文件级部分要放在 Qt 档：交付物读写用 QJsonDocument（见
 * shared/ExpDeliverable.hh 的设计约束 ②），不带 Qt 就无从"真读文件"。
 * 但内核判据 ①~⑤ 与 ⑥ 的敏感性**不依赖 Qt**，故第 ① 档已能独立守住公式正确性。
 *
 * 全部判据都是**数值判据**（相对误差上限写死在代码里），失败即 return 1，可接 CI。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "MidshipSectionCore.hh"

#if defined(KESHIP_CHAIN_QT)
#include <QJsonArray>
#include <QJsonObject>

#include "../gui/keship/shared/ExpDeliverable.hh"
#endif

namespace {

int gFail = 0;
int gSkip = 0;

void Check(bool _cond, const std::string &_msg)
{
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (!_cond) ++gFail;
}

#if !defined(KESHIP_CHAIN_QT)
void Skip(const std::string &_msg)
{
  std::printf("[SKIP]  %s\n", _msg.c_str());
  ++gSkip;
}
#endif

/// 纯相对误差判据（不用 max(1,·) 兜底——那会把 1e-7 量级的量判得极松）
bool RelNear(double _a, double _b, double _rel)
{
  const double scale = std::max(std::abs(_a), std::abs(_b));
  if (scale < 1.0e-300) return std::abs(_a - _b) <= _rel;
  return std::abs(_a - _b) / scale <= _rel;
}

/// SD-01 默认结构要素表（与 StructureCognitionDialog 的默认表同值）。
/// Qt 档会断言"从 sd01_structure.json 读回来的 members 与这张表逐字段相等"，
/// 故本表不可能与界面上真正导出的东西悄悄走偏。
std::vector<keship::StructureMember> DefaultMembers()
{
  return {
      {"船底板（外板）", "bottom", 0.012, 0.25, 0.080, 0.006},
      {"甲板", "deck", 0.010, 0.25, 0.075, 0.006},
      {"舷侧板", "side", 0.010, 0.25, 0.075, 0.006},
      {"横舱壁", "bulkhead", 0.008, 0.30, 0.070, 0.006},
      {"纵桁", "girder", 0.009, 0.000, 0.000, 0.000},
      {"内底纵骨（骨材）", "stiffener", 0.000, 0.25, 0.075, 0.006},
      {"双层底（内底板）", "doublebottom", 0.010, 0.25, 0.000, 0.000},
  };
}

constexpr double kB = 1.5;    ///< 型宽 (m)——平台教学船
constexpr double kD = 1.0;    ///< 型深 (m)
constexpr double kHdb = 0.10; ///< 双层底高度 (m)

// ── ① 单块矩形板的解析对照 ─────────────────────────────────────────────────
void TestSinglePlate()
{
  std::printf("\n--- ① 单块矩形板解析对照：b=1.5 m, h=0.01 m, z=0.5 m, D=1.0 m ---\n");

  keship::MidshipInput in;
  in.breadthM = kB;
  in.depthM = kD;
  in.plates.push_back({1.5, 0.01, 0.5, "单板"});

  const auto r = keship::ComputeMidship(in);
  const double aExp = 1.5 * 0.01;              // 0.015
  const double iExp = 1.5 * 0.01 * 0.01 * 0.01 / 12.0;  // 1.25e-7

  std::printf("        A=%.17g (期望 %.17g)  z_NA=%.17g  I=%.17g (期望 %.17g)\n",
              r.areaM2, aExp, r.neutralAxisZM, r.ixM4, iExp);
  Check(r.valid, "① 单板截面有效（valid==true）");
  Check(RelNear(r.areaM2, aExp, 1e-9), "① A = b·h = 0.015 m²（相对误差 ≤1e-9）");
  Check(RelNear(r.neutralAxisZM, 0.5, 1e-9), "① z_NA = 0.5 m（相对误差 ≤1e-9）");
  Check(RelNear(r.ixM4, iExp, 1e-9), "① I = b·h³/12 = 1.25e-7 m⁴（相对误差 ≤1e-9）");
  Check(RelNear(r.ixM4, 1.25e-7, 1e-9), "① I 与教科书数值 1.25e-7 m⁴ 一致");

  // 单板退化情形：z_deck−z_NA = 0.5、z_NA = 0.5 ⇒ 两个剖面模数应当相等
  Check(RelNear(r.sectionModulusDeckM3, r.sectionModulusKeelM3, 1e-9),
        "① 对称单板 W_deck == W_keel");
}

// ── ② 加一块同高度的板：z_NA 不变、I 加倍 ────────────────────────────────────
void TestSymmetricPair()
{
  std::printf("\n--- ② 叠加一块同高度板：z_NA 不变、I 加倍 ---\n");

  keship::MidshipInput one;
  one.breadthM = kB;
  one.depthM = kD;
  one.plates.push_back({1.5, 0.01, 0.5, "板1"});
  const auto r1 = keship::ComputeMidship(one);

  keship::MidshipInput two = one;
  two.plates.push_back({1.5, 0.01, 0.5, "板2"});
  const auto r2 = keship::ComputeMidship(two);

  std::printf("        I1=%.17g  I2=%.17g  z_NA1=%.17g  z_NA2=%.17g\n",
              r1.ixM4, r2.ixM4, r1.neutralAxisZM, r2.neutralAxisZM);
  Check(RelNear(r2.neutralAxisZM, r1.neutralAxisZM, 1e-9),
        "② 叠加同高度板后 z_NA 不变");
  Check(RelNear(r2.ixM4, 2.0 * r1.ixM4, 1e-9), "② 叠加后 I 严格加倍");
  Check(RelNear(r2.areaM2, 2.0 * r1.areaM2, 1e-9), "② 叠加后 A 严格加倍");

  // 关于中和轴对称的一对板（z=0.3 / 0.7）：z_NA 仍在中点，且 I 中"移轴项"为
  // 2·A·d² —— 与解析值逐位对照，确认平行轴定理没有漏项
  keship::MidshipInput sym;
  sym.breadthM = kB;
  sym.depthM = kD;
  sym.plates.push_back({1.5, 0.01, 0.3, "下板"});
  sym.plates.push_back({1.5, 0.01, 0.7, "上板"});
  const auto rs = keship::ComputeMidship(sym);
  const double aOne = 1.5 * 0.01;
  const double iExp = 2.0 * (1.5 * 0.01 * 0.01 * 0.01 / 12.0) + 2.0 * aOne * 0.2 * 0.2;
  std::printf("        z_NA=%.17g  I=%.17g (解析 %.17g)\n", rs.neutralAxisZM, rs.ixM4, iExp);
  Check(RelNear(rs.neutralAxisZM, 0.5, 1e-9), "② 对称双板 z_NA = 0.5 m");
  Check(RelNear(rs.ixM4, iExp, 1e-9), "② 平行轴定理 I = Σ[I_i + A_i·d²]（含移轴项）");
}

// ── ③ 非对称骨材：z_NA 向骨材一侧偏移 ──────────────────────────────────────
void TestAsymmetricStiffener()
{
  std::printf("\n--- ③ 非对称骨材：z_NA 偏移方向 ---\n");

  keship::MidshipInput base;
  base.breadthM = kB;
  base.depthM = kD;
  base.plates.push_back({1.5, 0.01, 0.5, "底"});
  const auto r0 = keship::ComputeMidship(base);

  keship::MidshipInput up = base;
  up.stiffeners.push_back({0.10, 0.010, 0.25, 0.50, 1, "上侧骨材"});
  const auto rUp = keship::ComputeMidship(up);

  keship::MidshipInput down = base;
  down.stiffeners.push_back({0.10, 0.010, 0.25, 0.40, 1, "下侧骨材"});
  const auto rDn = keship::ComputeMidship(down);

  std::printf("        基准 z_NA=%.9f  骨材在上 z_NA=%.9f  骨材在下 z_NA=%.9f\n",
              r0.neutralAxisZM, rUp.neutralAxisZM, rDn.neutralAxisZM);
  Check(rUp.neutralAxisZM > r0.neutralAxisZM + 1e-9,
        "③ 骨材在板上方（zBase=0.5 向上）⇒ z_NA 上移");
  Check(rDn.neutralAxisZM < r0.neutralAxisZM - 1e-9,
        "③ 骨材在板下方（zBase=0.4 向上）⇒ z_NA 下移");
  Check(rUp.neutralAxisZM > r0.neutralAxisZM &&
            rDn.neutralAxisZM < r0.neutralAxisZM,
        "③ 偏移方向始终朝向骨材一侧");

  // 骨材形心必须落在 zBase + h/2（不是 zBase，也不是 zBase + h）——多道时面积还要乘 count
  keship::MidshipInput multi = base;
  multi.stiffeners.push_back({0.10, 0.010, 0.25, 0.50, 4, "四道骨材"});
  const auto rm = keship::ComputeMidship(multi);
  const double aStiff = 0.10 * 0.010 * 4.0;
  const double aTot = 1.5 * 0.01 + aStiff;
  const double zNaExp = (1.5 * 0.01 * 0.5 + aStiff * 0.55) / aTot;
  std::printf("        4 道骨材：z_NA=%.17g (解析 %.17g)\n", rm.neutralAxisZM, zNaExp);
  Check(RelNear(rm.neutralAxisZM, zNaExp, 1e-9),
        "③ 骨材形心取 zBase + h/2，面积乘 count（逐项手算对照）");
}

// ── ④ 剖面模数定义 ─────────────────────────────────────────────────────────
void TestSectionModulus()
{
  std::printf("\n--- ④ 剖面模数 W = I/(D−z_NA)、I/z_NA ---\n");

  const auto in = keship::MidshipFromMembers(DefaultMembers(), kB, kD, kHdb);
  const auto r = keship::ComputeMidship(in);
  Check(r.valid, "④ 默认中横剖面有效（valid==true）");

  const double wDeck = r.ixM4 / (kD - r.neutralAxisZM);
  const double wKeel = r.ixM4 / r.neutralAxisZM;
  std::printf("        A=%.6f m²  z_NA=%.6f m  I=%.6f m⁴  W_deck=%.6f  W_keel=%.6f\n",
              r.areaM2, r.neutralAxisZM, r.ixM4, r.sectionModulusDeckM3,
              r.sectionModulusKeelM3);
  Check(RelNear(r.sectionModulusDeckM3, wDeck, 1e-9),
        "④ W_deck == I/(D − z_NA)（相对误差 ≤1e-9）");
  Check(RelNear(r.sectionModulusKeelM3, wKeel, 1e-9),
        "④ W_keel == I/z_NA（相对误差 ≤1e-9）");
  Check(r.neutralAxisZM > 0.0 && r.neutralAxisZM < kD, "④ 0 < z_NA < D");
  Check(r.sectionModulusDeckM3 < r.sectionModulusKeelM3,
        "④ 甲板侧力臂更大 ⇒ W_deck < W_keel（甲板先达到许用应力）");

  // 逐构件面积之和必须等于总面积（构件表与结果表不能各算一套）
  double sumA = 0.0;
  for (double a : r.memberAreas) sumA += a;
  Check(RelNear(sumA, r.areaM2, 1e-12), "④ Σ memberAreas == areaM2");
  Check(r.members.size() == r.memberAreas.size(), "④ 构件表行数与面积表长度一致");
}

// ── ⑤ 总纵强度：σ = M/W、利用率、通过判据 ─────────────────────────────────
void TestStrength()
{
  std::printf("\n--- ⑤ 总纵强度 σ = M/W / 利用率 / 通过判据 ---\n");

  keship::StrengthInput s;
  s.sectionModulusDeckM3 = 0.02;   ///< 刻意取整数，便于手算对照
  s.sectionModulusKeelM3 = 0.03;
  s.stillWaterMomentKNm = 60.0;
  s.waveMomentKNm = 20.0;
  s.allowableStressMpa = 175.0;

  const auto r = keship::CheckLongitudinalStrength(s);
  const double mTotal = 80.0;
  const double sigDeck = mTotal * 1000.0 / s.sectionModulusDeckM3 / 1e6;  // 4 MPa
  const double sigKeel = mTotal * 1000.0 / s.sectionModulusKeelM3 / 1e6;

  std::printf("        M_total=%.6f kN·m  σ_deck=%.9f MPa  σ_keel=%.9f MPa  "
              "util=%.9f  passed=%d\n",
              r.totalMomentKNm, r.deckStressMpa, r.keelStressMpa, r.utilization,
              static_cast<int>(r.passed));
  Check(RelNear(r.totalMomentKNm, mTotal, 1e-12), "⑤ M_total = M_sw + M_w = 80 kN·m");
  Check(RelNear(r.deckStressMpa, sigDeck, 1e-9),
        "⑤ σ_deck = M/W（kN·m→N·m 后换算 MPa，相对误差 ≤1e-9）");
  Check(RelNear(r.keelStressMpa, sigKeel, 1e-9), "⑤ σ_keel = M/W（相对误差 ≤1e-9）");
  Check(RelNear(r.deckStressMpa, 4.0, 1e-12), "⑤ 手算值 σ_deck = 4.0 MPa");
  Check(RelNear(r.utilization, std::max(sigDeck, sigKeel) / 175.0, 1e-12),
        "⑤ utilization = σ_max/σ_allow");
  Check(r.passed == (r.utilization <= 1.0), "⑤ passed == (utilization ≤ 1)");

  // 边界：σ_max 恰等于许用应力 ⇒ 利用率恰为 1 ⇒ 必须判通过（≤ 不是 <）
  keship::StrengthInput edge = s;
  edge.allowableStressMpa = 4.0;
  const auto re = keship::CheckLongitudinalStrength(edge);
  Check(RelNear(re.utilization, 1.0, 1e-12), "⑤ σ_max == σ_allow 时 utilization == 1");
  Check(re.passed, "⑤ utilization == 1 判为通过（判据是 ≤1）");

  keship::StrengthInput over = s;
  over.allowableStressMpa = 3.9;
  const auto ro = keship::CheckLongitudinalStrength(over);
  Check(!ro.passed, "⑤ utilization > 1 判为不通过");

  // 许用应力为 0 不能"假通过"
  keship::StrengthInput zero = s;
  zero.allowableStressMpa = 0.0;
  const auto rz = keship::CheckLongitudinalStrength(zero);
  Check(!rz.passed, "⑤ 许用应力为 0 时不得判为通过（防空判据）");

  // 波浪弯矩估算式：M_w = 0.19·C·L²·B·Cb
  const double mw = keship::EstimateWaveMomentKNm(6.0, 1.5, 0.6, 1.0);
  std::printf("        M_w(L=6,B=1.5,Cb=0.6,C=1)=%.6f kN·m\n", mw);
  Check(RelNear(mw, 0.19 * 36.0 * 1.5 * 0.6, 1e-12), "⑤ 波浪弯矩估算式与 0.19·C·L²·B·Cb 一致");
  Check(mw > 0.0 && keship::WaveMomentFormulaText().find("0.19") != std::string::npos,
        "⑤ 公式文本里写明系数 0.19（交付物 params.formula 与代码同源）");
}

// ── ⑥ 链级：SD-01 结构要素 → 中横剖面 → W；板厚 +20% ⇒ W 变大 ───────────────
double WOf(const std::vector<keship::StructureMember> &_m)
{
  const auto in = keship::MidshipFromMembers(_m, kB, kD, kHdb);
  return keship::ComputeMidship(in).sectionModulusDeckM3;
}

void TestChainSensitivity()
{
  std::printf("\n--- ⑥ 链级：SD-01 结构要素 → SD-03 中横剖面 → W / 板厚敏感性 ---\n");

  const auto members = DefaultMembers();
  const auto in0 = keship::MidshipFromMembers(members, kB, kD, kHdb);
  const auto r0 = keship::ComputeMidship(in0);
  std::printf("        默认表：板 %zu 块 + 腹板/骨材 %zu 道，A=%.6f m²，z_NA=%.6f m，"
              "W_deck=%.6f m³\n",
              in0.plates.size(), in0.stiffeners.size(), r0.areaM2, r0.neutralAxisZM,
              r0.sectionModulusDeckM3);
  Check(r0.valid && r0.sectionModulusDeckM3 > 0.0, "⑥ SD-01 要素表可构造出有效中横剖面");

  // 板面积口径必须与 SD-01 交付物 metrics.totalPlateAreaM2 同源可对账。
  // 口径 = "板 + 等效腹板"，**不含骨材**；这里按默认表逐项手算：
  //   船底板 B·t = 1.5×0.012   = 0.0180
  //   甲板   B·t = 1.5×0.010   = 0.0150
  //   内底板 B·t = 1.5×0.010   = 0.0150
  //   舷侧板 2·D·t = 2×1.0×0.010 = 0.0200
  //   横舱壁 1·D·t = 1.0×0.008 = 0.0080
  //   纵桁   1·D·t = 1.0×0.009 = 0.0090
  const double plateArea = keship::TotalPlateAreaM2(members, kB, kD, kHdb);
  const double plateHand = kB * 0.012 + kB * 0.010 + kB * 0.010 + 2.0 * kD * 0.010 +
                           kD * 0.008 + kD * 0.009;
  std::printf("        板面积：TotalPlateAreaM2=%.9f m²，手算=%.9f m²\n", plateArea,
              plateHand);
  Check(RelNear(plateArea, plateHand, 1e-12),
        "⑥ totalPlateAreaM2 = 板 + 等效腹板（不含骨材），与逐项手算一致");

  // 板厚 +20%
  auto thick = members;
  for (auto &m : thick) {
    m.thicknessM *= 1.2;
    m.stiffenerWebThicknessM *= 1.2;
  }
  const auto rt = keship::ComputeMidship(keship::MidshipFromMembers(thick, kB, kD, kHdb));
  const double dW = (rt.sectionModulusDeckM3 - r0.sectionModulusDeckM3) /
                    r0.sectionModulusDeckM3 * 100.0;
  std::printf("        板厚 ×1.2：A=%.6f→%.6f m²，z_NA=%.6f→%.6f m，"
              "W_deck=%.6f→%.6f m³（%+.3f%%）\n",
              r0.areaM2, rt.areaM2, r0.neutralAxisZM, rt.neutralAxisZM,
              r0.sectionModulusDeckM3, rt.sectionModulusDeckM3, dW);
  Check(rt.sectionModulusDeckM3 > r0.sectionModulusDeckM3,
        "⑥ 上游板厚 +20% ⇒ 下游 W_deck 变大（链级敏感性非零）");
  Check(std::abs(dW) > 1.0, "⑥ W 变化幅度 >1%（不是数值噪声）");

  // 骨材间距减小（加密）⇒ W 也应变大：证明 SD-01 的"骨材间距"真的进了下游计算
  auto denser = members;
  for (auto &m : denser) {
    if (m.stiffenerSpacingM > 0.0) m.stiffenerSpacingM *= 0.8;
  }
  const double wDense = WOf(denser);
  std::printf("        骨材间距 ×0.8：W_deck=%.6f→%.6f m³（%+.3f%%）\n",
              r0.sectionModulusDeckM3, wDense,
              (wDense - r0.sectionModulusDeckM3) / r0.sectionModulusDeckM3 * 100.0);
  Check(wDense > r0.sectionModulusDeckM3,
        "⑥ 骨材间距 ×0.8 ⇒ 道数增加 ⇒ W_deck 变大（骨材尺寸也真实进入下游）");
}

#if defined(KESHIP_CHAIN_QT)
// ── ⑥(文件级) + ⑦：真读写 sd01/sd03/sd09 交付物、串号负例 ────────────────────
QJsonArray MembersToJson(const std::vector<keship::StructureMember> &_m)
{
  QJsonArray arr;
  for (const auto &m : _m) {
    QJsonObject o;
    o["name"] = QString::fromStdString(m.name);
    o["category"] = QString::fromStdString(m.category);
    o["thicknessM"] = m.thicknessM;
    o["stiffenerSpacingM"] = m.stiffenerSpacingM;
    o["stiffenerWebHeightM"] = m.stiffenerWebHeightM;
    o["stiffenerWebThicknessM"] = m.stiffenerWebThicknessM;
    arr.append(o);
  }
  return arr;
}

std::vector<keship::StructureMember> MembersFromJson(const QJsonArray &_arr)
{
  std::vector<keship::StructureMember> out;
  for (const auto &v : _arr) {
    const QJsonObject o = v.toObject();
    keship::StructureMember m;
    m.name = o["name"].toString().toStdString();
    m.category = o["category"].toString().toStdString();
    m.thicknessM = o["thicknessM"].toDouble();
    m.stiffenerSpacingM = o["stiffenerSpacingM"].toDouble();
    m.stiffenerWebHeightM = o["stiffenerWebHeightM"].toDouble();
    m.stiffenerWebThicknessM = o["stiffenerWebThicknessM"].toDouble();
    out.push_back(m);
  }
  return out;
}

void TestDeliverableChain()
{
  std::printf("\n--- ⑥(文件级) SD-01 → sd01_structure.json → SD-03 读入 → W ---\n");

  const auto members = DefaultMembers();

  // —— SD-01 导出（与 StructureCognitionDialog::onExport 同字段）——
  keship::ExpDeliverable d01;
  d01.expId = "SD-01";
  d01.createdAt = keship::NowIso8601();
  d01.object["type"] = "structure_cognition";
  d01.object["lengthM"] = 6.0;
  d01.object["breadthM"] = kB;
  d01.object["depthM"] = kD;
  d01.object["doubleBottomHeightM"] = kHdb;
  d01.metrics["memberCount"] = static_cast<int>(members.size());
  d01.metrics["totalPlateAreaM2"] = keship::TotalPlateAreaM2(members, kB, kD, kHdb);
  d01.metrics["scoredQuestions"] = 5;
  d01.metrics["score"] = 4;
  d01.extra["members"] = MembersToJson(members);

  const std::string path = "/tmp/keship_sd01_structure_test.json";
  std::string err;
  Check(keship::WriteExpDeliverable(path, d01, &err),
        "⑥ 写出 sd01_structure.json（" + path + "）");

  // —— SD-03 读入（与 MidshipSectionDialog::onImportSd01 同路径）——
  keship::ExpDeliverable rd;
  Check(keship::ReadExpDeliverable(path, rd, &err), "⑥ 读回 sd01_structure.json");
  Check(keship::ValidExpDeliverable(rd, "SD-01", &err),
        "⑥ ValidExpDeliverable(d,\"SD-01\") 通过");
  Check(rd.metrics["memberCount"].toInt() == static_cast<int>(members.size()),
        "⑥ 交付物 memberCount 与导出时一致");

  const auto back = MembersFromJson(rd.extra["members"].toArray());
  Check(back.size() == members.size(), "⑥ extra.members[] 行数往返一致");
  bool sameFields = (back.size() == members.size());
  for (size_t i = 0; sameFields && i < back.size(); ++i) {
    sameFields = back[i].name == members[i].name && back[i].category == members[i].category &&
                 RelNear(back[i].thicknessM, members[i].thicknessM, 1e-15) &&
                 RelNear(back[i].stiffenerSpacingM, members[i].stiffenerSpacingM, 1e-15) &&
                 RelNear(back[i].stiffenerWebHeightM, members[i].stiffenerWebHeightM, 1e-15) &&
                 RelNear(back[i].stiffenerWebThicknessM, members[i].stiffenerWebThicknessM,
                         1e-15);
  }
  Check(sameFields,
        "⑥ extra.members[] 六个字段（name/category/板厚/间距/腹板高/腹板厚）逐字段往返一致");

  // —— SD-03 由读入的 members 复原截面（不是用内存里的表）——
  const auto inR = keship::MidshipFromMembers(back, kB, kD, kHdb);
  const auto rR = keship::ComputeMidship(inR);
  const double wDirect = WOf(members);
  Check(rR.valid, "⑥ 由读入文件复原的截面有效");
  Check(RelNear(rR.sectionModulusDeckM3, wDirect, 1e-15),
        "⑥ 文件往返后的 W_deck 与直接计算逐位相同");

  // SD-01 的板面积口径 vs 本页由同一 members 复算（"上游值 vs 本页复现值"）
  const double areaUp = rd.metrics["totalPlateAreaM2"].toDouble();
  const double areaLocal = keship::TotalPlateAreaM2(back, kB, kD, kHdb);
  std::printf("        上游 totalPlateAreaM2=%.9f → 本页复算=%.9f（偏差 %.3e）\n", areaUp,
              areaLocal, std::abs(areaUp - areaLocal) / std::max(areaUp, 1e-30));
  Check(RelNear(areaUp, areaLocal, 1e-12), "⑥ SD-01 板面积在本页可复现（相对偏差 ≤1e-12）");

  // —— SD-03 导出 sd03_midship.json ——
  keship::ExpDeliverable d03;
  d03.expId = "SD-03";
  d03.createdAt = keship::NowIso8601();
  d03.object["lengthM"] = rd.object["lengthM"].toDouble();
  d03.object["breadthM"] = kB;
  d03.object["depthM"] = kD;
  d03.object["doubleBottomHeightM"] = kHdb;
  d03.metrics["sectionAreaM2"] = rR.areaM2;
  d03.metrics["neutralAxisZM"] = rR.neutralAxisZM;
  d03.metrics["ixM4"] = rR.ixM4;
  d03.metrics["sectionModulusDeckM3"] = rR.sectionModulusDeckM3;
  d03.metrics["sectionModulusKeelM3"] = rR.sectionModulusKeelM3;

  const std::string p03 = "/tmp/keship_sd03_midship_test.json";
  Check(keship::WriteExpDeliverable(p03, d03, &err), "⑥ 写出 sd03_midship.json");

  // —— SD-09 读入并校核 ——
  keship::ExpDeliverable r03;
  Check(keship::ReadExpDeliverable(p03, r03, &err) &&
            keship::ValidExpDeliverable(r03, "SD-03", &err),
        "⑥ SD-09 读入 sd03_midship.json 并通过 \"SD-03\" 校验");

  const double L = r03.object["lengthM"].toDouble();
  const double B = r03.object["breadthM"].toDouble();
  keship::StrengthInput si;
  si.sectionModulusDeckM3 = r03.metrics["sectionModulusDeckM3"].toDouble();
  si.sectionModulusKeelM3 = r03.metrics["sectionModulusKeelM3"].toDouble();
  si.stillWaterMomentKNm = 60.0;
  si.waveMomentKNm = keship::EstimateWaveMomentKNm(L, B, 0.6, 1.0);
  si.allowableStressMpa = 175.0;
  const auto sr = keship::CheckLongitudinalStrength(si);
  std::printf("        SD-09：W_deck=%.6f m³，M_w=%.4f kN·m，σ_deck=%.4f MPa，"
              "util=%.5f%%，passed=%d\n",
              si.sectionModulusDeckM3, si.waveMomentKNm, sr.deckStressMpa,
              sr.utilization * 100.0, static_cast<int>(sr.passed));
  Check(RelNear(sr.deckStressMpa, sr.totalMomentKNm / (1000.0 * si.sectionModulusDeckM3),
                1e-9),
        "⑥ SD-09 应力由 SD-03 交付的 W 算出（全链贯通）");
}

void TestCrossIdRejected()
{
  std::printf("\n--- ⑦ 负例：串号交付物必须被拒绝 ---\n");

  keship::ExpDeliverable d03;
  d03.expId = "SD-03";
  /// object 段不能为空：ValidExpDeliverable 要求 object / metrics 两段都有内容
  /// （空 object 会被判为"文件残缺"，所以真实导出的 SD-03 一定带船体主尺度）
  d03.object["type"] = "midship_section";
  d03.object["breadthM"] = kB;
  d03.object["depthM"] = kD;
  d03.metrics["sectionModulusDeckM3"] = 0.024;
  const std::string p = "/tmp/keship_sd03_crossid_test.json";
  std::string err;
  Check(keship::WriteExpDeliverable(p, d03, &err), "⑦ 写出一份 SD-03 交付物");

  keship::ExpDeliverable rd;
  Check(keship::ReadExpDeliverable(p, rd, &err), "⑦ 读回该交付物");
  std::string err2;
  const bool ok = keship::ValidExpDeliverable(rd, "SD-09", &err2);
  std::printf("        ValidExpDeliverable(d,\"SD-09\") => %s（%s）\n", ok ? "true" : "false",
              err2.c_str());
  Check(!ok, "⑦ SD-03 交付物拿 \"SD-09\" 校验必须失败（串号防御）");
  Check(!err2.empty(), "⑦ 拒绝时给出可读原因");
  Check(keship::ValidExpDeliverable(rd, "SD-03", &err2), "⑦ 同一文件用 \"SD-03\" 校验通过（对照）");

  // 反向：SD-01 交付物也不能被当成 SD-03 用
  keship::ExpDeliverable d01;
  d01.expId = "SD-01";
  Check(!keship::ValidExpDeliverable(d01, "SD-03", &err2),
        "⑦ SD-01 交付物拿 \"SD-03\" 校验必须失败");
  std::remove(p.c_str());
}
#endif  // KESHIP_CHAIN_QT

}  // namespace

int main()
{
  std::printf("=== L3 结构设计链内核测试（SD-01 → SD-03 → SD-09）===\n");
#if defined(KESHIP_CHAIN_QT)
  std::printf("模式：**带 Qt**（含交付物文件级链测试 + 串号负例）\n");
#else
  std::printf("模式：**不带 Qt**（内核独立编译；文件级交付物测试需 -DKESHIP_CHAIN_QT）\n");
#endif

  TestSinglePlate();
  TestSymmetricPair();
  TestAsymmetricStiffener();
  TestSectionModulus();
  TestStrength();
  TestChainSensitivity();

#if defined(KESHIP_CHAIN_QT)
  TestDeliverableChain();
  TestCrossIdRejected();
#else
  std::printf("\n");
  Skip("⑥ 文件级 sd01_structure.json 往返（需 Qt：-DKESHIP_CHAIN_QT 并链 ExpDeliverable.cc）");
  Skip("⑦ 串号负例 ValidExpDeliverable(d,\"SD-09\")（需 Qt 同上）");
#endif

  std::printf("\n=== 汇总：%s（失败 %d，跳过 %d）===\n", gFail == 0 ? "全部通过" : "存在失败",
              gFail, gSkip);
  return gFail == 0 ? 0 : 1;
}
