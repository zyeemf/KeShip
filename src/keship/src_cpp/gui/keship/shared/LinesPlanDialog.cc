/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 型线图对话框实现 — 船舶设计规范三视图 */

#include "LinesPlanDialog.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
// 通用绘图
// ═══════════════════════════════════════════════════════════

static void drawGrid(QPainter &_p, int _w, int _h,
                     int _ml, int _mr, int _mt, int _mb,
                     int _nx, int _ny) {
  _p.setPen(QPen(QColor(210, 210, 210), 1, Qt::DotLine));
  int pw = _w - _ml - _mr, ph = _h - _mt - _mb;
  for (int i = 0; i <= _nx; ++i) {
    int gx = _ml + pw * i / _nx;
    _p.drawLine(gx, _mt, gx, _mt + ph);
  }
  for (int i = 0; i <= _ny; ++i) {
    int gy = _mt + ph * i / _ny;
    _p.drawLine(_ml, gy, _ml + pw, gy);
  }
  _p.setPen(Qt::black);
  _p.drawRect(_ml, _mt, pw, ph);
}

// ═══════════════════════════════════════════════════════════
// BodyPlanWidget — 横剖线图
// 船舶惯例: 中心线右侧=前体, 左侧=后体, 各画半边
// ═══════════════════════════════════════════════════════════

BodyPlanWidget::BodyPlanWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumSize(200, 200);
}

void BodyPlanWidget::setLines(const keship::LinesData &_lines) {
  data_ = _lines; hasData_ = true; update();
}

void BodyPlanWidget::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), Qt::white);
  int w = width(), h = height();
  if (w < 60 || h < 60 || !hasData_) return;

  int ml = 55, mr = 25, mt = 20, mb = 35, pw = w - ml - mr, ph = h - mt - mb;
  drawGrid(p, w, h, ml, mr, mt, mb, 8, 8);

  // 范围
  double yMax = 0;
  for (const auto &row : data_.halfBreadths)
    for (double y : row) if (y > yMax) yMax = y;
  yMax = std::ceil(yMax * 1.15);
  if (yMax < 0.1) yMax = 0.5;
  double zMin = data_.waterlines.back(), zMax = data_.waterlines.front();

  // 中心线
  int cx = ml + pw / 2;
  p.setPen(QPen(Qt::darkGray, 1));
  p.drawLine(cx, mt, cx, mt + ph);

  auto txR = [&](double y) { return cx + (int)(y / yMax * (pw / 2 - 5)); };
  auto txL = [&](double y) { return cx - (int)(y / yMax * (pw / 2 - 5)); };
  auto ty  = [&](double z) { return mt + ph - (int)((z - zMin) / (zMax - zMin) * ph); };

  int Nx = (int)data_.stations.size();
  int mid = Nx / 2;
  QFont f; f.setPointSize(8); p.setFont(f);

  // 前体 (i=0..mid) → 右侧
  int step = std::max(1, mid / 5);
  for (int i = 0; i <= mid; i += step) {
    p.setPen(QPen(QColor(40, 80, 200), 1.5));
    QPainterPath path;
    bool first = true;
    for (size_t j = 0; j < data_.waterlines.size(); ++j) {
      double y = data_.halfBreadths[i][j];
      double z = data_.waterlines[j];
      if (first) { path.moveTo(txR(0), ty(z)); first = false; }
      path.lineTo(txR(y), ty(z));
    }
    p.drawPath(path);
    // 站位号 (顶部)
    p.setPen(Qt::black);
    double x = data_.stations[i];
    p.drawText(QRect(txR(0) - 10, mt + 2, 20, 12), Qt::AlignHCenter,
               QString::number(x, 'f', 1));
  }

  // 后体 (i=mid..Nx-1) → 左侧
  step = std::max(1, (Nx - mid) / 5);
  for (int i = mid; i < Nx; i += step) {
    p.setPen(QPen(QColor(200, 40, 40), 1.5));
    QPainterPath path;
    bool first = true;
    for (size_t j = 0; j < data_.waterlines.size(); ++j) {
      double y = data_.halfBreadths[i][j];
      double z = data_.waterlines[j];
      if (first) { path.moveTo(txL(0), ty(z)); first = false; }
      path.lineTo(txL(y), ty(z));
    }
    p.drawPath(path);
    p.setPen(Qt::black);
    double x = data_.stations[i];
    p.drawText(QRect(txL(0) - 10, mt + 2, 20, 12), Qt::AlignHCenter,
               QString::number(x, 'f', 1));
  }

  // 标题 + 图例
  p.setPen(Qt::black);
  f.setPointSize(10); f.setBold(true); p.setFont(f);
  p.drawText(QRect(ml, 2, pw, mt), Qt::AlignHCenter, "横剖线图");
  f.setPointSize(8); f.setBold(false); p.setFont(f);
  p.setPen(QPen(QColor(40, 80, 200), 1.5));
  p.drawLine(ml + 4, mt + 2, ml + 18, mt + 2);
  p.setPen(Qt::black);
  p.drawText(QRect(ml + 22, mt - 2, 40, 12), "前体");
  p.setPen(QPen(QColor(200, 40, 40), 1.5));
  p.drawLine(ml + 60, mt + 2, ml + 74, mt + 2);
  p.setPen(Qt::black);
  p.drawText(QRect(ml + 78, mt - 2, 40, 12), "后体");

  // 坐标标签
  p.setPen(Qt::black);
  p.drawText(QRect(ml, mt + ph + 2, pw, 20), Qt::AlignHCenter, "y (m)");
  p.save(); p.translate(ml - 12, mt + ph / 2); p.rotate(-90);
  p.drawText(QRect(-20, -10, 40, 20), Qt::AlignHCenter, "z (m)");
  p.restore();
}

// ═══════════════════════════════════════════════════════════
// HalfBreadthWidget — 半宽水线图
// 船舶惯例: 只画半边(右舷), 标注水线深度
// ═══════════════════════════════════════════════════════════

HalfBreadthWidget::HalfBreadthWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumSize(200, 200);
}

void HalfBreadthWidget::setLines(const keship::LinesData &_lines) {
  data_ = _lines; hasData_ = true; update();
}

void HalfBreadthWidget::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), Qt::white);
  int w = width(), h = height();
  if (w < 60 || h < 60 || !hasData_) return;

  int ml = 55, mr = 45, mt = 20, mb = 35, pw = w - ml - mr, ph = h - mt - mb;
  drawGrid(p, w, h, ml, mr, mt, mb, 8, 8);

  double xMin = data_.stations.front(), xMax = data_.stations.back();
  double yMax = 0;
  for (const auto &row : data_.halfBreadths)
    for (double y : row) if (y > yMax) yMax = y;
  yMax = std::ceil(yMax * 1.1);
  if (yMax < 0.1) yMax = 0.5;

  auto tx = [&](double x) { return ml + (int)((x - xMin) / (xMax - xMin) * pw); };
  auto ty = [&](double y) { return mt + ph - (int)(y / yMax * ph); };

  // 中心线
  p.setPen(QPen(Qt::darkGray, 1, Qt::DashLine));
  p.drawLine(ml, mt + ph, ml + pw, mt + ph);

  // 水线 (只画右舷半边)
  int step = std::max(1, (int)data_.waterlines.size() / 6);
  for (size_t j = 0; j < data_.waterlines.size(); j += step) {
    double z = data_.waterlines[j];
    // 颜色按深度渐变
    double t = (double)j / data_.waterlines.size();
    QColor c(40 + (int)(160 * t), 80 - (int)(40 * t), 200 - (int)(160 * t));
    p.setPen(QPen(c, 1.5));

    QPainterPath path;
    bool first = true;
    for (size_t i = 0; i < data_.stations.size(); ++i) {
      double x = data_.stations[i];
      double y = data_.halfBreadths[i][j];
      if (y < 0.001) continue;  // 跳过尖灭点
      if (first) { path.moveTo(tx(x), ty(y)); first = false; }
      path.lineTo(tx(x), ty(y));
    }
    p.drawPath(path);

    // 水线深度标注 (右侧)
    QFont f; f.setPointSize(7); p.setFont(f);
    p.setPen(c);
    double lastY = 0;
    for (int i = (int)data_.stations.size() - 1; i >= 0; --i) {
      lastY = data_.halfBreadths[i][j];
      if (lastY > 0.01) break;
    }
    p.drawText(QRect(ml + pw + 2, ty(lastY) - 6, mr - 4, 12),
               Qt::AlignLeft | Qt::AlignVCenter,
               QString("z=%1").arg(z, 0, 'f', 2));
  }

  // 站位竖线
  int stStep = std::max(1, (int)data_.stations.size() / 8);
  p.setPen(QPen(QColor(180, 180, 180), 1, Qt::DotLine));
  for (size_t i = 0; i < data_.stations.size(); i += stStep) {
    int sx = tx(data_.stations[i]);
    p.drawLine(sx, mt, sx, mt + ph);
    QFont f; f.setPointSize(7); p.setFont(f);
    p.setPen(Qt::darkGray);
    p.drawText(QRect(sx - 15, mt + ph + 2, 30, 14), Qt::AlignHCenter,
               QString::number(data_.stations[i], 'f', 1));
  }

  // 标题
  p.setPen(Qt::black);
  QFont f; f.setPointSize(10); f.setBold(true); p.setFont(f);
  p.drawText(QRect(ml, 2, pw, mt), Qt::AlignHCenter, "半宽水线图");
  f.setPointSize(9); f.setBold(false); p.setFont(f);
  p.drawText(QRect(ml, mt + ph + 14, pw, 20), Qt::AlignHCenter, "x (m)");
  p.save(); p.translate(ml - 12, mt + ph / 2); p.rotate(-90);
  p.drawText(QRect(-20, -10, 40, 20), Qt::AlignHCenter, "y (m)");
  p.restore();
}

// ═══════════════════════════════════════════════════════════
// SheerPlanWidget — 纵剖线图
// 船舶惯例: 站位竖线 + 水线横线 + buttock 曲线
// ═══════════════════════════════════════════════════════════

SheerPlanWidget::SheerPlanWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumSize(300, 180);
}

void SheerPlanWidget::setLines(const keship::LinesData &_lines) {
  data_ = _lines; hasData_ = true; update();
}

void SheerPlanWidget::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), Qt::white);
  int w = width(), h = height();
  if (w < 60 || h < 60 || !hasData_) return;

  int ml = 55, mr = 30, mt = 20, mb = 35, pw = w - ml - mr, ph = h - mt - mb;
  drawGrid(p, w, h, ml, mr, mt, mb, 8, 8);

  int Nx = (int)data_.stations.size();
  int Nz = (int)data_.waterlines.size();
  if (Nx < 2 || Nz < 2) return;

  double xMin = data_.stations.front(), xMax = data_.stations.back();
  double zMin = data_.waterlines.back(), zMax = data_.waterlines.front();

  auto tx = [&](double x) { return ml + (int)((x - xMin) / (xMax - xMin) * pw); };
  auto ty = [&](double z) { return mt + ph - (int)((z - zMin) / (zMax - zMin) * ph); };

  // 站位竖线 + 标号
  int stStep = std::max(1, Nx / 8);
  QFont f; f.setPointSize(7);
  for (int i = 0; i < Nx; i += stStep) {
    int sx = tx(data_.stations[i]);
    p.setPen(QPen(QColor(200, 200, 200), 1, Qt::DotLine));
    p.drawLine(sx, mt, sx, mt + ph);
    p.setPen(Qt::darkGray);
    p.setFont(f);
    p.drawText(QRect(sx - 15, mt + ph + 2, 30, 14), Qt::AlignHCenter,
               QString::number(data_.stations[i], 'f', 1));
  }

  // 水线横线 (参考线)
  int wlStep = std::max(1, Nz / 6);
  for (int j = 0; j < Nz; j += wlStep) {
    p.setPen(QPen(QColor(190, 210, 230), 1, Qt::DotLine));
    p.drawLine(ml, ty(data_.waterlines[j]), ml + pw, ty(data_.waterlines[j]));
  }

  // 龙骨线 (y=0, z=zMin)
  p.setPen(QPen(QColor(40, 80, 200), 2));
  p.drawLine(ml, ty(zMin), ml + pw, ty(zMin));

  // 甲板边线 (y=max at each station, z=zMax)
  p.setPen(QPen(QColor(200, 40, 40), 2));
  QPainterPath deck;
  deck.moveTo(tx(xMin), ty(zMax));
  deck.lineTo(tx(xMax), ty(zMax));
  p.drawPath(deck);

  // Buttock 线: y = B/4, B/2, 3B/4
  double yMax = 0;
  for (const auto &row : data_.halfBreadths)
    for (double y : row) if (y > yMax) yMax = y;

  QVector<double> buttockY = {yMax * 0.25, yMax * 0.5, yMax * 0.75};
  QVector<QColor> bColors = {QColor(40, 160, 40), QColor(160, 80, 40),
                             QColor(120, 40, 160)};
  QVector<QString> bLabels = {"B/4", "B/2", "3B/4"};

  for (int b = 0; b < buttockY.size(); ++b) {
    double yTarget = buttockY[b];
    p.setPen(QPen(bColors[b], 1.5));

    QPainterPath path;
    bool first = true;
    for (int i = 0; i < Nx; ++i) {
      // 查找最接近 yTarget 的 z
      double zBest = zMax, yBest = 1e9;
      for (int j = 0; j < Nz; ++j) {
        double diff = std::abs(data_.halfBreadths[i][j] - yTarget);
        if (diff < yBest) { yBest = diff; zBest = data_.waterlines[j]; }
      }
      // 跳过无法到达 yTarget 的站位
      if (yBest > yMax * 0.15) continue;
      if (first) { path.moveTo(tx(data_.stations[i]), ty(zBest)); first = false; }
      else path.lineTo(tx(data_.stations[i]), ty(zBest));
    }
    p.drawPath(path);
  }

  // 图例
  p.setPen(Qt::black);
  f.setPointSize(8); p.setFont(f);
  int lx = w - mr - 140, ly = mt + 2;
  p.setPen(QPen(QColor(200,40,40),2)); p.drawLine(lx,ly+5,lx+14,ly+5);
  p.setPen(Qt::black); p.drawText(QRect(lx+18,ly,50,12), "甲板");
  ly += 14;
  p.setPen(QPen(QColor(40,80,200),2)); p.drawLine(lx,ly+5,lx+14,ly+5);
  p.setPen(Qt::black); p.drawText(QRect(lx+18,ly,50,12), "龙骨");
  for (int b = 0; b < bLabels.size(); ++b) {
    ly += 14;
    p.setPen(QPen(bColors[b],1.5)); p.drawLine(lx,ly+5,lx+14,ly+5);
    p.setPen(Qt::black); p.drawText(QRect(lx+18,ly,60,12), bLabels[b]);
  }

  // 标题
  p.setPen(Qt::black);
  f.setPointSize(10); f.setBold(true); p.setFont(f);
  p.drawText(QRect(ml, 2, pw, mt), Qt::AlignHCenter, "纵剖线图");
  f.setPointSize(9); f.setBold(false); p.setFont(f);
  p.drawText(QRect(ml, mt + ph + 14, pw, 20), Qt::AlignHCenter, "x (m)");
  p.save(); p.translate(ml - 12, mt + ph / 2); p.rotate(-90);
  p.drawText(QRect(-20, -10, 40, 20), Qt::AlignHCenter, "z (m)");
  p.restore();
}

// ═══════════════════════════════════════════════════════════
// LinesPlanDialog
// ═══════════════════════════════════════════════════════════

LinesPlanDialog::LinesPlanDialog(const keship::ShipHullSpec &_spec,
                                 QWidget *_parent)
    : QDialog(_parent) {
  setWindowTitle("型线图");
  setModal(false);
  resize(820, 720);
  setAttribute(Qt::WA_DeleteOnClose);

  auto *mainLayout = new QVBoxLayout(this);

  QString title = QString("型线图 — L=%1 B=%2 D=%3")
      .arg(_spec.L, 0, 'f', 1).arg(_spec.B, 0, 'f', 2).arg(_spec.D, 0, 'f', 2);
  auto *titleLabel = new QLabel("<b>" + title + "</b>", this);
  titleLabel->setAlignment(Qt::AlignCenter);
  mainLayout->addWidget(titleLabel);

  auto *topRow = new QHBoxLayout();
  auto *bodyWidget = new BodyPlanWidget(this);
  auto *halfWidget = new HalfBreadthWidget(this);
  topRow->addWidget(bodyWidget, 1);
  topRow->addWidget(halfWidget, 1);
  mainLayout->addLayout(topRow, 2);

  auto *sheerWidget = new SheerPlanWidget(this);
  mainLayout->addWidget(sheerWidget, 1);

  auto *closeBtn = new QPushButton("关闭", this);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
  mainLayout->addWidget(closeBtn);

  keship::ShipHullGenerator hull(_spec);
  bodyWidget->setLines(hull.lines());
  halfWidget->setLines(hull.lines());
  sheerWidget->setLines(hull.lines());
}
