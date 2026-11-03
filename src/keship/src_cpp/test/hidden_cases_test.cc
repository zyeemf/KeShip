/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 无头测试：HiddenCaseGenerator（随机范围 / 可复现 / 可解性 / 算法一致性）
 */
// 无头测试：HiddenCaseGenerator（随机范围 / 可复现 / 可解性 / 算法一致性）
// 并导出每组时程 CSV + 基准值，供 Python 学生路径测试复用
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>

#include "HiddenCaseGenerator.hh"

static int g_fails = 0;
static void check(bool _ok, const std::string &_name, const std::string &_detail = "") {
  std::cout << (_ok ? "  [PASS] " : "  [FAIL] ") << _name;
  if (!_detail.empty()) std::cout << "  (" << _detail << ")";
  std::cout << std::endl;
  if (!_ok) ++g_fails;
}

int main() {
  const auto cases = keship::GenerateHiddenCases(5, 12345u);
  check(cases.size() == 5, "生成 5 组");

  bool rangesOk = true;
  for (const auto &c : cases) {
    rangesOk = rangesOk && c.period >= 3.0 && c.period <= 8.0 &&
               c.waveAmp >= 0.3 && c.waveAmp <= 0.8 && c.genRao >= 5.0 &&
               c.genRao <= 25.0 && c.trail.size() == 601 && c.refRao > 0.0;
  }
  check(rangesOk, "参数范围 / 采样点数(601) / 基准值有效");

  // 基准值应接近生成真值（同一算法在合成数据上应能恢复幅值，容差 5%）
  bool recoverOk = true;
  std::string detail;
  for (size_t i = 0; i < cases.size(); ++i) {
    const double dev = std::fabs(cases[i].refRao - cases[i].genRao) / cases[i].genRao * 100.0;
    if (dev > 5.0) { recoverOk = false; }
    char buf[160];
    std::snprintf(buf, sizeof(buf), "#%d T=%.2f a=%.2f gen=%.2f ref=%.2f dev=%.1f%%",
                  (int)i + 1, cases[i].period, cases[i].waveAmp, cases[i].genRao,
                  cases[i].refRao, dev);
    detail += buf;
    if (i + 1 < cases.size()) detail += " | ";
  }
  check(recoverOk, "基准值可恢复生成真值（≤5%）", detail);

  // 可复现：同 seed 结果逐位相同
  const auto again = keship::GenerateHiddenCases(5, 12345u);
  bool same = true;
  for (size_t i = 0; i < cases.size(); ++i)
    same = same && cases[i].refRao == again[i].refRao;
  check(same, "同 seed 可复现（refRao 逐位相同）");

  // 不同 seed 应不同
  const auto other = keship::GenerateHiddenCases(5, 999u);
  bool differ = false;
  for (size_t i = 0; i < cases.size(); ++i)
    differ = differ || std::fabs(cases[i].refRao - other[i].refRao) > 1e-6;
  check(differ, "不同 seed 结果不同");

  // 可解性：按题目要求的三步算法独立复算，应与基准值一致
  bool solvable = true;
  for (const auto &c : cases) {
    const double t0 = c.trail.front().t, t1 = c.trail.back().t;
    const double mid = 0.5 * (t0 + t1);
    double mn = 1e9, mx = -1e9;
    for (const auto &s : c.trail) {
      if (s.t < mid) continue;
      mn = std::min(mn, s.rollDeg);
      mx = std::max(mx, s.rollDeg);
    }
    const double rao = ((mx - mn) / 2.0) / c.waveAmp;
    if (std::fabs(rao - c.refRao) > 1e-6) solvable = false;
  }
  check(solvable, "按三步算法独立复算 = 基准值（可解性）");

  // 导出 CSV（供 Python 学生路径测试）
  for (size_t i = 0; i < cases.size(); ++i) {
    char path[256];
    std::snprintf(path, sizeof(path), "/tmp/hidden_case_%d.csv", (int)i + 1);
    std::ofstream f(path);
    f << "t_s,roll_deg\n";
    for (const auto &s : cases[i].trail) f << s.t << "," << s.rollDeg << "\n";
    f.close();
    char meta[256];
    std::snprintf(meta, sizeof(meta), "/tmp/hidden_case_%d.meta", (int)i + 1);
    std::ofstream g(meta);
    g << cases[i].period << "," << cases[i].waveAmp << "," << cases[i].refRao << "\n";
  }
  std::cout << "  [INFO] CSV/meta 已导出到 /tmp/hidden_case_*.csv|.meta" << std::endl;

  std::cout << "HIDDEN_CASES " << (g_fails ? "FAIL" : "PASS")
            << " (fails=" << g_fails << ")" << std::endl;
  return g_fails ? 1 : 0;
}
