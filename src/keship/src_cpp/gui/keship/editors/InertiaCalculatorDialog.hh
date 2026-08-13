/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 惯性计算工具对话框 — 加载 mesh 文件，自动计算惯量参数并预览 */
/// ZhangYu 创建 2026.7.18

#ifndef INERTIA_CALCULATOR_DIALOG_HH_
#define INERTIA_CALCULATOR_DIALOG_HH_

#include <QDialog>

#include "MeshUtils.hh"
#include "MeshInertiaCalculator.hh"

class QLineEdit;
class QLabel;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QGroupBox;
class Buoyancy3DView;

class InertiaCalculatorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit InertiaCalculatorDialog(QWidget *_parent = nullptr);
  ~InertiaCalculatorDialog() override;

private slots:
  void onBrowseFile();
  void onDensityPreset(int _idx);
  void onMethodChanged(int _idx);
  void onCalculate();
  void onCopySDF();

private:
  void loadMeshFile(const std::string &_path);
  void showResult(const keship::InertiaCalcResult &_r);
  void clearResult();
  std::string sciStr(double _v) const;

  // --- 文件选择 ---
  QLineEdit    *fileEdit_   = nullptr;
  QPushButton  *browseBtn_  = nullptr;
  QLabel       *meshInfo_   = nullptr;

  // --- 密度 ---
  QComboBox       *materialCombo_ = nullptr;
  QDoubleSpinBox  *densitySpin_   = nullptr;

  // --- 算法 ---
  QComboBox       *methodCombo_   = nullptr;

  // --- 3D 预览 ---
  Buoyancy3DView *preview_ = nullptr;

  // --- 按钮 ---
  QPushButton *calcBtn_ = nullptr;
  QPushButton *copyBtn_ = nullptr;

  // --- 结果 ---
  QGroupBox *resultGroup_ = nullptr;
  QLabel    *volumeLabel_ = nullptr;
  QLabel    *massLabel_   = nullptr;
  QLabel    *cogLabel_    = nullptr;
  QLabel    *tensorLabel_ = nullptr;
  QLabel    *statusLabel_ = nullptr;

  // --- 状态 ---
  keship::MeshDataSimple  loadedMesh_;
  keship::InertiaCalcResult lastResult_;
  std::string             meshPath_;
  double                  lastDensity_ = 1000.0;
};

#endif // INERTIA_CALCULATOR_DIALOG_HH_
