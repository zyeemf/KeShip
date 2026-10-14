/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-02 升沉与纵摇响应 / MS-03 横摇 RAO 测量 —— **共用核心层**（不依赖 Qt）
 *
 * 实验内容：施加**规则波**（指定周期/波幅）⇒ 记录船体 **横摇 / 纵摇 / 升沉** 时程
 *           ⇒ 由**稳态段幅值**计算 **RAO = 响应幅值 / 波幅**。
 *   · MS-02（观察类）：看时程与响应幅值
 *   · MS-03（操作类）：改变波浪周期，测 **RAO 随频率的曲线**（共振峰）
 * 波浪通路：`/keship/wavefield/parameters_set`（gz::msgs::Param）
 *   ⚠ 与 GUI「水域环境参数」对话框**同一通道**（MenuPluginKeship::PublishWaveParams）。
 */

#ifndef KESHIP_WAVE_RESPONSE_CORE_HH_
#define KESHIP_WAVE_RESPONSE_CORE_HH_

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/msgs/pose_v.pb.h>
#include <gz/msgs/param.pb.h>

namespace keship {

/// 响应采样点
struct WaveSample {
  double t = 0.0;
  double rollDeg = 0.0;    ///< 横摇角 (°)
  double pitchDeg = 0.0;   ///< 纵摇角 (°)
  double heave = 0.0;      ///< 升沉（相对初始 z，m）
};

/// 响应指标（RAO 口径）
struct WaveResponseMetrics {
  double waveAmp = 0.0;      ///< 波幅 (m)
  double wavePeriod = 0.0;   ///< 波浪周期 (s)
  double rollAmp = -1.0;     ///< 横摇稳态幅值 (°)
  double pitchAmp = -1.0;    ///< 纵摇稳态幅值 (°)
  double heaveAmp = -1.0;    ///< 升沉稳态幅值 (m)
  double rollRAO = -1.0;     ///< 横摇 RAO (°/m)
  double pitchRAO = -1.0;    ///< 纵摇 RAO (°/m)
  double heaveRAO = -1.0;    ///< 升沉 RAO (m/m)
  double rollPeriod = -1.0;  ///< 横摇响应周期 (s)（由过零检测）
  int    points = 0;
  bool   valid = false;
};

struct WavePoseShared {
  std::mutex mutex;
  bool alive = true;
  gz::msgs::Pose_V latest;
  bool found = false;
};

class WaveResponseCore {
public:
  WaveResponseCore(gz::transport::Node &_node, const std::string &_worldName);
  ~WaveResponseCore();

  WaveResponseCore(const WaveResponseCore &) = delete;
  WaveResponseCore &operator=(const WaveResponseCore &) = delete;

  void setTarget(const std::string &_model, const std::string &_area = "");
  void setSamplePeriod(double _s) { period_ = _s > 0 ? _s : 0.05; }

  /// ★ 下发规则波（与 GUI 水域环境参数同一通道）
  void sendWave(double _amplitude, double _period, double _gain = 1.0,
                const std::string &_model = "PMS");

  bool start();
  void stop();
  void clear();
  bool sample();

  bool fetchPose(double &_rollDeg, double &_pitchDeg, double &_z) const;

  const std::vector<WaveSample> &trail() const { return trail_; }
  bool running() const { return running_; }
  double elapsed() const { return elapsed_; }

  WaveResponseMetrics metrics() const;

  /// ★ 纯函数：由轨迹与波幅算指标（稳态段 = 后半段；幅值 = (max-min)/2）
  static WaveResponseMetrics computeMetrics(const std::vector<WaveSample> &_trail,
                                            double _waveAmp, double _wavePeriod);

  bool exportCsv(const std::string &_path) const;

private:
  gz::transport::Node &node_;
  std::string worldName_, poseTopic_, waveTopic_;
  std::string model_, area_;
  double period_ = 0.05;

  bool   running_ = false;
  double elapsed_ = 0.0;
  double z0_ = 0.0;
  double waveAmp_ = 0.0, wavePeriod_ = 5.0;
  std::vector<WaveSample> trail_;

  std::shared_ptr<WavePoseShared> pose_;
  gz::transport::Node::Publisher wavePub_;
  bool wavePubReady_ = false;
};

}  // namespace keship

#endif  // KESHIP_WAVE_RESPONSE_CORE_HH_
