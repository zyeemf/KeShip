/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-01~SC-12 控制设计链（L6）+ 感知→控制链（L7）—— **核心层**（不依赖 Qt）
 *
 * 体例完全照 `TurningExperimentCore`（2026-10-14 张老师确认的「同一份实现」原则）：
 *   GUI 对话框点「开始」调用的函数，与无头入口 / 单元测试调用的是**同一份代码**；
 *   发推力 / 每帧采样 / 指标计算全部下沉在这里，对话框退化为薄壳。
 *
 * 链位置：
 *   L6 控制设计链：SC-01 开环闭环 → SC-02 PID 参数 → SC-03 扰动 → SC-04 航向 →
 *                  SC-05 航速 → SC-06 LOS 循迹* → SC-07 DP → SC-09 自整定 → SC-10 航路点 →
 *                  SC-11 COLREGs → SC-12 编队
 *   L7 感知→控制链：SC-08 位姿估计 → **SC-06**（`useEstimatedPose` 用 SC-08 的估计航向闭环）
 *   （* SC-06 同时是 L7 的节点）
 *
 * 纯算法在 `ControlLaws.hh`（无 gz / 无 Qt），本文件只负责**与仿真打交道**的那一半：
 * 订阅 `/world/<world>/pose/info` + `/world/<world>/stats`，发布 `/keship/thrust/<模型>/<桨>`。
 *
 * ⚠️ 两处已知边界（详见交付报告 §5，**不要当成实现 bug**）：
 *   ① COLREGs 只建模**单个静止障碍**（真实 COLREGs 需要会遇船 + 相对运动 + 会遇局面判定）；
 *   ② 编队没有真实僚船，用**虚拟领航者（起点锚定）+ 固定偏移**替代多船一致性协议。
 */

#ifndef KESHIP_CONTROL_EXP_CORE_HH_
#define KESHIP_CONTROL_EXP_CORE_HH_

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/world_stats.pb.h>
#include <gz/transport/Node.hh>

#include "ControlLaws.hh"

namespace keship {

/// 位姿回调共享状态（与 TurningExperimentCore 同构，析构时置 alive=false 防 UAF）
struct ControlPoseShared {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  bool found = false;
};

struct ControlClockShared {
  std::mutex mutex;
  bool alive = true;
  bool found = false;
  double simTimeSec = 0.0;
};

class ControlExpCore {
public:
  ControlExpCore(gz::transport::Node &_node, const std::string &_worldName);
  ~ControlExpCore();

  ControlExpCore(const ControlExpCore &) = delete;
  ControlExpCore &operator=(const ControlExpCore &) = delete;

  /// 目标：模型名 + 左右推进器名（话题 = /keship/thrust/<模型>/<桨>）
  void setTarget(const std::string &_model, const std::string &_thrL,
                 const std::string &_thrR);
  void setParams(const ControlParams &_p);
  void setWaypoints(const std::vector<std::pair<double, double>> &_wps);
  void clear();

  bool start();
  void stop();

  /// 采样 → 算控制量 → 发推力 → 记录。返回本帧是否采到有效位姿。
  bool sample();

  bool fetchPose(double &_x, double &_y, double &_yawDeg) const;

  bool running() const { return running_; }
  double elapsed() const { return elapsed_; }
  const std::vector<ControlSample> &trail() const { return trail_; }
  ControlMetrics metrics() const;
  bool exportCsv(const std::string &_path) const;

  // ─── 扩展（对话框 / 无头 / 测试用；不改变上面约定的签名） ───
  void setSamplePeriod(double _s) { period_ = (_s > 0.0 && _s < 2.0) ? _s : 0.1; }
  double samplePeriod() const { return period_; }
  const ControlParams &params() const { return p_; }
  const std::vector<std::pair<double, double>> &waypoints() const { return wps_; }
  int waypointsReached() const { return wpReached_; }
  int waypointsTotal() const { return static_cast<int>(wps_.size()); }
  bool hasSimulationClock() const;
  double speedEstimate() const { return speed_; }
  double courseEstimateDeg() const { return course_; }
  /// 艏向↔航迹向标定值（起航后一次性锁定，∈ {0,±90,180}）
  double yawCourseOffsetDeg() const { return yawCourseOffset_; }
  bool   yawCourseOffsetLocked() const { return offsetLocked_; }

  /// L7：导入 SC-08 的航向漂移曲线（t → est−ref，度）。空表 = 清除。
  void setYawEstimateDrift(const std::vector<std::pair<double, double>> &_tDriftDeg);
  bool   hasYawEstimateDrift() const { return !drift_.empty(); }
  double yawEstimateDriftDeg(double _t) const;
  /// 本帧「估计航向 / 真值航向」（GUI 做真值 vs 估计对照用；未用估计时两者相等）
  double lastEstimatedYawDeg() const { return yawObsDeg_; }
  double lastTrueYawDeg() const { return yawTrueDeg_; }
  double lastSpeedObservedMs() const { return speedObsMs_; }

  MetricContext context() const;
  static ControlMetrics computeMetrics(const std::vector<ControlSample> &_trail,
                                       const MetricContext &_ctx) {
    return ComputeMetrics(_trail, _ctx);
  }

private:
  bool ensurePublishers();
  void sendThrust(double _left, double _right);
  void republishThrust();
  void advanceWaypoints(double _x, double _y);
  void controlStep(double _x, double _y, double _dt, double &_left, double &_right);

  gz::transport::Node &node_;
  std::string worldName_, poseTopic_;

  std::string model_, thrL_, thrR_;
  double period_ = 0.1;
  ControlParams p_;
  std::vector<std::pair<double, double>> wps_;

  bool   running_ = false;
  double elapsed_ = 0.0, elapsedWall_ = 0.0;
  double lastSampleSimTime_ = -1.0, planStartSimTime_ = -1.0;

  double startX_ = 0.0, startY_ = 0.0, startYaw_ = 0.0;
  double prevX_ = 0.0, prevY_ = 0.0;
  double anchorX_ = 0.0, anchorY_ = 0.0;   ///< 航迹向估计的锚点（每移动 0.5 m 更新）
  double travelled_ = 0.0;
  double speed_ = 0.0, course_ = 0.0;
  double yawCourseOffset_ = 0.0;
  bool   offsetLocked_ = false;

  // 观测（= 真值 + 扰动偏置 / SC-08 估计漂移）
  double yawTrueDeg_ = 0.0, yawObsDeg_ = 0.0, speedObsMs_ = 0.0;

  // PID 状态
  double integH_ = 0.0, prevErrH_ = 0.0, derivH_ = 0.0;
  double integV_ = 0.0, prevErrV_ = 0.0, derivV_ = 0.0;
  bool   hasPrevH_ = false, hasPrevV_ = false;

  double lastHeadingErr_ = 0.0, lastLateral_ = 0.0;
  double lastThrustL_ = 0.0, lastThrustR_ = 0.0;

  // 航路点
  int  wpIdx_ = 0;
  int  wpReached_ = 0;
  double legFromX_ = 0.0, legFromY_ = 0.0;

  // 编队目标（start 时一次算出）
  double formTargetX_ = 0.0, formTargetY_ = 0.0;

  std::vector<ControlSample> trail_;
  std::vector<std::pair<double, double>> drift_;   ///< (t, 估计−真值 °)

  std::shared_ptr<ControlPoseShared> pose_;
  std::shared_ptr<ControlClockShared> clock_;

  gz::transport::Node::Publisher pubL_, pubR_;
  std::string pubKeyL_, pubKeyR_;
};

}  // namespace keship

#endif  // KESHIP_CONTROL_EXP_CORE_HH_
