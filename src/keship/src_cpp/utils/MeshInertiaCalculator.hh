/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 惯性计算引擎 — 四面体分解法，基于 Mirtich 1996 / Eberly 2003
 * 纯离线计算，零外部依赖（复用 gz-math + MeshUtils）
 */
/// ZhangYu 创建 2026.7.18

#ifndef MESH_INERTIA_CALCULATOR_HH_
#define MESH_INERTIA_CALCULATOR_HH_

#include <string>

#include "MeshUtils.hh"
#include "ModelManager.hh"

namespace keship
{

enum class InertiaCalcMethod
{
  Direct,       // 直接四面体分解（需封闭水密网格）
  ConvexHull,   // 先 Quickhull 3D 凸包再分解（非水密网格推荐）
  VHACD         // V-HACD 凸分解合并（Phase 2 预留）
};

enum class InertiaCalcError
{
  Ok,
  MeshEmpty,
  VolumeNegative,
  MassZero,
  TensorInvalid,
  HullFailed
};

struct InertiaCalcResult
{
  InertiaCalcError error = InertiaCalcError::Ok;
  std::string message;
  InertiaParams params;
  double volume = 0.0;
  InertiaCalcMethod method = InertiaCalcMethod::Direct;
  int hullVertexCount = 0;
  int hullFaceCount = 0;
};

/// @brief 从三角形网格计算惯性参数
/// @param _mesh    输入网格
/// @param _density 材料密度 (kg/m³)
/// @param _method  计算方法
/// @return 计算结果（含错误码）
InertiaCalcResult ComputeInertiaFromMesh(const MeshDataSimple &_mesh,
                                          double _density,
                                          InertiaCalcMethod _method = InertiaCalcMethod::Direct);

/// @brief 计算三角形网格的 3D 凸包（Quickhull 算法）
/// @param _mesh 输入网格
/// @return 凸包网格（封闭流形），失败时 triangles 为空
MeshDataSimple ComputeConvexHull3D(const MeshDataSimple &_mesh);

/// @brief 验证惯量张量是否满足三角不等式
/// @return true 如果全部 3 个不等式满足
bool ValidateInertiaTensor(const InertiaParams &_ip);

/// @brief 将 InertiaParams 格式化为 SDF <inertial> 片段
std::string FormatInertiaSDF(const InertiaParams &_ip);

} // namespace keship

#endif // MESH_INERTIA_CALCULATOR_HH_
