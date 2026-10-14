/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-08 传感器感知与位姿估计 —— **核心层**（不依赖 Qt；GUI 与无头共用）
 *
 * 实验内容：读取 **IMU** 角速度 → **积分**得姿态估计 ⇒ 与**位姿真值**对比 ⇒
 *           评估估计误差（RMSE），观察**积分漂移**（纯积分无观测修正）。
 * 知识点：传感器感知 · 状态估计 · 惯性导航漂移 · 互补滤波的必要性。
 * 传感器通路：`/world/<world>/model/<模型>/link/<link>/sensor/<名>/imu`（gz::msgs::IMU）
 * 真值通路  ：`/world/<world>/pose/info`（gz::msgs::Pose_V）
 */

#ifndef KESHIP_SENSOR_ESTIMATION_CORE_HH_
#define KESHIP_SENSOR_ESTIMATION_CORE_HH_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/msgs/imu.pb.h>
#include <gz/msgs/pose_v.pb.h>

namespace keship {

/// 采样点：估计 vs 真值
struct SensorSample {
  double t = 0.0;
  double estRoll = 0.0, estPitch = 0.0, estYaw = 0.0;   ///< 积分估计 (°)
  double refRoll = 0.0, refPitch = 0.0, refYaw = 0.0;   ///< 真值 (°)
  double gyroX = 0.0, gyroY = 0.0, gyroZ = 0.0;         ///< 角速度 (rad/s)
};

/// 估计指标
struct SensorMetrics {
  double rmseRoll = -1.0, rmsePitch = -1.0, rmseYaw = -1.0;   ///< 均方根误差 (°)
  double maxDriftYaw = -1.0;                                   ///< 航向最大漂移 (°)
  double imuRateHz = -1.0;                                     ///< IMU 消息率估计 (Hz)
  int    points = 0;
  int    imuMsgs = 0;
  bool   valid = false;
};

struct SensorShared {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::IMU imu;
  bool imuFound = false;
  int  imuCount = 0;
  gz::msgs::Pose_V pose;
  bool poseFound = false;
};

class SensorEstimationCore {
public:
  SensorEstimationCore(gz::transport::Node &_node, const std::string &_worldName);
  ~SensorEstimationCore();

  SensorEstimationCore(const SensorEstimationCore &) = delete;
  SensorEstimationCore &operator=(const SensorEstimationCore &) = delete;

  /// @param _model 船模名 @param _imuTopic 完整 IMU 话题（空 = 按约定自动构造）
  void setTarget(const std::string &_model, const std::string &_imuTopic = "");
  void setSamplePeriod(double _s) { period_ = _s > 0 ? _s : 0.05; }

  bool start();
  void stop();
  void clear();
  bool sample();

  const std::vector<SensorSample> &trail() const { return trail_; }
  bool running() const { return running_; }
  int  imuCount() const { std::lock_guard<std::mutex> lk(shared_->mutex); return shared_->imuCount; }
  bool imuAlive() const { std::lock_guard<std::mutex> lk(shared_->mutex); return shared_->imuFound; }

  SensorMetrics metrics() const;
  static SensorMetrics computeMetrics(const std::vector<SensorSample> &_trail, double _duration);
  bool exportCsv(const std::string &_path) const;

private:
  bool fetchRef(double &_rollDeg, double &_pitchDeg, double &_yawDeg) const;
  bool fetchImu(double &_gx, double &_gy, double &_gz) const;

  gz::transport::Node &node_;
  std::string worldName_, poseTopic_, imuTopic_;
  std::string model_;
  double period_ = 0.05;

  bool   running_ = false;
  double elapsed_ = 0.0;
  double estRoll_ = 0.0, estPitch_ = 0.0, estYaw_ = 0.0;   ///< 积分估计累积值 (°)
  double refYawStart_ = 0.0;
  std::vector<SensorSample> trail_;

  std::shared_ptr<SensorShared> shared_;
  bool subscribed_ = false;
};

}  // namespace keship

#endif  // KESHIP_SENSOR_ESTIMATION_CORE_HH_
