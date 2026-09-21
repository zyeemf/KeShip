/*
 * Copyright (C) 2019  Rhys Mainwaring
 * Copyright (C) 2024  KeShip Team
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/

#include <gz/msgs/any.pb.h>
#include <gz/msgs/param.pb.h>

#include <cmath>
#include <iostream>
#include <mutex>
#include <string>

#include <Eigen/Dense>
#include <gz/common/Console.hh>
#include <gz/math/Pose3.hh>
#include <gz/math/Vector2.hh>
#include <gz/math/Vector3.hh>

#include "Wavefield.hh"

using namespace gz;
using namespace keship;

///////////////////////////////////////////////////////////////////////////////
// Utilities
std::ostream& operator<<(std::ostream &_os, const std::vector<double> &_vec)
{
  for (auto&& v : _vec ) // NOLINT
    _os << v << ", ";
  return _os;
}

///////////////////////////////////////////////////////////////////////////////
// Protobuf serialization helpers
namespace
{
  using ParamMap = google::protobuf::Map<std::string, gz::msgs::Any>;

  void SetParamVector2d(ParamMap *_params, const std::string &_key,
                        const math::Vector2d &_value)
  {
    gz::msgs::Any any;
    any.set_type(gz::msgs::Any_ValueType::Any_ValueType_VECTOR3D);
    any.mutable_vector3d_value()->set_x(_value.X());
    any.mutable_vector3d_value()->set_y(_value.Y());
    (*_params)[_key] = any;
  }

  void SetParamDouble(ParamMap *_params, const std::string &_key, double _value)
  {
    gz::msgs::Any any;
    any.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    any.set_double_value(_value);
    (*_params)[_key] = any;
  }

  void SetParamInt32(ParamMap *_params, const std::string &_key, int64_t _value)
  {
    gz::msgs::Any any;
    any.set_type(gz::msgs::Any_ValueType::Any_ValueType_INT32);
    any.set_int_value(_value);
    (*_params)[_key] = any;
  }

  void SetParamString(ParamMap *_params, const std::string &_key,
                      const std::string &_value)
  {
    gz::msgs::Any any;
    any.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    any.set_string_value(_value);
    (*_params)[_key] = any;
  }

  void TryGetParam(const ParamMap &_params, const std::string &_key,
                   double &_member)
  {
    if (_params.count(_key) > 0)
      _member = _params.at(_key).double_value();
  }

  void TryGetParam(const ParamMap &_params, const std::string &_key,
                   size_t &_member)
  {
    if (_params.count(_key) > 0)
      _member = static_cast<size_t>(_params.at(_key).int_value());
  }

  void TryGetParam(const ParamMap &_params, const std::string &_key,
                   std::string &_member)
  {
    if (_params.count(_key) > 0)
      _member = _params.at(_key).string_value();
  }

  void TryGetParam(const ParamMap &_params, const std::string &_key,
                   math::Vector2d &_member)
  {
    if (_params.count(_key) > 0)
    {
      _member = {_params.at(_key).vector3d_value().x(),
                 _params.at(_key).vector3d_value().y()};
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
/// \brief Private data for the WavefieldParameters.
class keship::WavefieldPrivate
{
  /// \brief Constructor.
  public: WavefieldPrivate():
    size({6000, 6000}),
    cellCount({300, 300}),
    model("PMS"),
    number(6),   // 默认 6 组分波（≤8，2026-08）
    scale(1.1),   // PMS 显著带内频率采样，避免高频分量欠采样（2026-08）
    angle(0.4),
    steepness(0.0),
    amplitude(0.0),
    period(5.0),
    phase(0.0),
    direction(0.0),
    angularFrequency(2.0*M_PI),
    wavelength(2 * M_PI / this->DeepWaterDispersionToWavenumber(2.0 * M_PI)),
    wavenumber(this->DeepWaterDispersionToWavenumber(2.0 * M_PI)),
    tau(2.0),
    gain(1.0)
  {
  }

  /// \brief Populate a msg::Param with the current wavefield parameters.
  public: void FillParameters();

  /// \brief Recalculate for constant wavelength-amplitude ratio.
  public: void RecalculateCwr();

  /// \brief Recalculate for Pierson-Moskowitz spectrum sampling model.
  public: void RecalculatePms();

  /// \brief Recalculate all derived quantities from inputs.
  public: void Recalculate();

  /// \brief Helper function.
  public: double DeepWaterDispersionToOmega(double _wavenumber) const;

  /// \brief Helper function.
  public: double DeepWaterDispersionToWavenumber(double _omega) const;

  /// \brief Pierson-Moskowitz wave spectrum.
  public: double Pm(double _omega, double _omegaP) const;

  /// \brief The size of the wavefield.
  public: math::Vector2d size;

  /// \brief The number of grid cells in the wavefield.
  public: math::Vector2d cellCount;

  /// \brief Name of wavefield model to use - must be "PMS" or "CWR"
  public: std::string model;

  /// \brief The number of component waves.
  public: size_t number;

  /// \brief Set the scale of the largest and smallest waves.
  public: double scale;

  /// \brief Set the angle between component waves and the mean direction.
  public: double angle;

  /// \brief Control the wave steepness. 0 is sine waves, 1 is Gerstner waves.
  public: double steepness;

  /// \brief The mean wave amplitude [m].
  public: double amplitude;

  /// \brief The mean wave period [s]
  public: double period;

  /// \brief The mean wave phase (not currently enabled).
  public: double phase;

  /// \brief The mean wave direction.
  public: double direction;

  /// \brief The time constant for exponential increasing waves on startup
  public: double tau;

  /// \brief The multiplier applied to PM spectra
  public: double gain;

  /// \brief Gravitational acceleration [m/s^2].
  static constexpr double kGravity = 9.81;

  /// \brief The mean wave angular frequency (derived).
  public: double angularFrequency;

  /// \brief The mean wavelength (derived).
  public: double wavelength;

  /// \brief The mean wavenumber (derived).
  public: double wavenumber;

  /// \brief The component wave angular frequencies (derived).
  public: std::vector<double> angularFrequencies;

  /// \brief The component wave amplitudes (derived).
  public: std::vector<double> amplitudes;

  /// \brief The component wave phases (derived).
  public: std::vector<double> phases;

  /// \brief The component wave steepness factors (derived).
  public: std::vector<double> steepnesses;

  /// \brief The component wavenumbers (derived).
  public: std::vector<double> wavenumbers;

  /// \brief The component wave dirctions (derived).
  public: std::vector<math::Vector2d> directions;

  /// \brief True when waves are present.
  public: bool active = false;

  /// \brief Topic to publish/receive wavefield updates.
  public: std::string topic = "/wavefield/parameters";

  /// \brief A msgs::Param containing all parameters. This is useful to
  /// communicate all wavefield parameters via Transport.
  public: msgs::Param params;

  /// \brief 线程安全：波浪场为全局共享对象（WavesModel 写参数，
  /// 各模型浮力插件每帧采样读），见 notes/waves_global_refactor.md。
  public: std::mutex mutex;
};

/////////////////////////////////////////////////////////////////////////////
// Template helper shared by RecalculateCwr and RecalculatePms.
// Defined here (not in header) since it's only used within this translation unit.
namespace
{
  template<typename F>
  void RecalculateComponents(WavefieldPrivate &d, F&& getPerComponent)
  {
    d.angularFrequencies.clear();
    d.amplitudes.clear();
    d.phases.clear();
    d.wavenumbers.clear();
    d.steepnesses.clear();
    d.directions.clear();

    for (size_t i = 0; i < d.number; ++i)
    {
      auto [n, a, omega] = getPerComponent(i);
      const double k = d.DeepWaterDispersionToWavenumber(omega);
      // 确定性伪随机相位：打破均匀相位导致的规则条纹，同时保证
      // 每次 Recalculate 结果稳定（波浪不闪烁）。seed 固定 → 可复现。
      uint64_t h = 0x9E3779B97F4A7C15ULL ^ (static_cast<uint64_t>(i) + 0x5D83);
      h *= 0xBF58476D1CE4E5B9ULL;
      const double phi = d.phase + (static_cast<double>(h >> 11) / 9007199254740992.0)
          * 2.0 * M_PI;
      double q = 0.0;
      if (!math::equal(a, 0.0))
        q = std::min(1.0, d.steepness / (a * k * d.number));

      d.amplitudes.push_back(a);
      d.angularFrequencies.push_back(omega);
      d.phases.push_back(phi);
      d.steepnesses.push_back(q);
      d.wavenumbers.push_back(k);

      const double c = std::cos((n * d.angle) + d.direction);
      const double s = std::sin((n * d.angle) + d.direction);
      d.directions.push_back(math::Vector2d(c, s));
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
void WavefieldPrivate::RecalculateCwr()
{
  this->angularFrequency = 2.0 * M_PI / this->period;
  this->wavenumber =
    this->DeepWaterDispersionToWavenumber(this->angularFrequency);
  this->wavelength = 2.0 * M_PI / this->wavenumber;

  RecalculateComponents(*this,
      [&](size_t i) -> std::tuple<int, double, double> {
        const int n = static_cast<int>(i) - static_cast<int>(this->number) / 2;
        const double s = std::pow(this->scale, n);
        return {n, s * this->amplitude,
                this->DeepWaterDispersionToOmega(this->wavenumber / s)};
      });
}

///////////////////////////////////////////////////////////////////////////////
double WavefieldPrivate::Pm(double _omega, double _omegaP) const
{
  double alpha = 0.0081;
  return alpha * std::pow(kGravity, 2.0) / std::pow(_omega, 5.0) * \
    std::exp(-(5.0 / 4.0) * std::pow(_omegaP / _omega, 4.0));
}

///////////////////////////////////////////////////////////////////////////////
void WavefieldPrivate::RecalculatePms()
{
  this->angularFrequency = 2.0 * M_PI / this->period;
  this->wavenumber =
    this->DeepWaterDispersionToWavenumber(this->angularFrequency);
  this->wavelength = 2.0 * M_PI / this->wavenumber;

  // PMS 分量带宽动态生成：频率网格 omega_n = angularFrequency * scale^n，
  // n 从 -(number-1)/2 到 +(number-1)/2（主波 omega=omegaP 居中，对数均匀）。
  // 带宽取相邻频率间隔：内部点两侧各半，边界单侧；number=1 取全谱范围。
  // number=3 时与原 asv_wave_sim 固定 3 带宽逐位一致（回归安全），
  // number>3 不再越界产生 NaN（2026-08 波浪显示修复）。
  const double halfN = static_cast<double>(this->number - 1) / 2.0;
  auto bandwidthFor = [&](double n) -> double {
    if (this->number == 1)
      return this->angularFrequency * (this->scale - 1.0 / this->scale);
    if (n == -halfN)  // 左边界：omega_n -> omega_{n+1}
      return this->angularFrequency * std::pow(this->scale, n)
             * (this->scale - 1.0);
    if (n == halfN)   // 右边界：omega_{n-1} -> omega_n
      return this->angularFrequency * std::pow(this->scale, n)
             * (1.0 - 1.0 / this->scale);
    // 内部点：左右间隔平均
    return this->angularFrequency * std::pow(this->scale, n)
           * (this->scale - 1.0 / this->scale) / 2.0;
  };

  RecalculateComponents(*this,
      [&](size_t i) -> std::tuple<double, double, double> {
        const double n = static_cast<double>(i) - halfN;
        const double s = std::pow(this->scale, n);
        const double omega = this->angularFrequency * s;
        const double pms = this->Pm(omega, this->angularFrequency);
        return {n, this->gain * std::sqrt(2.0 * pms * bandwidthFor(n)), omega};
      });
}

/////////////////////////////////////////////////
void WavefieldPrivate::Recalculate()
{
  if (this->model == "PMS")
  {
    this->RecalculatePms();
  }
  else if (this->model == "CWR")
  {
    this->RecalculateCwr();
  }
  else
  {
    gzwarn << "Wavefield model specified as <" << this->model
           << "> which is not one of the two supported wavefield models: "
           << "PMS or CWR!!!" << std::endl;
  }
}

/////////////////////////////////////////////////
double WavefieldPrivate::DeepWaterDispersionToOmega(double _wavenumber)
  const
{
  return std::sqrt(kGravity * _wavenumber);
}

/////////////////////////////////////////////////
double WavefieldPrivate::DeepWaterDispersionToWavenumber(double _omega)
   const
{
  return _omega * _omega / kGravity;
}

///////////////////////////////////////////////////////////////////////////////
void WavefieldPrivate::FillParameters()
{
  this->params.mutable_params()->clear();
  auto *p = this->params.mutable_params();

  SetParamVector2d(p, "size", this->size);
  SetParamVector2d(p, "cell_count", this->cellCount);
  SetParamInt32(p, "number", static_cast<int64_t>(this->number));
  SetParamDouble(p, "scale", this->scale);
  SetParamDouble(p, "angle", this->angle);
  SetParamDouble(p, "steepness", this->steepness);
  SetParamDouble(p, "amplitude", this->amplitude);
  SetParamDouble(p, "period", this->period);
  SetParamDouble(p, "phase", this->phase);
  SetParamDouble(p, "direction", this->direction);
  SetParamString(p, "model", this->model);
  SetParamDouble(p, "gain", this->gain);
  SetParamDouble(p, "tau", this->tau);
}

/////////////////////////////////////////////////////////////////////////////
Wavefield::Wavefield()
  : data(std::make_unique<WavefieldPrivate>())
{
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
Wavefield::~Wavefield()
{
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::Load(const std::shared_ptr<const sdf::Element> &_sdf)
{
  if (!_sdf->HasElement("wavefield"))
    return;

  std::lock_guard<std::mutex> lock(this->data->mutex);

  auto ptr = const_cast<sdf::Element *>(_sdf.get());
  auto sdfWavefield = ptr->GetElement("wavefield");

  this->data->topic = sdfWavefield->Get<std::string>("topic",
    this->data->topic).first;
  this->data->size = sdfWavefield->Get<math::Vector2d>("size",
    this->data->size).first;
  this->data->cellCount = sdfWavefield->Get<math::Vector2d>("cell_count",
    this->data->cellCount).first;
  if (sdfWavefield->HasElement("wave"))
  {
    auto sdfWave = sdfWavefield->GetElement("wave");

    this->data->model = sdfWave->Get<std::string>("model", "PMS").first;
    this->data->number =
      sdfWave->Get<double>("number", this->data->number).first;
    this->data->amplitude =
      sdfWave->Get<double>("amplitude", this->data->amplitude).first;
    this->data->period =
      sdfWave->Get<double>("period", this->data->period).first;
    this->data->phase = sdfWave->Get<double>("phase", this->data->phase).first;
    this->data->direction =
      sdfWave->Get<double>("direction", this->data->direction).first;
    this->data->scale = sdfWave->Get<double>("scale", this->data->scale).first;
    this->data->angle = sdfWave->Get<double>("angle", this->data->angle).first;
    this->data->steepness =
      sdfWave->Get<double>("steepness", this->data->steepness).first;
    this->data->tau = sdfWave->Get<double>("tau", this->data->tau).first;
    this->data->gain = sdfWave->Get<double>("gain", this->data->gain).first;

    this->data->Recalculate();
  }

  this->data->FillParameters();
  this->DebugPrint();
  this->data->active = true;
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::Load(const msgs::Param &_msg)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);

  const auto &params = _msg.params();

  TryGetParam(params, "size", this->data->size);
  TryGetParam(params, "cell_count", this->data->cellCount);
  TryGetParam(params, "number", this->data->number);
  TryGetParam(params, "scale", this->data->scale);
  TryGetParam(params, "angle", this->data->angle);
  TryGetParam(params, "steepness", this->data->steepness);
  TryGetParam(params, "amplitude", this->data->amplitude);
  TryGetParam(params, "period", this->data->period);
  TryGetParam(params, "phase", this->data->phase);
  TryGetParam(params, "direction", this->data->direction);
  TryGetParam(params, "model", this->data->model);
  TryGetParam(params, "gain", this->data->gain);
  TryGetParam(params, "tau", this->data->tau);

  this->data->FillParameters();
  this->data->Recalculate();
  this->data->active = true;
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetWaveParams(const keship::WaveParams &_wp)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);

  this->data->model      = _wp.model;
  this->data->number     = static_cast<size_t>(_wp.number);
  this->data->period     = _wp.period;
  this->data->amplitude  = _wp.amplitude;
  this->data->direction  = _wp.direction;
  this->data->angle      = _wp.angle;
  this->data->scale      = _wp.scale;
  this->data->steepness  = _wp.steepness;
  this->data->tau        = _wp.tau;
  this->data->gain       = _wp.gain;

  this->data->FillParameters();
  this->data->Recalculate();
  this->data->active = true;
}

///////////////////////////////////////////////////////////////////////////////
std::string Wavefield::Topic() const
{
  return this->data->topic;
}

///////////////////////////////////////////////////////////////////////////////
bool Wavefield::Active() const
{
  return this->data->active;
}

///////////////////////////////////////////////////////////////////////////////
size_t Wavefield::Number() const
{
  return this->data->number;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Angle() const
{
  return this->data->angle;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Scale() const
{
  return this->data->scale;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Steepness() const
{
  return this->data->steepness;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::AngularFrequency() const
{
  return this->data->angularFrequency;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Amplitude() const
{
  return this->data->amplitude;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Period() const
{
  return this->data->period;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Phase() const
{
  return this->data->phase;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Wavelength() const
{
  return this->data->wavelength;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Wavenumber() const
{
  return this->data->wavenumber;
}

///////////////////////////////////////////////////////////////////////////////
float Wavefield::Tau() const
{
  return this->data->tau;
}

///////////////////////////////////////////////////////////////////////////////
float Wavefield::Gain() const
{
  return this->data->gain;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::Direction() const
{
  return this->data->direction;
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetNumber(size_t _number)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->number = _number;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetAngle(double _angle)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->angle = _angle;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetScale(double _scale)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->scale = _scale;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetSteepness(double _steepness)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->steepness = _steepness;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetAmplitude(double _amplitude)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->amplitude = _amplitude;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetPeriod(double _period)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->period = _period;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetPhase(double _phase)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->phase = _phase;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetTau(double _tau)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->tau = _tau;
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetGain(double _gain)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->gain = _gain;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::SetDirection(double _direction)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  this->data->direction = _direction;
  this->data->Recalculate();
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<double> &Wavefield::AngularFrequency_V() const
{
  return this->data->angularFrequencies;
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<double> &Wavefield::Amplitude_V() const
{
  return this->data->amplitudes;
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<double> &Wavefield::Phase_V() const
{
  return this->data->phases;
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<double> &Wavefield::Steepness_V() const
{
  return this->data->steepnesses;
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<double> &Wavefield::Wavenumber_V() const
{
  return this->data->wavenumbers;
}

///////////////////////////////////////////////////////////////////////////////
const std::vector<math::Vector2d> &Wavefield::Direction_V() const
{
  return this->data->directions;
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::DebugPrint() const
{
  gzmsg << "Input Parameters:" << std::endl;
  gzmsg << "model:     " << this->data->model << std::endl;
  gzmsg << "number:     " << this->data->number << std::endl;
  gzmsg << "scale:      " << this->data->scale << std::endl;
  gzmsg << "angle:      " << this->data->angle << std::endl;
  gzmsg << "steepness:  " << this->data->steepness << std::endl;
  gzmsg << "amplitude:  " << this->data->amplitude << std::endl;
  gzmsg << "period:     " << this->data->period << std::endl;
  gzmsg << "direction:  " << this->data->direction << std::endl;
  gzmsg << "tau:  " << this->data->tau << std::endl;
  gzmsg << "gain:  " << this->data->gain << std::endl;
  gzmsg << "Derived Parameters:" << std::endl;
  gzmsg << "amplitudes:  " << this->data->amplitudes << std::endl;
  gzmsg << "wavenumbers: " << this->data->wavenumbers << std::endl;
  gzmsg << "omegas:      " << this->data->angularFrequencies << std::endl;
  gzmsg << "periods:     ";
  for (auto&& omega : this->data->angularFrequencies) // NOLINT
  {
    gzmsg << 2.0 * M_PI / omega <<", ";
  }
  gzmsg << std::endl;
  gzmsg << "phases:      " << this->data->phases << std::endl;
  gzmsg << "steepnesses: " << this->data->steepnesses << std::endl;
  gzmsg << "directions:  ";
  for (auto&& d : this->data->directions) // NOLINT
  {
    gzmsg << d << "; ";
  }
  gzmsg << std::endl;
}

///////////////////////////////////////////////////////////////////////////////
double Wavefield::ComputeDepthSimply(const math::Vector3d &_point,
  double _time, double _timeInit)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);

  double h = 0.0;
  for (std::size_t i = 0; i < this->Number(); ++i)
  {
    double k = this->Wavenumber_V()[i];
    double a = this->Amplitude_V()[i];
    double dx =  this->Direction_V()[i].X();
    double dy =  this->Direction_V()[i].Y();
    double dot = _point.X() * dx + _point.Y() * dy;
    double omega = this->AngularFrequency_V()[i];
    double phi = this->Phase_V()[i];
    double theta = k * dot - omega * _time + phi;
    double c = cos(theta);
    h += a*c;
  }

  // Exponentially grow the waves
  return h * (1 - exp(-1.0 * (_time - _timeInit) / this->Tau()));
}

///////////////////////////////////////////////////////////////////////////////
void Wavefield::ComputeHeightAndGradient(const math::Vector3d &_point,
  double _time, double _timeInit, double *_height, double *_dhdx,
  double *_dhdy)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);

  double h = 0.0;
  double dhdx = 0.0;
  double dhdy = 0.0;

  const std::size_t n = this->Number();
  for (std::size_t i = 0; i < n; ++i)
  {
    const double k = this->Wavenumber_V()[i];
    const double a = this->Amplitude_V()[i];
    const double dx = this->Direction_V()[i].X();
    const double dy = this->Direction_V()[i].Y();
    const double omega = this->AngularFrequency_V()[i];
    const double phi = this->Phase_V()[i];
    const double dot = _point.X() * dx + _point.Y() * dy;
    const double theta = k * dot - omega * _time + phi;
    const double c = std::cos(theta);
    const double s = std::sin(theta);
    h += a * c;
    /// 解析梯度：差分（ds=1 m）会让每个分量相位滞后 k·ds/2，短波甚至反号
    dhdx -= a * k * dx * s;
    dhdy -= a * k * dy * s;
  }

  // Exponentially grow the waves
  const double ramp = 1 - exp(-1.0 * (_time - _timeInit) / this->Tau());

  if (_height) *_height = h * ramp;
  if (_dhdx)   *_dhdx   = dhdx * ramp;
  if (_dhdy)   *_dhdy   = dhdy * ramp;
}

///////////////////////////////////////////////////////////////////////////////
/// ⚠️ 2026-09-21 审核标注（缺陷 W-4）：**当前全项目无调用者**（死代码）。
/// 如将来要启用，需先修三处：
///   1. **缺 Gerstner 的 q 因子** —— 函数内 `F(0) += a*dx*s` / `F(1) += a*dy*s`
///      没有乘 steepness q，而顶点着色器是 `P.x -= q*a*dx*s`（`GerstnerWaves_vs_330.glsl`）
///      ⇒ 即使 steepness=0（纯正弦）也会施加满幅水平位移，与 `ComputeDepthSimply` 不一致；
///   2. **返回值语义与函数名/注释不符** —— 名与文档说"depth = pz − point.z()"，
///      实际已改成返回**高度**（见函数尾部 `const double h = pz;` 及上方注释），
///      调用方若按"深度"用会**符号相反**；
///   3. Newton 求解里的 F 未乘缓起因子（`1−exp(−t/tau)`）而返回值乘了 ⇒ 启动瞬态自相矛盾。
/// 生产路径用的是 `ComputeHeightAndGradient`（W-7 修复后）与 `ComputeDepthSimply`。
double Wavefield::ComputeDepthDirectly(const math::Vector3d &_point,
  double _time, double _timeInit)
{
  std::lock_guard<std::mutex> lock(this->data->mutex);
  // Struture for passing wave parameters to lambdas
  struct WaveParams
  {
    WaveParams(
      const std::vector<double>& _a,
      const std::vector<double>& _k,
      const std::vector<double>& _omega,
      const std::vector<double>& _phi,
      const std::vector<double>& _q,
      const std::vector<math::Vector2d>& _dir) :
      a(_a), k(_k), omega(_omega), phi(_phi), q(_q), dir(_dir) {}

    const std::vector<double>& a;
    const std::vector<double>& k;
    const std::vector<double>& omega;
    const std::vector<double>& phi;
    const std::vector<double>& q;
    const std::vector<math::Vector2d>& dir;
  };

  // Compute the target function and Jacobian. Also calculate pz,
  // the z-component of the Gerstner wave, which we essentially get for free.
  // cppcheck-suppress constParameter
  auto wave_fdf = [this, _time, _timeInit](auto x, auto p, auto t, auto &wp,
                   auto &F, auto &J)
  {
    double pz = 0;
    F(0) = p.x() - x.x();
    F(1) = p.y() - x.y();
    J(0, 0) = -1;
    J(0, 1) =  0;
    J(1, 0) =  0;
    J(1, 1) = -1;
    const size_t n = wp.a.size();
    for (auto&& i = 0; i < n; ++i) // NOLINT
    {
      const double dx = wp.dir[i].X();
      const double dy = wp.dir[i].Y();
      const double q = wp.q[i];
      const double a = wp.a[i];
      const double k = wp.k[i];
      const double dot = x.x() * dx + x.y() * dy;
      const double theta = k * dot - wp.omega[i] * t + wp.phi[i];
      const double s = std::sin(theta);
      const double c = std::cos(theta);
      const double qakc = q * a * k * c;
      const double df1x = qakc * dx * dx;
      const double df1y = qakc * dx * dy;
      const double df2x = df1y;
      const double df2y = qakc * dy * dy;
      pz += a * c;
      F(0) += a * dx * s;
      F(1) += a * dy * s;
      J(0, 0) += df1x;
      J(0, 1) += df1y;
      J(1, 0) += df2x;
      J(1, 1) += df2y;
    }
    // Exponentially grow the waves
    return pz * (1 - exp(-1.0 * (_time - _timeInit) / this->Tau()));
  };

  // Simple multi-variate Newton solver -
  // this version returns the z-component of the
  // wave field at the desired point p.
  // cppcheck-suppress constParameter
  auto solver = [](auto& fdfunc, auto x0, auto p, auto t,
                   auto& wp, auto tol, auto nmax)
  {
    int n = 0;
    double err = 1;
    double pz = 0;
    auto xn = x0;
    Eigen::Vector2d F;
    Eigen::Matrix2d J;
    while (std::abs(err) > tol && n < nmax)
    {
      pz = fdfunc(x0, p, t, wp, F, J);
      xn = x0 - J.inverse() * F;
      x0 = xn;
      err = F.norm();
      n++;
    }
    return pz;
  };

  // Set up parameter references
  WaveParams wp(
    this->Amplitude_V(),
    this->Wavenumber_V(),
    this->AngularFrequency_V(),
    this->Phase_V(),
    this->Steepness_V(),
    this->Direction_V());

  // Tolerances etc.
  const double tol = 1.0E-10;
  const double nmax = 30;

  // Use the target point as the initial guess
  // (this is within sum{amplitudes} of the solution)
  Eigen::Vector2d p2(_point.X(), _point.Y());
  const double pz = solver(wave_fdf, p2, p2, _time, wp, tol, nmax);
  // Removed so that height is reported relative to mean water level
  // const double h = pz - _point.Z();
  const double h = pz;
  return h;
}

///////////////////////////////////////////////////////////////////////////////
msgs::Param Wavefield::Parameters() const
{
  return this->data->params;
}
