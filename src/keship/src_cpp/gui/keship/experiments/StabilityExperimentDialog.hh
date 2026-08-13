/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SP-02 漂浮圆柱体初稳性实验对话框 */
/// 2026.5.27 重构：从配载-吃水模式改为初稳性观测模式

#ifndef STABILITY_EXPERIMENT_DIALOG_HH_
#define STABILITY_EXPERIMENT_DIALOG_HH_

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <QPaintEvent>
#include <QButtonGroup>

#include <memory>
#include <mutex>
#include <vector>
#include <string>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>

/// 回调与对话框之间共享的位姿数据，独立于对话框生命周期
struct StabilityPoseState
{
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  double heelDeg = 0.0;
  double draft = 0.0;
  bool found = false;
};

/// 稳定域图控件（s vs D/L 参数平面 + 理论临界曲线）
class StabilityChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit StabilityChartWidget(QWidget *_parent = nullptr);

  void setData(const std::vector<std::pair<double, double>> &_stable,
               const std::vector<std::pair<double, double>> &_unstable);
  void clearData();

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  std::vector<std::pair<double, double>> stablePts_;
  std::vector<std::pair<double, double>> unstablePts_;
  double sMin_ = 0.0, sMax_ = 1.0, dlMin_ = 0.0, dlMax_ = 2.5;
};

class StabilityExperimentDialog : public QDialog
{
  Q_OBJECT

public:
  StabilityExperimentDialog(
      gz::transport::Node &_transportNode,
      const std::string &_worldName,
      QWidget *_parent = nullptr);

  ~StabilityExperimentDialog() override;

private slots:
  void onSpawn();
  void onParamChanged();
  void onRecord();
  void onDeleteSelected();
  void onClearAll();
  void onUpdatePose();

private:
  void setupUI();
  void initPoseSubscription();
  void updateTheoryDisplay();
  void updateStatusDisplay();
  bool doDeleteCylinder();
  bool doSpawnCylinder(double _diameter, double _length,
                        double _density, double _rollDeg);
  std::string makeCylinderSDF(double _diameter, double _length,
                               double _density, double _rollDeg) const;
  double computeGM(double _d, double _l, double _s) const;

  gz::transport::Node &transportNode_;
  std::string worldName_;
  std::string cylinderName_;
  std::shared_ptr<StabilityPoseState> poseState_;
  std::string poseTopic_;

  /// 参数输入
  QDoubleSpinBox *diameterSpin_;
  QDoubleSpinBox *lengthSpin_;
  QDoubleSpinBox *densitySpin_;

  /// 扰动
  QRadioButton *lightPerturbBtn_;
  QRadioButton *mediumPerturbBtn_;
  QRadioButton *heavyPerturbBtn_;

  /// 理论显示
  QLabel *sLabel_;
  QLabel *dlLabel_;
  QLabel *gmLabel_;
  QLabel *dlCritLabel_;

  /// 实时状态
  QLabel *heelLabel_;
  QLabel *draftLabel_;
  QLabel *statusLabel_;

  /// 按钮
  QPushButton *spawnBtn_;
  QPushButton *recordBtn_;
  QPushButton *clearBtn_;

  /// 数据
  QTableWidget *dataTable_;
  StabilityChartWidget *chartWidget_;

  /// 定时器
  QTimer *poseTimer_;

  /// 图表数据
  std::vector<std::pair<double, double>> stablePts_;
  std::vector<std::pair<double, double>> unstablePts_;

  /// 当前值
  double currentHeelDeg_ = 0.0;
  double currentDraft_ = 0.0;
  int observeCounter_ = 0;
  bool observing_ = false;
  int stabilityResult_ = 0;  // 0=未判定, 1=稳定, 2=不稳定, 3=临界

  static constexpr int kObserveFrames = 150;
  static constexpr double kFluidDensity = 1000.0;
};

#endif  // STABILITY_EXPERIMENT_DIALOG_HH_
