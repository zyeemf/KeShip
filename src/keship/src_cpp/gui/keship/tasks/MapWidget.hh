/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: 2D 地图组件 — 航路点编辑 + 任务监控显示 */

#ifndef MAP_WIDGET_HH_
#define MAP_WIDGET_HH_

#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <vector>

#include "keship/utils/WaterArea.hh"

struct Waypoint {
  double x = 0.0, y = 0.0;
  double heading = -1.0;  // < 0 = 无航向要求; [0,360) = 导航惯例(0=北/世界+Y,顺时针)
};

class MapWidget : public QWidget {
  Q_OBJECT
 public:
  explicit MapWidget(QWidget *_parent = nullptr);

  // 数据设置
  void setWaypoints(const std::vector<Waypoint> &_wpts);
  void setWaypointStatus(int _idx, int _status);  // 0=未访问, 1=当前目标, 2=已到达
  void setShipPose(double _x, double _y, double _heading);
  void addTrailPoint(double _x, double _y);
  void clearTrail();
  void clearWaypoints();

  // DP 目标设置
  void setDpTarget(double _x, double _y, double _tolerance);
  void clearDpTarget();

  // goto 目标设置
  void setGotoTarget(double _x, double _y, double _radius);

  // 视图控制
  void setAutoFollow(bool _on) { autoFollow_ = _on; }
  void setEditable(bool _on) { editable_ = _on; }
  bool isEditable() const { return editable_; }
  void setTrackingMode(bool _on) { trackingMode_ = _on; }
  void setTrackingTolerance(double _tol) { trackingTol_ = _tol; }
  void setTrackingShape(bool _on) { trackingShape_ = _on; }  // 预设形状模式（点击设参考点）
  void setRefPointCount(int _n) { refPointsNeeded_ = _n; refPoints_.clear(); }
  void setFreeDrawInterval(double _m) { freeDrawInterval_ = _m; }
  bool getRefPoint(int _idx, double &_x, double &_y) const;
  void clearRefPoints() { refPoints_.clear(); update(); }
  void setHintText(const QString &_text);

  void setPolygonMode(bool _on);
  void setPolygonVertices(
      const std::vector<std::pair<double,double>> &_verts);
  const std::vector<std::pair<double,double>>& polygonVertices() const
      { return polygonVertices_; }

  void setSceneModels(const std::vector<keship::ModelFootprint> &_models);
  void clearSceneModels() { sceneModels_.clear(); update(); }
  void fitToSceneModels();
  void setReferenceWaterAreas(
      const std::vector<keship::WaterArea> &_areas);  // 只读参考水域（不参与编辑交互）
  void fitToAll();   // 当前多边形 + 场景模型 + 参考水域并集适配视图

  void setModelRenderScheme(keship::ModelRenderScheme _s)
      { renderScheme_ = _s; update(); }
  keship::ModelRenderScheme modelRenderScheme() const
      { return renderScheme_; }

  // 查询
  const std::vector<Waypoint> &waypoints() const { return wpts_; }

 signals:
  void waypointsChanged();
  void refPointPlaced(int _idx, double _x, double _y);  // 参考点已放置
  void freeDrawChanged();                                 // 自定义轨迹改变
  void polygonFinished();                                 // 多边形闭合

 protected:
  void paintEvent(QPaintEvent *_event) override;
  void mousePressEvent(QMouseEvent *_event) override;
  void mouseMoveEvent(QMouseEvent *_event) override;
  void mouseReleaseEvent(QMouseEvent *_event) override;
  void mouseDoubleClickEvent(QMouseEvent *_event) override;
  void resizeEvent(QResizeEvent *_event) override;
  void wheelEvent(QWheelEvent *_event) override;

 private:
  void autoPanToShip();
  void initViewScale();
  void fitViewToWidget();
  int toX(double _wx) const;
  int toY(double _wy) const;
  double fromX(int _px) const;
  double fromY(int _py) const;
  int ml() const { return 40; }
  int mr() const { return 20; }
  int mt() const { return 15; }
  /// 底部留白：绘图区下方要依次容纳「X 轴刻度 → 操作提示 → 范围信息」
  /// 三行文字。提示为空时只需两行（刻度 + 范围信息），故动态取值，
  /// 避免无谓压缩绘图区高度（2026-09-18 修复刻度与提示重叠）。
  int mb() const { return hintText_.isEmpty() ? 35 : 50; }
  int plotW() const { return width() - ml() - mr(); }
  int plotH() const { return height() - mt() - mb(); }

  std::vector<Waypoint> wpts_;
  std::vector<int> wpStatus_;       // 平行于 wpts_，每航路点状态码
  struct TrailPt { double x, y; };
  std::vector<TrailPt> trail_;
  double shipX_ = 0, shipY_ = 0, shipHdg_ = 0;
  bool showShip_ = false;
  bool autoFollow_ = true;          // 自动平移跟踪船位
  bool editable_ = true;            // 是否允许编辑航路点（监控模式下禁用）
  bool trackingMode_ = false;
  bool trackingShape_ = false;      // true=预设形状(点击设参考点), false=自定义拖拽
  double trackingTol_ = 3.0;
  bool freeDraw_ = false;           // 正在自由绘制中
  double freeDrawInterval_ = 2.0;   // 自由绘制采样间距 (m)
  double lastDrawX_ = 0, lastDrawY_ = 0;  // 上一个采样点
  std::vector<std::pair<double,double>> refPoints_;  // 参考点列表
  int refPointsNeeded_ = 0;

  // 多边形绘制
  bool polygonMode_ = false;
  std::vector<std::pair<double,double>> polygonVertices_;

  // 场景模型足迹
  std::vector<keship::ModelFootprint> sceneModels_;
  keship::ModelRenderScheme renderScheme_ = keship::ModelRenderScheme::ProjectedImage;  // ProjectedImage, ConvexHull

  // 只读参考水域（编辑水域时展示其他水域位置，不可交互）
  std::vector<keship::WaterArea> referenceAreas_;

  void paintReferenceAreas(QPainter &_p) const;
  void paintModelRect(QPainter &_p, const keship::ModelFootprint &_m,
      int _pw, int _ph) const;
  void paintModelPolygon(QPainter &_p, const keship::ModelFootprint &_m,
      const std::vector<std::pair<double,double>> &_outline,
      int _pw, int _ph) const;
  void paintModelSubPolygons(QPainter &_p, const keship::ModelFootprint &_m,
      const std::vector<std::vector<std::pair<double,double>>> &_outlines,
      int _pw, int _ph) const;
  void paintModelImage(QPainter &_p, const keship::ModelFootprint &_m,
      int _pw, int _ph) const;

  // DP 目标
  bool showDp_ = false;
  double dpX_ = 0, dpY_ = 0, dpTol_ = 3.0;

  // goto 目标
  bool showGoto_ = false;
  double gotoX_ = 0, gotoY_ = 0, gotoR_ = 3.0;

  int dragIdx_ = -1;
  bool panning_ = false;
  QString hintText_ = "左键添加航路点  拖拽移动  右键删除  双击设置航向";
  QPointF panStart_;

  double viewXMin_ = -20, viewXMax_ = 80;
  double viewYMin_ = -30, viewYMax_ = 30;
  double scale_ = 1.0;
};

#endif
