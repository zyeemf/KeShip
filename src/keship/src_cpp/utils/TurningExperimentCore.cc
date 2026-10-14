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
}

TurningExperimentCore::~TurningExperimentCore() {
  if (pose_) {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    pose_->alive = false;
  }
  if (running_) sendThrust(0.0, 0.0);   // 安全：退出前归零
}

void TurningExperimentCore::setTarget(const std::string &_model,
                                      const std::string &_thrusterL,
                                      const std::string &_thrusterR) {
  model_ = _model;
  thrL_ = _thrusterL;
  thrR_ = _thrusterR;
  pubKeyL_.clear();   // 强制下次重建 Publisher
  pubKeyR_.clear();
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
  speed_ = 0.0;
  running_ = true;
  started_ = true;
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

bool TurningExperimentCore::sample() {
  if (!running_) return false;
  republishThrust();          // ★ 周期性重发推力（防首帧丢失）
  double x, y, yaw;
  if (!fetchPose(x, y, yaw)) return false;

  elapsed_ += period_;
  const double d = std::hypot(x - prevX_, y - prevY_);
  if (period_ > 0) speed_ = 0.9 * speed_ + 0.1 * (d / period_);
  prevX_ = x; prevY_ = y;

  trail_.push_back(TurningSample{elapsed_, x, y, yaw});
  return true;
}

TurningMetrics TurningExperimentCore::metrics() const {
  return computeMetrics(trail_, startX_, startY_, startYaw_);
}

TurningMetrics TurningExperimentCore::computeMetrics(
    const std::vector<TurningSample> &_trail,
    double _startX, double _startY, double _startYaw) {
  TurningMetrics m;
  m.points = static_cast<int>(_trail.size());
  if (_trail.size() < 3) return m;

  for (const auto &p : _trail) {
    m.maxX = std::max(m.maxX, std::abs(p.x - _startX));
    m.maxY = std::max(m.maxY, std::abs(p.y - _startY));
  }

  // ★ 累积转向角（**逐点增量累加**，正确处理 ±180° 环绕）
  //   ⚠ 旧版用「yaw - startYaw 归一化到 ±180 后取绝对值」⇒ 船转了 358° 也只报 179.8°，
  //     导致「战术直径」永远取不到值（2026-10-14 无头实测暴露）。
  std::vector<double> cumTurn(_trail.size(), 0.0);
  for (size_t i = 1; i < _trail.size(); ++i) {
    double d = _trail[i].yawDeg - _trail[i - 1].yawDeg;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    cumTurn[i] = cumTurn[i - 1] + d;
  }

  for (size_t i = 0; i < _trail.size(); ++i) {
    const double turn = std::abs(cumTurn[i]);
    const double disp = std::hypot(_trail[i].x - _startX, _trail[i].y - _startY);
    if (m.advance < 0 && turn >= 90.0)  m.advance  = disp;
    if (m.tactical < 0 && turn >= 180.0) m.tactical = disp;
  }

  // 包围盒估计（仅作对照）
  m.turningBox = std::min(m.maxX, m.maxY) * 2.0;

  // ★ 严格 IMO 口径：**定常回转段最小二乘圆拟合**
  //   定常段判定：累积转向 ≥ 180° 之后（排除转舵段与过渡段）
  size_t from = 0;
  for (size_t i = 0; i < _trail.size(); ++i) {
    if (std::abs(cumTurn[i]) >= 180.0) { from = i; break; }
  }
  double cx = 0, cy = 0, R = 0, r2 = 0;
  if (fitCircle(_trail, from, cx, cy, R, r2)) {
    m.fitR2 = r2;
    m.fitPoints = static_cast<int>(_trail.size() - from);
  }
  // ★ 定常回转判据：拟合优度 R² ≥ 0.90 才认「定常段圆」。
  //   ⚠ 2026-10-14 实测：本平台**差动推力回转**下 R² ≈ 0（角速度由 14.6 持续降到 1.1 °/s
  //     ⇒ 轨迹是**收缩螺旋**而非圆）—— 即**未达定常回转**。此时按 IMO 惯例
  //     用**战术直径**作为回转直径的替代口径，并置 steadyTurn=false 供调用方判断。
  constexpr double kR2Threshold = 0.90;
  if (m.fitR2 >= kR2Threshold) {
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
  ofs << "t_s,x_m,y_m,yaw_deg\n";
  for (const auto &p : trail_)
    ofs << p.t << "," << p.x << "," << p.y << "," << p.yawDeg << "\n";
  return true;
}

}  // namespace keship
