/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 圆柱体多面体网格生成器 */
/// ZhangYu 创建 2026.5.5

#ifndef CYLINDER_MESH_GENERATOR_HH_
#define CYLINDER_MESH_GENERATOR_HH_

#include <string>
#include <vector>

#include "MeshUtils.hh"

namespace keship
{

/// @brief 生成圆柱体多面体网格（三角形面片）
/// @param _radius 半径 (m)
/// @param _length 长度 (m)，圆柱体沿Z轴放置，-length/2 到 +length/2
/// @param _segments 圆周分段数（越大越接近真圆，默认32）
/// @return 包含顶点和三角形面片的网格数据
MeshDataSimple GenerateCylinderMesh(double _radius, double _length, int _segments = 32);

/// @brief 直接生成圆柱体OBJ文件（无需中间MeshDataSimple）
/// @param _radius 半径 (m)
/// @param _length 长度 (m)
/// @param _segments 圆周分段数
/// @param _filePath 输出OBJ文件路径
/// @return 成功返回true
bool GenerateCylinderObjFile(double _radius, double _length, int _segments, const std::string &_filePath);

/// @brief 将MeshDataSimple写入OBJ文件
/// @param _mesh 网格数据
/// @param _filePath 输出文件路径
/// @return 成功返回true
bool WriteMeshToObjFile(const MeshDataSimple &_mesh, const std::string &_filePath);

} // namespace keship

#endif // CYLINDER_MESH_GENERATOR_HH_
