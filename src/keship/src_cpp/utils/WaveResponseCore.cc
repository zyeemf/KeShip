/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-02 / MS-03 共用核心层 —— 实现 */

#include "WaveResponseCore.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <thread>

#include <gz/msgs/any.pb.h>
#include <gz/msgs/pose.pb.h>

namespace keship {

namespace {
constexpr double kRad2Deg = 180.0 / M_PI;

/// 由四元数取 roll / pitch（deg）
void quatToRpy(const gz::msgs::Quaternion &_q, double &_rollDeg, double &_pitchDeg) {
  const double x = _q.x(), y = _q.y(), z = _q.z(), w = _q.w();
  const double sinr = 2.0 * (w * x + y * z);
  const double cosr = 1.0 - 2.0 * (x * x + y * y);
  const double roll = std::atan2(sinr, cosr);
  double sinp = 2.0 * (w * y - z * x);
  sinp = std::max(-1.0, std::min(1.0, sinp));
  const double pitch = std::asin(sinp);
  _rollDeg = roll * kRad2Deg;
  _pitchDeg = pitch * kRad2Deg;
}

/// 按仿真时间戳对后半段上升过零点插值，估计横摇周期。
double estimatePeriod(const std::vector<WaveSample> &_trail, size_t _from) {
  if (_trail.size() < _from + 4) return -1.0;
  double mean = 0.0;
  for (size_t i = _from; i < _trail.size(); ++i) mean += _trail[i].rollDeg;
  mean /= static_cast<double>(_trail.size() - _from);
  std::vector<double> zc;
  for (size_t i = _from + 1; i < _trail.size(); ++i) {
    const double a = _trail[i - 1].rollDeg - mean;
    const double b = _trail[i].rollDeg - mean;
    const double dt = _trail[i].t - _trail[i - 1].t;
    if (dt <= 0.0) continue;
    if (a <= 0.0 && b > 0.0) {
      const double fraction = -a / (b - a);
      zc.push_back(_trail[i - 1].t + fraction * dt);
    }
  }
  if (zc.size() < 2) return -1.0;
  double sum = 0.0;
  for (size_t i = 1; i < zc.size(); ++i) sum += zc[i] - zc[i - 1];
  return sum / static_cast<double>(zc.size() - 1);
}
}  // namespace

WaveResponseCore::WaveResponseCore(gz::transport::Node &_node,
                                   const std::string &_worldName)
    : node_(_node), worldName_(_worldName) {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  pose_ = std::make_shared<WavePoseShared>();
  auto st = pose_;
  node_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->latest = _msg;
        st->found = true;
      });
  clock_ = std::make_shared<WaveClockShared>();
  auto clockState = clock_;
  node_.Subscribe<gz::msgs::WorldStatistics>("/world/" + worldName_ + "/stats",
      [clockState](const gz::msgs::WorldStatistics &_msg) {
        std::lock_guard<std::mutex> lk(clockState->mutex);
        if (!clockState->alive) return;
        clockState->simTimeSec = _msg.sim_time().sec()
                               + _msg.sim_time().nsec() * 1e-9;
        clockState->found = true;
      });
}

WaveResponseCore::~WaveResponseCore() {
  if (pose_) {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    pose_->alive = false;
  }
  if (clock_) {
    std::lock_guard<std::mutex> lk(clock_->mutex);
    clock_->alive = false;
  }

  // 恢复实验前波浪参数：CWR 规则波在波浪场侧持续生效，关闭对话框
  // 必须覆盖（2026-10-16 修复：原析构不恢复，CWR 残留使船持续晃动）
  keship::WaveParamSnapshot::Instance().Restore(node_);
}

void WaveResponseCore::setTarget(const std::string &_model, const std::string &_area) {
  model_ = _model;
  area_ = _area;
}

void WaveResponseCore::sendWave(double _amplitude, double _period,
                                double _gain, double _direction) {
  // MS-02/MS-03 测量的是给定振幅/周期的**单一规则波**，故固定使用 CWR。
  // PMS 分支的幅值由谱密度和频带宽度计算；若沿用下方 scale=1、number=1，
  // bandwidth = omega * (scale - 1/scale) = 0，所有分量振幅为 0，实验仅采到静水响应。
  // CWR 的 number=1、scale=1 时恰为单频波，振幅字段直接生效。
  waveAmp_ = std::abs(_amplitude * _gain);
  wavePeriod_ = _period;
  waveDirection_ = _direction;   // ★ 重发时须原样带回（见 hh 成员注释）
  waveConfigured_ = true;
  gz::msgs::Param msg;
  auto *p = msg.mutable_params();
  gz::msgs::Any v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
  v.set_string_value("CWR"); (*p)["model"] = v;
  if (!area_.empty()) { v.set_string_value(area_); (*p)["area"] = v; }
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
  v.set_double_value(1.0);        (*p)["gain"] = v;
  v.set_double_value(waveAmp_);   (*p)["amplitude"] = v;
  v.set_double_value(_period);    (*p)["period"] = v;
  v.set_double_value(_direction); (*p)["direction"] = v;
  v.set_double_value(0.0);        (*p)["angle"] = v;
  v.set_double_value(1.0);        (*p)["scale"] = v;
  v.set_double_value(0.0);        (*p)["steepness"] = v;
  v.set_double_value(2.0);        (*p)["tau"] = v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_INT32);
  v.set_int_value(1);             (*p)["number"] = v;

  // 经进程内唯一发布器下发：同一进程重复 Advertise 同一话题会拿到无效
  // 发布器而静默失效（2026-10-16 修复，详见 WavefieldPublisher.hh 文件头）
  keship::WavefieldPublisher(node_).Publish(msg);
}

bool WaveResponseCore::fetchPose(double &_rollDeg, double &_pitchDeg, double &_z) const {
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
    quatToRpy(p.orientation(), _rollDeg, _pitchDeg);
    _z = p.position().z();
    return true;
  }
  return false;
}

bool WaveResponseCore::fetchSimTime(double &_simTimeSec) const {
  if (!clock_) return false;
  std::lock_guard<std::mutex> lk(clock_->mutex);
  if (!clock_->found) return false;
  _simTimeSec = clock_->simTimeSec;
  return true;
}

bool WaveResponseCore::start() {
  running_ = false;
  clear();
  double r = 0, pi = 0, z = 0, simTime = 0.0;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {
    if (fetchPose(r, pi, z) && fetchSimTime(simTime)) { ok = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!ok) return false;
  z0_ = z;
  startSimTime_ = simTime;
  lastSampleSimTime_ = simTime;
  elapsed_ = 0.0;
  clockReset_ = false;
  running_ = true;
  return true;
}

void WaveResponseCore::stop() { running_ = false; }

void WaveResponseCore::clear() {
  trail_.clear();
  elapsed_ = 0.0;
  if (running_) {
    double simTime = 0.0, r = 0.0, pi = 0.0, z = 0.0;
    if (fetchSimTime(simTime)) {
      startSimTime_ = simTime;
      lastSampleSimTime_ = simTime;
    }
    if (fetchPose(r, pi, z)) z0_ = z;
  }
}

bool WaveResponseCore::sample() {
  if (!running_) return false;
  // 周期性重发波浪参数（gz-transport 首帧易丢）。
  // ★ 2026-10-16 修复：重发必须携带 direction（原实现漏传 ⇒ 方向被重置为 0，
  //   横浪设置静默失效、横摇力矩恒 0）。gain 传 1.0 正确：waveAmp_ 已含增益。
  if (waveConfigured_) sendWave(waveAmp_, wavePeriod_, 1.0, waveDirection_);
  double r = 0, pi = 0, z = 0, simTime = 0.0;
  if (!fetchSimTime(simTime) || !fetchPose(r, pi, z)) return false;
  if (simTime + 1e-9 < lastSampleSimTime_) {
    clockReset_ = true;  // 仿真被重置，跨重置拼接的时程/周期无效
    running_ = false;
    return false;
  }
  if (simTime <= lastSampleSimTime_ + 1e-9) return false;  // 暂停或同一仿真步
  lastSampleSimTime_ = simTime;
  elapsed_ = simTime - startSimTime_;
  trail_.push_back(WaveSample{elapsed_, r, pi, z - z0_});
  return true;
}

WaveResponseMetrics WaveResponseCore::metrics() const {
  return computeMetrics(trail_, waveAmp_, wavePeriod_);
}

WaveResponseMetrics WaveResponseCore::computeMetrics(
    const std::vector<WaveSample> &_trail, double _waveAmp, double _wavePeriod) {
  WaveResponseMetrics m;
  m.points = static_cast<int>(_trail.size());
  m.waveAmp = _waveAmp;
  m.wavePeriod = _wavePeriod;
  if (_trail.size() < 10) return m;

  // 稳态段 = 按**仿真时间**取后半段（排除起振瞬态）；不可用点数中点，
  // 因为墙钟轮询与仿真 RTF 可能变化，采样点时间间隔并不保证均匀。
  const double tMid = _trail.front().t
      + 0.5 * (_trail.back().t - _trail.front().t);
  const auto midIt = std::lower_bound(_trail.begin(), _trail.end(), tMid,
      [](const WaveSample &_s, double _t) { return _s.t < _t; });
  const size_t from = static_cast<size_t>(std::distance(_trail.begin(), midIt));
  if (from >= _trail.size()) return m;
  double rMin = _trail[from].rollDeg,  rMax = rMin;
  double pMin = _trail[from].pitchDeg, pMax = pMin;
  double hMin = _trail[from].heave,    hMax = hMin;
  for (size_t i = from; i < _trail.size(); ++i) {
    rMin = std::min(rMin, _trail[i].rollDeg);  rMax = std::max(rMax, _trail[i].rollDeg);
    pMin = std::min(pMin, _trail[i].pitchDeg); pMax = std::max(pMax, _trail[i].pitchDeg);
    hMin = std::min(hMin, _trail[i].heave);    hMax = std::max(hMax, _trail[i].heave);
  }
  m.rollAmp  = (rMax - rMin) / 2.0;
  m.pitchAmp = (pMax - pMin) / 2.0;
  m.heaveAmp = (hMax - hMin) / 2.0;

  // RAO = 响应幅值 / 波幅（波幅为 0 时不计算）
  if (_waveAmp > 1e-9) {
    m.rollRAO  = m.rollAmp / _waveAmp;
    m.pitchRAO = m.pitchAmp / _waveAmp;
    m.heaveRAO = m.heaveAmp / _waveAmp;
  }
  m.rollPeriod = estimatePeriod(_trail, from);
  m.valid = true;
  return m;
}

bool WaveResponseCore::exportCsv(const std::string &_path) const {
  std::ofstream ofs(_path);
  if (!ofs) return false;
  ofs << "t_sim_s,roll_deg,pitch_deg,heave_m\n";
  for (const auto &p : trail_)
    ofs << p.t << "," << p.rollDeg << "," << p.pitchDeg << "," << p.heave << "\n";
  return true;
}

}  // namespace keship
