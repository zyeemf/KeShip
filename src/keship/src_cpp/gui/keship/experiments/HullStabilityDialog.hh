/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SP-04 数学船型稳性对比 — 对话框 */

#ifndef HULL_STABILITY_DIALOG_HH_
#define HULL_STABILITY_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <QPaintEvent>

#include <memory>
#include <mutex>
#include <vector>
#include <string>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>

#include "ShipHullGenerator.hh"

/// 回调共享位姿数据
struct HullPoseState {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  double heelDeg = 0.0;
  double hullZ = 0.0;
  bool found = false;
};

/// 数据点
struct HullDataPoint {
  double y = 0.0, deltaY = 0.0, heelDeg = 0.0;
  double tanPhi = 0.0, wDy = 0.0;
};

/// 散点图
class HullChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit HullChartWidget(QWidget *_parent = nullptr);
  void setData(const std::vector<HullDataPoint> &_pts,
               double _expGM, double _thGM);
  void clearData();
protected:
  void paintEvent(QPaintEvent *_event) override;
private:
  std::vector<HullDataPoint> pts_;
  double expGM_ = 0.0, thGM_ = 0.0;
  double xMin_ = 0, xMax_ = 100, yMin_ = 0, yMax_ = 0.1;
};

class HullStabilityDialog : public QDialog {
  Q_OBJECT
public:
  HullStabilityDialog(gz::transport::Node &_transportNode,
                      const std::string &_worldName,
                      QWidget *_parent = nullptr);
  ~HullStabilityDialog() override;

private slots:
  void onSpawn();
  void onSliderChanged(int _value);
  /// ★ 2026-09-21 新增：「移动压载」确认按钮（与 SP-03 同一次改造）
  void onApplyWeight();
  /// ★ 2026-09-21 新增：滑条防抖到期后真正下发
  void onMoveDebounced();
  void onRecord();
  void onDeleteSelected();
  void onResetWeight();
  void onClearAll();
  void onUpdatePose();
  void onHullTypeChanged();

private:
  void setupUI();
  void initPoseSubscription();
  void updateHydroDisplay();
  void updateStatusDisplay();
  void updateChart();

  std::string makeHullSDF() const;
  std::string makeWeightSDF(double _y) const;
  bool doDeleteAll();
  bool doSpawnAll();
  bool resolveEntityID(const std::string &_name, uint32_t &_id);
  bool moveWeight(double _y);
  /// ★ 2026-09-21 新增：移动压载的**统一入口**（与 SP-03 同一套写法）
  void commitMove(double _y);
  void computeLinearFit(const std::vector<HullDataPoint> &_pts,
                         double &_slope, double &_intercept) const;

  gz::transport::Node &transportNode_;
  std::string worldName_;
  std::string hullName_ = "SP03_hull";
  std::string weightName_ = "SP03_weight";
  std::string tempDir_;  // DAE 文件临时目录

  std::shared_ptr<HullPoseState> poseState_;
  std::string poseTopic_;

  QComboBox *hullTypeCombo_;
  QDoubleSpinBox *lengthSpin_, *breadthSpin_, *depthSpin_;

  QLabel *awLabel_, *cwpLabel_, *ciLabel_;
  QLabel *volLabel_, *kbLabel_, *bmLabel_, *gmLabel_;

  QSlider *weightSlider_;
  /// ★ 2026-09-21：压载位置输入 **由 QDoubleSpinBox 改为 QLineEdit**（同 SP-03）。
  /// QDoubleSpinBox 会把值格式化成固定小数位（decimals=3 → "0.000"），
  /// 于是"在小数点后插入一个 1"必然产生 4 位小数 → 被判非法 → **按键被吞**。
  /// QLineEdit 保留用户原文 + QDoubleValidator（小数位 6）即可自由编辑。
  QLineEdit *weightEdit_;
  QPushButton *applyBtn_;      ///< 「移动压载」确认按钮
  QLabel *sliderValueLabel_, *heelLabel_, *deltaYLabel_, *wDyLabel_;

  QPushButton *spawnBtn_, *recordBtn_, *resetBtn_, *clearBtn_;

  QTableWidget *dataTable_;
  HullChartWidget *chartWidget_;
  std::vector<HullDataPoint> recordedData_;

  QTimer *poseTimer_;
  QTimer *moveDebounce_ = nullptr;   ///< ★ 2026-09-21：滑条防抖（300 ms）
  double pendingY_ = 0.0;            ///< 防抖期间待下发的位置

  uint32_t hullEntityId_ = 0;
  uint32_t weightEntityId_ = 0;
  bool spawned_ = false;
  double currentHeelDeg_ = 0.0, currentHullZ_ = 0.0, weightY_ = 0.0;
  double lastHeelDeg_ = 0.0;
  int settleCounter_ = 0;
  bool quiescent_ = false;

  keship::ShipHullSpec params_;
  keship::HydroProperties hydro_;
  keship::HydroProperties spawnedHydro_;  // spawn 时快照，updateChart 用此值

  static constexpr double kWeightForce = 1000.0;
  static constexpr double kWeightMass = 102.0;
  static constexpr double kFluidDensity = 1000.0;
  static constexpr double kGravity = 9.81;
};

#endif
