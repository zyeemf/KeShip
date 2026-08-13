/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 激光雷达点云查看器实现 — QDialog + QWidget(QPainter) 方案
 * Zhangyu 创建 2026.5.15
 */

#include "LidarViewerDialog.hh"
#include "TopicUtil.hh"

#include <QPainter>
#include <QVBoxLayout>
#include <cmath>

//////////////////////////////////////////////////////////////////////////////////
// LidarViewWidget
//////////////////////////////////////////////////////////////////////////////////

LidarViewWidget::LidarViewWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumSize(420, 300);
  setFocusPolicy(Qt::StrongFocus);
}

void LidarViewWidget::setScanData(const std::vector<float> &_ranges,
                                   float _angleMin, float _angleIncr,
                                   float _rangeMin, float _rangeMax)
{
  std::lock_guard<std::mutex> lock(mtx_);
  ranges_ = _ranges;
  angleMin_ = _angleMin;
  angleIncr_ = _angleIncr;
  rangeMin_ = _rangeMin;
  rangeMax_ = _rangeMax;
  if (dist_ < 0.f) dist_ = rangeMax_;
}

void LidarViewWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  std::lock_guard<std::mutex> lock(mtx_);

  if (ranges_.empty()) return;
  if (dist_ < 0.f) dist_ = rangeMax_;

  float vmax = std::min((float)width(), (float)height()) * 0.45f;
  float cx = (float)width() * 0.5f;
  float cy = (float)height() * 0.5f;
  float scale = vmax / dist_;

  p.fillRect(rect(), QColor(18, 18, 28));

  float step;
  if (dist_ <= 5.f)       step = 1.f;
  else if (dist_ <= 10.f) step = 2.f;
  else if (dist_ <= 30.f) step = 5.f;
  else                    step = 10.f;

  QPen ringPen(QColor(60, 60, 75), 1.f);
  for (float r = step; r <= dist_; r += step)
  {
    float sr = r * scale;
    p.setPen(ringPen);
    p.drawEllipse(QPointF(cx, cy), sr, sr);
  }

  if (rangeMax_ > dist_ + 0.1f)
  {
    QPen outerPen(QColor(80, 80, 50), 1.f, Qt::DotLine);
    p.setPen(outerPen);
    float sr = rangeMax_ * scale;
    p.drawEllipse(QPointF(cx, cy), sr, sr);
  }

  QPen ptPen(QColor(20, 255, 40), 3.f);
  ptPen.setCapStyle(Qt::RoundCap);
  p.setPen(ptPen);
  for (size_t i = 0; i < ranges_.size(); i++)
  {
    float rg = ranges_[i];
    if (std::isnan(rg) || std::isinf(rg) || rg < rangeMin_ || rg > rangeMax_) continue;
    float angle = angleMin_ + (float)i * angleIncr_;
    float px = cx + rg * scale * cosf(angle);
    float py = cy + rg * scale * sinf(angle);
    p.drawPoint(QPointF(px, py));
  }
}

void LidarViewWidget::wheelEvent(QWheelEvent *_event)
{
  if (_event->angleDelta().y() > 0)
    dist_ = std::max(2.f, dist_ * 0.8f);
  else
    dist_ = std::min(100.f, dist_ * 1.25f);
  update();
}

//////////////////////////////////////////////////////////////////////////////////
// LidarViewerDialog
//////////////////////////////////////////////////////////////////////////////////

LidarViewerDialog::LidarViewerDialog(
    rclcpp::Node::SharedPtr _rosNode,
    const QString &_modelName,
    const QString &_instName,
    const QString &_typePrefix,
    QWidget *_parent)
    : QDialog(_parent)
{
  setWindowTitle("激光雷达: " + _modelName + " / " + _instName);
  setMinimumSize(420, 300);
  setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  widget_ = new LidarViewWidget(this);
  layout->addWidget(widget_);

  std::string topic = keship::SanitizeRosTopic(
      _modelName.toStdString() + "/" + _typePrefix.toStdString() + "/" + _instName.toStdString());

  sub_ = _rosNode->create_subscription<sensor_msgs::msg::LaserScan>(
      topic, rclcpp::SensorDataQoS(),
      [this](sensor_msgs::msg::LaserScan::ConstSharedPtr _msg) {
        std::lock_guard<std::mutex> lock(mtx_);
        ranges_ = _msg->ranges;
        angleMin_ = _msg->angle_min;
        angleIncr_ = _msg->angle_increment;
        rangeMin_ = _msg->range_min;
        rangeMax_ = _msg->range_max;
        newData_ = true;
      });

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, [this]() {
    bool has = false;
    std::vector<float> rangesCopy;
    float aMin, aIncr, rMin, rMax;
    {
      std::lock_guard<std::mutex> lock(mtx_);
      has = newData_;
      newData_ = false;
      if (has) {
        rangesCopy = ranges_;
        aMin = angleMin_; aIncr = angleIncr_;
        rMin = rangeMin_; rMax = rangeMax_;
      }
    }
    if (has)
      widget_->setScanData(rangesCopy, aMin, aIncr, rMin, rMax);
    widget_->update();
  });
  timer_->start(100);
}
