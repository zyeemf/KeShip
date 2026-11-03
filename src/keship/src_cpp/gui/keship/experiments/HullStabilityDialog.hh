/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-05 数学船型稳性对比 — 对话框 */

#ifndef HULL_STABILITY_DIALOG_HH_
#define HULL_STABILITY_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
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

  /// ★ 2026-11-03（D10）：无交互导入 SD-08 型线交付物（测试 / 脚本入口）。
  /// 与「导入 SD-08」按钮走**同一份实现**，仅跳过 QFileDialog 与模态提示 ——
  /// 无头环境下弹模态框会让驱动定时器挂起（D2 教训）。
  /// @return false = 文件不可用（原因见 _err）；true 且 _err 非空 = 导入成功但有警告（复现偏差 >2%）
  bool importSd08From(const QString &_path, QString *_err = nullptr);

private slots:
  void onSpawn();
  /// ★ 2026-10-29（D5）：压载位置控件由 QSlider 改为 QDoubleSpinBox（同 HS-04）。
  /// 语义与原 onSliderChanged 相同：防抖 300 ms 后才真正下发（见 onMoveDebounced）。
  void onBallastSpinChanged(double _value);
  /// ★ 2026-09-21 新增：「移动压载」确认按钮（与 HS-04 同一次改造）
  void onApplyWeight();
  /// ★ 2026-09-21 新增：压载移动防抖到期后真正下发
  void onMoveDebounced();
  void onRecord();
  void onDeleteSelected();
  void onResetWeight();
  void onClearAll();
  void onUpdatePose();
  void onHullTypeChanged();
  /// ★ 2026-10-27（L1 链）：导入 SD-08 型线交付物 —— 链判据要求下游真的读上游产出
  void onImportSd08();

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
  /// ★ 2026-09-21 新增：移动压载的**统一入口**（与 HS-04 同一套写法）
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

  /// ★ 2026-10-27（L1 链）：SD-08 交付物导入状态。
  /// 存在的意义：SD-08 的船型由「船型族 + 两个形状指数 + 密度」共同决定，
  /// 只拷 L/B/D 会生成**另一条船**（指数不同 ⇒ 型线不同 ⇒ GM 不同），链就断在"看起来读进去了"。
  bool sd08Imported_ = false;
  int sd08FamilyIndex_ = 0;
  double sd08WlExp_ = 2.0, sd08SecExp_ = 2.0, sd08Density_ = 500.0;
  QLabel *sd08Label_ = nullptr;
  QPushButton *importSd08Btn_ = nullptr;

  QLabel *awLabel_, *cwpLabel_, *ciLabel_;
  QLabel *volLabel_, *kbLabel_, *bmLabel_, *gmLabel_;

  /// ★ 2026-10-29（D5）：QSlider 改为 QDoubleSpinBox（滑条无程序化写入口，驱动写不进）。
  QDoubleSpinBox *weightSpin_;
  /// ★ 2026-09-21：压载位置输入 **由 QDoubleSpinBox 改为 QLineEdit**（同 HS-04）。
  /// QDoubleSpinBox 会把值格式化成固定小数位（decimals=3 → "0.000"），
  /// 于是"在小数点后插入一个 1"必然产生 4 位小数 → 被判非法 → **按键被吞**。
  /// QLineEdit 保留用户原文 + QDoubleValidator（小数位 6）即可自由编辑。
  QLineEdit *weightEdit_;
  QPushButton *applyBtn_;      ///< 「移动压载」确认按钮
  QLabel *heelLabel_, *deltaYLabel_, *wDyLabel_;

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
