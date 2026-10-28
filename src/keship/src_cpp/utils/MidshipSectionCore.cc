/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L3 结构设计链 —— 中横剖面组合截面特性 / 总纵强度 内核实现（纯函数，无状态）
 *
 * 本文件不含任何 Qt / gz 头文件：`g++ -std=c++17 -I utils test/structure_chain_test.cc
 * utils/MidshipSectionCore.cc` 即可独立编译运行（见 test/structure_chain_test.cc）。
 */

#include "MidshipSectionCore.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>

namespace keship {

namespace {

/// 由"跨度 / 间距"折算道数。间距 ≤ 0 或跨度 ≤ 0 时按 1 道计；
/// 四舍五入（而非向上取整）是为了让"1.5 m 板宽 / 0.25 m 间距 = 6 道"这种
/// 教科书数字原样复现，避免多出一道让 W 与手算对不上。
int CountBySpacing(double _spanM, double _spacingM)
{
  if (_spanM <= 1.0e-9 || _spacingM <= 1.0e-9) return 1;
  const long n = std::lround(_spanM / _spacingM);
  return static_cast<int>(std::max(1L, n));
}

/// 类别归一化：既接受交付物里的英文键（bottom/deck/…），也接受界面上的中文名，
/// 保证学生把类别列改成"甲板"时不会整行被静默丢弃。
std::string NormalizeCategory(const std::string &_raw)
{
  std::string s;
  s.reserve(_raw.size());
  for (char c : _raw) s.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

  if (s == "bottom" || s == "shell" || s == "bottom_shell" || s == "船底板" ||
      s == "外板" || s == "船底外板") return "bottom";
  if (s == "deck" || s == "甲板") return "deck";
  if (s == "side" || s == "side_shell" || s == "舷侧板" || s == "舷侧外板") return "side";
  if (s == "bulkhead" || s == "横舱壁") return "bulkhead";
  if (s == "girder" || s == "纵桁" || s == "中桁材") return "girder";
  if (s == "stiffener" || s == "骨材" || s == "纵骨") return "stiffener";
  if (s == "doublebottom" || s == "double_bottom" || s == "innerbottom" ||
      s == "inner_bottom" || s == "双层底" || s == "内底板") return "doublebottom";
  return s;  ///< 未知类别：原样返回，由 Resolve() 忽略（不静默当板用）
}

/// SD-01 结构要素 → 中横剖面（同时给出"板面积"口径，供交付物对账）
struct Resolved {
  MidshipInput in;
  double plateArea = 0.0;
};

/// 教学简化映射规则（逐条对应 SD-01 表里的 7 类结构要素）：
///   bottom       → 水平板 B×t，形心 z=t/2（船底板中面）；骨材自 z=t 向上（船底纵骨）
///   deck         → 水平板 B×t，形心 z=D−t/2；骨材自 z=D−t−h 向上顶到甲板（甲板纵骨）
///   doublebottom → 水平板 B×t，形心 z=h_db（内底板中面）；骨材自 h_db 向上（内底纵骨）
///   side         → 等效腹板 h=D、t=t、count=2（左右两舷合并成一"块"精确等效：
///                  2 × t·D³/12 与 2 × D·t 的 A·z² 完全等价于两道独立竖板）
///                  舷侧纵骨按"沿高度均布、形心取中高"处理：count=2·round(D/s)，zBase=D/2−h/2
///   bulkhead     → 等效腹板 h=D、t=t、count=1；扶强材同舷侧纵骨（count=round(D/s)）
///   girder       → 等效腹板 h=D、t=t、count=1（中桁材，沿全深）
///   stiffener    → 无板，只贡献骨材；附着于内底板顶面（zBase=h_db），即"内底纵骨"行
/// 说明：骨材一律"自 zBase 沿 +z 生长"，故甲板纵骨用 zBase=D−t−h（向上顶到甲板）。
Resolved Resolve(const std::vector<StructureMember> &_members, double _B, double _D,
                 double _hdb)
{
  Resolved r;
  r.in.breadthM = _B;
  r.in.depthM = _D;
  if (_B <= 0.0 || _D <= 0.0) return r;

  const double hdb = std::max(0.0, std::min(_hdb, _D));  ///< 双层底高不得超过型深

  for (const auto &m : _members) {
    const std::string cat = NormalizeCategory(m.category);
    const double t = m.thicknessM;
    const double s = m.stiffenerSpacingM;
    const double h = m.stiffenerWebHeightM;
    const double tw = m.stiffenerWebThicknessM;
    const bool hasStiffener = (h > 1.0e-9 && tw > 1.0e-9);

    auto addPlate = [&](double _width, double _z) {
      if (_width <= 1.0e-9 || t <= 1.0e-9) return;
      PlateSegment p;
      p.widthM = _width;
      p.thicknessM = t;
      p.zCenterM = _z;
      p.name = m.name;
      r.in.plates.push_back(p);
      r.plateArea += _width * t;
    };
    auto addWeb = [&](int _count) {
      if (t <= 1.0e-9) return;
      Stiffener st;
      st.webHeightM = _D;
      st.webThicknessM = t;
      st.spacingM = 0.0;
      st.zBaseM = 0.0;
      st.count = _count;
      st.name = m.name;
      r.in.stiffeners.push_back(st);
      r.plateArea += static_cast<double>(_count) * _D * t;
    };
    auto addStiffener = [&](int _count, double _zBase) {
      if (!hasStiffener || _count <= 0) return;
      Stiffener st;
      st.webHeightM = h;
      st.webThicknessM = tw;
      st.spacingM = s;
      st.zBaseM = _zBase;
      st.count = _count;
      st.name = m.name + " 骨材";
      r.in.stiffeners.push_back(st);
    };

    if (cat == "bottom") {
      addPlate(_B, 0.5 * t);
      addStiffener(CountBySpacing(_B, s), t);
    } else if (cat == "deck") {
      addPlate(_B, _D - 0.5 * t);
      addStiffener(CountBySpacing(_B, s), _D - t - h);
    } else if (cat == "doublebottom") {
      addPlate(_B, hdb);
      addStiffener(CountBySpacing(_B, s), hdb);
    } else if (cat == "side") {
      addWeb(2);
      addStiffener(2 * CountBySpacing(_D, s), 0.5 * _D - 0.5 * h);
    } else if (cat == "bulkhead") {
      addWeb(1);
      addStiffener(CountBySpacing(_D, s), 0.5 * _D - 0.5 * h);
    } else if (cat == "girder") {
      addWeb(1);
    } else if (cat == "stiffener") {
      addStiffener(CountBySpacing(_B, s), hdb);
    }
    // 其他类别：忽略（在 NormalizeCategory 处已注释，不静默当板用）
  }
  return r;
}

}  // namespace

//-----------------------------------------------------------------------------
MidshipResult ComputeMidship(const MidshipInput &_in)
{
  MidshipResult r;

  struct Geo {
    std::string name;
    double area, z, iOwn;
  };
  std::vector<Geo> g;
  g.reserve(_in.plates.size() + _in.stiffeners.size());

  for (const auto &p : _in.plates) {
    if (p.widthM <= 0.0 || p.thicknessM <= 0.0) continue;
    const double h = p.thicknessM;
    g.push_back({p.name, p.widthM * h, p.zCenterM, p.widthM * h * h * h / 12.0});
  }
  for (const auto &s : _in.stiffeners) {
    if (s.webHeightM <= 0.0 || s.webThicknessM <= 0.0) continue;
    const int n = (s.count < 1) ? 1 : s.count;
    const double h = s.webHeightM, t = s.webThicknessM;
    g.push_back({s.name, h * t * static_cast<double>(n),
                 s.zBaseM + 0.5 * h,
                 static_cast<double>(n) * t * h * h * h / 12.0});
  }

  double area = 0.0, firstMoment = 0.0;
  for (const auto &e : g) {
    area += e.area;
    firstMoment += e.area * e.z;
    r.memberAreas.push_back(e.area);
    MidshipMember mm;
    mm.name = e.name;
    mm.areaM2 = e.area;
    mm.zCenterM = e.z;
    mm.iOwnM4 = e.iOwn;
    r.members.push_back(mm);
  }

  if (area <= 1.0e-12) {
    r.note = "截面总面积为 0：没有可计入的板或腹板（检查板厚/骨材尺寸是否 > 0）";
    return r;
  }
  const double zNa = firstMoment / area;

  double ix = 0.0;
  for (const auto &e : g) {
    const double d = e.z - zNa;
    ix += e.iOwn + e.area * d * d;
  }

  r.areaM2 = area;
  r.neutralAxisZM = zNa;
  r.ixM4 = ix;

  if (zNa <= 1.0e-9) {
    r.note = "中和轴落在龙骨处（z_NA=0）：W_keel = I/z_NA 发散，剖面无物理意义";
    return r;
  }
  if (_in.depthM - zNa <= 1.0e-9) {
    r.note = "中和轴已到/超过型深（z_NA ≥ D）：W_deck = I/(D−z_NA) 发散";
    return r;
  }
  if (ix <= 0.0) {
    r.note = "惯性矩非正：输入几何异常";
    return r;
  }

  r.sectionModulusKeelM3 = ix / zNa;
  r.sectionModulusDeckM3 = ix / (_in.depthM - zNa);
  r.valid = true;
  return r;
}

//-----------------------------------------------------------------------------
StrengthResult CheckLongitudinalStrength(const StrengthInput &_in)
{
  StrengthResult r;
  r.totalMomentKNm = _in.stillWaterMomentKNm + _in.waveMomentKNm;

  /// 单位换算：M[kN·m] × 1000 = N·m；σ[Pa] = M[N·m]/W[m³]；σ[MPa] = σ[Pa]/1e6
  /// ⇒ σ[MPa] = M[kN·m] / (1000 · W[m³])。这一行是整个强度链的唯一换算处，
  ///    测试 ⑤ 用同一式子反查，禁止在对话框里再写一遍（少一个 1000 就会差三个量级）。
  const double mN = r.totalMomentKNm * 1000.0;  ///< N·m

  if (_in.sectionModulusDeckM3 > 1.0e-12)
    r.deckStressMpa = mN / _in.sectionModulusDeckM3 / 1.0e6;
  if (_in.sectionModulusKeelM3 > 1.0e-12)
    r.keelStressMpa = mN / _in.sectionModulusKeelM3 / 1.0e6;

  const double sMax = std::max(std::abs(r.deckStressMpa), std::abs(r.keelStressMpa));
  if (_in.allowableStressMpa <= 1.0e-12) {
    /// 许用应力未给出（0）时不能"利用率=0 即通过"——那是假通过。
    r.utilization = std::numeric_limits<double>::infinity();
    r.passed = false;
    return r;
  }
  r.utilization = sMax / _in.allowableStressMpa;
  r.passed = (r.utilization <= 1.0);
  return r;
}

//-----------------------------------------------------------------------------
double EstimateWaveMomentKNm(double _lengthM, double _breadthM, double _cb, double _c)
{
  /// 出处：IACS 统一要求 URS11（总纵强度）给出的垂向波浪弯矩形式
  ///       M_w = ±0.19·C·L²·B·Cb  (kN·m)
  /// 其中规范 C 是**按船长 L 查表**的波浪系数（L<90 m 段 C = 0.0792L 等）。
  /// 本平台教学船为 6 m 量级、且课程只要求"弯矩随尺度的量级关系"，
  /// 故这里**固定 C = 1.0**：既保留 L²·B·Cb 的尺度律，又不去伪造一张规范表。
  /// ⚠ 这是**教学简化值，不是规范值**；交付物 params.formula 里逐字写明。
  return 0.19 * _c * _lengthM * _lengthM * _breadthM * _cb;
}

std::string WaveMomentFormulaText()
{
  return "Mw = 0.19*C*L^2*B*Cb (kN*m), C=1.0 fixed (IACS URS11 form; "
         "regulation C is table-lookup by L => teaching estimate, NOT a rule value)";
}

//-----------------------------------------------------------------------------
MidshipInput MidshipFromMembers(const std::vector<StructureMember> &_members,
                                double _breadthM, double _depthM,
                                double _doubleBottomHeightM)
{
  return Resolve(_members, _breadthM, _depthM, _doubleBottomHeightM).in;
}

double TotalPlateAreaM2(const std::vector<StructureMember> &_members, double _breadthM,
                        double _depthM, double _doubleBottomHeightM)
{
  return Resolve(_members, _breadthM, _depthM, _doubleBottomHeightM).plateArea;
}

}  // namespace keship
