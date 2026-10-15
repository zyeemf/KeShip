/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 —— 核心层实现 */

#include "TurningExperimentCore.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

#include <gz/msgs/double.pb.h>
#include <gz/msgs/pose.pb.h>

namespace keship {

namespace {
constexpr double kRad2Deg = 180.0 / M_PI;

/// 3×3 行列式
double det3(double a11, double a12, double a13,
            double a21, double a22, double a23,
            double a31, double a32, double a33) {
  return a11 * (a22 * a33 - a23 * a32)
       - a12 * (a21 * a33 - a23 * a31)
       + a13 * (a21 * a32 - a22 * a31);
}

/// **最小二乘圆拟合**（Kåsa 法）—— 严格 IMO 口径的「定常回转直径」
///   最小化 Σ(x²+y²+Dx+Ey+F)² ⇒ 解 3×3 线性方程组（克拉默法则）
///   ⚠ 与「包围盒估计」不同：这里求的是**真实拟合圆**的直径。
bool fitCircle(const std::vector<TurningSample> &_pts, size_t _from,
               double &_cx, double &_cy, double &_R, double &_r2) {
  if (_pts.size() <= _from + 4) return false;
  const double n = static_cast<double>(_pts.size() - _from);
  double sx = 0, sy = 0, sz = 0, sxx = 0, syy = 0, sxy = 0, sxz = 0, syz = 0;
  for (size_t i = _from; i < _pts.size(); ++i) {
    const double x = _pts[i].x, y = _pts[i].y, z = x * x + y * y;
    sx += x; sy += y; sz += z;
    sxx += x * x; syy += y * y; sxy += x * y;
    sxz += x * z; syz += y * z;
  }
  const double D0 = det3(sxx, sxy, sx, sxy, syy, sy, sx, sy, n);
  if (std::abs(D0) < 1e-12) return false;
  const double D = det3(-sxz, sxy, sx, -syz, syy, sy, -sz, sy, n) / D0;
  const double E = det3(sxx, -sxz, sx, sxy, -syz, sy, sx, -sz, n) / D0;
  const double F = det3(sxx, sxy, -sxz, sxy, syy, -syz, sx, sy, -sz) / D0;
  _cx = -D / 2.0;
  _cy = -E / 2.0;
  _R = std::sqrt(std::max(0.0, _cx * _cx + _cy * _cy - F));
  if (_R <= 1e-9) return false;
  // 拟合优度 R²
  double rMean = 0;
  for (size_t i = _from; i < _pts.size(); ++i)
    rMean += std::hypot(_pts[i].x - _cx, _pts[i].y - _cy);
  rMean /= n;
  double ssRes = 0, ssTot = 0;
  for (size_t i = _from; i < _pts.size(); ++i) {
    const double d = std::hypot(_pts[i].x - _cx, _pts[i].y - _cy);
    ssRes += (d - _R) * (d - _R);
    ssTot += (d - rMean) * (d - rMean);
  }
  _r2 = (ssTot > 1e-12) ? (1.0 - ssRes / ssTot) : 1.0;
  return true;
}
}  // namespace

TurningExperimentCore::TurningExperimentCore(gz::transport::Node &_node,
                                             const std::string &_worldName)
    : node_(_node), worldName_(_worldName) {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  pose_ = std::make_shared<TurningPoseShared>();
  auto st = pose_;
  node_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->latest = _msg;
        st->found = true;
      });
  subscribed_ = true;

  clock_ = std::make_shared<TurningClockShared>();
  auto clockState = clock_;
  node_.Subscribe<gz::msgs::WorldStatistics>("/world/" + worldName_ + "/stats",
      [clockState](const gz::msgs::WorldStatistics &_msg) {
        std::lock_guard<std::mutex> lk(clockState->mutex);
        if (!clockState->alive) return;
        clockState->simTimeSec = _msg.sim_time().sec() + _msg.sim_time().nsec() * 1e-9;
        clockState->found = true;
      });
  statsSubscribed_ = true;
}

TurningExperimentCore::~TurningExperimentCore() {
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

void TurningExperimentCore::setTarget(const std::string &_model,
                                      const std::string &_thrusterL,
                                      const std::string &_thrusterR) {
  if (_model != model_ || _thrusterL != thrL_ || _thrusterR != thrR_) {
    model_ = _model;
    thrL_ = _thrusterL;
    thrR_ = _thrusterR;
    pubKeyL_.clear();   // 目标变化才强制重建 Publisher
    pubKeyR_.clear();
  }
}

bool TurningExperimentCore::ensurePublishers() {
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
  bool okL = ensure(pubL_, thrL_, pubKeyL_);
  bool okR = ensure(pubR_, thrR_, pubKeyR_);
  return okL || okR;
}

void TurningExperimentCore::sendThrust(double _left, double _right) {
  curLeft_ = _left;
  curRight_ = _right;
  if (!ensurePublishers()) return;
  if (!pubKeyL_.empty()) {
    gz::msgs::Double m; m.set_data(_left); pubL_.Publish(m);
  }
  if (!pubKeyR_.empty()) {
    gz::msgs::Double m; m.set_data(_right); pubR_.Publish(m);
  }
}

/// ⚠ 推力**周期性重发**：gz-transport 的 pub/sub 连接建立有延迟，
///   只发一次会让首条消息丢失（2026-10-14 无头实测：船几乎没动，轨迹仅 2 cm）。
void TurningExperimentCore::republishThrust() {
  if (!running_) return;
  if (!ensurePublishers()) return;
  if (!pubKeyL_.empty()) {
    gz::msgs::Double m; m.set_data(curLeft_); pubL_.Publish(m);
  }
  if (!pubKeyR_.empty()) {
    gz::msgs::Double m; m.set_data(curRight_); pubR_.Publish(m);
  }
}

bool TurningExperimentCore::start() {
  clear();
  // ⚠ 订阅是**异步**的：首次读位姿前必须等回调到达（2026-10-14 无头实测踩到）
  double x = 0.0, y = 0.0, yaw = 0.0;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {          // 最多等 5 s
    if (fetchPose(x, y, yaw)) { ok = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!ok) return false;
  startX_ = prevX_ = x;
  startY_ = prevY_ = y;
  startYaw_ = yaw;
  elapsed_ = 0.0;
  elapsedWall_ = 0.0;
  speed_ = 0.0;
  currentStage_ = 0;
  measurementStartIndex_ = 0;
  planStartSimTime_ = -1.0;
  lastSimTime_ = -1.0;
  lastSampleSimTime_ = -1.0;
  measurementStarted_ = false;
  pendingMeasurementStart_ = false;
  if (clock_) {
    std::lock_guard<std::mutex> lk(clock_->mutex);
    if (clock_->found) {
      planStartSimTime_ = lastSimTime_ = clock_->simTimeSec;
    }
  }
  running_ = true;
  started_ = true;
  if (measurementStartStage_ == 0) {
    measurementStarted_ = true;
    pendingMeasurementStart_ = true;
    startX_ = prevX_ = x;
    startY_ = y;
    startYaw_ = yaw;
  }
  trail_.push_back(TurningSample{0.0, x, y, yaw, 0, measurementStartStage_ == 0});
  measurementStartIndex_ = 0;
  pendingMeasurementStart_ = false;
  sendThrust(stages_.front().leftN, stages_.front().rightN);
  return true;
}

void TurningExperimentCore::stop() {
  running_ = false;
  sendThrust(0.0, 0.0);
}

void TurningExperimentCore::clear() {
  trail_.clear();
  elapsed_ = 0.0;
  speed_ = 0.0;
  started_ = false;
  running_ = false;            // 防御：清零后必须可再次开始
  currentStage_ = -1;
  measurementStartIndex_ = 0;
  planStartSimTime_ = -1.0;
  lastSimTime_ = -1.0;
  elapsedWall_ = 0.0;
  lastSampleSimTime_ = -1.0;
  measurementStarted_ = false;
  pendingMeasurementStart_ = false;
  // 注意：stages_ / model_ / thrL_ / thrR_ / pubKey_ 由调用方保留，
  //       下次 start() 通过 syncStagesFromTable + syncParamsToCore 重新注入
}

bool TurningExperimentCore::advanceSimulationTime(double _simTimeSec) {
  if (!running_ || currentStage_ < 0 || currentStage_ >= static_cast<int>(stages_.size())
      || !std::isfinite(_simTimeSec)) return false;
  if (planStartSimTime_ < 0.0) {
    planStartSimTime_ = _simTimeSec;
    lastSimTime_ = _simTimeSec;
    return false;
  }
  if (_simTimeSec < lastSimTime_) {
    // 仿真被 reset / 回滚：重锚时间，避免阶段跳转连发。
    planStartSimTime_ = _simTimeSec - elapsedWall_;
    lastSimTime_ = _simTimeSec;
    return false;
  }
  lastSimTime_ = _simTimeSec;
  const double elapsedSim = _simTimeSec - planStartSimTime_;
  double stageStart = 0.0;
  for (int i = 0; i < currentStage_; ++i)
    stageStart += stages_[i].durationSec;
  bool changed = false;
  while (currentStage_ + 1 < static_cast<int>(stages_.size())) {
    const double d = stages_[currentStage_].durationSec;
    if (d <= 0.0) break;  // 最后阶段持续至手动停止
    const double stageEnd = stageStart + d;
    if (elapsedSim + 1e-9 < stageEnd) break;
    stageStart = stageEnd;
    ++currentStage_;
    sendThrust(stages_[currentStage_].leftN, stages_[currentStage_].rightN);
    changed = true;
    if (currentStage_ == measurementStartStage_) pendingMeasurementStart_ = true;
  }
  elapsed_ = std::max(0.0, elapsedSim);
  return changed;
}

bool TurningExperimentCore::fetchPose(double &_x, double &_y, double &_yawDeg) const {
  gz::msgs::Pose_V snap;
  {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    if (!pose_->found) return false;
    snap = pose_->latest;
  }
  for (int i = 0; i < snap.pose_size(); ++i) {
    const auto &p = snap.pose(i);
    std::string nm = p.name();
    auto pos = nm.rfind("::");
    if (pos != std::string::npos) nm = nm.substr(pos + 2);
    if (nm != model_) continue;
    _x = p.position().x();
    _y = p.position().y();
    double w = p.orientation().w(), z = p.orientation().z();
    _yawDeg = std::atan2(2.0 * w * z, 1.0 - 2.0 * z * z) * kRad2Deg;
    return true;
  }
  return false;
}

bool TurningExperimentCore::hasSimulationClock() const {
  if (!clock_) return false;
  std::lock_guard<std::mutex> lk(clock_->mutex);
  return clock_->found;
}

bool TurningExperimentCore::sample() {
  if (!running_) return false;
  double simTime = -1.0;
  if (clock_) {
    std::lock_guard<std::mutex> lk(clock_->mutex);
    if (clock_->found) simTime = clock_->simTimeSec;
  }

  double dt = period_;
  if (simTime >= 0.0) {
    // 仿真暂停时 sim_time 不变，既不切阶段也不重复写同一轨迹点。
    if (lastSampleSimTime_ >= 0.0) {
      dt = simTime - lastSampleSimTime_;
      if (dt <= 1e-9) {
        republishThrust();
        return false;
      }
    }
    lastSampleSimTime_ = simTime;
    advanceSimulationTime(simTime);
    elapsed_ = std::max(0.0, simTime - planStartSimTime_);
  } else {
    // 没有 stats 发布者时退回采样周期作为时钟，GUI/CTL 两侧行为仍一致。
    elapsedWall_ += period_;
    elapsed_ = elapsedWall_;
    advanceSimulationTime(elapsedWall_);
  }
  republishThrust();

  double x = 0.0, y = 0.0, yaw = 0.0;
  if (!fetchPose(x, y, yaw)) return false;
  const double d = std::hypot(x - prevX_, y - prevY_);
  if (dt > 0) speed_ = 0.9 * speed_ + 0.1 * (d / dt);
  prevX_ = x; prevY_ = y;

  const bool marker = pendingMeasurementStart_;
  if (marker) {
    measurementStartIndex_ = trail_.size();
    startX_ = x; startY_ = y; startYaw_ = yaw;
    measurementStarted_ = true;
    pendingMeasurementStart_ = false;
  }
  trail_.push_back(TurningSample{elapsed_, x, y, yaw,
      std::max(0, currentStage_), marker});
  if (currentStage_ == measurementStartStage_ && !measurementStarted_) {
    measurementStartIndex_ = trail_.size() - 1;
    startX_ = x; startY_ = y; startYaw_ = yaw;
    measurementStarted_ = true;
    trail_.back().measurementStart = true;
  }
  return true;
}

bool TurningExperimentCore::setStages(
    const std::vector<TurningStage> &_stages, std::string *_error) {
  if (_stages.empty()) {
    if (_error) *_error = "至少需要一个推力阶段";
    return false;
  }
  for (size_t i = 0; i < _stages.size(); ++i) {
    const auto &st = _stages[i];
    if (st.durationSec < 0.0 || !std::isfinite(st.durationSec)) {
      if (_error) *_error = "阶段时长必须是非负有限数";
      return false;
    }
    if (st.durationSec <= 0.0 && i + 1 != _stages.size()) {
      if (_error) *_error = "只有最后一个阶段可使用 0 秒（持续到停止）";
      return false;
    }
    if (!std::isfinite(st.leftN) || !std::isfinite(st.rightN)) {
      if (_error) *_error = "左右推力必须是有限数";
      return false;
    }
  }
  int startStage = -1;
  for (size_t i = 0; i < _stages.size(); ++i) {
    if (_stages[i].measurementStart) {
      if (startStage >= 0) {
        if (_error) *_error = "只能指定一个测量起点阶段";
        return false;
      }
      startStage = static_cast<int>(i);
    }
  }
  stages_ = _stages;
  measurementStartStage_ = startStage >= 0 ? startStage : 0;
  if (_error) _error->clear();
  return true;
}

TurningMetrics TurningExperimentCore::metrics() const {
  if (!measurementStarted_) return TurningMetrics{};
  return metricsFrom(measurementStartIndex_);
}

TurningMetrics TurningExperimentCore::metricsFrom(size_t _startIndex) const {
  if (trail_.empty()) return TurningMetrics{};
  const size_t from = std::min(_startIndex, trail_.size() - 1);
  auto m = computeMetrics(trail_, startX_, startY_, startYaw_, from);
  m.measurementStartIndex = static_cast<int>(from);
  return m;
}

TurningMetrics TurningExperimentCore::computeMetrics(
    const std::vector<TurningSample> &_trail,
    double _startX, double _startY, double _startYaw, size_t _from) {
  TurningMetrics m;
  const size_t from = std::min(_from, _trail.size());
  m.measurementStartIndex = static_cast<int>(from);
  m.points = static_cast<int>(_trail.size() - from);
  if (_trail.size() - from < 3) return m;

  for (size_t i = from; i < _trail.size(); ++i) {
    const auto &p = _trail[i];
    m.maxX = std::max(m.maxX, std::abs(p.x - _startX));
    m.maxY = std::max(m.maxY, std::abs(p.y - _startY));
  }

  // ★ 累积转向角（**逐点增量累加**，正确处理 ±180° 环绕）
  //   ⚠ 旧版用「yaw - startYaw 归一化到 ±180 后取绝对值」⇒ 船转了 358° 也只报 179.8°，
  //     导致「战术直径」永远取不到值（2026-10-14 无头实测暴露）。
  std::vector<double> cumTurn(_trail.size(), 0.0);
  for (size_t i = from + 1; i < _trail.size(); ++i) {
    double d = _trail[i].yawDeg - _trail[i - 1].yawDeg;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    cumTurn[i] = cumTurn[i - 1] + d;
  }

  bool advanceFound = false, tacticalFound = false;
  // ★ 参考轴 = **实际航迹向（course）**，而不是 yaw。
  //   ⚠ 本平台推进器约定「正推力沿模型 local -X」（见 KSPlugin_Thrust.cc:195），
  //     部分模型（如 ke0812_1）的 yaw 因此与其前进方向相差 180°；若按 yaw 投影，
  //     「进距」会算成负值而被显示为「—」（2026-10-15 张老师实测暴露）。
  //   做法：取「累积转向 ≈5° 处」的弦向估计初始航迹向（比两点差分稳），再按半角回正。
  double hx = std::cos(_startYaw / kRad2Deg);
  double hy = std::sin(_startYaw / kRad2Deg);
  {
    size_t refIdx = from;
    for (size_t i = from + 1; i < _trail.size(); ++i) {
      refIdx = i;
      if (std::abs(cumTurn[i]) >= 5.0) break;
    }
    const double dx0 = _trail[refIdx].x - _startX;
    const double dy0 = _trail[refIdx].y - _startY;
    const double len = std::hypot(dx0, dy0);
    if (len > 1e-6) {
      // 弦向相对**初始切线**偏转了 cumTurn/2（回转几何），回正后再作为参考轴，
      // 否则会把「进距」系统性放大几个百分点。
      const double ang = std::atan2(dy0, dx0) - 0.5 * cumTurn[refIdx] / kRad2Deg;
      hx = std::cos(ang);
      hy = std::sin(ang);
    }
  }
  for (size_t i = from; i < _trail.size(); ++i) {
    const double turn = std::abs(cumTurn[i]);
    const double dx = _trail[i].x - _startX;
    const double dy = _trail[i].y - _startY;
    const double longitudinal = dx * hx + dy * hy;
    const double lateral = -dx * hy + dy * hx;
    if (!advanceFound && turn >= 90.0) { m.advance = longitudinal; advanceFound = true; }
    if (!tacticalFound && turn >= 180.0) { m.tactical = std::abs(lateral); tacticalFound = true; }
  }

  // 包围盒估计（仅作对照）
  m.turningBox = std::min(m.maxX, m.maxY) * 2.0;

  // ★ 严格 IMO 口径：**定常回转段最小二乘圆拟合**
  //   定常段判定：累积转向 ≥ 180° 之后（排除转舵段与过渡段）
  size_t fitFrom = from;
  for (size_t i = from; i < _trail.size(); ++i) {
    if (std::abs(cumTurn[i]) >= 180.0) { fitFrom = i; break; }
  }
  double cx = 0, cy = 0, R = 0, r2 = 0;
  if (fitCircle(_trail, fitFrom, cx, cy, R, r2)) {
    m.fitR2 = r2;
    m.fitPoints = static_cast<int>(_trail.size() - fitFrom);
  }
  // ★ 定常回转判据：拟合优度 R² ≥ 0.90 才认「定常段圆」。
  //   ⚠ 2026-10-14 实测：本平台**差动推力回转**下 R² ≈ 0（角速度由 14.6 持续降到 1.1 °/s
  //     ⇒ 轨迹是**收缩螺旋**而非圆）—— 即**未达定常回转**。此时按 IMO 惯例
  //     用**战术直径**作为回转直径的替代口径，并置 steadyTurn=false 供调用方判断。
  constexpr double kR2Threshold = 0.90;
  /// ★ 全程累积转向角（绝对值）—— 判「船是否真的在回转」。
  ///   ⚠ 2026-10-14 水动力作用点修复后新增：两桨**等推力**时船**直航**，
  ///   此时若仍按包围盒给「回转直径」会报出**直线轨迹的包围盒**（实测 24.15 m），属误导。
  ///   转向角 < 10° 一律判「未回转」（战术/回转直径均无定义，报 -1）。
  const double totalTurnDeg = std::abs(cumTurn.back());
  m.turnedDeg = totalTurnDeg;
  if (totalTurnDeg < 10.0) {
    m.turning = -1.0;
    m.noTurn = true;
    m.steadyTurn = false;
  } else if (m.fitR2 >= kR2Threshold) {
    m.turning = 2.0 * R;
    m.steadyTurn = true;
  } else {
    m.turning = (m.tactical >= 0) ? m.tactical : m.turningBox;
    m.steadyTurn = false;
  }
  m.valid = true;
  return m;
}

bool TurningExperimentCore::exportCsv(const std::string &_path) const {
  std::ofstream ofs(_path);
  if (!ofs) return false;
  ofs << "t_sim_s,x_m,y_m,yaw_deg,stage,measurement_start\n";
  for (const auto &p : trail_)
    ofs << p.t << "," << p.x << "," << p.y << "," << p.yawDeg << ","
        << p.stage << "," << (p.measurementStart ? 1 : 0) << "\n";
  return true;
}

}  // namespace keship
