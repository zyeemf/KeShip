/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 — 对话框实现
 *
 * 数据通路：
 *   · 位姿：订阅 /world/<world>/pose/info（gz::msgs::Pose_V）
 *   · 推力：发布 /keship/thrust/<模型>/<推进器>（gz::msgs::Double，插件 clamp 到 ±max_force）
 * 指标（IMO 回转试验）：进距（转 90° 的纵向位移）/ 战术直径（转 180° 的横向位移）/ 回转直径
 */

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
#include <fstream>
#include <iostream>

#include <gz/msgs/double.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/pose_v.pb.h>

namespace {
constexpr double kDeg2Rad = M_PI / 180.0;
constexpr double kRad2Deg = 180.0 / M_PI;
}  // namespace

// ══════════════════════════════════════════════════════════════════
// 轨迹图
// ══════════════════════════════════════════════════════════════════
TurningChartWidget::TurningChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(320);
  setAutoFillBackground(true);
}

void TurningChartWidget::setTrail(const std::vector<TurningPoint> &_pts) {
  pts_ = _pts;
  // 自适应范围（留 10% 边距）
  if (!pts_.empty()) {
    xMin_ = xMax_ = pts_[0].x;
    yMin_ = yMax_ = pts_[0].y;
    for (const auto &p : pts_) {
      xMin_ = std::min(xMin_, p.x); xMax_ = std::max(xMax_, p.x);
      yMin_ = std::min(yMin_, p.y); yMax_ = std::max(yMax_, p.y);
    }
    double dx = std::max(1.0, xMax_ - xMin_), dy = std::max(1.0, yMax_ - yMin_);
    double d = std::max(dx, dy) * 0.1;
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

  const double W = width() - 20.0, H = height() - 20.0;
  const double sx = W / std::max(1e-6, xMax_ - xMin_);
  const double sy = H / std::max(1e-6, yMax_ - yMin_);
  auto toPix = [&](double x, double y) {
    return QPointF(10 + (x - xMin_) * sx, 10 + H - (y - yMin_) * sy);
  };

  // 网格
  p.setPen(QColor(232, 235, 242));
  for (int i = 1; i < 5; ++i) {
    double gx = 10 + W * i / 5.0, gy = 10 + H * i / 5.0;
    p.drawLine(QPointF(gx, 10), QPointF(gx, 10 + H));
    p.drawLine(QPointF(10, gy), QPointF(10 + W, gy));
  }

  // 轨迹
  QPainterPath path;
  path.moveTo(toPix(pts_[0].x, pts_[0].y));
  for (size_t i = 1; i < pts_.size(); ++i)
    path.lineTo(toPix(pts_[i].x, pts_[i].y));
  p.setPen(QPen(QColor(60, 110, 200), 2));
  p.drawPath(path);

  // 起点 / 当前点
  p.setBrush(QColor(50, 160, 90));
  p.setPen(Qt::NoPen);
  p.drawEllipse(toPix(pts_.front().x, pts_.front().y), 5, 5);
  p.setBrush(QColor(210, 80, 70));
  p.drawEllipse(toPix(pts_.back().x, pts_.back().y), 5, 5);

  // 航向箭头（当前点）
  const auto &last = pts_.back();
  double yawRad = last.yawDeg * kDeg2Rad;
  QPointF c = toPix(last.x, last.y);
  p.setPen(QPen(QColor(210, 80, 70), 2));
  p.drawLine(c, c + QPointF(std::cos(yawRad) * 22, -std::sin(yawRad) * 22));

  // 图例
  p.setPen(QColor(90, 95, 105));
  p.drawText(12, 20, QString("轨迹点数 %1  ·  x∈[%2, %3]  y∈[%4, %5]")
      .arg(pts_.size())
      .arg(xMin_, 0, 'f', 1).arg(xMax_, 0, 'f', 1)
      .arg(yMin_, 0, 'f', 1).arg(yMax_, 0, 'f', 1));
}

// ══════════════════════════════════════════════════════════════════
// 对话框
// ══════════════════════════════════════════════════════════════════
TurningCircleDialog::TurningCircleDialog(gz::transport::Node &_transportNode,
                                         const std::string &_worldName,
                                         const QStringList &_models,
                                         QWidget *_parent)
    : QDialog(_parent), transportNode_(_transportNode), worldName_(_worldName) {
  setWindowTitle("MS-01 回转性观测");
  resize(880, 620);

  auto *root = new QVBoxLayout(this);

  // ── 参数区 ──────────────────────────────────────────────
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
  leftThrustSpin_->setRange(-2000, 2000);
  leftThrustSpin_->setValue(120.0);
  leftThrustSpin_->setSuffix(" N");
  rightThrustSpin_ = new QDoubleSpinBox(paramBox);
  rightThrustSpin_->setRange(-2000, 2000);
  rightThrustSpin_->setValue(60.0);
  rightThrustSpin_->setSuffix(" N");
  form->addRow("左推力：", leftThrustSpin_);
  form->addRow("右推力：", rightThrustSpin_);

  periodSpin_ = new QDoubleSpinBox(paramBox);
  periodSpin_->setRange(0.02, 2.0);
  periodSpin_->setValue(0.1);
  periodSpin_->setSingleStep(0.02);
  periodSpin_->setSuffix(" s");
  form->addRow("采样周期：", periodSpin_);

  root->addWidget(paramBox);

  // ── 控制区 ──────────────────────────────────────────────
  auto *ctrlRow = new QHBoxLayout();
  startBtn_ = new QPushButton("开始", this);
  stopBtn_ = new QPushButton("停止", this);
  clearBtn_ = new QPushButton("清零", this);
  exportBtn_ = new QPushButton("导出 CSV", this);
  stopBtn_->setEnabled(false);
  ctrlRow->addWidget(startBtn_);
  ctrlRow->addWidget(stopBtn_);
  ctrlRow->addWidget(clearBtn_);
  ctrlRow->addStretch();
  ctrlRow->addWidget(exportBtn_);
  root->addLayout(ctrlRow);

  // ── 读数 + 结果 ─────────────────────────────────────────
  readoutLabel_ = new QLabel("t = —   x = —   y = —   航向 = —   速度 = —", this);
  resultLabel_ = new QLabel("进距 = —   战术直径 = —   回转直径 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  // ── 轨迹图 ──────────────────────────────────────────────
  chart_ = new TurningChartWidget(this);
  root->addWidget(chart_, 1);

  // ── 位姿订阅 ────────────────────────────────────────────
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  poseState_ = std::make_shared<TurningPoseState>();
  auto st = poseState_;
  transportNode_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->latest = _msg;
        st->found = true;
      });

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &TurningCircleDialog::onTick);
  connect(startBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onStop);
  connect(clearBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onClear);
  connect(exportBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onExport);

  std::cout << "MS-01: 回转性观测对话框已创建（位姿话题 " << poseTopic_ << "）" << std::endl;
}

TurningCircleDialog::~TurningCircleDialog() {
  if (poseState_) {
    std::lock_guard<std::mutex> lk(poseState_->mutex);
    poseState_->alive = false;
  }
  // 退出时推力归零（安全）
  if (running_) sendThrust(0.0, 0.0);
}

void TurningCircleDialog::sendThrust(double _left, double _right) {
  const std::string model = modelCombo_->currentText().toStdString();
  const std::string tl = leftNameEdit_->text().toStdString();
  const std::string tr = rightNameEdit_->text().toStdString();
  // 推进器/模型变化时重建 Publisher；否则复用
  auto ensure = [&](gz::transport::Node::Publisher &_pub,
                    const std::string &_thruster, std::string &_key) {
    if (_thruster.empty()) return false;
    const std::string key = model + "/" + _thruster;
    if (key != _key) {
      _pub = transportNode_.Advertise<gz::msgs::Double>("/keship/thrust/" + key);
      _key = key;
    }
    return true;
  };
  if (ensure(pubLeft_, tl, pubKeyL_)) {
    gz::msgs::Double m; m.set_data(_left); pubLeft_.Publish(m);
  }
  if (ensure(pubRight_, tr, pubKeyR_)) {
    gz::msgs::Double m; m.set_data(_right); pubRight_.Publish(m);
  }
}

bool TurningCircleDialog::fetchModelPose(double &_x, double &_y, double &_yawDeg) {
  gz::msgs::Pose_V snap;
  {
    std::lock_guard<std::mutex> lk(poseState_->mutex);
    if (!poseState_->found) return false;
    snap = poseState_->latest;
  }
  const std::string want = modelCombo_->currentText().toStdString();
  for (int i = 0; i < snap.pose_size(); ++i) {
    const auto &p = snap.pose(i);
    // gz pose/info 的 name 字段可能带 :: 前缀，取末段比较
    std::string nm = p.name();
    auto pos = nm.rfind("::");
    if (pos != std::string::npos) nm = nm.substr(pos + 2);
    if (nm != want) continue;
    _x = p.position().x();
    _y = p.position().y();
    double w = p.orientation().w(), z = p.orientation().z();
    _yawDeg = std::atan2(2.0 * w * z, 1.0 - 2.0 * z * z) * kRad2Deg;
    return true;
  }
  return false;
}

void TurningCircleDialog::onStart() {
  if (running_) return;
  running_ = true;
  t0_ = -1.0;
  startX_ = startY_ = startYaw_ = 0.0;
  maxX_ = maxY_ = 0.0;
  prevT_ = 0.0; speed_ = 0.0;
  trail_.clear();
  chart_->clearTrail();
  startBtn_->setEnabled(false);
  stopBtn_->setEnabled(true);
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
  sendThrust(leftThrustSpin_->value(), rightThrustSpin_->value());
  std::cout << "MS-01: 开始回转（左 " << leftThrustSpin_->value()
            << " N / 右 " << rightThrustSpin_->value() << " N）" << std::endl;
}

void TurningCircleDialog::onStop() {
  if (!running_) return;
  running_ = false;
  timer_->stop();
  sendThrust(0.0, 0.0);
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  computeResults();
  std::cout << "MS-01: 已停止，推力归零" << std::endl;
}

void TurningCircleDialog::onClear() {
  trail_.clear();
  chart_->clearTrail();
  t0_ = -1.0;
  maxX_ = maxY_ = 0.0;
  readoutLabel_->setText("t = —   x = —   y = —   航向 = —   速度 = —");
  resultLabel_->setText("进距 = —   战术直径 = —   回转直径 = —");
}

void TurningCircleDialog::onTick() {
  double x, y, yaw;
  if (!fetchModelPose(x, y, yaw)) return;

  if (t0_ < 0.0) {
    t0_ = 0.0;                 // 首帧为参考
    startX_ = x; startY_ = y; startYaw_ = yaw;
    prevX_ = x; prevY_ = y; prevT_ = 0.0;
  }
  double t = prevT_ + periodSpin_->value();
  double dt = periodSpin_->value();
  double d = std::hypot(x - prevX_, y - prevY_);
  if (dt > 0) speed_ = 0.9 * speed_ + 0.1 * (d / dt);
  prevX_ = x; prevY_ = y; prevT_ = t;

  TurningPoint pt{t, x, y, yaw};
  trail_.push_back(pt);
  maxX_ = std::max(maxX_, std::abs(x - startX_));
  maxY_ = std::max(maxY_, std::abs(y - startY_));
  chart_->setTrail(trail_);
  updateReadout();
}

void TurningCircleDialog::updateReadout() {
  const auto &p = trail_.back();
  readoutLabel_->setText(QString("t = %1 s   x = %2   y = %3   航向 = %4°   速度 = %5 m/s")
      .arg(p.t, 0, 'f', 1).arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2)
      .arg(p.yawDeg, 0, 'f', 1).arg(speed_, 0, 'f', 2));
}

void TurningCircleDialog::computeResults() {
  if (trail_.size() < 3) {
    resultLabel_->setText("进距 = —   战术直径 = —   回转直径 = —（轨迹点不足）");
    return;
  }
  // 相对初始航向的转向角
  auto relTurn = [&](const TurningPoint &p) {
    double d = p.yawDeg - startYaw_;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return std::abs(d);
  };
  double advance = -1.0, tactical = -1.0;   // 进距（转 90°）、战术直径（转 180°）
  for (const auto &p : trail_) {
    double turn = relTurn(p);
    if (advance < 0 && turn >= 90.0)  advance  = std::hypot(p.x - startX_, p.y - startY_);
    if (tactical < 0 && turn >= 180.0) tactical = std::hypot(p.x - startX_, p.y - startY_);
  }
  // 回转直径 ≈ 战术直径（IMO 定义下二者接近；无 180° 数据时用包围盒估计）
  double turning = tactical;
  if (turning < 0) turning = std::min(maxX_, maxY_) * 2.0;
  auto fmt = [](double v) { return v < 0 ? QString("—") : QString::number(v, 'f', 2); };
  resultLabel_->setText(QString("进距 = %1 m   战术直径 = %2 m   回转直径 = %3 m（点数 %4）")
      .arg(fmt(advance)).arg(fmt(tactical)).arg(fmt(turning)).arg(trail_.size()));
}

void TurningCircleDialog::onExport() {
  if (trail_.empty()) {
    QMessageBox::information(this, "导出", "尚无轨迹数据。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出回转轨迹 CSV",
      QString::fromStdString(modelCombo_->currentText().toStdString()) + "_turning.csv",
      "CSV (*.csv)");
  if (path.isEmpty()) return;
  std::ofstream ofs(path.toStdString());
  if (!ofs) {
    QMessageBox::warning(this, "导出", "无法写入文件。");
    return;
  }
  ofs << "t_s,x_m,y_m,yaw_deg\n";
  for (const auto &p : trail_)
    ofs << p.t << "," << p.x << "," << p.y << "," << p.yawDeg << "\n";
  std::cout << "MS-01: 轨迹已导出 → " << path.toStdString() << std::endl;
}
