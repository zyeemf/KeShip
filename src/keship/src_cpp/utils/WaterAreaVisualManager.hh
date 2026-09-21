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

  /// \brief 某水域的网格规模统计（2026-09-21 新增）
  ///
  /// **不写文件、不建模型**，只跑一遍网格生成并统计 —— 供探针/自检使用，
  /// 也便于将来对"顶点数超阈值"告警。复用 `GeneratePolygonMesh`，不复制逻辑。
  struct MeshStats
  {
    size_t samplesX = 0;   ///< 该轴采样点数
    size_t samplesY = 0;
    size_t vertices = 0;   ///< 多边形裁剪后的实际顶点数
    size_t triangles = 0;
  };
  MeshStats ComputeMeshStats(const WaterArea &_area) const;

  /// \brief 网格 LOD 档位（2026-09-21 新增）
  /// radiusFrac：该档覆盖到"距水域中心的半径 / 半跨"，0~1；
  /// stepMul   ：该档步长相对基准步长（hFine）的倍数。
  /// minRadius / maxRadius：该档半径的**绝对下限 / 上限**（米），≤0 表示不限制。
  /// - `maxRadius` 抑制"大水域时中心细区被比例撑得过大"
  ///   （±1000 m 下 radiusFrac=0.15 会得到 150 m 的细区）；
  /// - `minRadius` 防止"小水域时中心细区被比例压得太小"
  ///   —— 100 m 水域下 0.15×50 = 7.5 m 的细区只有 3 个采样点，
  ///   步长被迫拉大 ⇒ 波浪几何完全显示不出来（2026-09-21 边界探针发现）。
  struct LodBand
  {
    double radiusFrac;
    double stepMul;
    double minRadius = -1.0;
    double maxRadius = -1.0;
  };

  /// \brief 构造沿单轴的**非均匀**采样坐标：中心密、外围疏（LOD）。
  ///
  /// 保留**规则拓扑**（只是顶点位置非均匀）⇒ 相邻格共享顶点、**无 T-junction 裂缝**，
  /// 三角形拼接逻辑完全不用改。
  ///
  /// \param[in] _lo     轴的下界（世界坐标）
  /// \param[in] _hi     轴的上界
  /// \param[in] _center 分级中心（取水域几何中心）
  /// \param[in] _hFine  中心区基准步长
  /// \param[in] _bands  档位表（按 radiusFrac 升序；首档步长恒为 _hFine）
  /// \return 升序坐标数组，长度 = 该轴采样点数（格数 = 点数 − 1）
  static std::vector<double> BuildAxisSamples(double _lo, double _hi,
      double _center, double _hFine, const std::vector<LodBand> &_bands);

private:
  std::set<std::string> spawnedNames_;
  std::string shaderDir_;
  std::string tempDir_ = KeshipWavesDir();
  mutable int meshGenerationId_ = 0;

  /// 射线法：判断点是否在多边形内部
  static bool PointInPolygon(double x, double y,
      const std::vector<std::pair<double,double>>& verts);

  /// 生成贴合多边形的三角网格。
  ///
  /// `_samplesX/_samplesY`（可选出参）回传**实际采用**的采样点数 ——
  /// 必须由本函数回传，不能让调用方自己重算：本函数内部有"总点数上限"的
  /// **循环夹紧**，重算会得到与真实生成不一致的数字（2026-09-21 踩到）。
  MeshDataSimple GeneratePolygonMesh(const WaterArea &_area,
      size_t *_samplesX = nullptr, size_t *_samplesY = nullptr) const;

  /// 中心区基准步长（由 PMS 最高显著分量波长反推，clamp 到 [0.5, 5.0]）。
  /// 抽成独立函数以便 `GeneratePolygonMesh` 与 `ComputeMeshStats` **共用同一份逻辑**。
  static double ComputeBaseStep(const WaveParams &_wp);

  /// 网格 LOD 档位表（唯一来源，两处共用）。
  static const std::vector<LodBand> &LodBands();

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
