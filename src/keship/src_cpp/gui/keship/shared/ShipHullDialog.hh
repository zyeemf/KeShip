/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型参数输入对话框 — Phase B */

#ifndef SHIP_HULL_DIALOG_HH_
#define SHIP_HULL_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>

#include "ShipHullGenerator.hh"

/// 数学船型参数输入对话框（非模态）
/// 选择船型 + 输入 L/B/D/ρ → 预览静水力 → "生成模型"发信号 → 外部 spawn
class ShipHullDialog : public QDialog {
  Q_OBJECT
 public:
  ShipHullDialog(QWidget *_parent = nullptr);
  ~ShipHullDialog() override = default;

 signals:
  /// 用户点击"生成模型"时发出，由外部负责 spawn
  void spawnRequested(keship::ShipHullSpec spec, std::string name);

 private slots:
  void onTypeChanged();
  void onParamChanged();
  void onShowLinesPlan();
  void onSpawn();

 private:
  void setupUI();
  void updateHydroDisplay();
  std::string modelName() const;

  QComboBox *typeCombo_;
  QDoubleSpinBox *lengthSpin_, *breadthSpin_, *depthSpin_;
  QDoubleSpinBox *densitySpin_;

  QLabel *awLabel_, *cwpLabel_, *ciLabel_;
  QLabel *volLabel_, *kbLabel_, *bmLabel_, *gmLabel_;
  QLabel *draftLabel_;

  QPushButton *spawnBtn_, *closeBtn_;

  keship::ShipHullSpec spec_;
};

#endif  // SHIP_HULL_DIALOG_HH_
