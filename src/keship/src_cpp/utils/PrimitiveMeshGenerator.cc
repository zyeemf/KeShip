/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 基础体网格生成器实现 */
/// ZhangYu 创建 2026.5.6

#include "PrimitiveMeshGenerator.hh"

#include <cmath>
#include <iostream>

namespace keship
{

namespace
{

MeshDataSimple GenerateBoxMesh(const BoxParams &_p)
{
  MeshDataSimple mesh;

  const double hx = _p.dx * 0.5;
  const double hy = _p.dy * 0.5;
  const double hz = _p.dz * 0.5;

  /// 8 顶点（索引布局固定）
  mesh.vertices.push_back({-hx, -hy, -hz}); // 0
  mesh.vertices.push_back({ hx, -hy, -hz}); // 1
  mesh.vertices.push_back({ hx,  hy, -hz}); // 2
  mesh.vertices.push_back({-hx,  hy, -hz}); // 3
  mesh.vertices.push_back({-hx, -hy,  hz}); // 4
  mesh.vertices.push_back({ hx, -hy,  hz}); // 5
  mesh.vertices.push_back({ hx,  hy,  hz}); // 6
  mesh.vertices.push_back({-hx,  hy,  hz}); // 7

  /// 6 面 × 2 三角形 = 12 三角形
  auto &v = mesh.vertices;
  // -Z (bottom)
  mesh.triangles.push_back({v[0], v[2], v[1]});
  mesh.triangles.push_back({v[0], v[3], v[2]});
  // +Z (top)
  mesh.triangles.push_back({v[4], v[5], v[6]});
  mesh.triangles.push_back({v[4], v[6], v[7]});
  // -Y (front)
  mesh.triangles.push_back({v[0], v[1], v[5]});
  mesh.triangles.push_back({v[0], v[5], v[4]});
  // +Y (back)
  mesh.triangles.push_back({v[2], v[3], v[7]});
  mesh.triangles.push_back({v[2], v[7], v[6]});
  // -X (left)
  mesh.triangles.push_back({v[0], v[4], v[7]});
  mesh.triangles.push_back({v[0], v[7], v[3]});
  // +X (right)
  mesh.triangles.push_back({v[1], v[2], v[6]});
  mesh.triangles.push_back({v[1], v[6], v[5]});

  std::cout << "PrimitiveMeshGenerator: 生成 Box — "
            << _p.dx << "×" << _p.dy << "×" << _p.dz << ", "
            << mesh.vertices.size() << " 顶点, "
            << mesh.triangles.size() << " 三角形" << std::endl;

  return mesh;
}

MeshDataSimple GenerateCylinderMeshImpl(const CylinderParams &_p)
{
  MeshDataSimple mesh;

  const double halfLen = _p.length * 0.5;
  const int segs = _p.segments;

  gz::math::Vector3d topCenter(0, 0, halfLen);
  gz::math::Vector3d bottomCenter(0, 0, -halfLen);

  std::vector<gz::math::Vector3d> topVerts(segs);
  std::vector<gz::math::Vector3d> bottomVerts(segs);

  for (int i = 0; i < segs; i++)
  {
    double angle = 2.0 * M_PI * i / segs;
    double x = _p.radius * std::cos(angle);
    double y = _p.radius * std::sin(angle);
    topVerts[i] = gz::math::Vector3d(x, y, halfLen);
    bottomVerts[i] = gz::math::Vector3d(x, y, -halfLen);
  }

  mesh.vertices.push_back(topCenter);     // 0
  mesh.vertices.push_back(bottomCenter);  // 1
  for (int i = 0; i < segs; i++)
    mesh.vertices.push_back(topVerts[i]);     // 2..2+segs-1
  for (int i = 0; i < segs; i++)
    mesh.vertices.push_back(bottomVerts[i]);  // 2+segs..2+2*segs-1

  auto topIdx = [](int i) -> unsigned { return 2 + i; };
  auto bottomIdx = [segs](int i) -> unsigned { return 2 + segs + i; };

  auto &v = mesh.vertices;
  for (int i = 0; i < segs; i++)
    mesh.triangles.push_back({v[0], v[topIdx(i)], v[topIdx((i + 1) % segs)]});
  for (int i = 0; i < segs; i++)
    mesh.triangles.push_back({v[1], v[bottomIdx((i + 1) % segs)], v[bottomIdx(i)]});
  for (int i = 0; i < segs; i++)
  {
    mesh.triangles.push_back({v[topIdx(i)], v[bottomIdx(i)], v[topIdx((i + 1) % segs)]});
    mesh.triangles.push_back({v[bottomIdx(i)], v[bottomIdx((i + 1) % segs)], v[topIdx((i + 1) % segs)]});
  }

  std::cout << "PrimitiveMeshGenerator: 生成 Cylinder — "
            << "r=" << _p.radius << ", l=" << _p.length << ", segs=" << segs << ", "
            << mesh.vertices.size() << " 顶点, "
            << mesh.triangles.size() << " 三角形" << std::endl;

  return mesh;
}

} // anonymous namespace

MeshDataSimple GeneratePrimitiveMesh(PrimitiveType _type,
                                     const BoxParams *_box,
                                     const CylinderParams *_cyl)
{
  switch (_type)
  {
  case PrimitiveType::Box:
    if (!_box)
    {
      std::cerr << "PrimitiveMeshGenerator: Box 类型需要 BoxParams" << std::endl;
      return {};
    }
    return GenerateBoxMesh(*_box);
  case PrimitiveType::Cylinder:
    if (!_cyl)
    {
      std::cerr << "PrimitiveMeshGenerator: Cylinder 类型需要 CylinderParams" << std::endl;
      return {};
    }
    return GenerateCylinderMeshImpl(*_cyl);
  }
  return {};
}

} // namespace keship
