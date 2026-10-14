/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 — 对话框（**薄壳**）
 *
 * ⚠ 本文件**只做界面**：参数输入、按钮、显示。
 *   发推力 / 采样 / 指标计算**全部委托** `keship::TurningExperimentCore` ——
 *   与无头入口 `keship_ctl exp ms01-run` **共用同一份实现**（2026-10-14 张老师确认的设计原则）。
 */

#ifndef TURNING_CIRCLE_DIALOG_HH_
#define TURNING_CIRCLE_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QPaintEvent>

#include <memory>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>

#include "TurningExperimentCore.hh"

/// 回转轨迹图（自绘，x-y 平面）
class TurningChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit TurningChartWidget(QWidget *_parent = nullptr);
  void setTrail(const std::vector<keship::TurningSample> &_pts);
  void clearTrail();
protected:
  void paintEvent(QPaintEvent *_event) override;
private:
  std::vector<keship::TurningSample> pts_;
  double xMin_ = -10, xMax_ = 10, yMin_ = -10, yMax_ = 10;
};

class TurningCircleDialog : public QDialog {
  Q_OBJECT
public:
  TurningCircleDialog(gz::transport::Node &_transportNode,
                      const std::string &_worldName,
                      const QStringList &_models,
                      QWidget *_parent = nullptr);
  ~TurningCircleDialog() override;

private slots:
  void onStart();
  void onStop();
  void onClear();
  void onTick();
  void onExport();

private:
  void syncParamsToCore();
  void updateReadout();
  void showResults();

  // --- 参数 ---
  QComboBox      *modelCombo_    = nullptr;
  QLineEdit      *leftNameEdit_  = nullptr;
  QLineEdit      *rightNameEdit_ = nullptr;
  QDoubleSpinBox *leftThrustSpin_  = nullptr;
  QDoubleSpinBox *rightThrustSpin_ = nullptr;
  QDoubleSpinBox *periodSpin_    = nullptr;

  // --- 控制 ---
  QPushButton *startBtn_  = nullptr;
  QPushButton *stopBtn_   = nullptr;
  QPushButton *clearBtn_  = nullptr;
  QPushButton *exportBtn_ = nullptr;

  // --- 显示 ---
  TurningChartWidget *chart_ = nullptr;
  QLabel *readoutLabel_ = nullptr;
  QLabel *resultLabel_  = nullptr;

  QTimer *timer_ = nullptr;

  /// ★ 核心层（GUI 与无头共用）
  std::unique_ptr<keship::TurningExperimentCore> core_;
};

#endif  // TURNING_CIRCLE_DIALOG_HH_
