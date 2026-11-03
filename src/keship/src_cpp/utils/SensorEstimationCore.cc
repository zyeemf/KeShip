/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-08 传感器感知与位姿估计 —— 核心层实现 */

#include "SensorEstimationCore.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <gz/msgs/pose.pb.h>

namespace keship {

namespace {
constexpr double kRad2Deg = 180.0 / M_PI;

void quatToRpy(const gz::msgs::Quaternion &_q, double &_rollDeg, double &_pitchDeg, double &_yawDeg) {
  const double x = _q.x(), y = _q.y(), z = _q.z(), w = _q.w();
  const double sinr = 2.0 * (w * x + y * z);
  const double cosr = 1.0 - 2.0 * (x * x + y * y);
  _rollDeg = std::atan2(sinr, cosr) * kRad2Deg;
  double sinp = 2.0 * (w * y - z * x);
  sinp = std::max(-1.0, std::min(1.0, sinp));
  _pitchDeg = std::asin(sinp) * kRad2Deg;
  _yawDeg = std::atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z)) * kRad2Deg;
}

double norm180(double _d) {
  while (_d > 180.0) _d -= 360.0;
  while (_d <= -180.0) _d += 360.0;
  return _d;
}
}  // namespace

SensorEstimationCore::SensorEstimationCore(gz::transport::Node &_node,
                                           const std::string &_worldName)
    : node_(_node), worldName_(_worldName) {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  shared_ = std::make_shared<SensorShared>();
  auto st = shared_;
  node_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->pose = _msg;
        st->poseFound = true;
      });
}

SensorEstimationCore::~SensorEstimationCore() {
  if (shared_) {
    std::lock_guard<std::mutex> lk(shared_->mutex);
    shared_->alive = false;
  }
}

void SensorEstimationCore::setTarget(const std::string &_model,
                                     const std::string &_imuTopic) {
  model_ = _model;
  if (!_imuTopic.empty()) {
    imuTopic_ = _imuTopic;
  } else if (!_model.empty()) {
    // 约定：/world/<world>/model/<模型>/link/<link>/sensor/<名>/imu
    // 常用 link 为 base_link、传感器名多为 IMU（ke0812_1 实测）
    imuTopic_ = "/world/" + worldName_ + "/model/" + _model
              + "/link/base_link/sensor/IMU/imu";
  }
  if (!imuTopic_.empty() && !subscribed_) {
    auto st = shared_;
    node_.Subscribe<gz::msgs::IMU>(imuTopic_,
        [st](const gz::msgs::IMU &_msg) {
          std::lock_guard<std::mutex> lk(st->mutex);
          if (!st->alive) return;
          st->imu = _msg;
          st->imuFound = true;
          st->imuCount++;
        });
    subscribed_ = true;
  }
}

bool SensorEstimationCore::fetchRef(double &_rollDeg, double &_pitchDeg, double &_yawDeg) const {
  gz::msgs::Pose_V snap;
  {
    std::lock_guard<std::mutex> lk(shared_->mutex);
    if (!shared_->poseFound) return false;
    snap = shared_->pose;
  }
  for (int i = 0; i < snap.pose_size(); ++i) {
    const auto &p = snap.pose(i);
    std::string nm = p.name();
    auto pos = nm.rfind("::");
    if (pos != std::string::npos) nm = nm.substr(pos + 2);
    if (nm != model_) continue;
    quatToRpy(p.orientation(), _rollDeg, _pitchDeg, _yawDeg);
    return true;
  }
  return false;
}

bool SensorEstimationCore::fetchImu(double &_gx, double &_gy, double &_gz) const {
  std::lock_guard<std::mutex> lk(shared_->mutex);
  if (!shared_->imuFound) return false;
  _gx = shared_->imu.angular_velocity().x();
  _gy = shared_->imu.angular_velocity().y();
  _gz = shared_->imu.angular_velocity().z();
  return true;
}

bool SensorEstimationCore::start() {
  clear();
  double r = 0, p = 0, y = 0;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {
    if (fetchRef(r, p, y)) { ok = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!ok) return false;
  estRoll_ = r; estPitch_ = p; estYaw_ = y;
  refYawStart_ = y;
  elapsed_ = 0.0;
  running_ = true;
  return true;
}

void SensorEstimationCore::stop() { running_ = false; }

void SensorEstimationCore::clear() {
  trail_.clear();
  elapsed_ = 0.0;
}

bool SensorEstimationCore::sample() {
  if (!running_) return false;
  double rr = 0, pr = 0, yr = 0;
  if (!fetchRef(rr, pr, yr)) return false;
  double gx = 0, gy = 0, gz = 0;
  fetchImu(gx, gy, gz);      // 无 IMU 时按 0 处理（仍记录真值）

  // ★ 纯积分姿态估计（无观测修正 ⇒ 会漂移，正是本实验要观察的现象）
  estRoll_  += gx * period_ * kRad2Deg;
  estPitch_ += gy * period_ * kRad2Deg;
  estYaw_   += gz * period_ * kRad2Deg;

  elapsed_ += period_;
  trail_.push_back(SensorSample{elapsed_, estRoll_, estPitch_, estYaw_, rr, pr, yr, gx, gy, gz});
  return true;
}

SensorMetrics SensorEstimationCore::metrics() const {
  return computeMetrics(trail_, elapsed_);
}

SensorMetrics SensorEstimationCore::computeMetrics(
    const std::vector<SensorSample> &_trail, double _duration) {
  SensorMetrics m;
  m.points = static_cast<int>(_trail.size());
  if (_trail.size() < 3) return m;

  double sR = 0, sP = 0, sY = 0;
  double maxDrift = 0.0;
  for (const auto &s : _trail) {
    const double dR = norm180(s.estRoll - s.refRoll);
    const double dP = norm180(s.estPitch - s.refPitch);
    const double dY = norm180(s.estYaw - s.refYaw);
    sR += dR * dR; sP += dP * dP; sY += dY * dY;
    maxDrift = std::max(maxDrift, std::abs(dY));
  }
  const double n = static_cast<double>(_trail.size());
  m.rmseRoll  = std::sqrt(sR / n);
  m.rmsePitch = std::sqrt(sP / n);
  m.rmseYaw   = std::sqrt(sY / n);
  m.maxDriftYaw = maxDrift;
  if (_duration > 1e-9) m.imuRateHz = n / _duration;
  m.valid = true;
  return m;
}

bool SensorEstimationCore::exportCsv(const std::string &_path) const {
  std::ofstream ofs(_path);
  if (!ofs) return false;
  ofs << "t_s,est_roll,est_pitch,est_yaw,ref_roll,ref_pitch,ref_yaw,gx,gy,gz\n";
  for (const auto &s : trail_)
    ofs << s.t << "," << s.estRoll << "," << s.estPitch << "," << s.estYaw << ","
        << s.refRoll << "," << s.refPitch << "," << s.refYaw << ","
        << s.gyroX << "," << s.gyroY << "," << s.gyroZ << "\n";
  return true;
}

}  // namespace keship
