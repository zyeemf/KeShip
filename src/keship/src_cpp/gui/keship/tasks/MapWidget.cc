/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: 2D 地图组件实现 */

#include "MapWidget.hh"

#include <QPainter>
#include <QPainterPath>
#include <QToolTip>
#include <QWheelEvent>
#include <QInputDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDoubleSpinBox>
#include <cmath>
#include <algorithm>

MapWidget::MapWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumSize(280, 200);
  setMouseTracking(true);
}

void MapWidget::setWaypoints(const std::vector<Waypoint> &_wpts) {
  wpts_ = _wpts;
  wpStatus_.assign(wpts_.size(), 0);  // 全部未访问
  update();
}

void MapWidget::setWaypointStatus(int _idx, int _status) {
  if (_idx >= 0 && _idx < (int)wpStatus_.size())
    wpStatus_[_idx] = _status;
  update();
}

void MapWidget::setShipPose(double _x, double _y, double _heading) {
  shipX_ = _x; shipY_ = _y; shipHdg_ = _heading; showShip_ = true;
  if (autoFollow_) autoPanToShip();
  update();
}

void MapWidget::setDpTarget(double _x, double _y, double _tolerance) {
  dpX_ = _x; dpY_ = _y; dpTol_ = _tolerance; showDp_ = true; update();
}

void MapWidget::clearDpTarget() { showDp_ = false; update(); }

void MapWidget::setGotoTarget(double _x, double _y, double _radius) {
  gotoX_ = _x; gotoY_ = _y; gotoR_ = _radius; showGoto_ = true; update();
}

void MapWidget::clearWaypoints() { wpts_.clear(); wpStatus_.clear(); update(); }

void MapWidget::setHintText(const QString &_text) { hintText_ = _text; update(); }

void MapWidget::setPolygonMode(bool _on) {
  polygonMode_ = _on;
  polygonVertices_.clear();
  dragIdx_ = -1;
  if (_on) {
    setEditable(true);
    setHintText("左键放置顶点  拖拽移动  右键删除  双击闭合");
  }
  update();
}

void MapWidget::setPolygonVertices(
    const std::vector<std::pair<double,double>> &_verts) {
  polygonVertices_ = _verts;
  if (!_verts.empty()) {
    double xMin = _verts[0].first, xMax = _verts[0].first;
    double yMin = _verts[0].second, yMax = _verts[0].second;
    for (auto &v : _verts) {
      xMin = std::min(xMin, v.first); xMax = std::max(xMax, v.first);
      yMin = std::min(yMin, v.second); yMax = std::max(yMax, v.second);
    }
    double pad = std::max(xMax - xMin, yMax - yMin) * 0.2;
    viewXMin_ = xMin - pad; viewXMax_ = xMax + pad;
    viewYMin_ = yMin - pad; viewYMax_ = yMax + pad;
    initViewScale();
  }
  update();
}

void MapWidget::addTrailPoint(double _x, double _y) {
  trail_.push_back({_x, _y});
  if (trail_.size() > 5000) trail_.erase(trail_.begin());
}

void MapWidget::clearTrail() { trail_.clear(); update(); }

void MapWidget::setSceneModels(const std::vector<keship::ModelFootprint> &_models)
{
  sceneModels_ = _models;
  update();
}

void MapWidget::setReferenceWaterAreas(
    const std::vector<keship::WaterArea> &_areas)
{
  referenceAreas_ = _areas;
  update();
}

void MapWidget::fitToAll()
{
  bool hasAny = false;
  double xMin = INFINITY, xMax = -INFINITY;
  double yMin = INFINITY, yMax = -INFINITY;
  auto grow = [&](double _x, double _y) {
    hasAny = true;
    if (_x < xMin) xMin = _x;
    if (_x > xMax) xMax = _x;
    if (_y < yMin) yMin = _y;
    if (_y > yMax) yMax = _y;
  };

  // 当前编辑多边形（顶点）
  for (const auto &v : polygonVertices_)
    grow(v.first, v.second);

  // 场景模型（半对角线包围盒）
  for (const auto &m : sceneModels_)
  {
    double halfDiag = std::sqrt(m.localW * m.localW + m.localH * m.localH) * 0.5;
    grow(m.worldX - halfDiag, m.worldY - halfDiag);
    grow(m.worldX + halfDiag, m.worldY + halfDiag);
  }

  // 参考水域（vertices 或矩形四角）
  for (const auto &a : referenceAreas_)
  {
    std::vector<std::pair<double,double>> pts;
    if (!a.vertices.empty()) pts = a.vertices;
    else pts = {{a.xMin,a.yMin},{a.xMax,a.yMin},{a.xMax,a.yMax},{a.xMin,a.yMax}};
    for (const auto &pt : pts)
      grow(pt.first, pt.second);
  }

  if (!hasAny) return;
  double margin = std::max(xMax - xMin, yMax - yMin) * 0.1;
  if (margin < 1e-9) margin = 1.0;
  viewXMin_ = xMin - margin;
  viewXMax_ = xMax + margin;
  viewYMin_ = yMin - margin;
  viewYMax_ = yMax + margin;
  initViewScale();
  update();
}

void MapWidget::paintReferenceAreas(QPainter &_p) const
{
  if (referenceAreas_.empty()) return;
  QFont nf; nf.setPointSize(8); nf.setBold(true); _p.setFont(nf);
  for (const auto &a : referenceAreas_)
  {
    std::vector<std::pair<double,double>> pts;
    if (!a.vertices.empty()) pts = a.vertices;
    else pts = {{a.xMin,a.yMin},{a.xMax,a.yMin},{a.xMax,a.yMax},{a.xMin,a.yMax}};
    if (pts.size() < 3) continue;

    QPolygonF poly;
    double cx = 0.0, cy = 0.0;
    for (const auto &v : pts)
    {
      poly << QPointF(toX(v.first), toY(v.second));
      cx += v.first; cy += v.second;
    }
    cx /= (double)pts.size();
    cy /= (double)pts.size();

    // 半透明浅灰填充 + 灰色虚线边框（只读参考，与当前水域蓝色可编辑样式区分）
    _p.setPen(Qt::NoPen);
    _p.setBrush(QColor(150, 150, 150, 35));
    _p.drawPolygon(poly);
    _p.setBrush(Qt::NoBrush);
    _p.setPen(QPen(QColor(120, 120, 120, 200), 1, Qt::DashLine));
    _p.drawPolygon(poly);

    // 水域名称标签（质心）
    _p.setPen(QColor(120, 120, 120));
    _p.drawText(QRect(toX(cx) - 40, toY(cy) - 8, 80, 16),
                Qt::AlignCenter,
                QString::fromStdString(a.name));
  }
}

void MapWidget::fitToSceneModels()
{
  if (sceneModels_.empty()) return;
  double xMin = INFINITY, xMax = -INFINITY;
  double yMin = INFINITY, yMax = -INFINITY;
  for (const auto &m : sceneModels_)
  {
    double halfDiag = std::sqrt(m.localW * m.localW + m.localH * m.localH) * 0.5;
    if (m.worldX - halfDiag < xMin) xMin = m.worldX - halfDiag;
    if (m.worldX + halfDiag > xMax) xMax = m.worldX + halfDiag;
    if (m.worldY - halfDiag < yMin) yMin = m.worldY - halfDiag;
    if (m.worldY + halfDiag > yMax) yMax = m.worldY + halfDiag;
  }
  double margin = std::max(xMax - xMin, yMax - yMin) * 0.1;
  viewXMin_ = xMin - margin;
  viewXMax_ = xMax + margin;
  viewYMin_ = yMin - margin;
  viewYMax_ = yMax + margin;
  initViewScale();
  update();
}

void MapWidget::fitViewToWidget()
{
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0 || scale_ < 1e-12) return;
  double cx = (viewXMin_ + viewXMax_) * 0.5;
  double cy = (viewYMin_ + viewYMax_) * 0.5;
  viewXMin_ = cx - pw / scale_ * 0.5;
  viewXMax_ = cx + pw / scale_ * 0.5;
  viewYMin_ = cy - ph / scale_ * 0.5;
  viewYMax_ = cy + ph / scale_ * 0.5;
}

void MapWidget::initViewScale()
{
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;
  double rangeX = viewXMax_ - viewXMin_;
  double rangeY = viewYMax_ - viewYMin_;
  if (rangeX < 1e-12 || rangeY < 1e-12) return;
  scale_ = std::min(pw / rangeX, ph / rangeY);
  fitViewToWidget();
}

void MapWidget::resizeEvent(QResizeEvent *_event)
{
  QWidget::resizeEvent(_event);
  fitViewToWidget();
  update();
}

void MapWidget::paintModelRect(QPainter &_p,
    const keship::ModelFootprint &_m, int _pw, int _ph) const
{
  double pixW = _m.localW * _pw / (viewXMax_ - viewXMin_);
  double pixH = _m.localH * _ph / (viewYMax_ - viewYMin_);
  QRectF rect(-pixW / 2, -pixH / 2, pixW, pixH);

  if (_m.isStatic) {
    _p.setBrush(QColor(160, 160, 160, 80));
    _p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DashLine));
  } else {
    _p.setBrush(QColor(100, 150, 220, 60));
    _p.setPen(QPen(QColor(80, 120, 200), 1));
  }
  _p.drawRect(rect);
}

void MapWidget::paintModelPolygon(QPainter &_p,
    const keship::ModelFootprint &_m,
    const std::vector<std::pair<double,double>> &_outline,
    int _pw, int _ph) const
{
  if (_outline.size() < 3) return;

  double sx = _pw / (viewXMax_ - viewXMin_);
  double sy = _ph / (viewYMax_ - viewYMin_);

  QPolygonF poly;
  for (const auto &v : _outline)
    poly << QPointF(v.first * sx, -(v.second * sy));

  if (_m.isStatic) {
    _p.setBrush(QColor(160, 160, 160, 80));
    _p.setPen(QPen(QColor(120, 120, 120), 1.5, Qt::DashLine));
  } else {
    _p.setBrush(QColor(100, 150, 220, 60));
    _p.setPen(QPen(QColor(80, 120, 200), 1.5));
  }
  _p.drawPolygon(poly);
}

void MapWidget::paintModelSubPolygons(QPainter &_p,
    const keship::ModelFootprint &_m,
    const std::vector<std::vector<std::pair<double,double>>> &_outlines,
    int _pw, int _ph) const
{
  if (_outlines.empty()) return;

  double sx = _pw / (viewXMax_ - viewXMin_);
  double sy = _ph / (viewYMax_ - viewYMin_);

  if (_m.isStatic) {
    _p.setBrush(QColor(160, 160, 160, 80));
    _p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DashLine));
  } else {
    _p.setBrush(QColor(100, 150, 220, 60));
    _p.setPen(QPen(QColor(80, 120, 200), 1));
  }

  for (const auto &outline : _outlines)
  {
    if (outline.size() < 3) continue;
    QPolygonF poly;
    for (const auto &v : outline)
      poly << QPointF(v.first * sx, -(v.second * sy));
    _p.drawPolygon(poly);
  }
}

void MapWidget::paintModelImage(QPainter &_p,
    const keship::ModelFootprint &_m, int _pw, int _ph) const
{
  if (_m.previewData.empty()) return;

  QImage img(_m.previewData.data(), _m.previewW, _m.previewH,
             QImage::Format_RGBA8888);
  if (img.isNull()) return;

  double sx = _pw / (viewXMax_ - viewXMin_);
  double sy = _ph / (viewYMax_ - viewYMin_);

  double offX = _m.localXMin + _m.localW / 2.0;
  double offY = _m.localYMin + _m.localH / 2.0;

  double pixW = _m.localW * sx;
  double pixH = _m.localH * sy;

  QRectF rect(offX * sx - pixW / 2.0, -(offY * sy) - pixH / 2.0, pixW, pixH);

  if (_m.isStatic) {
    _p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DashLine));
    _p.setBrush(Qt::NoBrush);
    _p.drawRect(rect);
  }
  _p.drawImage(rect, img);
}

// ═══════════════════════════════════════════════
// 坐标变换
// ═══════════════════════════════════════════════

int MapWidget::toX(double _wx) const {
  int pw = plotW(); if (pw <= 0) return ml();
  return ml() + (int)((_wx - viewXMin_) / (viewXMax_ - viewXMin_) * pw);
}
int MapWidget::toY(double _wy) const {
  int ph = plotH(); if (ph <= 0) return mt();
  return mt() + ph - (int)((_wy - viewYMin_) / (viewYMax_ - viewYMin_) * ph);
}
double MapWidget::fromX(int _px) const {
  int pw = plotW(); if (pw <= 0) return 0;
  return viewXMin_ + (_px - ml()) * (viewXMax_ - viewXMin_) / (double)pw;
}
double MapWidget::fromY(int _py) const {
  int ph = plotH(); if (ph <= 0) return 0;
  return viewYMin_ + (mt() + ph - _py) * (viewYMax_ - viewYMin_) / (double)ph;
}

// ═══════════════════════════════════════════════
// 自动平移
// ═══════════════════════════════════════════════

void MapWidget::autoPanToShip() {
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;

  double margin = 0.20;  // 20% 边界触发平移
  double sx = toX(shipX_), sy = toY(shipY_);
  double left = ml() + pw * margin;
  double right = ml() + pw * (1.0 - margin);
  double top = mt() + ph * margin;
  double bottom = mt() + ph * (1.0 - margin);

  double dx = 0, dy = 0;
  if (sx < left)  dx = sx - left;
  if (sx > right) dx = sx - right;
  if (sy < top)    dy = sy - top;
  if (sy > bottom) dy = sy - bottom;

  if (std::abs(dx) < 1 && std::abs(dy) < 1) return;

  double dw = viewXMax_ - viewXMin_;
  double dh = viewYMax_ - viewYMin_;
  viewXMin_ += dx / pw * dw;
  viewXMax_ += dx / pw * dw;
  viewYMin_ -= dy / ph * dh;
  viewYMax_ -= dy / ph * dh;
}

// ═══════════════════════════════════════════════
// 绘制
// ═══════════════════════════════════════════════

void MapWidget::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  p.fillRect(rect(), QColor(245, 248, 252));
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;

  // ═══ 第 1 层：方框 + 坐标轴（永远不被裁剪） ═══
  p.setPen(Qt::black);
  p.drawRect(ml(), mt(), pw, ph);

  {
    QFont tf; tf.setPointSize(7); p.setFont(tf); p.setPen(Qt::black);
    double dx = (viewXMax_ - viewXMin_) / 4;
    double dy = (viewYMax_ - viewYMin_) / 4;
    for (int i = 0; i <= 4; ++i) {
      double xv = viewXMin_ + dx * i, yv = viewYMin_ + dy * i;
      int tx = ml() + (int)((xv - viewXMin_) / (viewXMax_ - viewXMin_) * pw);
      int ty = mt() + ph - (int)((yv - viewYMin_) / (viewYMax_ - viewYMin_) * ph);
      p.drawLine(tx, mt() + ph, tx, mt() + ph + 4);
      p.drawText(QRect(tx - 25, mt() + ph + 4, 50, 12), Qt::AlignHCenter,
                 QString::number(xv, 'f', 0));
      p.drawLine(ml(), ty, ml() - 4, ty);
      p.drawText(QRect(2, ty - 6, ml() - 6, 12), Qt::AlignRight | Qt::AlignVCenter,
                 QString::number(yv, 'f', 0));
    }
  }

  // 小坐标系指示器
  {
    int cx = ml() + 12, cy = mt() + 14;
    p.setPen(QPen(Qt::red, 2));   p.drawLine(cx, cy, cx + 12, cy);
    p.setPen(QPen(QColor(0,140,0), 2)); p.drawLine(cx, cy, cx, cy - 8);
    p.setPen(Qt::black);
    QFont xf; xf.setPointSize(7); xf.setBold(true); p.setFont(xf);
    p.drawText(cx + 13, cy + 4, "X");
    p.drawText(cx - 2, cy - 9, "Y");
  }

  // ═══ 第 2 层：绘图区内容（裁剪，防止放大后超出方框） ═══
  p.save();
  p.setClipRect(ml(), mt(), pw, ph);

  // 网格
  p.setPen(QPen(QColor(220, 220, 220), 1, Qt::DotLine));
  for (int i = 0; i <= 8; ++i) {
    int gx = ml() + pw * i / 8;
    int gy = mt() + ph * i / 8;
    p.drawLine(gx, mt(), gx, mt() + ph);
    p.drawLine(ml(), gy, ml() + pw, gy);
  }

  // 场景模型足迹
  if (!sceneModels_.empty())
  {
    for (const auto &m : sceneModels_)
    {
      p.save();

      int cx = toX(m.worldX), cy = toY(m.worldY);
      double angleDeg = -m.worldYaw * 180.0 / M_PI;

      p.translate(cx, cy);
      if (std::abs(m.worldYaw) > 1e-6)
        p.rotate(angleDeg);

      bool rendered = false;
      switch (renderScheme_)
      {
      case keship::ModelRenderScheme::ConvexHull:
        if (!m.convexOutline.empty()) {
          paintModelPolygon(p, m, m.convexOutline, pw, ph);
          rendered = true;
        }
        break;
      case keship::ModelRenderScheme::AlphaShape:
        if (!m.alphaOutline.empty()) {
          paintModelPolygon(p, m, m.alphaOutline, pw, ph);
          rendered = true;
        }
        break;
      case keship::ModelRenderScheme::SubMeshHulls:
        if (!m.subOutlines.empty()) {
          paintModelSubPolygons(p, m, m.subOutlines, pw, ph);
          rendered = true;
        }
        break;
      case keship::ModelRenderScheme::ProjectedImage:
        if (!m.previewData.empty()) {
          paintModelImage(p, m, pw, ph);
          rendered = true;
        }
        break;
      default: break;
      }

      if (!rendered)
        paintModelRect(p, m, pw, ph);

      p.restore();

      p.setPen(QColor(60, 60, 60));
      QFont f = p.font();
      f.setPixelSize(10);
      p.setFont(f);
      p.drawText(QRectF(toX(m.worldX) - 50,
                        toY(m.worldY) - 14, 100, 14),
                 Qt::AlignCenter,
                 QString::fromStdString(m.name));
    }
  }

  // 只读参考水域（编辑水域时的其他水域参考，绘制在模型之上保证可见，
  // 半透明灰框不影响模型观察；当前水域蓝色多边形随后绘制在最上层）
  paintReferenceAreas(p);

  // DP 目标区域
  if (showDp_) {
    int dpx = toX(dpX_), dpy = toY(dpY_);
    int rPx = (int)(dpTol_ / (viewXMax_ - viewXMin_) * pw);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 200, 40, 50));
    p.drawEllipse(QPoint(dpx, dpy), rPx, rPx);
    p.setPen(QPen(QColor(40, 160, 40), 1.5, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPoint(dpx, dpy), rPx, rPx);
    p.setPen(QPen(QColor(40, 160, 40), 1));
    p.drawLine(dpx - 6, dpy, dpx + 6, dpy);
    p.drawLine(dpx, dpy - 6, dpx, dpy + 6);
  }

  // goto 目标（旗帜标记 + 抵达圈）
  if (showGoto_) {
    int gx = toX(gotoX_), gy = toY(gotoY_);
    int rPx = std::max(3, (int)(gotoR_ / (viewXMax_ - viewXMin_) * pw));
    // 抵达圈
    p.setPen(QPen(QColor(200, 120, 0), 1.5, Qt::DashLine));
    p.setBrush(QColor(200, 120, 0, 40));
    p.drawEllipse(QPoint(gx, gy), rPx, rPx);
    // 旗帜杆
    p.setPen(QPen(QColor(200, 80, 0), 2.5));
    p.drawLine(gx, gy, gx, gy - 20);
    // 旗帜三角
    p.setBrush(QColor(240, 120, 20));
    QPointF flag[3] = { QPointF(gx, gy - 20), QPointF(gx + 12, gy - 14), QPointF(gx, gy - 8) };
    p.drawPolygon(flag, 3);
    // 十字准心
    p.setPen(QPen(QColor(200, 80, 0), 1));
    p.setBrush(Qt::NoBrush);
    p.drawLine(gx - 4, gy, gx + 4, gy);
    p.drawLine(gx, gy - 4, gx, gy + 4);
  }

  // 船标
  if (showShip_) {
    p.setPen(QPen(QColor(40, 80, 200), 2));
    p.setBrush(QColor(40, 80, 200));
    int sx = toX(shipX_), sy = toY(shipY_);
    double rad = (90.0 - shipHdg_) * M_PI / 180.0;
    QPointF pts[3] = {
      QPointF(sx + 10 * cos(rad), sy - 10 * sin(rad)),
      QPointF(sx + 5 * cos(rad + 2.5), sy - 5 * sin(rad + 2.5)),
      QPointF(sx + 5 * cos(rad - 2.5), sy - 5 * sin(rad - 2.5)),
    };
    p.drawPolygon(pts, 3);
  }

  // 轨迹线
  if (trail_.size() >= 2) {
    p.setPen(QPen(QColor(40, 80, 200, 180), 2));
    for (size_t i = 1; i < trail_.size(); ++i)
      p.drawLine(QPointF(toX(trail_[i-1].x), toY(trail_[i-1].y)),
                 QPointF(toX(trail_[i].x),   toY(trail_[i].y)));
  }

  // 参考点
  if (!refPoints_.empty()) {
    for (size_t i = 0; i < refPoints_.size(); ++i) {
      int rx = toX(refPoints_[i].first), ry = toY(refPoints_[i].second);
      p.setPen(QPen(QColor(200, 80, 0), 2));
      p.setBrush(Qt::NoBrush);
      p.drawLine(rx - 6, ry, rx + 6, ry);
      p.drawLine(rx, ry - 6, rx, ry + 6);
      p.drawEllipse(QPoint(rx, ry), 8, 8);
      QFont rf; rf.setPointSize(8); rf.setBold(true); p.setFont(rf);
      p.setPen(Qt::white);
      p.drawText(QRect(rx - 6, ry - 6, 12, 12), Qt::AlignCenter, QString::number(i + 1));
    }
  }

  // ── 多边形渲染 ──
  if (polygonMode_ && polygonVertices_.size() >= 3) {
    QPolygonF poly;
    for (auto &v : polygonVertices_)
      poly << QPointF(toX(v.first), toY(v.second));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(70, 130, 200, 45));
    p.drawPolygon(poly);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(70, 130, 200, 200), 1.5, Qt::DashLine));
    p.drawPolygon(poly);
    p.setPen(QPen(QColor(40, 80, 160), 2));
    p.setBrush(Qt::white);
    for (size_t i = 0; i < polygonVertices_.size(); ++i) {
      int vx = toX(polygonVertices_[i].first),
          vy = toY(polygonVertices_[i].second);
      p.drawEllipse(QPoint(vx, vy), 5, 5);
      QFont vf; vf.setPointSize(8); vf.setBold(true); p.setFont(vf);
      p.setPen(QColor(40, 80, 160));
      p.drawText(QRect(vx - 8, vy - 16, 16, 14), Qt::AlignCenter,
                 QString::number(i + 1));
      p.setPen(QPen(QColor(40, 80, 160), 2));
    }
  }
  if (polygonMode_ && polygonVertices_.size() > 0 &&
      polygonVertices_.size() < 3) {
    p.setPen(QPen(QColor(70, 130, 200, 160), 1.5, Qt::DashLine));
    p.setBrush(Qt::white);
    for (size_t i = 0; i < polygonVertices_.size(); ++i) {
      int vx = toX(polygonVertices_[i].first),
          vy = toY(polygonVertices_[i].second);
      p.drawEllipse(QPoint(vx, vy), 5, 5);
      if (i > 0)
        p.drawLine(toX(polygonVertices_[i-1].first),
                   toY(polygonVertices_[i-1].second), vx, vy);
    }
  }

  // ---- 参考路径连线 ----
  if (trackingMode_) {
    // tracking 模式：渲染路径走廊（半透明带宽 = tolerance）
    if (wpts_.size() >= 2) {
      // 计算走廊对应像素宽度
      double tolPx = trackingTol_ / (viewXMax_ - viewXMin_) * pw;
      if (tolPx < 3) tolPx = 3;
      // 半透明走廊
      p.setPen(QPen(QColor(40, 120, 200, 60), tolPx * 2, Qt::SolidLine, Qt::RoundCap));
      for (size_t i = 1; i < wpts_.size(); ++i)
        p.drawLine(toX(wpts_[i-1].x), toY(wpts_[i-1].y),
                   toX(wpts_[i].x),   toY(wpts_[i].y));
      // 中心参考线
      p.setPen(QPen(QColor(40, 80, 200), 2, Qt::SolidLine));
      for (size_t i = 1; i < wpts_.size(); ++i)
        p.drawLine(toX(wpts_[i-1].x), toY(wpts_[i-1].y),
                   toX(wpts_[i].x),   toY(wpts_[i].y));
      // 路径点标记（小圆，蓝色）
      p.setPen(QPen(QColor(40, 80, 200), 1.5));
      p.setBrush(QColor(40, 120, 200, 100));
      for (size_t i = 0; i < wpts_.size(); ++i)
        p.drawEllipse(QPoint(toX(wpts_[i].x), toY(wpts_[i].y)), 4, 4);
    }
  } else {
    // waypoint 模式：已到达=实线绿色，未到达=虚线红色
    for (size_t i = 1; i < wpts_.size(); ++i) {
      bool reached = (i < wpStatus_.size() && wpStatus_[i] >= 2);
      p.setPen(reached
          ? QPen(QColor(40, 160, 40), 1.5)
          : QPen(QColor(200, 40, 40), 1.5, Qt::DashLine));
      p.drawLine(toX(wpts_[i-1].x), toY(wpts_[i-1].y),
                 toX(wpts_[i].x),   toY(wpts_[i].y));
    }
  }

  // ---- 航路点（三色：灰/黄/绿）—— 仅 waypoint 模式 ----
  if (!trackingMode_) {

  for (size_t i = 0; i < wpts_.size(); ++i) {
    auto &wp = wpts_[i];
    int wx = toX(wp.x), wy = toY(wp.y);
    int st = (i < wpStatus_.size()) ? wpStatus_[i] : 0;

    QColor fill, border;
    switch (st) {
      case 2: fill = QColor(40, 200, 40); border = QColor(20, 140, 20); break; // 已到达(绿)
      case 1: fill = QColor(240, 200, 40); border = QColor(200, 150, 20); break; // 当前(黄)
      default: fill = QColor(180, 180, 180); border = QColor(120, 120, 120); break; // 未访问(灰)
    }

    // 圈
    p.setPen(QPen(border, 2));
    p.setBrush(fill);
    p.drawEllipse(QPoint(wx, wy), 7, 7);

    // 小旗标 (当前目标)
    if (st == 1) {
      p.setPen(QPen(QColor(200, 150, 20), 2));
      p.drawLine(wx, wy - 10, wx, wy - 22);
      p.setBrush(QColor(240, 200, 40));
      QPointF flag[3] = { QPointF(wx, wy - 22), QPointF(wx + 8, wy - 18), QPointF(wx, wy - 14) };
      p.drawPolygon(flag, 3);
    }
    if (st == 2) {
      // 绿色小勾
      p.setPen(QPen(Qt::white, 2));
      p.drawLine(wx - 3, wy, wx - 1, wy + 3);
      p.drawLine(wx - 1, wy + 3, wx + 4, wy - 3);
    }

    // 序号
    p.setPen(st == 1 ? Qt::black : Qt::white);
    QFont f; f.setPointSize(7); f.setBold(true); p.setFont(f);
    p.drawText(QRect(wx - 8, wy - 8, 16, 16), Qt::AlignCenter, QString::number(i + 1));

    // 航向箭头（仅当有要求时）
    if (wp.heading >= 0) {
      double hRad = (90.0 - wp.heading) * M_PI / 180.0;
      int ax = wx + (int)(14 * cos(hRad)), ay = wy - (int)(14 * sin(hRad));
      p.setPen(QPen(border, 1.5));
      p.drawLine(wx, wy, ax, ay);
    }
  }

  }  // end if (!trackingMode_)

  // 恢复裁剪（比例尺和底部信息在裁剪区外渲染）
  p.restore();

  // ---- 动作提示（地图下方，不遮挡地图内容；操作说明不随模式误显示"双击设航向"）----
  // 注意：X 轴刻度数字画在 ph+4~ph+16，故提示必须从 ph+18 起，否则两者重叠
  if (!hintText_.isEmpty()) {
    p.setPen(Qt::darkGray);
    QFont hf; hf.setPointSize(8); p.setFont(hf);
    p.drawText(QRect(ml(), mt() + ph + 18, pw, 14), Qt::AlignCenter, hintText_);
  }

  // ---- 底部信息（紧跟提示行之后；无提示时上移一行）----
  {
    const int infoTop = mt() + ph + (hintText_.isEmpty() ? 18 : 34);
    QFont rf; rf.setPointSize(7); p.setFont(rf); p.setPen(Qt::darkGray);
    p.drawText(QRect(ml(), infoTop, pw, 14), Qt::AlignCenter,
               QString("范围 X:%1~%2 Y:%3~%4 | 滚轮缩放 中键平移")
               .arg(viewXMin_,0,'f',0).arg(viewXMax_,0,'f',0)
               .arg(viewYMin_,0,'f',0).arg(viewYMax_,0,'f',0));
  }
}

// ═══════════════════════════════════════════════
// 鼠标交互
// ═══════════════════════════════════════════════

void MapWidget::mousePressEvent(QMouseEvent *_event) {
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;
  double cx = _event->pos().x(), cy = _event->pos().y();
  if (cx < ml() || cx > ml() + pw || cy < mt() || cy > mt() + ph) return;

  // 中键 → 平移
  if (_event->button() == Qt::MiddleButton) {
    panning_ = true; panStart_ = _event->pos();
    setCursor(Qt::ClosedHandCursor);
    return;
  }

  double wx = fromX((int)cx), wy = fromY((int)cy);

  // 只读模式：仅允许平移和缩放
  if (!editable_ && _event->button() != Qt::MiddleButton) return;

  // ── 多边形模式 ──
  if (polygonMode_ && _event->button() == Qt::LeftButton) {
    double minDist = 8.0 * (viewXMax_ - viewXMin_) / pw;
    int hitIdx = -1; double best = minDist;
    for (size_t i = 0; i < polygonVertices_.size(); ++i) {
      double d = std::hypot(polygonVertices_[i].first - wx,
                             polygonVertices_[i].second - wy);
      if (d < best) { best = d; hitIdx = (int)i; }
    }
    if (hitIdx >= 0) {
      dragIdx_ = hitIdx;
    } else {
      polygonVertices_.push_back({wx, wy});
      dragIdx_ = (int)polygonVertices_.size() - 1;
    }
    update();
    return;
  }

  if (polygonMode_ && _event->button() == Qt::RightButton) {
    double minDist = 12.0 * (viewXMax_ - viewXMin_) / pw;
    int delIdx = -1; double best = minDist;
    for (size_t i = 0; i < polygonVertices_.size(); ++i) {
      double d = std::hypot(polygonVertices_[i].first - wx,
                             polygonVertices_[i].second - wy);
      if (d < best) { best = d; delIdx = (int)i; }
    }
    if (delIdx >= 0) {
      polygonVertices_.erase(polygonVertices_.begin() + delIdx);
      update();
    }
    return;
  }

  // ── tracking 形状模式：点击放置参考点 ──
  if (trackingMode_ && trackingShape_ && _event->button() == Qt::LeftButton) {
    // 如果参考点已放满，清空重新开始（避免点击被"吃掉"）
    if ((int)refPoints_.size() >= refPointsNeeded_) {
      refPoints_.clear();
      update();
    }
    refPoints_.push_back({wx, wy});
    emit refPointPlaced((int)refPoints_.size() - 1, wx, wy);
    update();
    return;
  }

  // ── tracking 自定义模式：按住拖拽画线 ──
  if (trackingMode_ && !trackingShape_ && _event->button() == Qt::LeftButton) {
    freeDraw_ = true;
    wpts_.clear(); wpStatus_.clear();
    wpts_.push_back({wx, wy, -1.0});
    lastDrawX_ = wx; lastDrawY_ = wy;
    update(); emit waypointsChanged();
    return;
  }

  // freeDraw 中忽略右键
  if (freeDraw_) return;

  // 右键 → 删除最近航路点
  if (_event->button() == Qt::RightButton) {
    double minDist = 15.0 * (viewXMax_ - viewXMin_) / pw;
    int delIdx = -1; double best = minDist;
    for (size_t i = 0; i < wpts_.size(); ++i) {
      double d = std::hypot(wpts_[i].x - wx, wpts_[i].y - wy);
      if (d < best) { best = d; delIdx = (int)i; }
    }
    if (delIdx >= 0) {
      wpts_.erase(wpts_.begin() + delIdx);
      if (delIdx < (int)wpStatus_.size())
        wpStatus_.erase(wpStatus_.begin() + delIdx);
      update();
      emit waypointsChanged();
    }
    return;
  }

  if (_event->button() != Qt::LeftButton) return;

  // ── waypoint 模式：点击添加/拖拽移动 ──
  double minDist = 5.0 * (viewXMax_ - viewXMin_) / pw;
  dragIdx_ = -1; double best = minDist;
  for (size_t i = 0; i < wpts_.size(); ++i) {
    double d = std::hypot(wpts_[i].x - wx, wpts_[i].y - wy);
    if (d < best) { best = d; dragIdx_ = (int)i; }
  }

  if (dragIdx_ < 0) {
    wpts_.push_back({wx, wy, -1.0});
    dragIdx_ = (int)wpts_.size() - 1;
    update(); emit waypointsChanged();
  }
}

void MapWidget::mouseMoveEvent(QMouseEvent *_event) {
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;

  if (panning_) {
    double dx = (_event->pos().x() - panStart_.x()) / pw * (viewXMax_ - viewXMin_);
    double dy = (_event->pos().y() - panStart_.y()) / ph * (viewYMax_ - viewYMin_);
    viewXMin_ -= dx; viewXMax_ -= dx;
    viewYMin_ += dy; viewYMax_ += dy;
    panStart_ = _event->pos();
    update(); return;
  }

  // ── tracking 自定义模式：拖拽连续采样 ──
  if (freeDraw_) {
    double wx = fromX((int)_event->pos().x()), wy = fromY((int)_event->pos().y());
    double d = std::hypot(wx - lastDrawX_, wy - lastDrawY_);
    if (d >= freeDrawInterval_ && wpts_.size() < 2000) {
      wpts_.push_back({wx, wy, -1.0});
      lastDrawX_ = wx; lastDrawY_ = wy;
      update(); emit freeDrawChanged();
    }
    return;
  }

  // ── 多边形拖拽 ──
  if (polygonMode_ && dragIdx_ >= 0 &&
      dragIdx_ < (int)polygonVertices_.size()) {
    polygonVertices_[dragIdx_].first  = fromX((int)_event->pos().x());
    polygonVertices_[dragIdx_].second = fromY((int)_event->pos().y());
    update();
    return;
  }

  // ── 多边形悬停提示 ──
  if (polygonMode_ && dragIdx_ < 0) {
    int px = (int)_event->pos().x(), py = (int)_event->pos().y();
    for (size_t i = 0; i < polygonVertices_.size(); ++i) {
      int vx = toX(polygonVertices_[i].first);
      int vy = toY(polygonVertices_[i].second);
      if (std::hypot(px - vx, py - vy) < 8) {
        QToolTip::showText(_event->globalPos(),
            QString("顶点 %1  X=%2  Y=%3")
                .arg(i + 1)
                .arg(polygonVertices_[i].first, 0, 'f', 1)
                .arg(polygonVertices_[i].second, 0, 'f', 1));
        return;
      }
    }
    QToolTip::hideText();
  }

  if (!editable_ || dragIdx_ < 0) return;
  wpts_[dragIdx_].x = fromX((int)_event->pos().x());
  wpts_[dragIdx_].y = fromY((int)_event->pos().y());
  update(); emit waypointsChanged();
}

void MapWidget::mouseReleaseEvent(QMouseEvent *_event) {
  if (_event->button() == Qt::MiddleButton) {
    panning_ = false; setCursor(Qt::ArrowCursor); return;
  }
  if (freeDraw_ && _event->button() == Qt::LeftButton) {
    freeDraw_ = false;
    emit waypointsChanged();
    return;
  }
  dragIdx_ = -1;
}

void MapWidget::mouseDoubleClickEvent(QMouseEvent *_event) {
  if (_event->button() != Qt::LeftButton) return;

  // ── 多边形模式：双击顶点编辑坐标 ──
  if (polygonMode_) {
    int pw = plotW(), ph = plotH();
    if (pw > 0 && ph > 0) {
      double wx = fromX((int)_event->pos().x()),
             wy = fromY((int)_event->pos().y());
      double minDist = 8.0 * (viewXMax_ - viewXMin_) / pw;
      int hitIdx = -1; double best = minDist;
      for (size_t i = 0; i < polygonVertices_.size(); ++i) {
        double d = std::hypot(polygonVertices_[i].first - wx,
                               polygonVertices_[i].second - wy);
        if (d < best) { best = d; hitIdx = (int)i; }
      }
      if (hitIdx >= 0) {
        // 一个对话框同时编辑 X/Y（两个独立 QInputDialog 的体验不直观）
        QDialog dlg(this);
        dlg.setWindowTitle(QString("编辑顶点 %1").arg(hitIdx + 1));
        dlg.setModal(true);
        auto *form = new QFormLayout(&dlg);
        auto *sx = new QDoubleSpinBox(&dlg);
        sx->setRange(-9999, 9999); sx->setDecimals(1);
        sx->setValue(polygonVertices_[hitIdx].first);
        auto *sy = new QDoubleSpinBox(&dlg);
        sy->setRange(-9999, 9999); sy->setDecimals(1);
        sy->setValue(polygonVertices_[hitIdx].second);
        form->addRow("X 坐标:", sx);
        form->addRow("Y 坐标:", sy);
        auto *btns = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        form->addRow(btns);
        if (dlg.exec() != QDialog::Accepted) return;
        polygonVertices_[hitIdx].first  = sx->value();
        polygonVertices_[hitIdx].second = sy->value();
        update();
        return;
      }
    }
    if (polygonVertices_.size() >= 3) {
      emit polygonFinished();
      return;
    }
  }

  // 仅多边形模式时阻止 waypoint 双击逻辑
  if (polygonMode_) return;

  if (!editable_) return;
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;
  double cx = _event->pos().x(), cy = _event->pos().y();
  if (cx < ml() || cx > ml() + pw || cy < mt() || cy > mt() + ph) return;
  double wx = fromX((int)cx), wy = fromY((int)cy);

  // 找最近的航路点
  double minDist = 8.0 * (viewXMax_ - viewXMin_) / pw;
  int idx = -1; double best = minDist;
  for (size_t i = 0; i < wpts_.size(); ++i) {
    double d = std::hypot(wpts_[i].x - wx, wpts_[i].y - wy);
    if (d < best) { best = d; idx = (int)i; }
  }
  if (idx < 0) return;

  bool ok;
  double newHdg = QInputDialog::getDouble(
      this, "航路点航向",
      QString("航路点 %1 的目标航向 (°)\n(-1 = 无要求, 0~360 = 导航航向)").arg(idx + 1),
      wpts_[idx].heading, -1.0, 360.0, 1, &ok);
  if (ok) {
    wpts_[idx].heading = newHdg;
    update(); emit waypointsChanged();
  }
}

bool MapWidget::getRefPoint(int _idx, double &_x, double &_y) const {
  if (_idx < 0 || _idx >= (int)refPoints_.size()) return false;
  _x = refPoints_[_idx].first;
  _y = refPoints_[_idx].second;
  return true;
}

void MapWidget::wheelEvent(QWheelEvent *_event) {
  int pw = plotW(), ph = plotH();
  if (pw <= 0 || ph <= 0) return;

  double cx = _event->position().x(), cy = _event->position().y();
  double wx = fromX((int)cx), wy = fromY((int)cy);
  double zoom = (_event->angleDelta().y() > 0) ? 0.8 : 1.25;
  double newDX = (viewXMax_ - viewXMin_) * zoom;
  double newDY = (viewYMax_ - viewYMin_) * zoom;
  double rx = (wx - viewXMin_) / (viewXMax_ - viewXMin_);
  double ry = (wy - viewYMin_) / (viewYMax_ - viewYMin_);
  viewXMin_ = wx - rx * newDX;
  viewXMax_ = viewXMin_ + newDX;
  viewYMin_ = wy - ry * newDY;
  viewYMax_ = viewYMin_ + newDY;
  scale_ *= (1.0 / zoom);
  update();
}
