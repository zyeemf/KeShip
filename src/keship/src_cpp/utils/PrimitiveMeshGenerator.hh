/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 基础体网格生成器 — Box/Cylinder 统一入口 */
/// ZhangYu 创建 2026.5.6

#ifndef PRIMITIVE_MESH_GENERATOR_HH_
#define PRIMITIVE_MESH_GENERATOR_HH_

#include "MeshUtils.hh"

namespace keship
{

enum class PrimitiveType { Box, Cylinder };

struct BoxParams
{
  double dx = 1.0;
  double dy = 1.0;
  double dz = 1.0;
};

struct CylinderParams
{
  double radius = 0.5;
  double length = 1.0;
  int segments = 32;
};

MeshDataSimple GeneratePrimitiveMesh(PrimitiveType _type,
                                     const BoxParams *_box = nullptr,
                                     const CylinderParams *_cyl = nullptr);

} // namespace keship

#endif // PRIMITIVE_MESH_GENERATOR_HH_
