/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 惯性计算引擎 — 四面体分解法 + Quickhull 3D + 算法分支
 * 算法参考: Brian Mirtich, "Fast and Accurate Computation of Polyhedral Mass
 * Properties", Journal of Graphics Tools 1996.
 * Quickhull: Barber, Dobkin, Huhdanpaa, "The Quickhull Algorithm for Convex
 * Hulls", ACM Trans. Math. Software 1996.
 */
/// ZhangYu 创建 2026.7.18

#include "MeshInertiaCalculator.hh"

#include <cmath>
#include <sstream>
#include <iomanip>
#include <map>
#include <set>
#include <queue>
#include <algorithm>
#include <limits>

namespace keship
{

// =========================================================================
// Internal: 10 几何积分累加器
// =========================================================================
namespace
{

struct TetraAccumulator
{
  double vol   = 0.0;
  double ix    = 0.0,  iy    = 0.0,  iz    = 0.0;
  double ixx   = 0.0,  iyy   = 0.0,  izz   = 0.0;
  double ixy   = 0.0,  ixz   = 0.0,  iyz   = 0.0;
};

double signedTetraVol(const gz::math::Vector3d &a,
                      const gz::math::Vector3d &b,
                      const gz::math::Vector3d &c)
{
  return a.Dot(b.Cross(c)) / 6.0;
}

void addTetra(TetraAccumulator &acc,
              const gz::math::Vector3d &a,
              const gz::math::Vector3d &b,
              const gz::math::Vector3d &c)
{
  double v = signedTetraVol(a, b, c);
  acc.vol += v;

  acc.ix += v * (a.X() + b.X() + c.X()) / 4.0;
  acc.iy += v * (a.Y() + b.Y() + c.Y()) / 4.0;
  acc.iz += v * (a.Z() + b.Z() + c.Z()) / 4.0;

  double ax = a.X(), bx = b.X(), cx = c.X();
  double ay = a.Y(), by = b.Y(), cy = c.Y();
  double az = a.Z(), bz = b.Z(), cz = c.Z();

  acc.ixx += v * (ax*ax + bx*bx + cx*cx + ax*bx + ax*cx + bx*cx) / 10.0;
  acc.iyy += v * (ay*ay + by*by + cy*cy + ay*by + ay*cy + by*cy) / 10.0;
  acc.izz += v * (az*az + bz*bz + cz*cz + az*bz + az*cz + bz*cz) / 10.0;

  acc.ixy += v * (
      2.0*ax*ay + 2.0*bx*by + 2.0*cx*cy
      + ax*by + bx*ay + ax*cy + cx*ay + bx*cy + cx*by) / 20.0;
  acc.ixz += v * (
      2.0*ax*az + 2.0*bx*bz + 2.0*cx*cz
      + ax*bz + bx*az + ax*cz + cx*az + bx*cz + cx*bz) / 20.0;
  acc.iyz += v * (
      2.0*ay*az + 2.0*by*bz + 2.0*cy*cz
      + ay*bz + by*az + ay*cz + cy*az + by*cz + cy*bz) / 20.0;
}

// =========================================================================
// Internal: Quickhull 3D
// =========================================================================

struct QFace
{
  int v[3];
  gz::math::Vector3d normal;
  double dist = 0.0;
  std::vector<int> outside;
  bool deleted = false;
};

using EdgeKey = std::pair<int, int>;

static EdgeKey makeEdgeKey(int a, int b)
{
  return {std::min(a, b), std::max(a, b)};
}

static double tripleVol(const gz::math::Vector3d &a,
                         const gz::math::Vector3d &b,
                         const gz::math::Vector3d &c)
{
  return a.Dot(b.Cross(c));
}

// plane参数: normal·p = dist
// normal=(b-a)×(c-a) 归一化, dist=normal·a
static void setPlane(QFace &f, int v0, int v1, int v2,
                     const std::vector<gz::math::Vector3d> &pts)
{
  f.v[0] = v0; f.v[1] = v1; f.v[2] = v2;
  gz::math::Vector3d a = pts[v0], b = pts[v1], c = pts[v2];
  gz::math::Vector3d n = (b - a).Cross(c - a);
  double len = n.Length();
  if (len > 1e-30)
    n /= len;
  f.normal = n;
  f.dist = n.Dot(a);
}

// 返回点在平面正侧（凸包外部）
static bool isOutside(const gz::math::Vector3d &p,
                      const gz::math::Vector3d &normal,
                      double dist, double eps)
{
  return normal.Dot(p) - dist > eps;
}

static std::vector<int> findHorizon(
    int startFace,
    const gz::math::Vector3d &eye,
    double eps,
    const std::vector<QFace> &faces,
    const std::map<EdgeKey, std::array<int, 2>> &edgeMap)
{
  std::vector<int> horizon;
  std::vector<bool> visited(faces.size(), false);
  std::queue<int> q;
  q.push(startFace);
  visited[startFace] = true;

  while (!q.empty())
  {
    int fi = q.front(); q.pop();
    const auto &f = faces[fi];
    for (int j = 0; j < 3; ++j)
    {
      int a = f.v[j];
      int b = f.v[(j + 1) % 3];
      auto it = edgeMap.find(makeEdgeKey(a, b));
      if (it == edgeMap.end()) continue;
      const auto &sides = it->second;
      int adj = (sides[0] == fi) ? sides[1] : sides[0];
      if (adj < 0 || adj >= (int)faces.size()) continue;
      if (visited[adj]) continue;

      if (isOutside(eye, faces[adj].normal, faces[adj].dist, eps))
      {
        visited[adj] = true;
        q.push(adj);
      }
      else
      {
        horizon.push_back(a);
        horizon.push_back(b);
      }
    }
  }
  return horizon;
}

// 主 Quickhull 3D 实现
static MeshDataSimple quickhull3D(
    const std::vector<gz::math::Vector3d> &points,
    double eps)
{
  MeshDataSimple result;
  int np = (int)points.size();
  if (np < 4) return result;

  // ---- Step 1: 初始四面体 ----
  int minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
  for (int i = 1; i < np; ++i)
  {
    const auto &p = points[i];
    if (p.X() < points[minX].X()) minX = i;
    if (p.X() > points[maxX].X()) maxX = i;
    if (p.Y() < points[minY].Y()) minY = i;
    if (p.Y() > points[maxY].Y()) maxY = i;
    if (p.Z() < points[minZ].Z()) minZ = i;
    if (p.Z() > points[maxZ].Z()) maxZ = i;
  }

  std::set<int> ex = {minX, maxX, minY, maxY, minZ, maxZ};
  std::vector<int> xlist(ex.begin(), ex.end());

  // 极值点中最远点对
  int v0 = xlist[0], v1 = xlist[1];
  double d2max = (points[v0] - points[v1]).SquaredLength();
  for (size_t i = 0; i < xlist.size(); ++i)
    for (size_t j = i + 1; j < xlist.size(); ++j)
    {
      double d2 = (points[xlist[i]] - points[xlist[j]]).SquaredLength();
      if (d2 > d2max) { d2max = d2; v0 = xlist[i]; v1 = xlist[j]; }
    }

  // 距离直线最远的第三点
  int v2 = -1;
  double dmax = -1.0;
  gz::math::Vector3d dir = points[v1] - points[v0];
  double dirLen = dir.Length();
  if (dirLen < eps) return result;
  dir /= dirLen;
  for (int i = 0; i < np; ++i)
  {
    gz::math::Vector3d diff = points[i] - points[v0];
    double d = (diff - dir * diff.Dot(dir)).Length();
    if (d > dmax) { dmax = d; v2 = i; }
  }
  if (v2 < 0 || dmax < eps) return result;

  // 距离平面最远的第四点
  int v3 = -1;
  dmax = -1.0;
  gz::math::Vector3d n = (points[v1] - points[v0]).Cross(points[v2] - points[v0]);
  double nLen = n.Length();
  if (nLen < eps) return result;
  n /= nLen;
  double planeD = n.Dot(points[v0]);
  for (int i = 0; i < np; ++i)
  {
    double d = std::abs(n.Dot(points[i]) - planeD);
    if (d > dmax) { dmax = d; v3 = i; }
  }
  if (v3 < 0 || dmax < eps) return result;

  // 确保四面体有正有符号体积（右手系）
  double tv = tripleVol(points[v1] - points[v0],
                        points[v2] - points[v0],
                        points[v3] - points[v0]);
  if (tv < 0) std::swap(v2, v3);

  // ---- Step 2: 建 4 个初始面 ----
  std::vector<QFace> faces(4);
  // 面 0: 对面 v0, 正向 (v1, v2, v3)
  setPlane(faces[0], v1, v2, v3, points);
  // 确保法线朝外
  if (faces[0].normal.Dot(points[v0] - points[v1]) > 0)
    setPlane(faces[0], v1, v3, v2, points);

  // 面 1: 对面 v1, 正向 (v0, v3, v2)
  setPlane(faces[1], v0, v3, v2, points);
  if (faces[1].normal.Dot(points[v1] - points[v0]) > 0)
    setPlane(faces[1], v0, v2, v3, points);

  // 面 2: 对面 v2, 正向 (v0, v1, v3)
  setPlane(faces[2], v0, v1, v3, points);
  if (faces[2].normal.Dot(points[v2] - points[v0]) > 0)
    setPlane(faces[2], v0, v3, v1, points);

  // 面 3: 对面 v3, 正向 (v0, v2, v1)
  setPlane(faces[3], v0, v2, v1, points);
  if (faces[3].normal.Dot(points[v3] - points[v0]) > 0)
    setPlane(faces[3], v0, v1, v2, points);

  // ---- Step 3: 分类所有点到各面 ----
  for (int pi = 0; pi < np; ++pi)
  {
    if (pi == v0 || pi == v1 || pi == v2 || pi == v3) continue;
    for (auto &f : faces)
    {
      if (isOutside(points[pi], f.normal, f.dist, eps))
      {
        f.outside.push_back(pi);
        break;
      }
    }
  }

  // ---- Step 4: 主循环 ----
  auto newEdgeMap = [&]() -> std::map<EdgeKey, std::array<int, 2>> {
    std::map<EdgeKey, std::array<int, 2>> em;
    for (int fi = 0; fi < (int)faces.size(); ++fi)
    {
      if (faces[fi].deleted) continue;
      for (int j = 0; j < 3; ++j)
      {
        EdgeKey ek = makeEdgeKey(faces[fi].v[j], faces[fi].v[(j + 1) % 3]);
        auto it = em.find(ek);
        if (it == em.end())
        {
          std::array<int, 2> val = {fi, -1};
          em[ek] = val;
        }
        else
          it->second[1] = fi;
      }
    }
    return em;
  };

  while (true)
  {
    // 找 outside 非空的面中最远点
    int bestFace = -1;
    int bestPoint = -1;
    double bestDist = 0.0;
    for (int fi = 0; fi < (int)faces.size(); ++fi)
    {
      if (faces[fi].deleted || faces[fi].outside.empty()) continue;
      for (int pi : faces[fi].outside)
      {
        double d = faces[fi].normal.Dot(points[pi]) - faces[fi].dist;
        if (d > bestDist) { bestDist = d; bestFace = fi; bestPoint = pi; }
      }
    }
    if (bestFace < 0) break;

    const auto &eyePt = points[bestPoint];
    auto edgeMap = newEdgeMap();

    // 找 horizon
    auto hEdges = findHorizon(bestFace, eyePt, eps, faces, edgeMap);
    if (hEdges.empty()) { faces[bestFace].outside.clear(); continue; }

    // 收集需要删除的面（BFS 中 visited=true 的面）
    std::vector<bool> vis(faces.size(), false);
    std::queue<int> q;
    q.push(bestFace);
    vis[bestFace] = true;
    while (!q.empty())
    {
      int fi = q.front(); q.pop();
      for (int j = 0; j < 3; ++j)
      {
        int a = faces[fi].v[j], b = faces[fi].v[(j + 1) % 3];
        auto it = edgeMap.find(makeEdgeKey(a, b));
        if (it == edgeMap.end()) continue;
        const auto &s = it->second;
        int adj = (s[0] == fi) ? s[1] : s[0];
        if (adj >= 0 && adj < (int)faces.size() && !vis[adj]
            && isOutside(eyePt, faces[adj].normal, faces[adj].dist, eps))
        {
          vis[adj] = true;
          q.push(adj);
        }
      }
    }

    // 收集被删除面的所有外部点
    std::vector<int> reassign;
    for (int fi = 0; fi < (int)faces.size(); ++fi)
    {
      if (vis[fi])
      {
        reassign.insert(reassign.end(),
                        faces[fi].outside.begin(),
                        faces[fi].outside.end());
        faces[fi].deleted = true;
      }
    }

    // 创建新面
    int nNewFaces = (int)hEdges.size() / 2;
    int faceStart = (int)faces.size();
    faces.resize(faces.size() + nNewFaces);
    for (int i = 0; i < nNewFaces; ++i)
    {
      int ei = i * 2;
      int a = hEdges[ei], b = hEdges[ei + 1];
      // 确定 winding: 从可见面看 horizon 边 (a→b)
      // 新面应为 (b, a, bestPoint) — 法线朝外
      setPlane(faces[faceStart + i], a, b, bestPoint, points);
    }

    // 重分配点
    for (int pi : reassign)
    {
      for (int fi = faceStart; fi < (int)faces.size(); ++fi)
      {
        if (isOutside(points[pi], faces[fi].normal, faces[fi].dist, eps))
        {
          faces[fi].outside.push_back(pi);
          break;
        }
      }
    }
  }

  // ---- Step 5: 收集结果 ----
  std::map<int, int> vmap; // old → new
  for (const auto &f : faces)
  {
    if (f.deleted) continue;
    for (int j = 0; j < 3; ++j)
    {
      if (vmap.find(f.v[j]) == vmap.end())
      {
        int newIdx = (int)result.vertices.size();
        vmap[f.v[j]] = newIdx;
        result.vertices.push_back(points[f.v[j]]);
      }
    }
    // 输出三角形（保留 winding, 保持法线朝外）
    Triangle3D tri;
    tri.v0 = points[f.v[0]];
    tri.v1 = points[f.v[1]];
    tri.v2 = points[f.v[2]];
    result.triangles.push_back(tri);
    result.triangleIndices.push_back({
        (unsigned)vmap[f.v[0]],
        (unsigned)vmap[f.v[1]],
        (unsigned)vmap[f.v[2]]});
  }

  return result;
}

// 对单个网格做四面体分解（内部工具）
static InertiaCalcResult decomposeMesh(const MeshDataSimple &mesh,
                                        double density)
{
  InertiaCalcResult base;
  if (mesh.triangles.empty())
  {
    base.error = InertiaCalcError::MeshEmpty;
    base.message = "网格不含三角形面片";
    return base;
  }

  TetraAccumulator acc;
  for (const auto &tri : mesh.triangles)
    addTetra(acc, tri.v0, tri.v1, tri.v2);

  double vol = acc.vol;
  if (vol <= 0.0)
  {
    base.error = InertiaCalcError::VolumeNegative;
    base.message = "体积为非正 (" + std::to_string(vol) + ")";
    return base;
  }

  double cmx = acc.ix / vol;
  double cmy = acc.iy / vol;
  double cmz = acc.iz / vol;

  double J0xx = acc.iyy + acc.izz;
  double J0yy = acc.ixx + acc.izz;
  double J0zz = acc.ixx + acc.iyy;
  double J0xy = -acc.ixy;
  double J0xz = -acc.ixz;
  double J0yz = -acc.iyz;

  double gxx = vol * (cmy*cmy + cmz*cmz);
  double gyy = vol * (cmx*cmx + cmz*cmz);
  double gzz = vol * (cmx*cmx + cmy*cmy);
  double gxy = vol * cmx * cmy;
  double gxz = vol * cmx * cmz;
  double gyz = vol * cmy * cmz;

  base.volume = vol;
  base.params.mass   = vol * density;
  base.params.cogX   = cmx;
  base.params.cogY   = cmy;
  base.params.cogZ   = cmz;
  base.params.ixx    = (J0xx - gxx) * density;
  base.params.iyy    = (J0yy - gyy) * density;
  base.params.izz    = (J0zz - gzz) * density;
  base.params.ixy    = (J0xy + gxy) * density;
  base.params.ixz    = (J0xz + gxz) * density;
  base.params.iyz    = (J0yz + gyz) * density;

  return base;
}

} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================

InertiaCalcResult ComputeInertiaFromMesh(const MeshDataSimple &_mesh,
                                          double _density,
                                          InertiaCalcMethod _method)
{
  InertiaCalcResult result;
  result.method = _method;

  if (_mesh.triangles.empty())
  {
    result.error   = InertiaCalcError::MeshEmpty;
    result.message = "网格不含三角形面片";
    return result;
  }
  if (_density <= 0.0)
  {
    result.error   = InertiaCalcError::MassZero;
    result.message = "密度必须大于 0";
    return result;
  }

  MeshDataSimple workMesh;

  switch (_method)
  {
  case InertiaCalcMethod::Direct:
    workMesh = _mesh;
    break;

  case InertiaCalcMethod::ConvexHull:
  {
    // 计算包围盒对角线作为 epsilon 参考
    double minX = 1e30, maxX = -1e30, minY = 1e30, maxY = -1e30,
           minZ = 1e30, maxZ = -1e30;
    for (const auto &v : _mesh.vertices)
    {
      if (v.X() < minX) minX = v.X(); if (v.X() > maxX) maxX = v.X();
      if (v.Y() < minY) minY = v.Y(); if (v.Y() > maxY) maxY = v.Y();
      if (v.Z() < minZ) minZ = v.Z(); if (v.Z() > maxZ) maxZ = v.Z();
    }
    double diag = std::sqrt((maxX-minX)*(maxX-minX) +
                            (maxY-minY)*(maxY-minY) +
                            (maxZ-minZ)*(maxZ-minZ));
    double eps = std::max(1e-12, diag * 1e-9);

    workMesh = ComputeConvexHull3D(_mesh);
    if (workMesh.triangles.empty())
    {
      result.error = InertiaCalcError::HullFailed;
      result.message = "凸包计算失败（点云退化为平面或直线）";
      return result;
    }
    result.hullVertexCount = (int)workMesh.vertices.size();
    result.hullFaceCount   = (int)workMesh.triangles.size();
    break;
  }

  case InertiaCalcMethod::VHACD:
  {
    result.error = InertiaCalcError::HullFailed;
    result.message = "V-HACD 模式尚未集成，请切换到 Direct 或 ConvexHull";
    return result;
  }
  }

  result = decomposeMesh(workMesh, _density);
  result.method = _method;
  if (_method == InertiaCalcMethod::ConvexHull)
  {
    result.hullVertexCount = (int)workMesh.vertices.size();
    result.hullFaceCount   = (int)workMesh.triangles.size();
  }

  if (result.error == InertiaCalcError::Ok
      && !ValidateInertiaTensor(result.params))
  {
    result.error   = InertiaCalcError::TensorInvalid;
    result.message = "惯量张量不满足三角不等式，计算结果可能不准确";
  }

  return result;
}

// =========================================================================
MeshDataSimple ComputeConvexHull3D(const MeshDataSimple &_mesh)
{
  if (_mesh.vertices.empty()) return {};

  // 去重（容差 1e-9 * 对角）
  double minX = 1e30, maxX = -1e30;
  double minY = 1e30, maxY = -1e30;
  double minZ = 1e30, maxZ = -1e30;
  for (const auto &v : _mesh.vertices)
  {
    if (v.X() < minX) minX = v.X(); if (v.X() > maxX) maxX = v.X();
    if (v.Y() < minY) minY = v.Y(); if (v.Y() > maxY) maxY = v.Y();
    if (v.Z() < minZ) minZ = v.Z(); if (v.Z() > maxZ) maxZ = v.Z();
  }
  double diag = std::sqrt((maxX-minX)*(maxX-minX) +
                          (maxY-minY)*(maxY-minY) +
                          (maxZ-minZ)*(maxZ-minZ));
  double eps = std::max(1e-12, diag * 1e-9);

  std::vector<gz::math::Vector3d> unique;
  for (const auto &v : _mesh.vertices)
  {
    bool dup = false;
    for (const auto &u : unique)
    {
      if ((v - u).SquaredLength() < eps * eps)
      { dup = true; break; }
    }
    if (!dup) unique.push_back(v);
  }

  return quickhull3D(unique, eps);
}

// =========================================================================
bool ValidateInertiaTensor(const InertiaParams &_ip)
{
  return (_ip.ixx + _ip.iyy) > _ip.izz
      && (_ip.ixx + _ip.izz) > _ip.iyy
      && (_ip.iyy + _ip.izz) > _ip.ixx;
}

// =========================================================================
std::string FormatInertiaSDF(const InertiaParams &_ip)
{
  std::ostringstream ss;
  ss << std::scientific << std::setprecision(6);
  ss << "<inertial>\n";
  if (_ip.cogX != 0.0 || _ip.cogY != 0.0 || _ip.cogZ != 0.0)
    ss << "  <pose>" << _ip.cogX << " " << _ip.cogY << " " << _ip.cogZ
       << " 0 0 0</pose>\n";
  ss << "  <mass>" << _ip.mass << "</mass>\n";
  ss << "  <inertia>\n";
  ss << "    <ixx>" << _ip.ixx  << "</ixx>"
     << "<ixy>"  << _ip.ixy  << "</ixy>"
     << "<ixz>"  << _ip.ixz  << "</ixz>\n";
  ss << "    <iyy>" << _ip.iyy  << "</iyy>"
     << "<iyz>"  << _ip.iyz  << "</iyz>\n";
  ss << "    <izz>" << _ip.izz  << "</izz>\n";
  ss << "  </inertia>\n";
  ss << "</inertial>\n";
  return ss.str();
}

} // namespace keship
