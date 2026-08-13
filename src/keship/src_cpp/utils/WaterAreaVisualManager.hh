/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水区 Waves 模型管理器 — 动态生成 per-area 小水面 */

#ifndef KESHIP_WATER_AREA_VISUAL_MANAGER_HH_
#define KESHIP_WATER_AREA_VISUAL_MANAGER_HH_

#include <string>
#include <vector>
#include <set>

#include <gz/transport/Node.hh>

#include "keship/utils/WaterArea.hh"
#include "keship/utils/KeshipPaths.hh"
#include "MeshUtils.hh"

namespace keship
{

class WaterAreaVisualManager
{
public:
  /// 设置 shader 资源目录（在 SpawnAll 前调用一次）
  void SetShaderDir(const std::string &_dir) { shaderDir_ = _dir; }

  /// 为单水区生成 Waves 模型 SDF 字符串
  std::string MakeSurfaceSdf(const WaterArea &_area) const;

  /// 批量创建所有水区水面模型
  void SpawnAll(const std::vector<WaterArea> &_areas,
                gz::transport::Node &_node);

  /// 删除所有已创建的水面模型
  void DeleteAll(gz::transport::Node &_node);

private:
  std::set<std::string> spawnedNames_;
  std::string shaderDir_;
  std::string tempDir_ = KeshipWavesDir();
  mutable int meshGenerationId_ = 0;

  /// 射线法：判断点是否在多边形内部
  static bool PointInPolygon(double x, double y,
      const std::vector<std::pair<double,double>>& verts);

  /// 生成贴合多边形的三角网格
  MeshDataSimple GeneratePolygonMesh(const WaterArea &_area) const;

  /// 写入含 UV/法线的 OBJ 文件
  static bool WritePolygonObjFile(const MeshDataSimple &_mesh,
      const std::string &_filePath);

  /// 文件名脱敏（只保留 a-z A-Z 0-9 _ -）
  static std::string SanitizeName(const std::string &_name);

  /// 生成临时 OBJ 路径
  std::string GetTempMeshPath(const std::string &_areaName) const;
};

} // namespace keship

#endif
