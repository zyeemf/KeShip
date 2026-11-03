/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* SD-10 型值表编程生成可浮船型 —— T1 离屏测试
 *
 * 覆盖：
 *   ① 解析：行序打乱仍能正确归位（学生"行序不限"的契约）
 *   ② 六条判据各有负例：缺格 / 重复点 / 负半宽 / 超 B/2 / 缺端点 /
 *      端部未趋零 / 体积偏差 / 网格过疏 —— 每条都必须被抓到
 *   ③ 插值：端点与中点、区间外必须返回 0（保证船体封闭）
 *   ④ 体积对拍：21x11 查表 vs 解析值 4LBD/9
 *   ⑤ 三角数不超浮力上限
 *
 * 纪律：每条判据配一个"故意写错必须被抓到"的负例，否则判据可能空转。
 */

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "HullOffsets.hh"

namespace {

int gFail = 0;

void Check(bool _cond, const std::string &_msg)
{
  std::printf("%s  %s\n", _cond ? "[PASS]" : "[FAIL]", _msg.c_str());
  if (!_cond) ++gFail;
}

bool Near(double _a, double _b, double _rel = 1e-9)
{
  const double d = std::abs(_a - _b);
  const double scale = std::max({1.0, std::abs(_a), std::abs(_b)});
  return d <= _rel * scale;
}

// 标准题参数（与实验模板一致）
constexpr double L = 6.0;
constexpr double B = 1.5;
constexpr double D = 1.0;

/// 生成一行 CSV 文本
std::string Row(double _x, double _z, double _y)
{
  char buf[128];
  std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f", _x, _z, _y);
  return std::string(buf);
}

/// Wigley 解析半宽
double Hb(double _x, double _z)
{
  const double xi = 2.0 * _x / L - 1.0;
  const double zt = -_z / D;
  return 0.5 * B * (1.0 - xi * xi) * (1.0 - zt * zt);
}

/// 生成完整型值表行（按 x 外层、z 内层顺序）
std::vector<std::string> Lines(int _nx, int _nz, double _scaleY = 1.0)
{
  std::vector<std::string> out;
  for (int i = 0; i <= _nx; ++i) {
    const double x = L * i / _nx;
    for (int j = 0; j <= _nz; ++j) {
      const double z = -D * j / _nz;
      out.push_back(Row(x, z, _scaleY * Hb(x, z)));
    }
  }
  return out;
}

std::string Join(const std::vector<std::string> &_v)
{
  std::string s;
  for (const auto &l : _v) s += l + "\n";
  return s;
}

/// 判据是否命中（在 errors 里找关键词）
bool HasErr(const keship::OffsetsReport &_r, const std::string &_key)
{
  for (const auto &e : _r.errors)
    if (e.find(_key) != std::string::npos) return true;
  return false;
}

bool HasParseErr(const std::vector<std::string> &_e, const std::string &_key)
{
  for (const auto &e : _e)
    if (e.find(_key) != std::string::npos) return true;
  return false;
}

}  // namespace

int main()
{
  const double exactV = keship::WigleyExactVolume(L, B, D);

  // ── ① 正例 + 行序打乱 ────────────────────────────────
  {
    keship::LinesData d;
    std::vector<std::string> err;

    auto lines = Lines(20, 10);
    Check(keship::ParseOffsetsCsv(Join(lines), d, err),
          "正例：21x11 型值表解析成功" + (err.empty() ? std::string() : "：" + err[0]));
    Check(d.stations.size() == 21 && d.waterlines.size() == 11,
          "正例：站位 21 / 水线 11");
    Check(Near(d.stations.front(), 0.0) && Near(d.stations.back(), L),
          "正例：站位覆盖 [0, L]");
    Check(Near(d.waterlines.front(), 0.0) && Near(d.waterlines.back(), -D),
          "正例：水线覆盖 [-D, 0]（降序 0 -> -D）");

    // 行序打乱：反转后必须得到同一张表
    auto rev = lines;
    std::reverse(rev.begin(), rev.end());
    keship::LinesData d2;
    std::vector<std::string> err2;
    Check(keship::ParseOffsetsCsv(Join(rev), d2, err2),
          "行序打乱：解析成功" + (err2.empty() ? std::string() : "：" + err2[0]));
    bool same = (d2.stations.size() == d.stations.size() &&
                 d2.waterlines.size() == d.waterlines.size());
    if (same) {
      for (size_t i = 0; i < d.stations.size() && same; ++i)
        for (size_t j = 0; j < d.waterlines.size() && same; ++j)
          if (!Near(d.halfBreadths[i][j], d2.halfBreadths[i][j], 1e-12)) same = false;
    }
    Check(same, "行序打乱：半宽表逐位一致（行序无关）");

    // 校验通过 + 体积对拍
    auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
    Check(rep.ok, "正例：六条校验通过" +
                      (rep.errors.empty() ? std::string() : "：" + rep.errors[0]));
    Check(std::abs(rep.volumeErrPct) < 0.5,
          "正例：体积偏差 " + std::to_string(rep.volumeErrPct) + "% < 0.5%（21x11 插值损失）");
    Check(rep.estTriangles == 2 * 20 * 10 * 2 + 2 * 20,
          "正例：三角数 " + std::to_string(rep.estTriangles) + " 计算正确");
    Check(rep.estTriangles < 10000, "正例：三角数低于浮力上限 10000");
  }

  // ── ② 负例：缺格 ─────────────────────────────────────
  {
    auto lines = Lines(20, 10);
    lines.erase(lines.begin() + 55);   // 删一个点
    keship::LinesData d;
    std::vector<std::string> err;
    const bool ok = keship::ParseOffsetsCsv(Join(lines), d, err);
    Check(!ok && HasParseErr(err, "缺格"), "负例 V1：缺一个点必须报「缺格」");
  }

  // ── ② 负例：重复点 ───────────────────────────────────
  {
    auto lines = Lines(20, 10);
    lines.push_back(lines.front());
    keship::LinesData d;
    std::vector<std::string> err;
    const bool ok = keship::ParseOffsetsCsv(Join(lines), d, err);
    Check(!ok && HasParseErr(err, "重复"), "负例 V2：重复坐标必须报「重复」");
  }

  // ── ② 负例：列数不对 ─────────────────────────────────
  {
    keship::LinesData d;
    std::vector<std::string> err;
    const bool ok = keship::ParseOffsetsCsv("0,0\n", d, err);
    Check(!ok && HasParseErr(err, "3 列"), "负例：列数不足必须报「3 列」");
  }

  // ── ② 负例：负半宽 ───────────────────────────────────
  {
    auto lines = Lines(20, 10);
    // 只改半宽、不动坐标（否则触发的是缺格/重复，测不到 V3a）
    for (auto &l : lines)
      if (l.rfind("3.000000,-0.500000,", 0) == 0) { l = Row(3.0, -0.5, -0.05); break; }
    keship::LinesData d;
    std::vector<std::string> err;
    if (keship::ParseOffsetsCsv(Join(lines), d, err)) {
      auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
      Check(!rep.ok && HasErr(rep, "半宽为负"), "负例 V3a：负半宽必须被拒");
    } else {
      Check(false, "负例 V3a：解析意外失败");
    }
  }

  // ── ② 负例：漏 /2（半宽超 B/2）───────────────────────
  {
    auto lines = Lines(20, 10, 2.0);   // 整表乘 2 = 漏了 /2
    keship::LinesData d;
    std::vector<std::string> err;
    if (keship::ParseOffsetsCsv(Join(lines), d, err)) {
      auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
      Check(!rep.ok && HasErr(rep, "超过 B/2"), "负例 V3b：半宽超 B/2 必须被拒并提示漏 /2");
      Check(HasErr(rep, "偏差"), "负例 V6：整表乘 2 的体积偏差也必须报出");
      Check(std::abs(rep.volumeErrPct - 100.0) < 1.0,
            "负例 V6：体积偏差应约 +100%（实测 " + std::to_string(rep.volumeErrPct) + "%）");
    } else {
      Check(false, "负例 V3b：解析意外失败");
    }
  }

  // ── ② 负例：缺端点 ───────────────────────────────────
  {
    auto lines = Lines(20, 10);
    std::vector<std::string> keep;
    for (const auto &l : lines)
      if (l.rfind("6.000000,", 0) != 0) keep.push_back(l);   // 去掉 x=L
    keship::LinesData d;
    std::vector<std::string> err;
    if (keship::ParseOffsetsCsv(Join(keep), d, err)) {
      auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
      Check(!rep.ok && HasErr(rep, "未覆盖"), "负例 V4：缺 x=L 端点必须被拒");
    } else {
      Check(false, "负例 V4：解析意外失败");
    }
  }

  // ── ② 负例：端部未趋零 ───────────────────────────────
  {
    auto lines = Lines(20, 10);
    lines[0] = Row(0.0, 0.0, 0.5);   // 艉端水线处半宽 0.5
    keship::LinesData d;
    std::vector<std::string> err;
    if (keship::ParseOffsetsCsv(Join(lines), d, err)) {
      auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
      Check(!rep.ok && HasErr(rep, "未趋零"), "负例 V5：艉端半宽未趋零必须被拒");
    } else {
      Check(false, "负例 V5：解析意外失败");
    }
  }

  // ── ② 负例：网格过疏 ─────────────────────────────────
  {
    auto lines = Lines(10, 5);   // 11x6
    keship::LinesData d;
    std::vector<std::string> err;
    if (keship::ParseOffsetsCsv(Join(lines), d, err)) {
      auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
      Check(!rep.ok && HasErr(rep, "站位数"), "负例：11x6 网格必须因密度不足被拒");
    } else {
      Check(false, "负例（网格）：解析意外失败");
    }
  }

  // ── ③ 插值 ───────────────────────────────────────────
  {
    keship::LinesData d;
    std::vector<std::string> err;
    keship::ParseOffsetsCsv(Join(Lines(20, 10)), d, err);

    Check(Near(keship::SampleOffsetsBilinear(d, 0.0, 0.0), 0.0),
          "插值：艉端半宽为 0");
    Check(Near(keship::SampleOffsetsBilinear(d, L, 0.0), 0.0),
          "插值：艏端半宽为 0");
    Check(Near(keship::SampleOffsetsBilinear(d, L / 2.0, 0.0), 0.5 * B, 1e-6),
          "插值：中站水线处半宽 = B/2");
    Check(Near(keship::SampleOffsetsBilinear(d, -1.0, 0.0), 0.0),
          "插值：x < 0 返回 0（保证封闭）");
    Check(Near(keship::SampleOffsetsBilinear(d, L + 1.0, 0.0), 0.0),
          "插值：x > L 返回 0");
    Check(Near(keship::SampleOffsetsBilinear(d, L / 2.0, 1.0), 0.0),
          "插值：z > 0（水面以上）返回 0");
    Check(Near(keship::SampleOffsetsBilinear(d, L / 2.0, -D - 1.0), 0.0),
          "插值：z < -D（龙骨以下）返回 0");
  }

  // ── ③ 负零宽容 ───────────────────────────────────────
  {
    auto lines = Lines(20, 10);
    lines[0] = Row(0.0, 0.0, -0.0);   // 学生可能打印出 -0.000000
    keship::LinesData d;
    std::vector<std::string> err;
    if (keship::ParseOffsetsCsv(Join(lines), d, err)) {
      auto rep = keship::ValidateOffsets(d, L, B, D, exactV);
      Check(rep.ok, "-0.000000 必须被当作 0（不得误判为负半宽）" +
                        (rep.errors.empty() ? std::string() : "：" + rep.errors[0]));
    } else {
      Check(false, "负零：解析意外失败");
    }
  }

  // ── ④ 解析体积基准 ───────────────────────────────────
  {
    Check(Near(exactV, 4.0, 1e-12),
          "Wigley 解析体积 4LBD/9 = 4.0 m3（L6/B1.5/D1）");
  }

  // ── ⑥ 内核接入：老路径逐位不变 + 查表路径与解析路径对比 ──
  {
    keship::ShipHullSpec base = keship::ShipHullSpec::WigleyClassic(L, B, D, 500.0);
    keship::ShipHullGenerator gRef(base);
    const auto &ref = gRef.hydroProps();

    // 基线取自独立的 Python 解析积分（阶段 0），不是从 C++ 自身反推
    Check(std::abs(ref.Aw - 5.996250) < 1e-3, "老路径 Aw 与解析基线一致（5.996250）");
    Check(std::abs(ref.Ix - 0.771429) < 1e-4, "老路径 Ix 与解析基线一致（0.771429）");
    Check(std::abs(ref.volume - 3.996875) < 1e-3, "老路径 volume 与解析基线一致（3.996875）");
    Check(std::abs(ref.KB - 0.374824) < 1e-4, "老路径 KB 与解析基线一致（0.374824）");

    // 查表路径：同一船型用 21x11 型值表重建
    keship::LinesData d;
    std::vector<std::string> err;
    keship::ParseOffsetsCsv(Join(Lines(20, 10)), d, err);
    keship::ShipHullSpec s2 = base;
    s2.offsets = d;
    s2.useOffsets = true;
    keship::ShipHullGenerator gTab(s2);
    const auto &tab = gTab.hydroProps();

    const double dV = 100.0 * (tab.volume - ref.volume) / ref.volume;
    const double dA = 100.0 * (tab.Aw - ref.Aw) / ref.Aw;
    const double dK = 100.0 * (tab.KB - ref.KB) / ref.KB;
    std::printf("       [查表路径] dV=%+.4f%%  dAw=%+.4f%%  dKB=%+.4f%%  "
                "GMd ref=%.6f tab=%.6f\n", dV, dA, dK, ref.GMd, tab.GMd);

    Check(std::abs(dV) < 1.0, "查表路径 volume 与解析路径偏差 < 1%");
    Check(std::abs(dA) < 1.0, "查表路径 Aw 偏差 < 1%");
    Check(std::abs(dK) < 1.0, "查表路径 KB 偏差 < 1%");
    Check(std::abs(tab.GMd - ref.GMd) < 0.02,
          "查表路径 GMd 与解析路径差 < 0.02 m");

    // 网格与浮力网格均非空（否则 spawn 出去是空模型）
    Check(!gTab.visualMesh().vertices.empty() && !gTab.visualMesh().triangles.empty(),
          "查表路径：视觉网格非空");
    Check(!gTab.buoyancyMesh().vertices.empty() && !gTab.buoyancyMesh().triangles.empty(),
          "查表路径：浮力网格非空");
    Check(gTab.buoyancyMesh().triangles.size() <= 10000,
          "查表路径：浮力网格三角数 " +
              std::to_string(gTab.buoyancyMesh().triangles.size()) + " 不超上限");
  }

  // ── ⑤ 空输入 ─────────────────────────────────────────
  {
    keship::LinesData d;
    std::vector<std::string> err;
    Check(!keship::ParseOffsetsCsv("", d, err), "负例：空 stdout 必须失败");
    Check(!keship::ParseOffsetsCsv("# only comment\n", d, err),
          "负例：只有注释行必须失败");
  }

  std::printf("--- SD-10 T1 结果：%s（失败 %d 项）---\n",
              gFail == 0 ? "PASS" : "FAIL", gFail);
  return gFail == 0 ? 0 : 1;
}
