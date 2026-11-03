/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-04 方向稳定性与 Z 形试验 — 对话框（**薄壳**）
 *
 * ⚠ 本文件**只做界面**；逻辑全部委托 `keship::ZigzagExperimentCore`
 *   （与无头入口 `keship_ctl exp ms04-run` 共用同一份代码）。
 */

#ifndef ZIGZAG_DIALOG_HH_
#define ZIGZAG_DIALOG_HH_

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

#include "ZigzagExperimentCore.hh"

/// 航向时程图（自绘）
class ZigzagChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit ZigzagChartWidget(QWidget *_parent = nullptr);
  void setTrail(const std::vector<keship::ZigzagSample> &_pts, double _targetYaw);
  void clearTrail();
protected:
  void paintEvent(QPaintEvent *_event) override;
private:
  std::vector<keship::ZigzagSample> pts_;
  double targetYaw_ = 20.0;
};

class ZigzagDialog : public QDialog {
  Q_OBJECT
public:
  ZigzagDialog(gz::transport::Node &_transportNode,
               const std::string &_worldName,
               const QStringList &_models,
               QWidget *_parent = nullptr);
  ~ZigzagDialog() override;

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

  QComboBox      *modelCombo_    = nullptr;
  QLineEdit      *leftNameEdit_  = nullptr;
  QLineEdit      *rightNameEdit_ = nullptr;
  QDoubleSpinBox *baseSpin_      = nullptr;
  QDoubleSpinBox *deltaSpin_     = nullptr;
  QDoubleSpinBox *targetYawSpin_ = nullptr;
  QDoubleSpinBox *periodSpin_    = nullptr;

  QPushButton *startBtn_ = nullptr;
  QPushButton *stopBtn_  = nullptr;
  QPushButton *clearBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  ZigzagChartWidget *chart_ = nullptr;
  QLabel *readoutLabel_ = nullptr;
  QLabel *resultLabel_  = nullptr;

  QTimer *timer_ = nullptr;

  std::unique_ptr<keship::ZigzagExperimentCore> core_;
};

#endif  // ZIGZAG_DIALOG_HH_
