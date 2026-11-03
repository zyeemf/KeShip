/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-08 传感器感知与位姿估计 — 对话框（**薄壳**）
 * 逻辑全部委托 `keship::SensorEstimationCore`（与 `keship_ctl exp sc08-run` 共用）。
 */

#ifndef SENSOR_ESTIMATION_DIALOG_HH_
#define SENSOR_ESTIMATION_DIALOG_HH_

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

#include "SensorEstimationCore.hh"

/// 估计 vs 真值 对比图（自绘）
class SensorChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit SensorChartWidget(QWidget *_parent = nullptr);
  void setTrail(const std::vector<keship::SensorSample> &_pts);
  void clearTrail();
protected:
  void paintEvent(QPaintEvent *_event) override;
private:
  std::vector<keship::SensorSample> pts_;
};

class SensorEstimationDialog : public QDialog {
  Q_OBJECT
public:
  SensorEstimationDialog(gz::transport::Node &_transportNode,
                         const std::string &_worldName,
                         const QStringList &_models,
                         QWidget *_parent = nullptr);
  ~SensorEstimationDialog() override;

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

  QComboBox      *modelCombo_ = nullptr;
  QLineEdit      *imuTopicEdit_ = nullptr;
  QDoubleSpinBox *periodSpin_ = nullptr;

  QPushButton *startBtn_ = nullptr;
  QPushButton *stopBtn_  = nullptr;
  QPushButton *clearBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  SensorChartWidget *chart_ = nullptr;
  QLabel *readoutLabel_ = nullptr;
  QLabel *resultLabel_  = nullptr;

  QTimer *timer_ = nullptr;
  std::unique_ptr<keship::SensorEstimationCore> core_;
};

#endif  // SENSOR_ESTIMATION_DIALOG_HH_
