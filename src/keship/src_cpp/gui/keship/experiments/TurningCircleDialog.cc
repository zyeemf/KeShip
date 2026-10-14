/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 — 对话框实现（薄壳：界面 + 委托核心层） */

#include "TurningCircleDialog.hh"

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

namespace {
constexpr double kDeg2Rad = M_PI / 180.0;
}

// ══════════════════════════════════════════════════════════════════
// 轨迹图
// ══════════════════════════════════════════════════════════════════
TurningChartWidget::TurningChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(320);
  setAutoFillBackground(true);
}

void TurningChartWidget::setTrail(const std::vector<keship::TurningSample> &_pts) {
  pts_ = _pts;
  if (!pts_.empty()) {
    xMin_ = xMax_ = pts_[0].x;
    yMin_ = yMax_ = pts_[0].y;
    for (const auto &p : pts_) {
      xMin_ = std::min(xMin_, p.x); xMax_ = std::max(xMax_, p.x);
      yMin_ = std::min(yMin_, p.y); yMax_ = std::max(yMax_, p.y);
    }
    const double dx = std::max(1.0, xMax_ - xMin_), dy = std::max(1.0, yMax_ - yMin_);
    const double d = std::max(dx, dy) * 0.1;
    xMin_ -= d; xMax_ += d; yMin_ -= d; yMax_ += d;
  }
  update();
}

void TurningChartWidget::clearTrail() {
  pts_.clear();
  xMin_ = yMin_ = -10; xMax_ = yMax_ = 10;
  update();
}

void TurningChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (pts_.size() < 2) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "尚无轨迹 — 点「开始」施加差动推力");
    return;
  }

  // ── 坐标区（四周留出刻度标签空间） ──────────────────────────
  const double mL = 62, mR = 14, mT = 26, mB = 50;
  const double W = width() - mL - mR, H = height() - mT - mB;
  if (W < 80 || H < 60) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "窗口过小 — 无法绘制坐标轴");
    return;
  }

  // ★ 长宽比封顶 4:1 —— 极扁轨迹（如等推力直航 74 m × 2 m）若直接按 min 缩放，
  //   坐标框会退化成细条、刻度全挤在一起。此时按需扩展"短边"范围（仍是**同比例**），
  //   刻度值仍如实反映真实坐标，不会被拉宽误导。
  double axMin = xMin_, axMax = xMax_, ayMin = yMin_, ayMax = yMax_;
  constexpr double kMaxAspect = 4.0;
  for (int it = 0; it < 8; ++it) {
    const double rx = axMax - axMin, ry = ayMax - ayMin;
    if (rx > ry * kMaxAspect) {
      const double cy = 0.5 * (ayMin + ayMax);
      const double half = 0.5 * rx / kMaxAspect;
      ayMin = cy - half; ayMax = cy + half;
    } else if (ry > rx * kMaxAspect) {
      const double cx = 0.5 * (axMin + axMax);
      const double half = 0.5 * ry / kMaxAspect;
      axMin = cx - half; axMax = cx + half;
    } else {
      break;
    }
  }

  const double rangeX = axMax - axMin, rangeY = ayMax - ayMin;
  // ★ X / Y **同比例**（min 缩放，框在坐标区内居中）
  const double sScale = std::min(W / rangeX, H / rangeY);
  const double pw = rangeX * sScale, ph = rangeY * sScale;
  const double boxL = mL + (W - pw) * 0.5;
  const double boxB = mT + H - (H - ph) * 0.5;
  const double boxT = boxB - ph;

  auto toPix = [&](double x, double y) {
    return QPointF(boxL + (x - axMin) * sScale, boxB - (y - ayMin) * sScale);
  };

  // ── "好看的"刻度步长（1 / 2 / 5 × 10ⁿ） ────────────────────
  auto niceStep = [](double range, int target) {
    if (!(range > 0.0) || !std::isfinite(range)) return 1.0;
    const double raw = range / static_cast<double>(target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    double m = 1.0;
    if (n > 5.0) m = 10.0; else if (n > 2.0) m = 5.0; else if (n > 1.0) m = 2.0;
    return m * mag;
  };
  const double stepX = niceStep(rangeX, 6);
  const double stepY = niceStep(rangeY, 5);
  auto fmtTick = [](double v, double step) {
    int dec = 0;
    if (step < 1.0)
      dec = std::min(3, static_cast<int>(std::ceil(-std::log10(step))));
    return QString::number(v, 'f', dec);
  };

  QFont tickFont = p.font();
  tickFont.setPointSize(8);

  // ── 网格线 + 刻度 + 刻度值标签 ─────────────────────────────
  {
    int guard = 0;
    for (double v = std::ceil(axMin / stepX) * stepX;
         v <= axMax + 1e-9 && guard < 200; v += stepX, ++guard) {
      const double gx = toPix(v, 0).x();
      p.setPen(QColor(232, 235, 242));
      p.drawLine(QPointF(gx, boxT), QPointF(gx, boxB));
      p.setPen(QColor(126, 133, 148));
      p.drawLine(QPointF(gx, boxB), QPointF(gx, boxB + 5));
      p.setFont(tickFont);
      p.setPen(QColor(90, 95, 105));
      p.drawText(QRectF(gx - 44, boxB + 6, 88, 16),
                 Qt::AlignHCenter | Qt::AlignTop, fmtTick(v, stepX));
    }
    guard = 0;
    for (double v = std::ceil(ayMin / stepY) * stepY;
         v <= ayMax + 1e-9 && guard < 200; v += stepY, ++guard) {
      const double gy = toPix(0, v).y();
      p.setPen(QColor(232, 235, 242));
      p.drawLine(QPointF(boxL, gy), QPointF(boxL + pw, gy));
      p.setPen(QColor(126, 133, 148));
      p.drawLine(QPointF(boxL - 5, gy), QPointF(boxL, gy));
      p.setFont(tickFont);
      p.setPen(QColor(90, 95, 105));
      p.drawText(QRectF(boxL - 52, gy - 8, 46, 16),
                 Qt::AlignRight | Qt::AlignVCenter, fmtTick(v, stepY));
    }
  }

  // ── 坐标轴外框 + x 轴标题（单位由表头统一给出，避免竖排溢出） ──
  p.setPen(QPen(QColor(120, 128, 142), 1));
  p.drawRect(QRectF(boxL, boxT, pw, ph).adjusted(0, 0, -1, -1));
  p.setFont(tickFont);
  p.setPen(QColor(70, 76, 88));
  p.drawText(QRectF(boxL, boxB + 24, pw, 14),
             Qt::AlignHCenter | Qt::AlignTop, "x (m)");

  QPainterPath path;
  path.moveTo(toPix(pts_[0].x, pts_[0].y));
  for (size_t i = 1; i < pts_.size(); ++i)
    path.lineTo(toPix(pts_[i].x, pts_[i].y));
  p.setPen(QPen(QColor(60, 110, 200), 2));
  p.drawPath(path);

  p.setBrush(QColor(50, 160, 90));
  p.setPen(Qt::NoPen);
  p.drawEllipse(toPix(pts_.front().x, pts_.front().y), 5, 5);
  p.setBrush(QColor(210, 80, 70));
  p.drawEllipse(toPix(pts_.back().x, pts_.back().y), 5, 5);

  const auto &last = pts_.back();
  const double yawRad = last.yawDeg * kDeg2Rad;
  const QPointF c = toPix(last.x, last.y);
  p.setPen(QPen(QColor(210, 80, 70), 2));
  p.drawLine(c, c + QPointF(std::cos(yawRad) * 22, -std::sin(yawRad) * 22));

  p.setPen(QColor(90, 95, 105));
  p.setFont(tickFont);
  p.drawText(10, 12, QString("回转轨迹 · %1 点 · 单位 m（X/Y 同比例）").arg(pts_.size()));
  p.drawText(QRectF(width() - 206, 12, 196, 14),
             Qt::AlignRight | Qt::AlignTop,
             QString("每格 x %1 m / y %2 m")
                 .arg(stepX, 0, 'g', 4).arg(stepY, 0, 'g', 4));
}

// ══════════════════════════════════════════════════════════════════
// 对话框
// ══════════════════════════════════════════════════════════════════
TurningCircleDialog::TurningCircleDialog(gz::transport::Node &_transportNode,
                                         const std::string &_worldName,
                                         const QStringList &_models,
                                         QWidget *_parent)
    : QDialog(_parent) {
  setWindowTitle("MS-01 回转性观测");
  resize(880, 620);

  core_ = std::make_unique<keship::TurningExperimentCore>(_transportNode, _worldName);

  auto *root = new QVBoxLayout(this);

  auto *paramBox = new QGroupBox("回转参数（⚠ 平台不含舵：用双推进器差动替代舵角）", this);
  auto *form = new QFormLayout(paramBox);

  modelCombo_ = new QComboBox(paramBox);
  modelCombo_->addItems(_models.isEmpty() ? QStringList{"GT1"} : _models);
  form->addRow("船模：", modelCombo_);

  leftNameEdit_ = new QLineEdit("thrust_left", paramBox);
  rightNameEdit_ = new QLineEdit("thrust_right", paramBox);
  form->addRow("左推进器名：", leftNameEdit_);
  form->addRow("右推进器名：", rightNameEdit_);

  leftThrustSpin_ = new QDoubleSpinBox(paramBox);
  leftThrustSpin_->setRange(-2000, 2000); leftThrustSpin_->setValue(120.0);
  leftThrustSpin_->setSuffix(" N");
  rightThrustSpin_ = new QDoubleSpinBox(paramBox);
  rightThrustSpin_->setRange(-2000, 2000); rightThrustSpin_->setValue(60.0);
  rightThrustSpin_->setSuffix(" N");
  form->addRow("左推力：", leftThrustSpin_);
  form->addRow("右推力：", rightThrustSpin_);

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

  readoutLabel_ = new QLabel("t = —   x = —   y = —   航向 = —   速度 = —", this);
  resultLabel_ = new QLabel("进距 = —   战术直径 = —   回转直径 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  chart_ = new TurningChartWidget(this);
  root->addWidget(chart_, 1);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &TurningCircleDialog::onTick);
  connect(startBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onStop);
  connect(clearBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onClear);
  connect(exportBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onExport);

  std::cout << "MS-01: 回转性观测对话框已创建（核心层 TurningExperimentCore）" << std::endl;
}

TurningCircleDialog::~TurningCircleDialog() = default;

void TurningCircleDialog::syncParamsToCore() {
  core_->setTarget(modelCombo_->currentText().toStdString(),
                   leftNameEdit_->text().toStdString(),
                   rightNameEdit_->text().toStdString());
  core_->setSamplePeriod(periodSpin_->value());
}

void TurningCircleDialog::onStart() {
  if (core_->running()) return;
  syncParamsToCore();
  if (!core_->start()) {
    QMessageBox::warning(this, "MS-01",
        "未能读到船模位姿 —— 请确认「船模」名与场景中一致，且仿真已启动。");
    return;
  }
  core_->sendThrust(leftThrustSpin_->value(), rightThrustSpin_->value());
  chart_->clearTrail();
  startBtn_->setEnabled(false);
  stopBtn_->setEnabled(true);
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
  std::cout << "MS-01: 开始回转（左 " << leftThrustSpin_->value()
            << " N / 右 " << rightThrustSpin_->value() << " N）" << std::endl;
}

void TurningCircleDialog::onStop() {
  if (!core_->running()) return;
  core_->stop();
  timer_->stop();
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  showResults();
  std::cout << "MS-01: 已停止，推力归零" << std::endl;
}

void TurningCircleDialog::onClear() {
  core_->clear();
  chart_->clearTrail();
  readoutLabel_->setText("t = —   x = —   y = —   航向 = —   速度 = —");
  resultLabel_->setText("进距 = —   战术直径 = —   回转直径 = —");
}

void TurningCircleDialog::onTick() {
  if (!core_->sample()) return;
  chart_->setTrail(core_->trail());
  updateReadout();
}

void TurningCircleDialog::updateReadout() {
  const auto &t = core_->trail();
  if (t.empty()) return;
  const auto &p = t.back();
  readoutLabel_->setText(QString("t = %1 s   x = %2   y = %3   航向 = %4°   速度 = %5 m/s")
      .arg(p.t, 0, 'f', 1).arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2)
      .arg(p.yawDeg, 0, 'f', 1).arg(core_->speed(), 0, 'f', 2));
}

void TurningCircleDialog::showResults() {
  const auto m = core_->metrics();
  auto fmt = [](double v) { return v < 0 ? QString("—") : QString::number(v, 'f', 2); };
  if (!m.valid) {
    resultLabel_->setText("进距 = —   战术直径 = —   回转直径 = —（轨迹点不足）");
    return;
  }
  if (m.noTurn) {
    resultLabel_->setText(QString("进距 = %1 m   未回转（全程转向角 %2° < 10°，船基本直航）"
                                  "—— 战术直径 / 回转直径无定义")
        .arg(fmt(m.advance)).arg(m.turnedDeg, 0, 'f', 1));
    return;
  }
  resultLabel_->setText(QString("进距 = %1 m   战术直径 = %2 m   回转直径 = %3 m（圆拟合 %4 点, R²=%5）")
      .arg(fmt(m.advance)).arg(fmt(m.tactical)).arg(fmt(m.turning))
      .arg(m.fitPoints).arg(m.fitR2, 0, 'f', 4));
}

void TurningCircleDialog::onExport() {
  if (core_->trail().empty()) {
    QMessageBox::information(this, "导出", "尚无轨迹数据。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出回转轨迹 CSV",
      QString::fromStdString(modelCombo_->currentText().toStdString()) + "_turning.csv",
      "CSV (*.csv)");
  if (path.isEmpty()) return;
  if (!core_->exportCsv(path.toStdString()))
    QMessageBox::warning(this, "导出", "无法写入文件。");
  else
    std::cout << "MS-01: 轨迹已导出 → " << path.toStdString() << std::endl;
}
