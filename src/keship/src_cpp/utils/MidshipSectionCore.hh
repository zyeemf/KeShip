/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L3 结构设计链 —— 中横剖面组合截面特性 + 总纵强度校核内核
 *                        （纯计算，不依赖 Qt / gz，可独立 g++ 编译）
 *
 * 链位置（判据 B：后一级必须真读前一级的产出）：
 *   **SD-01 船体结构认知** ──sd01_structure.json──▶ **SD-03 横剖面结构**
 *   ──sd03_midship.json──▶ **SD-09 综合结构设计**
 *
 * 为什么单独抽内核：
 *   SD-01 的示意剖面、SD-03 的构件表/中和轴/W、SD-09 的应力、本模块的单元测试
 *   五处用的是**同一套几何**。若各写一份 Σ A·z 与 Σ[I+A d²]，任何一处笔误都会
 *   在界面上看不出来（画面照样"像条船"），但下游算出的剖面模数会悄悄不同。
 *   故这里只放纯函数：输入结构要素，输出面积/中和轴/惯性矩/剖面模数/应力，
 *   调用方（对话框、测试）只做展示与序列化，不重复任何公式。
 *
 * 坐标与符号约定（与 StabilityGzCore / DamageStabilityCore 全模块一致）：
 *   z 从**龙骨基线(0) 向上**到型深 D；本模块只做**垂向弯曲**（总纵弯曲）。
 *
 * 构件抽象（两种矩形，均可精确表示）：
 *   ① PlateSegment —— **水平板**：宽 widthM（型宽方向）× 厚 thicknessM，
 *      形心高 zCenterM。自身惯性矩 I = b·h³/12 = widthM·thicknessM³/12。
 *   ② Stiffener —— **竖直腹板**：从 zBaseM 沿 **+z** 伸展 webHeightM，
 *      厚度方向尺寸 webThicknessM，共 count 道。
 *      A = h·t·count，形心 z = zBaseM + h/2，I = count·t·h³/12。
 *      ⇒ 骨材、舷侧板（一道高 D、厚 t、count=2 的腹板）、横舱壁（count=1）、
 *        中桁材（count=1）都能**精确**落到这个抽象里，不需要再为竖直板
 *        另立一种类型，也就不存在"竖直板漏算自身惯性矩 t·D³/12"这类常见错误。
 *
 * 组合截面公式（教材式，无任何经验修正）：
 *   A        = Σ A_i
 *   z_NA     = Σ(A_i·z_i) / A
 *   I        = Σ[ I_i + A_i·(z_i − z_NA)² ]         （平行轴定理）
 *   W_deck   = I / (z_deck − z_NA)，z_deck = 型深 D（甲板边线）
 *   W_keel   = I / z_NA
 *
 * 总纵强度：
 *   M_total = M_sw（静水弯矩）+ M_w（波浪弯矩）
 *   σ_deck = M_total / W_deck，σ_keel = M_total / W_keel
 *   利用率   = max(σ_deck, σ_keel) / σ_allow
 *   通过     = (利用率 ≤ 1)
 */

#ifndef KESHIP_MIDSHIP_SECTION_CORE_HH_
#define KESHIP_MIDSHIP_SECTION_CORE_HH_

#include <string>
#include <vector>

namespace keship {

/// 水平板构件（甲板 / 船底板 / 内底板）：宽 × 厚，形心高 zCenterM
struct PlateSegment {
  double widthM = 0.0;      ///< 水平宽度 b (m)
  double thicknessM = 0.0;  ///< 板厚 h (m)
  double zCenterM = 0.0;    ///< 形心高度（距龙骨）(m)
  std::string name;
};

/// 竖直腹板构件（骨材腹板 / 舷侧板 / 横舱壁 / 纵桁）：自 zBaseM 沿 +z 伸展
struct Stiffener {
  double webHeightM = 0.0;     ///< 腹板高 h (m)
  double webThicknessM = 0.0;  ///< 腹板厚 t (m)
  double spacingM = 0.0;       ///< 骨材间距 (m)；仅用于"由间距反算道数"，count>0 时不参与计算
  double zBaseM = 0.0;         ///< 腹板下端（与板相接端）高度 (m)，向 +z 伸展
  int count = 0;               ///< 道数；< 1 时按 1 道计
  std::string name;
};

/// 中横剖面输入
struct MidshipInput {
  double breadthM = 0.0;   ///< 型宽 B（仅用于校核与展示，不进入 I 的计算）
  double depthM = 0.0;     ///< 型深 D（= z_deck，剖面模数取到此高度）
  std::vector<PlateSegment> plates;
  std::vector<Stiffener> stiffeners;
};

/// 单个构件在截面中的几何量（供 SD-03 构件表逐行显示；与内核同一份算法）
struct MidshipMember {
  std::string name;
  double areaM2 = 0.0;     ///< A_i
  double zCenterM = 0.0;   ///< z_i
  double iOwnM4 = 0.0;     ///< 对自身形心水平轴的惯性矩 I_i
};

/// 组合截面计算结果
struct MidshipResult {
  double areaM2 = 0.0;              ///< 总面积
  double neutralAxisZM = 0.0;       ///< 中和轴高度（距龙骨）
  double ixM4 = 0.0;                ///< 对中和轴的水平惯性矩
  double sectionModulusDeckM3 = 0.0;///< W_deck = I/(D − z_NA)
  double sectionModulusKeelM3 = 0.0;///< W_keel = I/z_NA
  std::vector<double> memberAreas;  ///< 逐构件面积（顺序 = plates 后接 stiffeners）
  /// ★ 附加字段（不改变上面 6 项）：逐构件几何，供 SD-03 构件表直接显示，
  ///   避免对话框为了列表而把 ΣA·z 与 I=b·h³/12 再实现一遍（那是漂移的温床）。
  std::vector<MidshipMember> members;
  bool valid = false;
  std::string note;  ///< valid=false 时的可读原因
};

/// 组合截面特性计算（公式见文件头）
MidshipResult ComputeMidship(const MidshipInput &_in);

/// 总纵强度校核输入
struct StrengthInput {
  double sectionModulusDeckM3 = 0.0;  ///< W_deck (m³)
  double sectionModulusKeelM3 = 0.0;  ///< W_keel (m³)
  double stillWaterMomentKNm = 0.0;   ///< 静水弯矩 M_sw (kN·m)
  double waveMomentKNm = 0.0;         ///< 波浪弯矩 M_w (kN·m)
  double allowableStressMpa = 175.0;  ///< 许用应力 (MPa)，教学取值见 .cc 注释
};

/// 总纵强度校核结果
struct StrengthResult {
  double totalMomentKNm = 0.0;
  double deckStressMpa = 0.0;
  double keelStressMpa = 0.0;
  double utilization = 0.0;  ///< max(σ_deck, σ_keel)/σ_allow
  bool passed = false;
};

/// 总纵弯曲应力校核：σ = M/W（kN·m → N·m 后除 m³，再换算 MPa）
StrengthResult CheckLongitudinalStrength(const StrengthInput &_in);

/// 教学用波浪弯矩估算：M_w = 0.19·C·L²·B·Cb（kN·m），C 默认 1.0。
/// 形式与 IACS URS11 的 M_w = ±0.19·C·L²·B·Cb 相同，但**规范中的 C 随 L 查表**，
/// 这里固定 C=1.0 ⇒ 仅作教学量级估算，**不是规范值**（详见 .cc 内注释）。
double EstimateWaveMomentKNm(double _lengthM, double _breadthM, double _cb,
                             double _c = 1.0);

/// 波浪弯矩公式的可读文本（写进交付物 params.formula，保证报告与代码同源）
std::string WaveMomentFormulaText();

//=============================================================================
// SD-01 → SD-03 的结构要素交接格式
//
// 字段与 `sd01_structure.json` 的 `extra.members[]` **逐字对应**（名称/类别/板厚/
// 骨材间距/骨材腹板高/骨材腹板厚）。SD-01 的示意剖面、SD-03 的导入、链级测试
// 三者都走这一个 struct + 一个转换函数，字段就不会在序列化边界上悄悄漂移。
//=============================================================================
struct StructureMember {
  std::string name;
  std::string category;            ///< bottom / deck / side / bulkhead / girder / stiffener / doublebottom
  double thicknessM = 0.0;
  double stiffenerSpacingM = 0.0;
  double stiffenerWebHeightM = 0.0;
  double stiffenerWebThicknessM = 0.0;
};

/// 结构要素表 → 中横剖面输入（教学简化规则逐条写在 .cc 内）
/// 说明：骨材道数由 `构件跨度 / 骨材间距` 四舍五入得到（≥1 道），
///       故上游改"骨材间距"会真实改变下游的剖面模数（链级敏感性非零）。
MidshipInput MidshipFromMembers(const std::vector<StructureMember> &_members,
                               double _breadthM, double _depthM,
                               double _doubleBottomHeightM);

/// 构件的板面积（只计"板"，不计骨材腹板）：SD-01 交付物 metrics.totalPlateAreaM2
/// 与 SD-03 导入后的复现值都用它，保证两者口径一致可对账。
double TotalPlateAreaM2(const std::vector<StructureMember> &_members,
                        double _breadthM, double _depthM,
                        double _doubleBottomHeightM);

}  // namespace keship

#endif  // KESHIP_MIDSHIP_SECTION_CORE_HH_
