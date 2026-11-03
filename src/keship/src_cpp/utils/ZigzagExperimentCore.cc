/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-04 方向稳定性与 Z 形试验 —— 核心层实现 */

#include "ZigzagExperimentCore.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <gz/msgs/double.pb.h>
#include <gz/msgs/pose.pb.h>

namespace keship {

namespace {
constexpr double kRad2Deg = 180.0 / M_PI;

/// 角度差归一化到 (-180, 180]
double norm180(double _d) {
  while (_d > 180.0) _d -= 360.0;
  while (_d <= -180.0) _d += 360.0;
  return _d;
}
}  // namespace

ZigzagExperimentCore::ZigzagExperimentCore(gz::transport::Node &_node,
                                           const std::string &_worldName)
    : node_(_node), worldName_(_worldName) {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  pose_ = std::make_shared<ZigzagPoseShared>();
  auto st = pose_;
  node_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->latest = _msg;
        st->found = true;
      });
}

ZigzagExperimentCore::~ZigzagExperimentCore() {
  if (pose_) {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    pose_->alive = false;
  }
  if (running_) sendThrust(0.0, 0.0);
}

void ZigzagExperimentCore::setTarget(const std::string &_model,
                                     const std::string &_thrusterL,
                                     const std::string &_thrusterR) {
  model_ = _model; thrL_ = _thrusterL; thrR_ = _thrusterR;
  pubKeyL_.clear(); pubKeyR_.clear();
}

void ZigzagExperimentCore::setParams(double _base, double _delta,
                                     double _targetYaw, double _period) {
  base_ = _base;
  delta_ = _delta;
  targetYaw_ = (_targetYaw > 1.0) ? _targetYaw : 20.0;
  period_ = (_period > 0) ? _period : 0.1;
}

bool ZigzagExperimentCore::ensurePublishers() {
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

void ZigzagExperimentCore::sendThrust(double _left, double _right) {
  curLeft_ = _left; curRight_ = _right;
  if (!ensurePublishers()) return;
  if (!pubKeyL_.empty()) { gz::msgs::Double m; m.set_data(_left);  pubL_.Publish(m); }
  if (!pubKeyR_.empty()) { gz::msgs::Double m; m.set_data(_right); pubR_.Publish(m); }
}

/// ⚠ 推力周期性重发（gz-transport 首帧易丢，见 MS-01 实测）
void ZigzagExperimentCore::republishThrust() {
  if (!running_) return;
  if (!ensurePublishers()) return;
  if (!pubKeyL_.empty()) { gz::msgs::Double m; m.set_data(curLeft_);  pubL_.Publish(m); }
  if (!pubKeyR_.empty()) { gz::msgs::Double m; m.set_data(curRight_); pubR_.Publish(m); }
}

bool ZigzagExperimentCore::fetchPose(double &_x, double &_y, double &_yawDeg) const {
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
    const double w = p.orientation().w(), z = p.orientation().z();
    _yawDeg = std::atan2(2.0 * w * z, 1.0 - 2.0 * z * z) * kRad2Deg;
    return true;
  }
  return false;
}

bool ZigzagExperimentCore::start() {
  clear();
  double x = 0, y = 0, yaw = 0;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {           // 等位姿就绪（订阅异步）
    if (fetchPose(x, y, yaw)) { ok = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!ok) return false;
  startYaw_ = yaw;
  cumYaw_ = 0.0;                 // 累积航向从 0 起
  phaseStartCum_ = 0.0;
  prevYaw_ = yaw;
  phase_ = 0;                    // 先左转
  elapsed_ = 0.0;
  lastSwitchT_ = -1.0;
  running_ = true;
  sendThrust(base_ + delta_, base_ - delta_);
  return true;
}

void ZigzagExperimentCore::stop() {
  running_ = false;
  sendThrust(0.0, 0.0);
}

void ZigzagExperimentCore::clear() {
  trail_.clear();
  elapsed_ = 0.0;
  phase_ = 0;
}

bool ZigzagExperimentCore::sample() {
  if (!running_) return false;
  republishThrust();
  double x = 0, y = 0, yaw = 0;
  if (!fetchPose(x, y, yaw)) return false;

  // ★ 累积航向：逐点增量累加 ⇒ 不受 ±180° 环绕影响
  //   （旧版用「yaw - phaseStartYaw」瞬时差 ⇒ 环绕时判据抖动，实测换向 86 次/60 s）
  cumYaw_ += norm180(yaw - prevYaw_);
  prevYaw_ = yaw;
  elapsed_ += period_;

  // ★ Z 形换向判据（累积角 + **不预设转向方向**）+ 最小换向间隔（防抖）
  //   ⚠ 2026-10-14 实测：左推力大（150/50）实际使 yaw **减小**（右转）——
  //     若按"左大=左转"假设写判据，会永不换向（实测累积航向转了 −12012° 仍 phase=0）。
  //     ⇒ 改用「本相位内累积航向变化的**绝对值**达阈值」判换向，与方向无关。
  const double dCum = cumYaw_ - phaseStartCum_;
  const bool canSwitch = (lastSwitchT_ < 0.0) ||
                         (elapsed_ - lastSwitchT_ >= minSwitchGap_);
  bool switched = false;
  if (canSwitch && std::abs(dCum) >= targetYaw_) {
    phase_ = 1 - phase_;            // 反向
    phaseStartCum_ = cumYaw_;
    switched = true;
  }
  if (switched) lastSwitchT_ = elapsed_;

  // 按相位下发推力（左转：左大右小；右转：左小右大）
  if (phase_ == 0) sendThrust(base_ + delta_, base_ - delta_);
  else             sendThrust(base_ - delta_, base_ + delta_);

  trail_.push_back(ZigzagSample{elapsed_, cumYaw_, curLeft_, curRight_, phase_});
  return true;
}

ZigzagMetrics ZigzagExperimentCore::metrics() const {
  return computeMetrics(trail_, startYaw_, targetYaw_);
}

ZigzagMetrics ZigzagExperimentCore::computeMetrics(
    const std::vector<ZigzagSample> &_trail, double _startYaw, double _targetYaw) {
  ZigzagMetrics m;
  m.points = static_cast<int>(_trail.size());
  m.targetYaw = _targetYaw;
  if (_trail.size() < 3) return m;

  // 找换向点（phase 变化处）
  std::vector<size_t> sw;
  for (size_t i = 1; i < _trail.size(); ++i)
    if (_trail[i].phase != _trail[i - 1].phase) sw.push_back(i);
  m.switches = static_cast<int>(sw.size());
  if (sw.empty()) return m;

  // 超调角：换向后，累积航向**继续朝换向前方向**变化的最大量（不预设左右）
  auto overshootAfter = [&](size_t _idx) -> double {
    if (_idx == 0) return 0.0;
    const int dir = (_trail[_idx].yawDeg >= _trail[_idx - 1].yawDeg) ? +1 : -1;
    const double ref = _trail[_idx].yawDeg;
    const int ph = _trail[_idx].phase;
    double best = 0.0;
    for (size_t i = _idx; i < _trail.size(); ++i) {
      if (i > _idx && _trail[i].phase != ph) break;   // 又换向了
      const double d = (_trail[i].yawDeg - ref) * dir;
      if (i > _idx && d < -1e-9) break;               // 已反向
      best = std::max(best, d);
    }
    return best;
  };
  if (sw.size() >= 1) m.overshoot1 = overshootAfter(sw[0]);
  if (sw.size() >= 2) m.overshoot2 = overshootAfter(sw[1]);

  // 周期：两次同向换向之间的时间
  if (sw.size() >= 2) m.period = _trail[sw[1]].t - _trail[sw[0]].t;
  m.valid = true;
  return m;
}

bool ZigzagExperimentCore::exportCsv(const std::string &_path) const {
  std::ofstream ofs(_path);
  if (!ofs) return false;
  ofs << "t_s,yaw_deg,thrust_l,thrust_r,phase\n";
  for (const auto &p : trail_)
    ofs << p.t << "," << p.yawDeg << "," << p.thrustL << ","
        << p.thrustR << "," << p.phase << "\n";
  return true;
}

}  // namespace keship
