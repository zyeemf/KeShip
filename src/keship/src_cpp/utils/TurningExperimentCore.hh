/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 —— **核心层**（不依赖 Qt；GUI 对话框与无头入口共用同一份实现）
 *
 * 设计原则（2026-10-14 张老师确认）：
 *   「GUI 对话框设定参数后点击确定」所调用的**函数与代码**，与无头测试**必须是同一份**。
 *   ⇒ 把发推力 / 采样 / 指标计算从对话框里**下沉**到这里；
 *     对话框退化为「薄壳」（只管界面），`keship_ctl exp ms01-run` 直接驱动核心层。
 *
 * 依据先例：`ModelManager::LoadModelFromKsmFile()`（2026-09-24，KSM 加载下沉，GUI 与 CTL 共用）。
 */

#ifndef KESHIP_TURNING_EXPERIMENT_CORE_HH_
#define KESHIP_TURNING_EXPERIMENT_CORE_HH_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>

namespace keship {

/// 轨迹采样点
struct TurningSample {
  double t = 0.0;        ///< 相对起始的时间 (s)
  double x = 0.0;        ///< 世界 x (m)
  double y = 0.0;        ///< 世界 y (m)
  double yawDeg = 0.0;   ///< 航向角 (°)
};

/// 回转性指标（IMO 回转试验口径）
struct TurningMetrics {
  double advance = -1.0;    ///< 进距：转向 90° 时沿原航向的前进距离 (m)
  double tactical = -1.0;   ///< 战术直径：转向 180° 时的横向位移 (m)
  double turning = -1.0;    ///< 回转直径（**定常段最小二乘圆拟合**；不足时退回包围盒估计）
  double turningBox = -1.0; ///< 回转直径（包围盒估计，仅作对照）
  double fitR2 = -1.0;      ///< 圆拟合优度 R²（判定定常段是否可信，越接近 1 越好）
  int    fitPoints = 0;     ///< 参与拟合的点数
  bool   steadyTurn = false;///< 是否**达到定常回转**（R² ≥ 阈值）；false ⇒ turning 退化为战术直径
  double maxX = 0.0, maxY = 0.0;  ///< 轨迹包围盒（估计用）
  int    points = 0;
  bool   valid = false;
};

/// 位姿回调共享状态
struct TurningPoseShared {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  bool found = false;
};

class TurningExperimentCore {
public:
  TurningExperimentCore(gz::transport::Node &_node, const std::string &_worldName);
  ~TurningExperimentCore();

  TurningExperimentCore(const TurningExperimentCore &) = delete;
  TurningExperimentCore &operator=(const TurningExperimentCore &) = delete;

  /// 设定目标（模型名 + 左右推进器名）
  void setTarget(const std::string &_model,
                 const std::string &_thrusterL,
                 const std::string &_thrusterR);

  void setSamplePeriod(double _s) { period_ = _s > 0 ? _s : 0.1; }
  double samplePeriod() const { return period_; }

  /// 下发推力（N）。GUI「开始」与无头运行器调用的是**同一个函数**。
  void sendThrust(double _left, double _right);

  /// 周期性重发当前推力（防 gz-transport 首帧丢失）
  void republishThrust();

  /// 开始记录：订阅位姿 + 清轨迹 + 标记起点
  bool start();

  /// 停止：推力归零 + 结束记录（指标可读）
  void stop();

  /// 清零：清轨迹与指标
  void clear();

  /// 采一帧：读位姿 → 追加轨迹点 → 更新速度。返回是否成功采到。
  bool sample();

  /// 读当前位姿（目标模型）
  bool fetchPose(double &_x, double &_y, double &_yawDeg) const;

  const std::vector<TurningSample> &trail() const { return trail_; }
  double speed() const { return speed_; }
  bool running() const { return running_; }
  double elapsed() const { return elapsed_; }

  /// 指标（基于当前轨迹）
  TurningMetrics metrics() const;

  /// ★ 纯函数：从任意轨迹序列计算指标（便于**独立复算/单元测试**，不依赖 gz）
  static TurningMetrics computeMetrics(const std::vector<TurningSample> &_trail,
                                       double _startX, double _startY, double _startYaw);

  /// 导出 CSV（t,x,y,yaw_deg）
  bool exportCsv(const std::string &_path) const;

private:
  bool ensurePublishers();

  gz::transport::Node &node_;
  std::string worldName_;
  std::string poseTopic_;

  // 目标
  std::string model_, thrL_, thrR_;
  double period_ = 0.1;

  // 运行状态
  bool   running_ = false;
  bool   started_ = false;
  double elapsed_ = 0.0;
  double speed_ = 0.0;
  double startX_ = 0.0, startY_ = 0.0, startYaw_ = 0.0;
  double prevX_ = 0.0, prevY_ = 0.0;
  std::vector<TurningSample> trail_;

  // 位姿订阅
  std::shared_ptr<TurningPoseShared> pose_;
  bool subscribed_ = false;

  // 推力发布（缓存）
  gz::transport::Node::Publisher pubL_, pubR_;
  std::string pubKeyL_, pubKeyR_;
  double curLeft_ = 0.0, curRight_ = 0.0;   ///< 当前推力（sample 时重发，防首帧丢失）
};

}  // namespace keship

#endif  // KESHIP_TURNING_EXPERIMENT_CORE_HH_
