/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * batch1_verify_probe.cc —— 批次一验收探针（只读）
 */
// batch1_verify_probe.cc —— 批次一验收探针（只读）
//
// 验证三件事：
//   ⑤ 等价性：LoadNormalizedObjVertices（新，流式只读 "v" 行）
//             与 LoadMeshFromFile（旧，Assimp）在**规范化 OBJ** 上的结果是否等价。
//      ⚠ 判据用**容差**而非逐位：Assimp 的 aiVector3D 是 **float32**，读文本 3.05 得
//        3.049999952；而 strtod 得 double 3.05 ⇒ 逐位比较必然不等（首轮探针已实测）。
//        规范化 OBJ 本身只写 6 位有效数字 ⇒ 真实精度上限就在这里，用相对容差 1e-6。
//      ⚠ 顶点**总数**不做判据：Assimp 会 JoinIdenticalVertices 合并重复点
//        （实测 200390→200346），那是它的内部行为，不是差异。
//        改为比较**去重后的 XY 点集**（= ComputeConvexHull 的输入）与**凸包输出**。
//   ⑤ 加速：两条路径各自耗时
//   ④ 加速：NormalizeMeshToSDFCache 第 1/2 次耗时（sidecar 命中后应显著更快）
//
// 凸包对照刻意用**独立实现**（单调链），不用生产代码 —— 这样"⑤ 不影响 2D 足迹轮廓"
// 这一结论不依赖单一实现（两个独立实现 + 两条解析路径 交叉一致）。
//
// 用法：/tmp/batch1_verify_probe <源文件1> [源文件2 ...]

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "MeshUtils.hh"

namespace fs = std::filesystem;

static const double kTol = 1e-6;   ///< 相对容差（规范化 OBJ 只写 6 位有效数字）

static bool Close(double _a, double _b)
{
  const double scale = std::max(1.0, std::max(std::fabs(_a), std::fabs(_b)));
  return std::fabs(_a - _b) <= kTol * scale;
}

static double NowMs()
{
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}
template <typename F> static double TimeIt(F &&f)
{
  const double t0 = NowMs();
  f();
  return NowMs() - t0;
}

/// 与 ComputeConvexHull 内部第一步一致：投影到 XY，sort + unique
static std::vector<std::pair<double,double>>
DedupSortedXY(const std::vector<gz::math::Vector3d> &_v)
{
  std::vector<std::pair<double,double>> p;
  p.reserve(_v.size());
  for (const auto &x : _v) p.emplace_back(x.X(), x.Y());
  std::sort(p.begin(), p.end());
  p.erase(std::unique(p.begin(), p.end()), p.end());
  return p;
}

/// 独立实现的 2D 凸包（单调链 / Andrew）。刻意不复用生产代码。
static std::vector<std::pair<double,double>>
HullMonotoneChain(std::vector<std::pair<double,double>> _p)
{
  std::sort(_p.begin(), _p.end());
  _p.erase(std::unique(_p.begin(), _p.end()), _p.end());
  const size_t n = _p.size();
  if (n < 3) return _p;
  auto cross = [](const std::pair<double,double> &O,
                  const std::pair<double,double> &A,
                  const std::pair<double,double> &B) {
    return (A.first - O.first) * (B.second - O.second)
         - (A.second - O.second) * (B.first - O.first);
  };
  std::vector<std::pair<double,double>> h(2 * n);
  size_t k = 0;
  for (size_t i = 0; i < n; ++i)
  {
    while (k >= 2 && cross(h[k - 2], h[k - 1], _p[i]) <= 0) --k;
    h[k++] = _p[i];
  }
  for (size_t i = n - 1, t = k + 1; i > 0; --i)
  {
    while (k >= t && cross(h[k - 2], h[k - 1], _p[i - 1]) <= 0) --k;
    h[k++] = _p[i - 1];
  }
  h.resize(k - 1);
  return h;
}

static bool SamePoints(const std::vector<std::pair<double,double>> &_a,
                       const std::vector<std::pair<double,double>> &_b)
{
  if (_a.size() != _b.size()) return false;
  for (size_t i = 0; i < _a.size(); ++i)
    if (!Close(_a[i].first, _b[i].first) || !Close(_a[i].second, _b[i].second))
      return false;
  return true;
}

struct BBox { double w, h, xMin, yMin, xMax, yMax; };
static BBox BoxOf(const std::vector<gz::math::Vector3d> &_v)
{
  double xMin = 1e300, xMax = -1e300, yMin = 1e300, yMax = -1e300;
  for (const auto &v : _v)
  {
    xMin = std::min(xMin, v.X()); xMax = std::max(xMax, v.X());
    yMin = std::min(yMin, v.Y()); yMax = std::max(yMax, v.Y());
  }
  return {xMax - xMin, yMax - yMin, xMin, yMin, xMax, yMax};
}

int main(int argc, char **argv)
{
  if (argc < 2) { std::fprintf(stderr, "用法: %s <源文件...>\n", argv[0]); return 2; }

  int nFail = 0;
  std::printf("容差 kTol = %.0e（相对，按 max(1,|a|,|b|) 缩放）\n\n", kTol);

  for (int i = 1; i < argc; ++i)
  {
    const std::string src = argv[i];
    if (!fs::exists(src)) { std::printf("%s  [源文件不存在]\n", src.c_str()); continue; }

    std::string norm, norm2;
    const double tNorm1 = TimeIt([&] { norm = keship::NormalizeMeshToSDFCache(src); });
    const double tNorm2 = TimeIt([&] { norm2 = keship::NormalizeMeshToSDFCache(src); });

    std::vector<gz::math::Vector3d> vOld;
    const double tOld = TimeIt([&] { vOld = keship::LoadMeshFromFile(norm).vertices; });
    std::vector<gz::math::Vector3d> vNew;
    const double tNew = TimeIt([&] { keship::LoadNormalizedObjVertices(norm, vNew); });

    const auto pOld = DedupSortedXY(vOld);
    const auto pNew = DedupSortedXY(vNew);
    const auto hOld = HullMonotoneChain(pOld);
    const auto hNew = HullMonotoneChain(pNew);
    const BBox bOld = BoxOf(vOld);
    const BBox bNew = BoxOf(vNew);

    const bool samePts  = SamePoints(pOld, pNew);
    const bool sameHull = SamePoints(hOld, hNew);
    const bool sameBox  = Close(bOld.w, bNew.w) && Close(bOld.h, bNew.h) &&
                          Close(bOld.xMin, bNew.xMin) && Close(bOld.yMin, bNew.yMin) &&
                          Close(bOld.xMax, bNew.xMax) && Close(bOld.yMax, bNew.yMax);
    const bool samePath = (norm == norm2) && !norm.empty();
    const bool ok = samePts && sameHull && sameBox && samePath;
    if (!ok) ++nFail;

    std::printf("──────── %s   %s\n", fs::path(src).filename().string().c_str(),
                ok ? "[PASS]" : "[FAIL]");
    std::printf("  ④ 规范化 : 第1次 %8.1f ms  /  第2次 %8.1f ms   (路径一致=%s)\n",
                tNorm1, tNorm2, norm == norm2 ? "是" : "否");
    std::printf("  ⑤ 解析   : 旧(Assimp) %8.1f ms  /  新(流式) %8.1f ms  → 提速 %.1fx\n",
                tOld, tNew, (tNew > 0 ? tOld / tNew : 0.0));
    std::printf("  ⑤ 顶点数 : 旧 %zu / 新 %zu  （仅供参考：Assimp 会合并重复点）\n",
                vOld.size(), vNew.size());
    std::printf("  ⑤ 去重XY : %zu / %zu  → %s\n",
                pOld.size(), pNew.size(), samePts ? "容差内逐点一致" : "★不一致");
    std::printf("  ⑤ 凸包   : %zu / %zu  → %s（独立单调链实现）\n",
                hOld.size(), hNew.size(), sameHull ? "容差内逐点一致" : "★不一致");
    std::printf("  ⑤ 包围盒 : 旧 w=%.9g h=%.9g xMin=%.9g yMin=%.9g\n",
                bOld.w, bOld.h, bOld.xMin, bOld.yMin);
    std::printf("              新 w=%.9g h=%.9g xMin=%.9g yMin=%.9g  → %s\n",
                bNew.w, bNew.h, bNew.xMin, bNew.yMin, sameBox ? "容差内一致" : "★不一致");
    std::fflush(stdout);
  }
  std::printf("\nFAIL 数 = %d\n", nFail);
  return nFail == 0 ? 0 : 1;
}
