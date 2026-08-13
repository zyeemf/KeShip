/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * QQuickPaintedItem 实现传感器画面渲染
 * ZhangYu 创建 2026.5.16
 */

#include "SensorPaintedItem.hh"

#include <QPainter>
#include <QColor>

static int typeId = []() {
  qmlRegisterType<SensorPaintedItem>("Keship", 1, 0, "SensorPaintedItem");
  return 0;
}();

void SensorPaintedItem::paint(QPainter *_p)
{
  _p->setRenderHint(QPainter::Antialiasing);

  std::lock_guard<std::mutex> lock(mtx_);

  if (mode_ == Camera)
  {
    _p->fillRect(boundingRect(), Qt::black);
    if (!image_.isNull())
    {
      auto scaled = image_.scaled((int)width(), (int)height(),
                                  Qt::KeepAspectRatio, Qt::SmoothTransformation);
      int x = ((int)width() - scaled.width()) / 2;
      int y = ((int)height() - scaled.height()) / 2;
      _p->drawImage(x, y, scaled);
    }
  }
  else
  {
    if (ranges_.empty()) return;
    if (dist_ < 0.f) dist_ = rangeMax_;

    float vmax = std::min((float)width(), (float)height()) * 0.45f;
    float cx = (float)width() * 0.5f;
    float cy = (float)height() * 0.5f;
    float scale = vmax / dist_;

    _p->fillRect(boundingRect(), QColor(18, 18, 28));

    float step;
    if (dist_ <= 5.f)       step = 1.f;
    else if (dist_ <= 10.f) step = 2.f;
    else if (dist_ <= 30.f) step = 5.f;
    else                    step = 10.f;

    QPen ringPen(QColor(60, 60, 75), 1.f);
    for (float r = step; r <= dist_; r += step)
    {
      float sr = r * scale;
      _p->setPen(ringPen);
      _p->drawEllipse(QPointF(cx, cy), sr, sr);
    }

    if (rangeMax_ > dist_ + 0.1f)
    {
      QPen outerPen(QColor(80, 80, 50), 1.f, Qt::DotLine);
      _p->setPen(outerPen);
      float sr = rangeMax_ * scale;
      _p->drawEllipse(QPointF(cx, cy), sr, sr);
    }

    QPen ptPen(QColor(20, 255, 40), 3.f);
    ptPen.setCapStyle(Qt::RoundCap);
    _p->setPen(ptPen);
    for (size_t i = 0; i < ranges_.size(); i++)
    {
      float rg = ranges_[i];
      if (std::isnan(rg) || std::isinf(rg) || rg < rangeMin_ || rg > rangeMax_) continue;
      float angle = angleMin_ + (float)i * angleIncr_;
      float px = cx + rg * scale * cosf(angle);
      float py = cy + rg * scale * sinf(angle);
      _p->drawPoint(QPointF(px, py));
    }
  }
}
