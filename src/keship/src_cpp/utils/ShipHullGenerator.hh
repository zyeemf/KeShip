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

  // ═══ ★ 缺陷 ⑩ 修复（2026-09-20）：**设计吃水处**的正确静水力 ═══
  // 上面 6 个字段的口径是错的（Aw/Ix 取**甲板**水线面；volume/KB 按**全深度**积分；
  // GM 把"距甲板深度"当"距龙骨高度"用）—— 但 **volume 被 4 处 `m = rho*volume`
  // 当作"均质船体体积"依赖**，故**不能就地改**。改为新增独立字段：
  // **显示与实验测量用下面这些；质量仍用 volume。** 详见 doc/plans/headless_ctl_plan.md §26。
  double volumeD = 0.0;   ///< 设计吃水处排水体积 [m³]（= volumeAtDraft(draft)，与质量自洽）
  double KBup    = 0.0;   ///< 浮心距**龙骨**高度 [m]（口径：自龙骨向上积分）
  double BMd     = 0.0;   ///< 设计吃水处稳心半径 [m]（= IxD / volumeD）
  double GMd     = 0.0;   ///< 设计吃水处初稳性高 [m]（= KBup + BMd − D/2）
  double AwD     = 0.0;   ///< 设计吃水处水线面面积 [m²]
  double IxD     = 0.0;   ///< 设计吃水处水线面惯性矩 [m⁴]

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

  /// 网格内容指纹（8 位十六进制）—— 供**内容寻址**的 OBJ 文件名使用。
  ///
  /// 为什么必须有它（2026-09-19 张老师实测："改了船长，生成的模型没变化"）：
  /// gz 的 `gz::common::MeshManager` 按**文件路径**缓存网格，同一路径换内容后
  /// 仍返回旧网格（进程内缓存、永不失效，GUI 进程常驻 → 永远看不到新船）。
  /// 实验对话框原先把船体网格写到固定文件名（`hull_vis.obj` / `hull_buoy.obj`），
  /// 于是"改参数 → 重新生成"后，界面上显示的仍是第一艘船。
  ///
  /// 把指纹写进文件名即得**内容寻址**：几何一变 → URI 就变 → 缓存必然未命中；
  /// 几何相同 → 文件名相同 → 命中缓存是正确的（内容本就一致）。
  std::string meshTag() const;

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
