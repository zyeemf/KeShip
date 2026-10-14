/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-08 传感器感知与位姿估计 — 对话框实现（薄壳） */

#include "SensorEstimationDialog.hh"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iostream>

// ══════════════════════════════════════════════════════════════════
// 估计 vs 真值 图
// ══════════════════════════════════════════════════════════════════
SensorChartWidget::SensorChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(320);
  setAutoFillBackground(true);
}

void SensorChartWidget::setTrail(const std::vector<keship::SensorSample> &_pts) {
  pts_ = _pts; update();
}
void SensorChartWidget::clearTrail() { pts_.clear(); update(); }

void SensorChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (pts_.size() < 2) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "尚无数据 — 点「开始」采集 IMU 与位姿真值");
    return;
  }

  const double tMax = pts_.back().t;
  double yMin = 0, yMax = 0;
  for (const auto &s : pts_) {
    yMin = std::min({yMin, s.estYaw, s.refYaw});
    yMax = std::max({yMax, s.estYaw, s.refYaw});
  }
  if (yMax - yMin < 1e-6) { yMax += 1; yMin -= 1; }
  const double pad = (yMax - yMin) * 0.1;
  yMin -= pad; yMax += pad;

  const double W = width() - 50.0, H = height() - 30.0;
  auto toPix = [&](double t, double y) {
    return QPointF(40 + (t / std::max(1e-6, tMax)) * W,
                   15 + H - ((y - yMin) / (yMax - yMin)) * H);
  };

  p.setPen(QColor(232, 235, 242));
  for (int i = 1; i < 5; ++i) {
    const double gy = 15 + H * i / 5.0;
    p.drawLine(QPointF(40, gy), QPointF(40 + W, gy));
  }

  auto drawCurve = [&](auto getter, const QColor &_c) {
    QPainterPath path;
    path.moveTo(toPix(pts_[0].t, getter(pts_[0])));
    for (size_t i = 1; i < pts_.size(); ++i)
      path.lineTo(toPix(pts_[i].t, getter(pts_[i])));
    p.setPen(QPen(_c, 2));
    p.drawPath(path);
  };
  drawCurve([](const keship::SensorSample &s) { return s.refYaw; }, QColor(50, 160, 90));
  drawCurve([](const keship::SensorSample &s) { return s.estYaw; }, QColor(210, 80, 70));

  p.setPen(QColor(90, 95, 105));
  p.drawText(12, 16, QString("绿=航向真值(°)  红=IMU 积分估计(°) · %1 点").arg(pts_.size()));
  p.drawText(6, 28, QString::number(yMax, 'f', 1));
  p.drawText(6, static_cast<int>(15 + H), QString::number(yMin, 'f', 1));
}

// ══════════════════════════════════════════════════════════════════
// 对话框
// ══════════════════════════════════════════════════════════════════
SensorEstimationDialog::SensorEstimationDialog(gz::transport::Node &_transportNode,
                                               const std::string &_worldName,
                                               const QStringList &_models,
                                               QWidget *_parent)
    : QDialog(_parent) {
  setWindowTitle("SC-08 传感器感知与位姿估计");
  resize(920, 660);

  core_ = std::make_unique<keship::SensorEstimationCore>(_transportNode, _worldName);

  auto *root = new QVBoxLayout(this);
  auto *paramBox = new QGroupBox("传感器与估计参数", this);
  auto *form = new QFormLayout(paramBox);

  modelCombo_ = new QComboBox(paramBox);
  modelCombo_->addItems(_models.isEmpty() ? QStringList{"GT1"} : _models);
  form->addRow("船模：", modelCombo_);

  imuTopicEdit_ = new QLineEdit(paramBox);
  imuTopicEdit_->setPlaceholderText("留空 = 自动构造 /world/<世界>/model/<模型>/link/base_link/sensor/IMU/imu");
  form->addRow("IMU 话题：", imuTopicEdit_);

  periodSpin_ = new QDoubleSpinBox(paramBox);
  periodSpin_->setRange(0.02, 1.0); periodSpin_->setValue(0.05);
  periodSpin_->setSingleStep(0.01); periodSpin_->setSuffix(" s");
  form->addRow("采样周期：", periodSpin_);
  root->addWidget(paramBox);

  auto *ctrlRow = new QHBoxLayout();
  startBtn_ = new QPushButton("开始采集", this);
  stopBtn_ = new QPushButton("停止", this);
  clearBtn_ = new QPushButton("清零", this);
  exportBtn_ = new QPushButton("导出 CSV", this);
  stopBtn_->setEnabled(false);
  ctrlRow->addWidget(startBtn_); ctrlRow->addWidget(stopBtn_); ctrlRow->addWidget(clearBtn_);
  ctrlRow->addStretch(); ctrlRow->addWidget(exportBtn_);
  root->addLayout(ctrlRow);

  readoutLabel_ = new QLabel("t = —   估计航向 = —   真值航向 = —   IMU 消息 = —", this);
  resultLabel_ = new QLabel("RMSE(横摇) = —   RMSE(纵摇) = —   RMSE(航向) = —   最大漂移 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  chart_ = new SensorChartWidget(this);
  root->addWidget(chart_, 1);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &SensorEstimationDialog::onTick);
  connect(startBtn_, &QPushButton::clicked, this, &SensorEstimationDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &SensorEstimationDialog::onStop);
  connect(clearBtn_, &QPushButton::clicked, this, &SensorEstimationDialog::onClear);
  connect(exportBtn_, &QPushButton::clicked, this, &SensorEstimationDialog::onExport);

  std::cout << "SC-08: 传感器位姿估计对话框已创建（核心层 SensorEstimationCore）" << std::endl;
}

SensorEstimationDialog::~SensorEstimationDialog() = default;

void SensorEstimationDialog::syncParamsToCore() {
  core_->setTarget(modelCombo_->currentText().toStdString(),
                   imuTopicEdit_->text().trimmed().toStdString());
  core_->setSamplePeriod(periodSpin_->value());
}

void SensorEstimationDialog::onStart() {
  if (core_->running()) return;
  syncParamsToCore();
  if (!core_->start()) {
    QMessageBox::warning(this, "SC-08",
        "未能读到船模位姿 —— 请确认「船模」名与场景一致，且仿真已启动。");
    return;
  }
  chart_->clearTrail();
  startBtn_->setEnabled(false);
  stopBtn_->setEnabled(true);
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
  std::cout << "SC-08: 开始采集（IMU 话题 " << imuTopicEdit_->text().toStdString()
            << "）" << std::endl;
}

void SensorEstimationDialog::onStop() {
  if (!core_->running()) return;
  core_->stop();
  timer_->stop();
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  showResults();
}

void SensorEstimationDialog::onClear() {
  core_->clear();
  chart_->clearTrail();
  readoutLabel_->setText("t = —   估计航向 = —   真值航向 = —   IMU 消息 = —");
}

void SensorEstimationDialog::onTick() {
  if (!core_->sample()) return;
  chart_->setTrail(core_->trail());
  updateReadout();
}

void SensorEstimationDialog::updateReadout() {
  const auto &t = core_->trail();
  if (t.empty()) return;
  const auto &s = t.back();
  readoutLabel_->setText(QString("t = %1 s   估计航向 = %2°   真值航向 = %3°   IMU 消息 = %4")
      .arg(s.t, 0, 'f', 1).arg(s.estYaw, 0, 'f', 2).arg(s.refYaw, 0, 'f', 2)
      .arg(core_->imuCount()));
}

void SensorEstimationDialog::showResults() {
  const auto m = core_->metrics();
  auto fmt = [](double v) { return v < 0 ? QString("—") : QString::number(v, 'f', 3); };
  if (!m.valid) { resultLabel_->setText("数据不足"); return; }
  resultLabel_->setText(QString("RMSE(横摇) = %1°   RMSE(纵摇) = %2°   RMSE(航向) = %3°   最大漂移 = %4°")
      .arg(fmt(m.rmseRoll)).arg(fmt(m.rmsePitch)).arg(fmt(m.rmseYaw)).arg(fmt(m.maxDriftYaw)));
}

void SensorEstimationDialog::onExport() {
  if (core_->trail().empty()) {
    QMessageBox::information(this, "导出", "尚无数据。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出估计对比 CSV",
      "sc08_estimation.csv", "CSV (*.csv)");
  if (path.isEmpty()) return;
  if (!core_->exportCsv(path.toStdString()))
    QMessageBox::warning(this, "导出", "无法写入文件。");
}
