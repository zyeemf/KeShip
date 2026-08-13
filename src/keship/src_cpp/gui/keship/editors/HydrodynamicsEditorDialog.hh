/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 流体动力学编辑器对话框 — 6-DOF Fossen 系数配置 */
/// ZhangYu 创建 2026.5.11
/// ZhaoYu 重构 2026.7.15: 6×6 M_A 对称矩阵 + DampingTerm 列表 + D 矩阵颜色预览

#ifndef HYDRODYNAMICS_EDITOR_DIALOG_HH_
#define HYDRODYNAMICS_EDITOR_DIALOG_HH_

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QWidget>
#include <string>
#include <vector>
#include <array>

#include "ModelManager.hh"

class HydrodynamicsEditorDialog : public QDialog
{
  Q_OBJECT

public:
  HydrodynamicsEditorDialog(const std::string &_modelName,
                            const std::string &_linkName,
                            QWidget *parent = nullptr);
  ~HydrodynamicsEditorDialog() override;

  keship::HydroConfig getConfig() const;
  void setConfig(const keship::HydroConfig &_config);

  static keship::HydroConfig cylinderDefaults(double _mass, double _radius, double _length);

  /// \brief 圆柱附加质量 M_A（6×6 行主序，物理公式）：
  /// 径向平移 1.0·ρV（无限长圆柱 C_a=1.0 精确）、轴向 0.02·ρV（细长体小）、
  /// 绕横轴转动 0.25·ρV·L²/12、绕轴自转 0.02·ρV·r²（单位 kg·m²，
  /// 与 DART fluid_added_mass 语义一致）。fossen/pose-based 共用。
  static std::array<double, 36> cylinderAddedMass(double _mass, double _radius,
                                                  double _length, double _rho = 1000.0);

private slots:
  void updateSymmetric();
  void rebuildTermList();
  void updateDMatrix();
  void addDampingTerm();
  void deleteDampingTerm(int _idx);
  void onTermChanged();
  void onMatrixCellClicked(int _row, int _col);

private:
  std::string modelName_;
  std::string linkName_;

  QDoubleSpinBox *amSpins_[36];

  std::vector<keship::DampingTerm> terms_;
  struct TermWidget {
    QWidget *row;
    QLabel *formula;
    QDoubleSpinBox *coeff;
    QLabel *color;
    QWidget *factorsPanel;
  };
  std::vector<TermWidget> termWidgets_;
  QWidget *termListWidget_;
  class DMatrixPreview *dMatrix_;

  void setupTermsFromConfig(const keship::HydroConfig &_cfg);
  std::string formatTermFormula(int _idx) const;
};

#endif
