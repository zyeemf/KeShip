/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-06 代理几何体建模与简化 对话框 — 对同一模型三档代理（Mesh/ConvexHull/None）
 * 并排对比：顶点/面数 · 体积/排水浮力 · 惯量差异；可切换 3D 预览档位 */
/// KeShip Team 创建 2026.10.15

#ifndef PROXY_GEOMETRY_DIALOG_HH_
#define PROXY_GEOMETRY_DIALOG_HH_

#include <QDialog>
#include <string>
#include <vector>

#include "MeshUtils.hh"
#include "MeshInertiaCalculator.hh"

class QLineEdit;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QGroupBox;
class Buoyancy3DView;

/// @brief SD-06：代理几何体三档对比
///
/// 平台已有 `MeshUtils::GenerateConvexHullCache()`（qhull 3D 凸包）与
/// `ModelManager::CollisionMode{Mesh, ConvexHull, None}`，本对话框补齐
/// "**同一模型三档并排对比**"的教学面板：
///   - **顶点数 / 三角面数** —— 几何复杂度（碰撞检测成本随面数增长）
///   - **体积 / 排水浮力** —— 代理几何体对浮力的影响（凸包对凹形船体会高估）
///   - **惯量 Ixx/Iyy/Izz** —— 简化对质量特性的影响
/// 数值全部由既有引擎离线计算（`ComputeInertiaFromMesh`），零新增算法。
class ProxyGeometryDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ProxyGeometryDialog(QWidget *_parent = nullptr);
  ~ProxyGeometryDialog() override;

  /// 对比表的指标行数（顶点/面数/体积/质量/浮力/Ixx/Iyy/Izz/体积偏差）
  static constexpr int kRows = 9;

private slots:
  void onBrowseFile();
  void onDensityPreset(int _idx);
  void onCompare();
  void onPreviewModeChanged(int _idx);

private:
  void loadMeshFile(const std::string &_path);
  void clearResult();
  void refreshPreview();
  static std::string fmt(double _v, int _dec = 4);

  // --- 输入 ---
  QLineEdit      *fileEdit_   = nullptr;
  QPushButton    *browseBtn_  = nullptr;
  QLabel         *meshInfo_   = nullptr;

  // --- 参数 ---
  QComboBox      *materialCombo_    = nullptr;   ///< 材料密度预设
  QDoubleSpinBox *densitySpin_      = nullptr;   ///< 材料密度 kg/m³
  QDoubleSpinBox *waterDensitySpin_ = nullptr;   ///< 水域密度 kg/m³（算浮力）

  // --- 对比表 ---
  QGroupBox *compareGroup_ = nullptr;
  QLabel    *cellMesh_[kRows] = {nullptr};   ///< 列：结构网格
  QLabel    *cellHull_[kRows] = {nullptr};   ///< 列：凸包
  QLabel    *cellNone_[kRows] = {nullptr};   ///< 列：无碰撞

  // --- 预览 ---
  QComboBox      *previewModeCombo_ = nullptr;
  QLabel         *previewHint_      = nullptr;
  Buoyancy3DView *preview_          = nullptr;

  QPushButton *compareBtn_ = nullptr;
  QLabel      *statusLabel_ = nullptr;

  // --- 状态 ---
  keship::MeshDataSimple loadedMesh_;
  std::string            srcPath_;    ///< 原始源文件（凸包缓存写入用）
  std::string            meshPath_;   ///< 规范化网格（加载/预览用）
  std::string            hullPath_;   ///< 凸包 OBJ（预览用，可为空）
  double                 lastDensity_  = 1000.0;
  double                 waterDensity_ = 1025.0;
};

#endif // PROXY_GEOMETRY_DIALOG_HH_
