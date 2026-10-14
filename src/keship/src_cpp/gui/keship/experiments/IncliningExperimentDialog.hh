/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-04 初稳性倾斜实验对话框 — 矩形驳船 + 移动压载 */
/// 2026.5.28 创建

#ifndef INCLINING_EXPERIMENT_DIALOG_HH_
#define INCLINING_EXPERIMENT_DIALOG_HH_

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

/// 回调与对话框之间共享的位姿数据，独立于对话框生命周期
struct IncliningPoseState
{
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  double heelDeg = 0.0;
  bool found = false;
};

/// 单次记录的数据点
struct IncliningDataPoint
{
  double y = 0.0;
  double deltaY = 0.0;
  double heelDeg = 0.0;
  double tanPhi = 0.0;
  double wDy = 0.0;
};

/// w·Δy vs tanφ 散点图 + OLS 拟合线
class IncliningChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit IncliningChartWidget(QWidget *_parent = nullptr);

  void setData(const std::vector<IncliningDataPoint> &_pts,
               double _expGM, double _thGM);
  void clearData();

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  std::vector<IncliningDataPoint> pts_;
  double expGM_ = 0.0;
  double thGM_ = 0.0;
  double xMin_ = 0.0, xMax_ = 100.0, yMin_ = 0.0, yMax_ = 0.1;
};

class IncliningExperimentDialog : public QDialog
{
  Q_OBJECT

public:
  IncliningExperimentDialog(
      gz::transport::Node &_transportNode,
      const std::string &_worldName,
      QWidget *_parent = nullptr);

  ~IncliningExperimentDialog() override;

private slots:
  void onSpawn();
  void onSliderChanged(int _value);
  /// ★ 2026-09-21 新增：「移动压载」确认按钮 —— 输入位置后**点它才真正下发**
  /// （原设计是输入框一改就下发，而下发走的是阻塞 RPC，见下方 commitMove 注释）
  void onApplyWeight();
  /// ★ 2026-09-21 新增：滑条防抖到期后真正下发（避免拖动时每格一次阻塞 RPC）
  void onMoveDebounced();
  void onRecord();
  void onDeleteSelected();
  void onResetWeight();
  void onClearAll();
  void onUpdatePose();

private:
  void setupUI();
  void initPoseSubscription();
  void updateTheoryDisplay();
  void updateStatusDisplay();
  void updateChart();

  std::string makeBargeSDF(double _L, double _B, double _H,
                            double _density) const;
  std::string makeWeightSDF(double _y) const;
  bool doDeleteBoth();
  bool doSpawnBoth();
  bool resolveEntityID(const std::string &_name, uint32_t &_id);
  bool moveWeight(double _y);
  /// ★ 2026-09-21 新增：移动压载的**统一入口**（记录 y、重置判稳、下发、刷新面板）。
  /// 所有路径（确认按钮 / 滑条防抖 / 归零 / 复位）都必须走它，保证状态一致。
  void commitMove(double _y);

  double computeTheoryGM(double _L, double _B, double _H,
                          double _density) const;
  double computeTheoryDraft(double _density, double _H) const;
  void computeLinearFit(const std::vector<IncliningDataPoint> &_pts,
                         double &_slope, double &_intercept) const;

  gz::transport::Node &transportNode_;
  std::string worldName_;
  std::string bargeName_ = "SP02_barge";
  std::string weightName_ = "SP02_weight";

  std::shared_ptr<IncliningPoseState> poseState_;
  std::string poseTopic_;

  /// 参数输入
  QDoubleSpinBox *lengthSpin_;
  QDoubleSpinBox *breadthSpin_;
  QDoubleSpinBox *heightSpin_;
  QDoubleSpinBox *densitySpin_;

  /// 理论显示
  QLabel *draftLabel_;
  QLabel *gmTheoryLabel_;

  /// 压载控制
  QSlider *weightSlider_;
  /// ★ 2026-09-21：**由 QDoubleSpinBox 改为 QLineEdit**。
  /// 原因：QDoubleSpinBox 会把值**自动格式化成固定小数位**（`setDecimals(3)` → "0.000"），
  /// 于是"在小数点后插入一个 1"必然产生 4 位小数 → 被 validate 判非法 → **按键被吞**
  /// （张老师实测："想把 0.000 改成 0.1，无法直接写 1"）。QLineEdit 保留用户原文，
  /// 配合 QDoubleValidator（小数位放宽到 6）即可自由编辑，范围仍受控。
  QLineEdit *weightEdit_;
  QPushButton *applyBtn_;          ///< 「移动压载」确认按钮（输入后点击才生效）
  QLabel *sliderValueLabel_;
  QLabel *heelLabel_;
  QLabel *deltaYLabel_;
  QLabel *wDyLabel_;

  /// 按钮
  QPushButton *spawnBtn_;
  QPushButton *recordBtn_;
  QPushButton *resetBtn_;
  QPushButton *clearBtn_;

  /// 数据
  QTableWidget *dataTable_;
  IncliningChartWidget *chartWidget_;
  std::vector<IncliningDataPoint> recordedData_;

  /// 定时器
  QTimer *poseTimer_;
  QTimer *moveDebounce_ = nullptr;   ///< ★ 2026-09-21：滑条防抖（300 ms），见 onSliderChanged

  /// 状态
  uint32_t bargeEntityId_ = 0;
  uint32_t weightEntityId_ = 0;
  bool spawned_ = false;
  double currentHeelDeg_ = 0.0;
  double weightY_ = 0.0;
  double pendingY_ = 0.0;        ///< 防抖期间待下发的目标位置（滑条拖动用）
  double lastHeelDeg_ = 0.0;
  int settleCounter_ = 0;
  bool quiescent_ = false;

  static constexpr double kWeightForce = 1000.0;
  static constexpr double kWeightMass = 102.0;
  static constexpr double kFluidDensity = 1000.0;
  static constexpr double kGravity = 9.81;
};

#endif  // INCLINING_EXPERIMENT_DIALOG_HH_
