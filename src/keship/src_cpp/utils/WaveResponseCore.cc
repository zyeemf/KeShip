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

/// 过零检测求周期（取后半段）
double estimatePeriod(const std::vector<double> &_v, double _dt) {
  if (_v.size() < 4) return -1.0;
  const size_t from = _v.size() / 2;
  double mean = 0.0;
  for (size_t i = from; i < _v.size(); ++i) mean += _v[i];
  mean /= static_cast<double>(_v.size() - from);
  std::vector<double> zc;
  for (size_t i = from + 1; i < _v.size(); ++i) {
    const double a = _v[i - 1] - mean, b = _v[i] - mean;
    if (a <= 0.0 && b > 0.0) zc.push_back(static_cast<double>(i) * _dt);
  }
  if (zc.size() < 2) return -1.0;
  double sum = 0.0;
  for (size_t i = 1; i < zc.size(); ++i) sum += (zc[i] - zc[i - 1]);
  return sum / static_cast<double>(zc.size() - 1);
}
}  // namespace

WaveResponseCore::WaveResponseCore(gz::transport::Node &_node,
                                   const std::string &_worldName)
    : node_(_node), worldName_(_worldName) {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  waveTopic_ = "/keship/wavefield/parameters_set";
  pose_ = std::make_shared<WavePoseShared>();
  auto st = pose_;
  node_.Subscribe<gz::msgs::Pose_V>(poseTopic_,
      [st](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lk(st->mutex);
        if (!st->alive) return;
        st->latest = _msg;
        st->found = true;
      });
}

WaveResponseCore::~WaveResponseCore() {
  if (pose_) {
    std::lock_guard<std::mutex> lk(pose_->mutex);
    pose_->alive = false;
  }
}

void WaveResponseCore::setTarget(const std::string &_model, const std::string &_area) {
  model_ = _model;
  area_ = _area;
}

void WaveResponseCore::sendWave(double _amplitude, double _period, double _gain,
                                const std::string &_model) {
  waveAmp_ = _amplitude;
  wavePeriod_ = _period;
  gz::msgs::Param msg;
  auto *p = msg.mutable_params();
  gz::msgs::Any v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
  v.set_string_value(_model); (*p)["model"] = v;
  if (!area_.empty()) { v.set_string_value(area_); (*p)["area"] = v; }
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
  v.set_double_value(_gain);      (*p)["gain"] = v;
  v.set_double_value(_amplitude); (*p)["amplitude"] = v;
  v.set_double_value(_period);    (*p)["period"] = v;
  v.set_double_value(0.0);        (*p)["direction"] = v;
  v.set_double_value(0.0);        (*p)["angle"] = v;
  v.set_double_value(1.0);        (*p)["scale"] = v;
  v.set_double_value(0.0);        (*p)["steepness"] = v;
  v.set_double_value(2.0);        (*p)["tau"] = v;
  v.set_type(gz::msgs::Any_ValueType::Any_ValueType_INT32);
  v.set_int_value(1);             (*p)["number"] = v;

  if (!wavePubReady_) {
    wavePub_ = node_.Advertise<gz::msgs::Param>(waveTopic_);
    wavePubReady_ = true;
  }
  wavePub_.Publish(msg);
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

bool WaveResponseCore::start() {
  clear();
  double r = 0, pi = 0, z = 0;
  bool ok = false;
  for (int i = 0; i < 50; ++i) {
    if (fetchPose(r, pi, z)) { ok = true; break; }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!ok) return false;
  z0_ = z;
  elapsed_ = 0.0;
  running_ = true;
  return true;
}

void WaveResponseCore::stop() { running_ = false; }

void WaveResponseCore::clear() {
  trail_.clear();
  elapsed_ = 0.0;
}

bool WaveResponseCore::sample() {
  if (!running_) return false;
  // ⚠ 周期性重发波浪参数（gz-transport 首帧易丢，见 MS-01 实测）
  if (waveAmp_ > 0.0) sendWave(waveAmp_, wavePeriod_);
  double r = 0, pi = 0, z = 0;
  if (!fetchPose(r, pi, z)) return false;
  elapsed_ += period_;
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

  // 稳态段 = 后半段（排除起振瞬态）
  const size_t from = _trail.size() / 2;
  double rMin = _trail[from].rollDeg,  rMax = rMin;
  double pMin = _trail[from].pitchDeg, pMax = pMin;
  double hMin = _trail[from].heave,    hMax = hMin;
  std::vector<double> rolls;
  for (size_t i = from; i < _trail.size(); ++i) {
    rMin = std::min(rMin, _trail[i].rollDeg);  rMax = std::max(rMax, _trail[i].rollDeg);
    pMin = std::min(pMin, _trail[i].pitchDeg); pMax = std::max(pMax, _trail[i].pitchDeg);
    hMin = std::min(hMin, _trail[i].heave);    hMax = std::max(hMax, _trail[i].heave);
    rolls.push_back(_trail[i].rollDeg);
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
  m.rollPeriod = estimatePeriod(rolls, _trail.size() > 1
      ? (_trail[1].t - _trail[0].t) : 0.05);
  m.valid = true;
  return m;
}

bool WaveResponseCore::exportCsv(const std::string &_path) const {
  std::ofstream ofs(_path);
  if (!ofs) return false;
  ofs << "t_s,roll_deg,pitch_deg,heave_m\n";
  for (const auto &p : trail_)
    ofs << p.t << "," << p.rollDeg << "," << p.pitchDeg << "," << p.heave << "\n";
  return true;
}

}  // namespace keship
