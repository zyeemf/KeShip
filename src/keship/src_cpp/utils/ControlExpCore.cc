/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-01~SC-12 控制设计链（L6）+ 感知→控制链（L7）—— 核心层实现
 *
 * 与 `TurningExperimentCore.cc` 同构的三件事：
 *   ① 位姿订阅 `/world/<w>/pose/info`、仿真时钟 `/world/<w>/stats`（异步，起航前必须等回调）；
 *   ② 推力话题 `/keship/thrust/<模型>/<桨>`（gz::msgs::Double），每帧**重发**防首帧丢失；
 *   ③ 指标全部走 `ControlLaws.hh` 的纯函数，GUI / 无头 / 单测同一份。
 */

#include "ControlExpCore.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <gz/msgs/double.pb.h>

namespace keship {

namespace {
/// 风对航向观测的线性折算系数（deg per m/s）—— **教学级简化**：
/// 真实风压力矩与上层建筑侧面积、相对风角有关，本版只保留「风速越大、航向观测偏得越多」的
/// 单调关系，用于 SC-03 演示「有/无扰动」的控制品质差异（边界见交付报告 §5）。
constexpr double kWindYawBiasDegPerMs = 1.5;
/// 航迹向估计锚点距离：每移动这么多米更新一次「航迹向」（抑制低速时的差分噪声）
constexpr double kCourseAnchorM = 0.5;
}  // namespace

//-----------------------------------------------------------------------------
ControlExpCore::ControlExpCore(gz::transport::Node &_node,
                               const std::string &_worldName)
    : node_(_node), worldName_(_worldName) {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  pose_ = std::make_shared<ControlPoseShared>();
  auto st = pose_;
  node_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->latest = _msg;
        st->found = true;
      });

  clock_ = std::make_shared<ControlClockShared>();
  auto cs = clock_;
  node_.Subscribe<gz::msgs::WorldStatistics>("/world/" + worldName_ + "/stats",
      [cs](const gz::msgs::WorldStatistics &_msg) {
        std::lock_guard<std::mutex> lk(cs->mutex);
        if (!cs->alive) return;
        cs->simTimeSec = _msg.sim_time().sec() + _msg.sim_time().nsec() * 1e-9;
        cs->found = true;
      });
}

//-----------------------------------------------------------------------------
ControlExpCore::~ControlExpCore() {
  if (pose_) {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    pose_->alive = false;
  }
  if (clock_) {
    std::lock_guard<std::mutex> lk(clock_->mutex);
    clock_->alive = false;
  }
  if (running_) sendThrust(0.0, 0.0);   // 安全：退出前归零
}

//-----------------------------------------------------------------------------
void ControlExpCore::setTarget(const std::string &_model,
                               const std::string &_thrL,
                               const std::string &_thrR) {
  if (_model != model_ || _thrL != thrL_ || _thrR != thrR_) {
    model_ = _model;
    thrL_ = _thrL;
    thrR_ = _thrR;
    pubKeyL_.clear();   // 目标变化才强制重建 Publisher
    pubKeyR_.clear();
  }
}

void ControlExpCore::setParams(const ControlParams &_p) { p_ = _p; }

void ControlExpCore::setWaypoints(
    const std::vector<std::pair<double, double>> &_wps) {
  wps_ = _wps;
}

void ControlExpCore::setYawEstimateDrift(
    const std::vector<std::pair<double, double>> &_tDriftDeg) {
  drift_ = _tDriftDeg;
  std::sort(drift_.begin(), drift_.end(),
            [](const std::pair<double, double> &_a,
               const std::pair<double, double> &_b) { return _a.first < _b.first; });
}

double ControlExpCore::yawEstimateDriftDeg(double _t) const {
  if (drift_.empty()) return 0.0;
  if (_t <= drift_.front().first) return drift_.front().second;
  if (_t >= drift_.back().first) return drift_.back().second;
  for (size_t i = 1; i < drift_.size(); ++i) {
    if (_t <= drift_[i].first) {
      const double t0 = drift_[i - 1].first, t1 = drift_[i].first;
      const double k = (t1 - t0 > 1e-9) ? (_t - t0) / (t1 - t0) : 0.0;
      return drift_[i - 1].second + k * (drift_[i].second - drift_[i - 1].second);
    }
  }
  return drift_.back().second;
}

//-----------------------------------------------------------------------------
bool ControlExpCore::ensurePublishers() {
  if (model_.empty()) return false;
  auto ensure = [&](gz::transport::Node::Publisher &_pub,
                    const std::string &_thruster, std::string &_key) {
    if (_thruster.empty()) return false;
    const std::string key = model_ + "/" + _thruster;
    if (key != _key) {
      _pub = node_.Advertise<gz::msgs::Double>("/keship/thrust/" + key);
      _key = key;
    }
    return true;
  };
  const bool okL = ensure(pubL_, thrL_, pubKeyL_);
  const bool okR = ensure(pubR_, thrR_, pubKeyR_);
  return okL || okR;
}

void ControlExpCore::sendThrust(double _left, double _right) {
  lastThrustL_ = _left;
  lastThrustR_ = _right;
  if (!ensurePublishers()) return;
  if (!pubKeyL_.empty()) {
    gz::msgs::Double m; m.set_data(_left); pubL_.Publish(m);
  }
  if (!pubKeyR_.empty()) {
    gz::msgs::Double m; m.set_data(_right); pubR_.Publish(m);
  }
}

/// 周期性重发当前推力：gz-transport 的 pub/sub 连接建立有延迟，只发一次会丢首帧
/// （与 TurningExperimentCore::republishThrust 同因，见 2026-10-14 无头实测记录）。
void ControlExpCore::republishThrust() {
  if (!running_) return;
  if (!ensurePublishers()) return;
  if (!pubKeyL_.empty()) {
    gz::msgs::Double m; m.set_data(lastThrustL_); pubL_.Publish(m);
  }
  if (!pubKeyR_.empty()) {
    gz::msgs::Double m; m.set_data(lastThrustR_); pubR_.Publish(m);
  }
}

//-----------------------------------------------------------------------------
bool ControlExpCore::fetchPose(double &_x, double &_y, double &_yawDeg) const {
  gz::msgs::Pose_V snap;
  {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    if (!pose_->found) return false;
    snap = pose_->latest;
  }
  for (int i = 0; i < snap.pose_size(); ++i) {
    const auto &p = snap.pose(i);
    std::string nm = p.name();
    const auto pos = nm.rfind("::");
    if (pos != std::string::npos) nm = nm.substr(pos + 2);
    if (nm != model_) continue;
    _x = p.position().x();
    _y = p.position().y();
    const double w = p.orientation().w(), z = p.orientation().z();
    _yawDeg = std::atan2(2.0 * w * z, 1.0 - 2.0 * z * z) * kRad2Deg;
    return true;
  }
  return false;
}

bool ControlExpCore::hasSimulationClock() const {
  if (!clock_) return false;
  std::lock_guard<std::mutex> lk(clock_->mutex);
  return clock_->found;
}

//-----------------------------------------------------------------------------
void ControlExpCore::clear() {
  trail_.clear();
  running_ = false;
  elapsed_ = 0.0;
  elapsedWall_ = 0.0;
  lastSampleSimTime_ = -1.0;
  planStartSimTime_ = -1.0;
  travelled_ = 0.0;
  speed_ = 0.0;
  course_ = 0.0;
  yawCourseOffset_ = 0.0;
  offsetLocked_ = false;
  yawTrueDeg_ = yawObsDeg_ = speedObsMs_ = 0.0;
  integH_ = prevErrH_ = derivH_ = 0.0;
  integV_ = prevErrV_ = derivV_ = 0.0;
  hasPrevH_ = hasPrevV_ = false;
  lastHeadingErr_ = lastLateral_ = 0.0;
  lastThrustL_ = lastThrustR_ = 0.0;
  wpIdx_ = 0;
  wpReached_ = 0;
  legFromX_ = legFromY_ = 0.0;
  formTargetX_ = formTargetY_ = 0.0;
  // 注意：model_ / thrL_ / thrR_ / p_ / wps_ / drift_ 由调用方保留
}

//-----------------------------------------------------------------------------
bool ControlExpCore::start() {
  clear();
  double x = 0.0, y = 0.0, yaw = 0.0;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {   // 订阅异步：最多等 5 s
    if (fetchPose(x, y, yaw)) { ok = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!ok) return false;

  startX_ = prevX_ = anchorX_ = x;
  startY_ = prevY_ = anchorY_ = y;
  startYaw_ = yaw;
  legFromX_ = x;
  legFromY_ = y;
  yawTrueDeg_ = yawObsDeg_ = yaw;

  // 编队目标点：虚拟领航者 = 起点（静止），偏移按**起始艏向坐标系**解释
  // （+X = 起始艏向，+Y = 其左舷）。单船条件下没有真实僚船 —— 见报告 §5。
  {
    const double c = std::cos(startYaw_ * kDeg2Rad), s = std::sin(startYaw_ * kDeg2Rad);
    formTargetX_ = startX_ + c * p_.formationOffsetXM - s * p_.formationOffsetYM;
    formTargetY_ = startY_ + s * p_.formationOffsetXM + c * p_.formationOffsetYM;
  }

  if (clock_) {
    std::lock_guard<std::mutex> lk(clock_->mutex);
    if (clock_->found) planStartSimTime_ = clock_->simTimeSec;
  }

  running_ = true;
  // 首个采样点就把「真实偏差」记下（否则 t=0 那一行会被当成误差 0，微微拉低 RMSE）
  lastHeadingErr_ = WrapDeg180(p_.targetHeadingDeg - yaw);
  lastLateral_ = 0.0;
  if (p_.law == ControlLaw::kDp)
    lastLateral_ = std::hypot(p_.targetXM - x, p_.targetYM - y);
  else if (p_.law == ControlLaw::kFormation)
    lastLateral_ = std::hypot(formTargetX_ - x, formTargetY_ - y);
  trail_.push_back(ControlSample{0.0, x, y, yaw, 0.0, lastLateral_, lastHeadingErr_,
                                 p_.baseThrustN, p_.baseThrustN});
  // 起航先给一帧基准推力（防订阅端在首个 sample 前没收到任何指令）
  sendThrust(p_.baseThrustN, p_.baseThrustN);
  return true;
}

void ControlExpCore::stop() {
  running_ = false;
  sendThrust(0.0, 0.0);
}

//-----------------------------------------------------------------------------
void ControlExpCore::advanceWaypoints(double _x, double _y) {
  if (wps_.empty()) return;
  const double r = std::max(0.1, p_.arriveRadiusM);
  while (wpIdx_ >= 0 && wpIdx_ < static_cast<int>(wps_.size())) {
    const auto &tgt = wps_[wpIdx_];
    const double d = std::hypot(_x - tgt.first, _y - tgt.second);
    // ★ 切段判据 = 「进入到达半径」**或**「越过该航路点的垂面」。
    //   只用半径会出事：初始转向瞬态把船横推到 4~5 m 外擦过航路点，半径永远判不到，
    //   于是永远沿第一段跑下去（2026-10-27 假对象闭环实测：船跑到 x=175 m 仍未切段）。
    const double dx0 = tgt.first - legFromX_, dy0 = tgt.second - legFromY_;
    const double legLen = std::hypot(dx0, dy0);
    const double along = (legLen > 1e-6)
                             ? ((_x - legFromX_) * dx0 + (_y - legFromY_) * dy0) / legLen
                             : 0.0;
    const bool passed = (legLen > 1e-6) && (along >= legLen);
    if (d > r && !passed) break;
    if (wpIdx_ + 1 < static_cast<int>(wps_.size())) {
      ++wpReached_;
      legFromX_ = tgt.first;   // 下一段的起点 = 刚到达的航路点
      legFromY_ = tgt.second;
      ++wpIdx_;
    } else {
      // 末点：**只计一次**（否则每帧加 1，到达率会 > 1）；之后保持在该点
      if (wpReached_ == wpIdx_) ++wpReached_;
      break;
    }
  }
}

//-----------------------------------------------------------------------------
/// 单帧控制律：**所有**控制律都在这里，纯算式来自 ControlLaws.hh。
void ControlExpCore::controlStep(double _x, double _y, double _dt,
                                 double &_left, double &_right) {
  const double yawObs = yawObsDeg_;

  auto headingPid = [&](double _yawTarget) {
    const double err = WrapDeg180(_yawTarget - yawObs);
    integH_ = ClampIntegral(integH_ + err * _dt, p_.ki, p_.diffLimitN);
    const double rawD = (_dt > 1e-6) ? (err - prevErrH_) / _dt : 0.0;
    derivH_ = hasPrevH_ ? (0.7 * derivH_ + 0.3 * rawD) : rawD;   // 一阶低通，抑噪
    prevErrH_ = err;
    hasPrevH_ = true;
    lastHeadingErr_ = err;
    return ComputeDiffThrust(err, integH_, derivH_, p_);
  };
  auto speedLoop = [&](double _vTarget) {
    const double err = _vTarget - speedObsMs_;
    integV_ = ClampIntegral(integV_ + err * _dt, p_.ki, p_.diffLimitN);
    const double rawD = (_dt > 1e-6) ? (err - prevErrV_) / _dt : 0.0;
    derivV_ = hasPrevV_ ? (0.7 * derivV_ + 0.3 * rawD) : rawD;
    prevErrV_ = err;
    hasPrevV_ = true;
    return ComputeCommonThrust(err, integV_, derivV_, p_);
  };
  /// 相对「起点 + 参考航迹向」参考线的横向偏移（正 = 港侧）
  auto lateralFromStart = [&]() {
    const double ref = (startYaw_ + yawCourseOffset_) * kDeg2Rad;
    return std::cos(ref) * (_y - startY_) - std::sin(ref) * (_x - startX_);
  };

  // ★ 起航标定段（只在几何类控制律下）：艏向↔航迹向的偏移还是未知数时**不要闭环**。
  //   否则「艏向与 yaw 差 180°」会被当成 180° 的航向误差猛打差动，船在标定完成前先横甩
  //   几米（2026-10-27 假对象闭环实测：横向摆到 9 m）。此阶段只给基准推力直航，
  //   等 `offsetLocked_` 一锁定，下一帧立刻进入正常闭环。
  const bool geometricLaw = (p_.law == ControlLaw::kLos || p_.law == ControlLaw::kDp ||
                             p_.law == ControlLaw::kColregs ||
                             p_.law == ControlLaw::kFormation);
  if (geometricLaw && !offsetLocked_) {
    _left = p_.baseThrustN;
    _right = p_.baseThrustN;
    lastHeadingErr_ = WrapDeg180(p_.targetHeadingDeg - yawObs);
    lastLateral_ = lateralFromStart();
    return;
  }

  switch (p_.law) {
    case ControlLaw::kOpenLoop: {
      // 固定推力、无反馈：SC-01 开环组用 openLoopDiffN=0（两桨等推力直航）；
      // SC-09 的阶跃识别用 openLoopDiffN>0（仍然无反馈）。
      const double diff = Clamp(p_.openLoopDiffN, -p_.diffLimitN, p_.diffLimitN);
      _left = p_.baseThrustN - diff;
      _right = p_.baseThrustN + diff;
      lastHeadingErr_ = WrapDeg180(p_.targetHeadingDeg - yawObs);
      lastLateral_ = lateralFromStart();
      break;
    }
    case ControlLaw::kPidHeading: {
      const auto d = headingPid(p_.targetHeadingDeg);
      _left = d.left;
      _right = d.right;
      lastLateral_ = lateralFromStart();
      break;
    }
    case ControlLaw::kPidSpeed: {
      const double common = speedLoop(p_.targetSpeedMs);
      _left = p_.baseThrustN + common;
      _right = p_.baseThrustN + common;
      lastHeadingErr_ = WrapDeg180(p_.targetHeadingDeg - yawObs);
      lastLateral_ = lateralFromStart();
      break;
    }
    case ControlLaw::kLos: {
      advanceWaypoints(_x, _y);
      const bool allReached =
          !wps_.empty() && wpReached_ >= static_cast<int>(wps_.size());
      if (allReached) {
        // 末点已到达 ⇒ 转为「在该点保持」（否则 LOS 会沿最后一段的方向一路开过去，
        // 出现「到达率 100% 但船已跑出 70 m」这种看起来对、其实不对的结果）
        const auto &last = wps_.back();
        const PolarCommand pc = PolarDpCommand(last.first - _x, last.second - _y,
                                               p_.targetSpeedMs, p_.posGainPerS);
        const auto d = headingPid(pc.courseDeg - yawCourseOffset_);
        const double common = speedLoop(pc.speedMs);
        _left = p_.baseThrustN + common - d.diff;
        _right = p_.baseThrustN + common + d.diff;
        lastLateral_ = std::hypot(last.first - _x, last.second - _y);
        break;
      }
      double pathHeading = p_.targetHeadingDeg;
      double e = 0.0;
      if (!wps_.empty()) {
        const auto &tgt = wps_[std::min<size_t>(wpIdx_, wps_.size() - 1)];
        const double dx = tgt.first - legFromX_, dy = tgt.second - legFromY_;
        if (std::hypot(dx, dy) > 1e-3) pathHeading = std::atan2(dy, dx) * kRad2Deg;
        e = CrossTrackErrorM(_x, _y, legFromX_, legFromY_, tgt.first, tgt.second);
      } else {
        e = lateralFromStart();
      }
      lastLateral_ = e;
      const double psiCourse = LosDesiredHeading(pathHeading, e, p_.lookaheadM);
      const auto d = headingPid(psiCourse - yawCourseOffset_);   // 航迹向 → yaw
      _left = d.left;
      _right = d.right;
      break;
    }
    case ControlLaw::kDp: {
      const PolarCommand pc = PolarDpCommand(p_.targetXM - _x, p_.targetYM - _y,
                                             p_.targetSpeedMs, p_.posGainPerS);
      const auto d = headingPid(pc.courseDeg - yawCourseOffset_);
      const double common = speedLoop(pc.speedMs);
      _left = p_.baseThrustN + common - d.diff;
      _right = p_.baseThrustN + common + d.diff;
      lastLateral_ = std::hypot(p_.targetXM - _x, p_.targetYM - _y);
      break;
    }
    case ControlLaw::kColregs: {
      const double ex = p_.obstacleXM - _x, ey = p_.obstacleYM - _y;
      const double range = std::hypot(ex, ey);
      const double bearing = std::atan2(ey, ex) * kRad2Deg;
      const double relBearing = WrapDeg180(bearing - course_);
      const double bias = ColregsHeadingBiasDeg(relBearing, range, p_.obstacleRadiusM);
      const double psiCourse = p_.targetHeadingDeg + bias;   // 基线航向按**航迹向**解释
      const auto d = headingPid(psiCourse - yawCourseOffset_);
      _left = d.left;
      _right = d.right;
      lastLateral_ = lateralFromStart();
      break;
    }
    case ControlLaw::kFormation: {
      const PolarCommand pc = PolarDpCommand(formTargetX_ - _x, formTargetY_ - _y,
                                             p_.targetSpeedMs, p_.posGainPerS);
      const auto d = headingPid(pc.courseDeg - yawCourseOffset_);
      const double common = speedLoop(pc.speedMs);
      _left = p_.baseThrustN + common - d.diff;
      _right = p_.baseThrustN + common + d.diff;
      lastLateral_ = std::hypot(formTargetX_ - _x, formTargetY_ - _y);
      break;
    }
  }
}

//-----------------------------------------------------------------------------
bool ControlExpCore::sample() {
  if (!running_) return false;

  double simTime = -1.0;
  if (clock_) {
    std::lock_guard<std::mutex> lk(clock_->mutex);
    if (clock_->found) simTime = clock_->simTimeSec;
  }
  double dt = period_;
  if (simTime >= 0.0) {
    if (planStartSimTime_ < 0.0) planStartSimTime_ = simTime;
    if (lastSampleSimTime_ >= 0.0) {
      dt = simTime - lastSampleSimTime_;
      if (dt <= 1e-9) {          // 仿真暂停：不重复写点，但要重发推力
        republishThrust();
        return false;
      }
    }
    lastSampleSimTime_ = simTime;
    elapsed_ = std::max(0.0, simTime - planStartSimTime_);
  } else {
    elapsedWall_ += period_;
    elapsed_ = elapsedWall_;
  }

  double x = 0.0, y = 0.0, yaw = 0.0;
  if (!fetchPose(x, y, yaw)) { republishThrust(); return false; }

  // ── 状态估计（真值 → 观测）─────────────────────────────────────────────
  const double moved = std::hypot(x - prevX_, y - prevY_);
  travelled_ += moved;
  const double instV = (dt > 1e-6) ? moved / dt : 0.0;
  speed_ = 0.7 * speed_ + 0.3 * instV;
  if (std::hypot(x - anchorX_, y - anchorY_) > kCourseAnchorM) {
    course_ = std::atan2(y - anchorY_, x - anchorX_) * kRad2Deg;
    anchorX_ = x;
    anchorY_ = y;
  }
  // ★ 艏向↔航迹向一次性标定（量化到 90° 整数倍）：本平台正推力沿 local −X，
  //   艏向与 yaw 可能相差 180°。起航后第一次「真正走起来」时锁定，避免把水流压差
  //   误当成 180° 翻转（漂角一般 < 15°）。
  if (!offsetLocked_ && travelled_ > 1.0 && speed_ > 0.25) {
    yawCourseOffset_ = std::round(WrapDeg180(course_ - yaw) / 90.0) * 90.0;
    offsetLocked_ = true;
  }

  yawTrueDeg_ = yaw;
  double yawBias = 0.0;
  double speedBias = 0.0;
  if (p_.disturbance) {
    // 教学级简化：把风/流折算成**控制器的观测偏置**（不注入物理力）。
    //   · 风 → 航向观测偏置：yawBias = k·windMs
    //   · 流 → 沿航迹向分量：speedBias = currentMs·cos(流向 − 航迹向)
    // 控制器据此作出的错误判断会**真实地**体现在推力与轨迹上（真值仍按位姿记录），
    // 因此指标会变差 —— 这正是 SC-03 要观察的现象。
    yawBias += kWindYawBiasDegPerMs * p_.windMs;
    speedBias += p_.currentMs * std::cos((p_.currentDirDeg - course_) * kDeg2Rad);
  }
  if (p_.useEstimatedPose) yawBias += yawEstimateDriftDeg(elapsed_);   // L7：SC-08 漂移
  yawObsDeg_ = yawTrueDeg_ + yawBias;
  speedObsMs_ = speed_ + speedBias;

  // ── 控制 → 推力 → 记录 ────────────────────────────────────────────────
  double left = p_.baseThrustN, right = p_.baseThrustN;
  controlStep(x, y, dt, left, right);
  sendThrust(left, right);

  prevX_ = x;
  prevY_ = y;
  trail_.push_back(ControlSample{elapsed_, x, y, yaw, speed_,
                                 lastLateral_, lastHeadingErr_, left, right});

  if (elapsed_ >= p_.simSeconds) {
    stop();   // 到时自动停：与对话框「一条曲线跑完」的语义一致
    return true;
  }
  return true;
}

//-----------------------------------------------------------------------------
MetricContext ControlExpCore::context() const {
  MetricContext c;
  c.law = p_.law;
  c.targetHeadingDeg = p_.targetHeadingDeg;
  c.targetSpeedMs = p_.targetSpeedMs;
  c.targetXM = p_.targetXM;
  c.targetYM = p_.targetYM;
  c.startXM = startX_;
  c.startYM = startY_;
  c.startYawDeg = startYaw_;
  c.hasObstacle = (p_.law == ControlLaw::kColregs);
  c.obstacleXM = p_.obstacleXM;
  c.obstacleYM = p_.obstacleYM;
  c.obstacleRadiusM = p_.obstacleRadiusM;
  c.formation = (p_.law == ControlLaw::kFormation);
  c.formationTargetXM = formTargetX_;
  c.formationTargetYM = formTargetY_;
  c.waypointsTotal = static_cast<int>(wps_.size());
  c.waypointsReached = wpReached_;
  c.arrivalRadiusM = p_.arriveRadiusM;
  return c;
}

ControlMetrics ControlExpCore::metrics() const { return ComputeMetrics(trail_, context()); }

//-----------------------------------------------------------------------------
bool ControlExpCore::exportCsv(const std::string &_path) const {
  std::ofstream ofs(_path);
  if (!ofs) return false;
  ofs << "t_sim_s,x_m,y_m,yaw_deg,speed_ms,lateral_err_m,heading_err_deg,"
         "thrust_l_n,thrust_r_n\n";
  for (const auto &s : trail_) {
    ofs << s.t << "," << s.x << "," << s.y << "," << s.yawDeg << "," << s.speedMs
        << "," << s.lateralErrM << "," << s.headingErrDeg << "," << s.thrustL
        << "," << s.thrustR << "\n";
  }
  return true;
}

}  // namespace keship
