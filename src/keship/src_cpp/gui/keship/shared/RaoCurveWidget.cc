/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-06 RAO–T 对比曲线实现（自绘：内置灰 / 学生蓝 / 峰值标记） */

#include "RaoCurveWidget.hh"

#include <QFont>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

RaoCurveWidget::RaoCurveWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(195);
}

void RaoCurveWidget::SetData(const std::vector<std::pair<double, double>> &_builtin,
                             const std::vector<std::pair<double, double>> &_student) {
  builtin_ = _builtin;
  student_ = _student;
  update();
}

void RaoCurveWidget::Clear() {
  builtin_.clear();
  student_.clear();
  update();
}

void RaoCurveWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), Qt::white);

  const int L = 52, R = 16, T = 26, B = 34;
  const QRectF plot(L, T, width() - L - R, height() - T - B);

  p.setPen(QColor("#333"));
  p.setFont(QFont("sans", 9));
  p.drawText(QRectF(0, 4, width(), 18), Qt::AlignCenter,
             "RAO–T 曲线（灰 = 内置算法，蓝 = 学生算法）");

  if (builtin_.empty() && student_.empty()) {
    p.setPen(QColor("#999"));
    p.drawText(plot, Qt::AlignCenter, "尚无数据：记录并运行后显示");
    return;
  }

  double tMin = 1e9, tMax = -1e9, rMax = 1e-9;
  auto scan = [&](const std::vector<std::pair<double, double>> &v) {
    for (const auto &pt : v) {
      tMin = std::min(tMin, pt.first);
      tMax = std::max(tMax, pt.first);
      rMax = std::max(rMax, pt.second);
    }
  };
  scan(builtin_);
  scan(student_);
  if (tMax - tMin < 0.5) { tMin -= 0.5; tMax += 0.5; }
  const double tPad = 0.3 * (tMax - tMin);
  tMin = std::max(0.0, tMin - tPad);
  tMax += tPad;
  rMax *= 1.15;

  auto X = [&](double t) { return plot.left() + (t - tMin) / (tMax - tMin) * plot.width(); };
  auto Y = [&](double r) { return plot.bottom() - r / rMax * plot.height(); };

  // 网格 + 刻度
  p.setPen(QPen(QColor("#e6e6ee"), 1));
  for (int i = 1; i <= 4; ++i) {
    const double y = plot.bottom() - plot.height() * i / 4.0;
    p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    p.setPen(QColor("#666"));
    p.drawText(QRectF(4, y - 8, L - 8, 16), Qt::AlignRight | Qt::AlignVCenter,
               QString::number(rMax * i / 4.0, 'f', 1));
    p.setPen(QPen(QColor("#e6e6ee"), 1));
  }
  const int ticks = 6;
  for (int i = 0; i <= ticks; ++i) {
    const double t = tMin + (tMax - tMin) * i / ticks;
    const double x = X(t);
    p.setPen(QPen(QColor("#f0f0f5"), 1));
    p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    p.setPen(QColor("#666"));
    p.drawText(QRectF(x - 18, plot.bottom() + 4, 36, 16), Qt::AlignCenter,
               QString::number(t, 'f', 1));
  }
  p.setPen(QPen(QColor("#333"), 1.2));
  p.drawLine(plot.bottomLeft(), plot.bottomRight());
  p.drawLine(plot.bottomLeft(), plot.topLeft());
  p.drawText(QRectF(plot.left(), plot.bottom() + 18, plot.width(), 14), Qt::AlignCenter,
             "波浪周期 T (s)");

  // 曲线 + 点
  auto drawSeries = [&](const std::vector<std::pair<double, double>> &v,
                        const QColor &c, bool _dashed, bool _label) {
    if (v.empty()) return;
    auto sorted = v;
    std::sort(sorted.begin(), sorted.end());
    QPen pen(c, 2.0);
    if (_dashed) pen.setStyle(Qt::DashLine);
    p.setPen(pen);
    QPainterPath path;
    path.moveTo(X(sorted[0].first), Y(sorted[0].second));
    for (size_t i = 1; i < sorted.size(); ++i)
      path.lineTo(X(sorted[i].first), Y(sorted[i].second));
    p.drawPath(path);
    for (const auto &pt : sorted) {
      p.setBrush(c);
      p.setPen(Qt::NoPen);
      p.drawEllipse(QPointF(X(pt.first), Y(pt.second)), 3.5, 3.5);
    }
    if (_label) {
      // 峰值标记
      const auto peak = *std::max_element(
          sorted.begin(), sorted.end(),
          [](const std::pair<double, double> &a, const std::pair<double, double> &b) {
            return a.second < b.second;
          });
      p.setBrush(Qt::NoBrush);
      p.setPen(QPen(QColor("#c0392b"), 1.4));
      p.drawEllipse(QPointF(X(peak.first), Y(peak.second)), 6.5, 6.5);
      p.setPen(QColor("#c0392b"));
      p.drawText(QPointF(X(peak.first) - 24, Y(peak.second) - 10),
                 QString("峰 %1 s").arg(peak.first, 0, 'f', 1));
    }
  };
  drawSeries(builtin_, QColor("#888"), false, true);
  drawSeries(student_, QColor("#1f6feb"), true, false);
}
