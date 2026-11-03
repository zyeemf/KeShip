/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-04 方向稳定性与 Z 形试验 —— **核心层**（不依赖 Qt；GUI 与无头共用）
 *
 * 实验内容：交替施加**左右差动推力**（模拟「操舵」）⇒ 记录航向响应 ⇒
 *           测量 **超调角（overshoot）** 与 **周期**（Z 形试验标准指标）。
 * 依据：IMO 操纵性衡准（Zigzag test）；⚠ 平台**不含舵** ⇒ 用差动推力替代舵角（S4 决定）。
 * 参照实现：TurningExperimentCore（MS-01，同族）
 */

#ifndef KESHIP_ZIGZAG_EXPERIMENT_CORE_HH_
#define KESHIP_ZIGZAG_EXPERIMENT_CORE_HH_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>

namespace keship {

/// Z 形采样点
struct ZigzagSample {
  double t = 0.0;
  double yawDeg = 0.0;
  double thrustL = 0.0, thrustR = 0.0;
  int    phase = 0;        ///< 0=左转 1=右转
};

/// Z 形指标
struct ZigzagMetrics {
  double overshoot1 = -1.0;  ///< 第一次换向后的超调角 (°)
  double overshoot2 = -1.0;  ///< 第二次换向后的超调角 (°)
  double period = -1.0;      ///< 换向周期 (s)
  double targetYaw = -1.0;   ///< 目标航向变化 (°)
  int    switches = 0;       ///< 换向次数
  int    points = 0;
  bool   valid = false;
};

/// 位姿回调共享状态
struct ZigzagPoseShared {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  bool found = false;
};

class ZigzagExperimentCore {
public:
  ZigzagExperimentCore(gz::transport::Node &_node, const std::string &_worldName);
  ~ZigzagExperimentCore();

  ZigzagExperimentCore(const ZigzagExperimentCore &) = delete;
  ZigzagExperimentCore &operator=(const ZigzagExperimentCore &) = delete;

  void setTarget(const std::string &_model,
                 const std::string &_thrusterL,
                 const std::string &_thrusterR);

  /// @param _base 基础推力 (N) @param _delta 差动量 (N)
  /// @param _targetYaw 触发换向的航向变化 (°)
  /// @param _period 采样周期 (s)
  void setParams(double _base, double _delta, double _targetYaw, double _period);

  bool start();
  void stop();
  void clear();
  bool sample();                        ///< 每帧：判换向 + 发推力 + 记录
  void sendThrust(double _left, double _right);
  void republishThrust();

  bool fetchPose(double &_x, double &_y, double &_yawDeg) const;

  const std::vector<ZigzagSample> &trail() const { return trail_; }
  bool running() const { return running_; }
  int  phase() const { return phase_; }
  ZigzagMetrics metrics() const;

  /// ★ 纯函数：从轨迹计算指标（便于独立复算）
  static ZigzagMetrics computeMetrics(const std::vector<ZigzagSample> &_trail,
                                      double _startYaw, double _targetYaw);

  bool exportCsv(const std::string &_path) const;

private:
  bool ensurePublishers();

  gz::transport::Node &node_;
  std::string worldName_, poseTopic_;

  std::string model_, thrL_, thrR_;
  double base_ = 100.0, delta_ = 50.0, targetYaw_ = 20.0, period_ = 0.1;

  bool   running_ = false;
  double elapsed_ = 0.0;
  int    phase_ = 0;                    ///< 0=左转 1=右转
  double startYaw_ = 0.0;
  double cumYaw_ = 0.0;                 ///< ★ 累积航向（逐点累加，避免 ±180 环绕抖动）
  double phaseStartCum_ = 0.0;          ///< 本相位起始的累积航向
  double prevYaw_ = 0.0;
  double lastSwitchT_ = -1.0;           ///< 上次换向时刻（-1 = 未换过）
  double minSwitchGap_ = 1.0;           ///< ★ 最小换向间隔 (s)，防判据抖动
  std::vector<ZigzagSample> trail_;

  std::shared_ptr<ZigzagPoseShared> pose_;
  gz::transport::Node::Publisher pubL_, pubR_;
  std::string pubKeyL_, pubKeyR_;
  double curLeft_ = 0.0, curRight_ = 0.0;
};

}  // namespace keship

#endif  // KESHIP_ZIGZAG_EXPERIMENT_CORE_HH_
