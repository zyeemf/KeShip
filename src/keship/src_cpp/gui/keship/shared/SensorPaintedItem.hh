/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* QQuickPaintedItem 实现传感器画面渲染（相机 / 激光雷达） */
/// ZhangYu 创建 2026.5.16

#ifndef SENSOR_PAINTED_ITEM_HH_
#define SENSOR_PAINTED_ITEM_HH_

#include <QQuickPaintedItem>
#include <QImage>
#include <vector>
#include <mutex>
#include <cmath>

class SensorPaintedItem : public QQuickPaintedItem
{
  Q_OBJECT

public:
  enum Mode { Camera, Lidar };

  explicit SensorPaintedItem(QQuickItem *_parent = nullptr)
      : QQuickPaintedItem(_parent) {}

  void setMode(Mode _mode) { mode_ = _mode; }

  void setCameraImage(const QImage &_img)
  {
    std::lock_guard<std::mutex> lock(mtx_);
    image_ = _img;
  }

  void setLidarData(const std::vector<float> &_ranges,
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

  void paint(QPainter *_p) override;

private:
  Mode mode_ = Camera;
  QImage image_;
  std::vector<float> ranges_;
  float angleMin_ = -M_PI, angleIncr_ = M_PI / 180.f;
  float rangeMin_ = 0.1f, rangeMax_ = 50.f;
  float dist_ = -1.f;
  std::mutex mtx_;
};

#endif
