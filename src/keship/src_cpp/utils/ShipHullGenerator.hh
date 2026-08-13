/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型生成器 v2 — 直接在 Gazebo Z_UP 坐标系中生成水密网格 */

#ifndef SHIP_HULL_GENERATOR_HH_
#define SHIP_HULL_GENERATOR_HH_

#include <string>
#include <vector>
#include <functional>

#include "MeshUtils.hh"

namespace keship {

// ═══════════════════════════════════════════════════════════
// 船型参数
// ═══════════════════════════════════════════════════════════

/// 水线面形状函数类型
enum class WaterlineType {
  PARABOLIC,   // W(ξ) = 1 - ξ^p
};

/// 横剖面形状函数类型
enum class SectionType {
  PARABOLIC,   // S(ζ) = 1 - ζ^q
  LEWIS_U,     // S(ζ) = √(1 - ζ²)
  LEWIS_V,     // S(ζ) = 1 - ζ
};

/// 统一数学船型规格
struct ShipHullSpec {
  // === 主尺度 ===
  double L  = 6.0;      // 船长 [m]
  double B  = 1.5;      // 型宽 [m]
  double D  = 1.0;      // 型深 [m]
  double rho = 500.0;   // 船体密度 [kg/m³]

  // === 水线面 ===
  WaterlineType wlType = WaterlineType::PARABOLIC;
  double wlExponent = 2.0;    // p: W(ξ) = 1-ξ^p, 经典=2

  // === 横剖面 ===
  SectionType secType = SectionType::PARABOLIC;
  double secExponent = 2.0;   // q: S(ζ)=1-ζ^q, 经典=2, 圆舭>2, 尖舭<2

  // === 网格分辨率 ===
  int visNx = 40, visNz = 20;    // 视觉网格
  int buoyNx = 20, buoyNz = 10;  // 浮力网格

  // === 工厂方法 ===
  static ShipHullSpec WigleyClassic(double L, double B, double D, double rho = 500.0);
  static ShipHullSpec WigleyFlat(double L, double B, double D, double rho = 500.0);
  static ShipHullSpec WigleySharp(double L, double B, double D, double rho = 500.0);
  static ShipHullSpec WigleyFine(double L, double B, double D, double rho = 500.0);
  static ShipHullSpec LewisU(double L, double B, double D, double rho = 500.0);
  static ShipHullSpec LewisV(double L, double B, double D, double rho = 500.0);
};

// ═══════════════════════════════════════════════════════════
// 静水力参数
// ═══════════════════════════════════════════════════════════

struct HydroProperties {
  // 设计吃水参数
  double draft = 0.0;       // 吃水 [m]
  double volume = 0.0;      // 排水体积 [m³]
  double KB = 0.0;          // 浮心垂向位置 [m]
  double BM = 0.0;          // 稳心半径 [m]
  double GM = 0.0;          // 初稳性高度 [m]
  double Aw = 0.0;          // 水线面面积 [m²]
  double Cwp = 0.0;         // 水线面面积系数
  double Ix = 0.0;          // 水线面惯性矩 [m⁴]
  double Ci = 0.0;          // 水线面惯性矩系数

  // 排水量曲线 (不同吃水)
  std::vector<double> draftCurve;       // 吃水采样值 [m]
  std::vector<double> volumeCurve;      // 对应排水体积 [m³]
};

// ═══════════════════════════════════════════════════════════
// 型值数据 (供型线图和学生编程)
// ═══════════════════════════════════════════════════════════

struct LinesData {
  std::vector<double> stations;    // 站位 x [Nx+1]
  std::vector<double> waterlines;  // 水线 z [Nz+1]
  // 半宽表 halfBreadth[stationIdx][waterlineIdx]
  std::vector<std::vector<double>> halfBreadths;
};

// ═══════════════════════════════════════════════════════════
// ShipHullGenerator
// ═══════════════════════════════════════════════════════════

/// 数学船型生成器 — Gazebo Z_UP 直接生成
///
/// 坐标系 (Gazebo Z_UP):
///   x ∈ [0, L]   — 艏向
///   y ∈ [-B/2, B/2] — 右舷正
///   z ∈ [-D, 0]  — 水线面 z=0, 龙骨 z=-D
///
/// 用法:
///   ShipHullGenerator hull(ShipHullSpec::WigleyClassic(6.0, 1.5, 1.0));
///   hull.writeVisualOBJ("/tmp/hull_vis.obj");
///   hull.writeBuoyancyOBJ("/tmp/hull_buoy.obj");
///   const auto &hp = hull.hydroProps();
///   double GM = hp.GM;
class ShipHullGenerator {
 public:
  /// 构造即完成全部计算
  explicit ShipHullGenerator(const ShipHullSpec &_spec);

  // === 网格 ===
  const MeshDataSimple &visualMesh()   const { return visMesh_; }
  const MeshDataSimple &buoyancyMesh() const { return buoyMesh_; }

  // === 文件输出 ===
  bool writeVisualOBJ(const std::string &_filepath) const;
  bool writeBuoyancyOBJ(const std::string &_filepath) const;

  // === 静水力 ===
  const HydroProperties &hydroProps() const { return hydro_; }
  double volumeAtDraft(double _draft) const;

  // === 型值 ===
  const LinesData &lines() const { return lines_; }

  // === Catto 验证 ===
  double cattoTotalVolume() const { return cattoVolume_; }

 private:
  // === 生成核心 ===
  void generate();
  void generateMesh(int _Nx, int _Nz, MeshDataSimple &_mesh);
  void computeHydroProps();
  void computeVolumeCurve();
  void computeLinesData();
  void verifyMesh(const MeshDataSimple &_mesh);

  // === 曲面函数 ===
  /// 水线面形状: W(ξ), ξ = 2x/L - 1 ∈ [-1, 1]
  double waterlineShape(double _xi) const;
  /// 横剖面形状: S(ζ), ζ ∈ [0, 1] (0=水线, 1=龙骨)
  double sectionShape(double _zeta) const;
  /// 半宽: y(x,z) = B/2 * W(ξ) * S(ζ)
  double halfBreadthFunc(double _x, double _z) const;

  // === OBJ 写入 ===
  bool writeOBJ(const MeshDataSimple &_mesh,
                const std::string &_filepath, double _cx) const;

  // === 梯形积分辅助 ===
  static double trapz(const std::vector<double> &_y, double _dx);

  ShipHullSpec spec_;
  MeshDataSimple visMesh_;
  MeshDataSimple buoyMesh_;
  HydroProperties hydro_;
  LinesData lines_;
  double cattoVolume_ = 0.0;
};

}  // namespace keship

#endif  // SHIP_HULL_GENERATOR_HH_
