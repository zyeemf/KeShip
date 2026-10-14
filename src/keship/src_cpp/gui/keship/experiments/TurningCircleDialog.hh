/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 — 对话框
 *
 * 实验内容：对船模施加**差动推力**（左/右推进器不同推力）→ 船做回转运动
 *           → 记录轨迹 → 计算 **回转直径 / 战术直径 / 进距 / 横距**
 * 依据：IMO 操纵性衡准（回转试验）；⚠ 平台**不含舵** ⇒ 用双推进器差动替代舵角
 *       （S4 决定，指导书须交代）
 * 参照实现：HullStabilityDialog（订阅位姿 → 记录数据点 → 自绘图表）
 */

#ifndef TURNING_CIRCLE_DIALOG_HH_
#define TURNING_CIRCLE_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTimer>
#include <QWidget>
#include <QPaintEvent>

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>

/// 回调共享位姿数据（与 HullStabilityDialog 同构）
struct TurningPoseState {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  bool found = false;
};

/// 轨迹点
struct TurningPoint {
  double t = 0.0;         ///< 仿真时间 (s)
  double x = 0.0;         ///< 世界 x (m)
  double y = 0.0;         ///< 世界 y (m)
  double yawDeg = 0.0;    ///< 航向角 (°)
};

/// 回转轨迹图（自绘，x-y 平面）
class TurningChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit TurningChartWidget(QWidget *_parent = nullptr);
  void setTrail(const std::vector<TurningPoint> &_pts);
  void clearTrail();
protected:
  void paintEvent(QPaintEvent *_event) override;
private:
  std::vector<TurningPoint> pts_;
  double xMin_ = -10, xMax_ = 10, yMin_ = -10, yMax_ = 10;
};

class TurningCircleDialog : public QDialog {
  Q_OBJECT
public:
  /// @param _transportNode 由 MenuPluginKeship 传入的 gz-transport 节点
  /// @param _worldName 世界名（位姿话题 /world/<name>/pose/info）
  /// @param _models 可选模型名列表（下拉）
  TurningCircleDialog(gz::transport::Node &_transportNode,
                      const std::string &_worldName,
                      const QStringList &_models,
                      QWidget *_parent = nullptr);
  ~TurningCircleDialog() override;

private slots:
  void onStart();          ///< 开始：下发差动推力 + 启动采样
  void onStop();           ///< 停止：推力归零 + 停采样
  void onClear();          ///< 清零：清轨迹与结果
  void onTick();           ///< 定时采样：读位姿 → 追加轨迹点
  void onExport();         ///< 导出 CSV

private:
  void sendThrust(double _left, double _right);
  void updateReadout();
  void computeResults();   ///< 回转直径 / 战术直径 / 进距 / 横距
  bool fetchModelPose(double &_x, double &_y, double &_yawDeg);

  // --- 参数 ---
  QComboBox      *modelCombo_  = nullptr;
  QLineEdit      *leftNameEdit_  = nullptr;   ///< 左推进器名（默认 thrust_left）
  QLineEdit      *rightNameEdit_ = nullptr;   ///< 右推进器名（默认 thrust_right）
  QDoubleSpinBox *leftThrustSpin_  = nullptr; ///< 左推力 (N)
  QDoubleSpinBox *rightThrustSpin_ = nullptr; ///< 右推力 (N)
  QDoubleSpinBox *periodSpin_  = nullptr;     ///< 采样周期 (s)

  // --- 控制 ---
  QPushButton *startBtn_ = nullptr;
  QPushButton *stopBtn_  = nullptr;
  QPushButton *clearBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  // --- 显示 ---
  TurningChartWidget *chart_ = nullptr;
  QLabel *readoutLabel_ = nullptr;   ///< 实时读数
  QLabel *resultLabel_  = nullptr;   ///< 结果（回转直径等）

  // --- 运行状态 ---
  QTimer *timer_ = nullptr;
  bool    running_ = false;
  double  t0_ = -1.0;                ///< 起始仿真时间（-1 = 未定）
  double  prevX_ = 0.0, prevY_ = 0.0, prevT_ = 0.0;
  double  speed_ = 0.0;              ///< 瞬时速度 (m/s)
  double  maxX_ = 0.0, maxY_ = 0.0;  ///< 轨迹包围盒（算进距/横距）
  double  startX_ = 0.0, startY_ = 0.0, startYaw_ = 0.0;
  std::vector<TurningPoint> trail_;

  // --- 位姿订阅 ---
  gz::transport::Node &transportNode_;
  std::string worldName_;
  std::string poseTopic_;
  std::shared_ptr<TurningPoseState> poseState_;

  // --- 推力发布（缓存 Publisher，避免每次重建） ---
  gz::transport::Node::Publisher pubLeft_, pubRight_;
  std::string pubKeyL_, pubKeyR_;
};

#endif // TURNING_CIRCLE_DIALOG_HH_
