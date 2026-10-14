/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-04 方向稳定性与 Z 形试验 — 对话框实现（薄壳：界面 + 委托核心层） */

#include "ZigzagDialog.hh"

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
// 航向时程图
// ══════════════════════════════════════════════════════════════════
ZigzagChartWidget::ZigzagChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(320);
  setAutoFillBackground(true);
}

void ZigzagChartWidget::setTrail(const std::vector<keship::ZigzagSample> &_pts,
                                 double _targetYaw) {
  pts_ = _pts;
  targetYaw_ = _targetYaw;
  update();
}

void ZigzagChartWidget::clearTrail() { pts_.clear(); update(); }

void ZigzagChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (pts_.size() < 2) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "尚无数据 — 点「开始」执行 Z 形试验");
    return;
  }

  double tMax = pts_.back().t;
  double yMin = pts_[0].yawDeg, yMax = pts_[0].yawDeg;
  for (const auto &s : pts_) { yMin = std::min(yMin, s.yawDeg); yMax = std::max(yMax, s.yawDeg); }
  if (yMax - yMin < 1e-6) { yMax += 1; yMin -= 1; }
  const double pad = (yMax - yMin) * 0.1;
  yMin -= pad; yMax += pad;

  // ── 坐标区（四周留出刻度标签空间） ──────────────────────────
  const double mL = 54, mR = 14, mT = 26, mB = 46;
  const double W = width() - mL - mR, H = height() - mT - mB;
  if (W < 60 || H < 40) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "窗口过小 — 无法绘制坐标轴");
    return;
  }
  const double tMaxD = std::max(1e-6, tMax);   // 时间轴从 0 起算
  auto toPix = [&](double t, double y) {
    return QPointF(mL + (t / tMaxD) * W,
                   mT + H - ((y - yMin) / (yMax - yMin)) * H);
  };

  // ── "好看的"刻度步长 ──────────────────────────────────────
  auto niceStep = [](double range, int target) {
    if (!(range > 0.0) || !std::isfinite(range)) return 1.0;
    const double raw = range / static_cast<double>(target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    double m = 1.0;
    if (n > 5.0) m = 10.0; else if (n > 2.0) m = 5.0; else if (n > 1.0) m = 2.0;
    return m * mag;
  };
  const double stepY = niceStep(yMax - yMin, 6);
  const double stepT = niceStep(tMax, 8);
  auto fmtTick = [](double v, double step) {
    int dec = 0;
    if (step < 1.0)
      dec = std::min(2, static_cast<int>(std::ceil(-std::log10(step))));
    return QString::number(v, 'f', dec);
  };

  QFont tickFont = p.font();
  tickFont.setPointSize(8);

  // ── 网格 + 刻度 + 标签 ────────────────────────────────────
  {
    int guard = 0;
    for (double v = std::ceil(yMin / stepY) * stepY;
         v <= yMax + 1e-9 && guard < 200; v += stepY, ++guard) {
      const double gy = toPix(0, v).y();
      p.setPen(QColor(232, 235, 242));
      p.drawLine(QPointF(mL, gy), QPointF(mL + W, gy));
      p.setPen(QColor(126, 133, 148));
      p.drawLine(QPointF(mL - 5, gy), QPointF(mL, gy));
      p.setFont(tickFont);
      p.setPen(QColor(90, 95, 105));
      p.drawText(QRectF(mL - 52, gy - 8, 46, 16),
                 Qt::AlignRight | Qt::AlignVCenter, fmtTick(v, stepY));
    }
    guard = 0;
    for (double v = 0.0; v <= tMax + 1e-9 && guard < 200; v += stepT, ++guard) {
      const double gx = toPix(v, 0).x();
      p.setPen(QColor(232, 235, 242));
      p.drawLine(QPointF(gx, mT), QPointF(gx, mT + H));
      p.setPen(QColor(126, 133, 148));
      p.drawLine(QPointF(gx, mT + H), QPointF(gx, mT + H + 5));
      p.setFont(tickFont);
      p.setPen(QColor(90, 95, 105));
      p.drawText(QRectF(gx - 44, mT + H + 6, 88, 16),
                 Qt::AlignHCenter | Qt::AlignTop, fmtTick(v, stepT));
    }
  }

  // ── 坐标轴外框 + 轴标题 ───────────────────────────────────
  p.setPen(QPen(QColor(120, 128, 142), 1));
  p.drawRect(QRectF(mL, mT, W, H).adjusted(0, 0, -1, -1));
  p.setFont(tickFont);
  p.setPen(QColor(70, 76, 88));
  p.drawText(QRectF(mL + W - 74, mT - 15, 72, 14),
             Qt::AlignRight | Qt::AlignBottom, "航向 (°)");
  p.drawText(QRectF(mL, mT + H + 23, W, 14),
             Qt::AlignHCenter | Qt::AlignTop, "t (s)");

  QPainterPath path;
  path.moveTo(toPix(pts_[0].t, pts_[0].yawDeg));
  for (size_t i = 1; i < pts_.size(); ++i)
    path.lineTo(toPix(pts_[i].t, pts_[i].yawDeg));
  p.setPen(QPen(QColor(60, 110, 200), 2));
  p.drawPath(path);

  // 换向点（phase 变化）标红
  p.setBrush(QColor(210, 80, 70));
  p.setPen(Qt::NoPen);
  for (size_t i = 1; i < pts_.size(); ++i) {
    if (pts_[i].phase != pts_[i - 1].phase)
      p.drawEllipse(toPix(pts_[i].t, pts_[i].yawDeg), 4, 4);
  }

  p.setPen(QColor(90, 95, 105));
  p.drawText(10, 12, QString("航向时程 · %1 点 · 换向点标红").arg(pts_.size()));
  p.setFont(tickFont);
  p.drawText(QRectF(width() - 196, 12, 186, 14),
             Qt::AlignRight | Qt::AlignTop,
             QString("航向范围 %1 ~ %2 °")
                 .arg(yMin, 0, 'f', 1).arg(yMax, 0, 'f', 1));
}

// ══════════════════════════════════════════════════════════════════
// 对话框
// ══════════════════════════════════════════════════════════════════
ZigzagDialog::ZigzagDialog(gz::transport::Node &_transportNode,
                           const std::string &_worldName,
                           const QStringList &_models,
                           QWidget *_parent)
    : QDialog(_parent) {
  setWindowTitle("MS-04 方向稳定性与 Z 形试验");
  resize(900, 640);

  core_ = std::make_unique<keship::ZigzagExperimentCore>(_transportNode, _worldName);

  auto *root = new QVBoxLayout(this);
  auto *paramBox = new QGroupBox("Z 形试验参数（⚠ 平台不含舵：差动推力替代舵角）", this);
  auto *form = new QFormLayout(paramBox);

  modelCombo_ = new QComboBox(paramBox);
  modelCombo_->addItems(_models.isEmpty() ? QStringList{"GT1"} : _models);
  form->addRow("船模：", modelCombo_);

  leftNameEdit_ = new QLineEdit("thrust_left", paramBox);
  rightNameEdit_ = new QLineEdit("thrust_right", paramBox);
  form->addRow("左推进器名：", leftNameEdit_);
  form->addRow("右推进器名：", rightNameEdit_);

  baseSpin_ = new QDoubleSpinBox(paramBox);
  baseSpin_->setRange(0, 2000); baseSpin_->setValue(100.0); baseSpin_->setSuffix(" N");
  deltaSpin_ = new QDoubleSpinBox(paramBox);
  deltaSpin_->setRange(0, 1000); deltaSpin_->setValue(50.0); deltaSpin_->setSuffix(" N");
  form->addRow("基础推力：", baseSpin_);
  form->addRow("差动量：", deltaSpin_);

  targetYawSpin_ = new QDoubleSpinBox(paramBox);
  targetYawSpin_->setRange(5, 45); targetYawSpin_->setValue(20.0); targetYawSpin_->setSuffix(" °");
  form->addRow("换向阈值（目标航向变化）：", targetYawSpin_);

  periodSpin_ = new QDoubleSpinBox(paramBox);
  periodSpin_->setRange(0.02, 2.0); periodSpin_->setValue(0.1);
  periodSpin_->setSingleStep(0.02); periodSpin_->setSuffix(" s");
  form->addRow("采样周期：", periodSpin_);
  root->addWidget(paramBox);

  auto *ctrlRow = new QHBoxLayout();
  startBtn_ = new QPushButton("开始", this);
  stopBtn_ = new QPushButton("停止", this);
  clearBtn_ = new QPushButton("清零", this);
  exportBtn_ = new QPushButton("导出 CSV", this);
  stopBtn_->setEnabled(false);
  ctrlRow->addWidget(startBtn_); ctrlRow->addWidget(stopBtn_); ctrlRow->addWidget(clearBtn_);
  ctrlRow->addStretch(); ctrlRow->addWidget(exportBtn_);
  root->addLayout(ctrlRow);

  readoutLabel_ = new QLabel("t = —   航向 = —   相位 = —", this);
  resultLabel_ = new QLabel("超调角1 = —   超调角2 = —   周期 = —   换向次数 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  chart_ = new ZigzagChartWidget(this);
  root->addWidget(chart_, 1);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &ZigzagDialog::onTick);
  connect(startBtn_, &QPushButton::clicked, this, &ZigzagDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &ZigzagDialog::onStop);
  connect(clearBtn_, &QPushButton::clicked, this, &ZigzagDialog::onClear);
  connect(exportBtn_, &QPushButton::clicked, this, &ZigzagDialog::onExport);

  std::cout << "MS-04: Z 形试验对话框已创建（核心层 ZigzagExperimentCore）" << std::endl;
}

ZigzagDialog::~ZigzagDialog() = default;

void ZigzagDialog::syncParamsToCore() {
  core_->setTarget(modelCombo_->currentText().toStdString(),
                   leftNameEdit_->text().toStdString(),
                   rightNameEdit_->text().toStdString());
  core_->setParams(baseSpin_->value(), deltaSpin_->value(),
                   targetYawSpin_->value(), periodSpin_->value());
}

void ZigzagDialog::onStart() {
  if (core_->running()) return;
  syncParamsToCore();
  if (!core_->start()) {
    QMessageBox::warning(this, "MS-04",
        "未能读到船模位姿 —— 请确认「船模」名与场景中一致，且仿真已启动。");
    return;
  }
  chart_->clearTrail();
  startBtn_->setEnabled(false);
  stopBtn_->setEnabled(true);
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
  std::cout << "MS-04: Z 形试验开始（基础 " << baseSpin_->value()
            << " N / 差动 " << deltaSpin_->value() << " N / 阈值 "
            << targetYawSpin_->value() << "°）" << std::endl;
}

void ZigzagDialog::onStop() {
  if (!core_->running()) return;
  core_->stop();
  timer_->stop();
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  showResults();
  std::cout << "MS-04: 已停止，推力归零" << std::endl;
}

void ZigzagDialog::onClear() {
  core_->clear();
  chart_->clearTrail();
  readoutLabel_->setText("t = —   航向 = —   相位 = —");
  resultLabel_->setText("超调角1 = —   超调角2 = —   周期 = —   换向次数 = —");
}

void ZigzagDialog::onTick() {
  if (!core_->sample()) return;
  chart_->setTrail(core_->trail(), targetYawSpin_->value());
  updateReadout();
}

void ZigzagDialog::updateReadout() {
  const auto &t = core_->trail();
  if (t.empty()) return;
  const auto &s = t.back();
  readoutLabel_->setText(QString("t = %1 s   航向 = %2°   相位 = %3")
      .arg(s.t, 0, 'f', 1).arg(s.yawDeg, 0, 'f', 2)
      .arg(s.phase == 0 ? "左转" : "右转"));
}

void ZigzagDialog::showResults() {
  const auto m = core_->metrics();
  auto fmt = [](double v) { return v < 0 ? QString("—") : QString::number(v, 'f', 2); };
  if (!m.valid) {
    resultLabel_->setText("超调角1 = —   超调角2 = —   周期 = —   （数据不足）");
    return;
  }
  resultLabel_->setText(QString("超调角1 = %1°   超调角2 = %2°   周期 = %3 s   换向次数 = %4")
      .arg(fmt(m.overshoot1)).arg(fmt(m.overshoot2)).arg(fmt(m.period)).arg(m.switches));
}

void ZigzagDialog::onExport() {
  if (core_->trail().empty()) {
    QMessageBox::information(this, "导出", "尚无数据。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出 Z 形试验 CSV",
      QString::fromStdString(modelCombo_->currentText().toStdString()) + "_zigzag.csv",
      "CSV (*.csv)");
  if (path.isEmpty()) return;
  if (!core_->exportCsv(path.toStdString()))
    QMessageBox::warning(this, "导出", "无法写入文件。");
}
