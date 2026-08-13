/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 多片水域数据结构 */

#ifndef KESHIP_WATER_AREA_HH_
#define KESHIP_WATER_AREA_HH_

#include <string>
#include <vector>
#include <utility>

namespace keship
{

  /// \brief 2D 模型渲染方案
  enum class ModelRenderScheme : int
  {
    Rect = 0,         // 矩形包围盒
    ConvexHull,       // 凸包（Graham Scan）
    AlphaShape,       // 凹包（Delaunay + Alpha filter）
    SubMeshHulls,     // 分组件凸包（Assimp sub-mesh 分解）
    ProjectedImage    // 三角形投影填充图像
  };
  /// \brief 波浪参数存储（纯数据，不依赖 Wavefield/Gazebo）
  struct WaveParams
  {
    std::string model = "PMS";
    double period     = 5.0;
    double amplitude  = 0.0;
    double direction  = 0.0;
    double angle      = 0.4;
    double scale      = 1.1;   // PMS 显著带内频率采样，避免高频分量欠采样（2026-08）
    double steepness  = 0.0;
    double tau        = 2.0;
    double gain       = 0.05;
    int    number     = 6;     // 默认 6 组分波（≤8，2026-08）
  };

  /// \brief 风参数
  struct WindParams
  {
    double speed     = 0.0;   // m/s
    double direction = 0.0;   // rad, 0 = 世界 +Y (北向)
  };

  /// \brief 流参数
  struct CurrentParams
  {
    double speed     = 0.0;   // m/s (表层)
    double direction = 0.0;   // rad
  };

  /// \brief 单片水域定义
  struct WaterArea
  {
    /// \brief 水域名称
    std::string name;

    /// \brief XY 平面矩形范围
    double xMin = 0.0, xMax = 0.0, yMin = 0.0, yMax = 0.0;

    /// \brief 静水面 Z 坐标
    double baseHeight = 0.0;

    /// \brief 该水域的波浪参数
    WaveParams waveParams;

    /// \brief 该水域的风参数
    WindParams windParams;

    /// \brief 该水域的流参数
    CurrentParams currentParams;

    /// \brief 浪向是否自动跟随风向
    bool waveDirectionAuto = true;

    /// \brief 多边形顶点（空=矩形模式）
    std::vector<std::pair<double,double>> vertices;

    /// \brief 判断点 (x, y) 是否在此水域范围内
    /// 包围盒快速拒绝 + 多边形射线法（vertices ≥3 时按真实多边形判定，
    /// 修复"三角形水域包围盒内、多边形外"误判为在水域内的问题）；
    /// vertices 为空（纯矩形）时保持原包围盒语义
    bool Contains(double x, double y) const
    {
      if (!(x >= xMin && x <= xMax && y >= yMin && y <= yMax))
        return false;
      if (vertices.size() < 3)
        return true;
      bool inside = false;
      const size_t n = vertices.size();
      for (size_t i = 0, j = n - 1; i < n; j = i++)
      {
        const double xi = vertices[i].first,  yi = vertices[i].second;
        const double xj = vertices[j].first,  yj = vertices[j].second;
        if (((yi > y) != (yj > y)) &&
            (x < (xj - xi) * (y - yi) / (yj - yi) + xi))
          inside = !inside;
      }
      return inside;
    }
  };

  /// \brief 模型的 2D 足迹，供 MapWidget 渲染场景参考
  struct ModelFootprint
  {
    std::string name;
    double worldX = 0;
    double worldY = 0;
    double worldYaw = 0;
    double localW = 0;
    double localH = 0;
    double localXMin = 0;
    double localYMin = 0;
    bool isStatic = false;

    std::vector<std::pair<double,double>> convexOutline;
    std::vector<std::pair<double,double>> alphaOutline;
    std::vector<std::vector<std::pair<double,double>>> subOutlines;

    std::vector<unsigned char> previewData;
    int previewW = 0;
    int previewH = 0;
  };

} // namespace keship

#endif
