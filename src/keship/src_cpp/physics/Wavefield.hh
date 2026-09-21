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

#ifndef KESHIP_WAVEFIELD_HH_
#define KESHIP_WAVEFIELD_HH_

#include <gz/msgs/param.pb.h>

#include <memory>
#include <vector>

#include <gz/sim/config.hh>
#include <gz/math/Vector2.hh>
#include <gz/math/Vector3.hh>
#include <sdf/sdf.hh>

#include "keship/utils/WaterArea.hh"

namespace keship
{
  /// \brief Class to hold private data for Wavefield.
  class WavefieldPrivate;

  /// \brief A class to generate a wave field.
  /// This is a port from https://github.com/srmainwaring/asv_wave_sim
  /// with some modifications.
  ///
  /// It uses SDF to parametrize the waves.
  ///
  /// ## Modes of operation: The class is designed to operate in two modes:
  ///
  /// 1. Primary: The plugin reads all the wavefield parameters from SDF.
  /// 2. Secondary: The plugin subscribes to a topic and receives the wavefield
  ///               parameters via a message.
  ///
  /// Note that the top level plugins using this class are responsible for
  /// publishing or subscribing to the wavefield topic. When configured in
  /// primary mode, you should use from the top level plugin:
  ///
  ///    Load(const std::shared_ptr<const sdf::Element> &_sdf)
  ///
  /// and:
  ///
  ///    Parameters()
  ///
  /// to read the current set of parameters and periodically send the wavefield
  /// parameters to other plugins configured in secondary mode.
  ///
  /// When configured in secondary mode, you should use from the top level
  /// plugin:
  ///
  ///    Load(const std::shared_ptr<const sdf::Element> &_sdf)
  ///
  /// and:
  ///
  ///    Load(const msgs::Param &_msg)
  ///
  /// to parse the topic and subscribe to the wavefield parameter updates.
  /// When the parameters are received, the call to the latter version of Load()
  /// will update the wavefield parameters.
  ///
  ///
  /// ## SDF parameters in primary mode:
  ///
  /// * `<topic>` (string, default: "/wavefield/parameters")
  ///    The topic for publishing wave field updates.
  ///
  /// * `<size>` (Vector2D, default: (6000 6000))
  ///   A two component vector for the size of the wave field in each direction.
  ///
  /// * `<cell_count>` (int, default: (300 300))
  ///   A two component vector for the number of grid cells in each direction.
  ///
  /// * `<number>` (int, default: 3)
  ///   The number of component waves.
  ///
  /// * `<scale>` (double, default: 1.1)
  ///   The scale between the mean and largest / smallest component waves.
  ///
  /// * `<angle>` (double, default: 0.4)
  ///   The angle between the mean wave direction and the
  ///   largest / smallest component waves.
  ///
  /// * `<steepness>` (double, default: 0.0)
  ///   A parameter in [0, 1] controlling the wave steepness with
  ///   1 being steepest.
  ///
  /// * `<amplitude>` (double, default: 0.0)
  ///   The amplitude of the mean wave in [m]. This parameter is only used when
  ///   model is CWR.
  ///
  /// * `<period>` (double, default: 5.0)
  ///   The period of the mean wave in [s].
  ///
  /// * `<phase>` (double, default: 0.0)
  ///   The phase of the mean wave.
  ///
  /// * `<direction>` (double, default: 0.0)
  ///   An angle defining mean wave direction (theta_m)
  ///
  /// * `<model>` (string, default: default)
  ///   The model used to generate component waves.
  ///   Should be either "PMS" or "CWR"
  ///
  /// * `<gain>` (double, default: 1.0)
  ///   For PMS, the multiplier applied to component amplitudes.
  ///
  /// * `<tau>` (double, default: 2.0)
  ///   Time constant used to gradually increase wavefield at startup.
  ///
  /// ## SDF parameters in secondary mode:
  ///
  /// * `<topic>` (string, default: "/wavefield/parameters")
  ///    The topic for receiving wave field updates.
  ///
  /// ## Example (primary mode)
  /// <wavefield>
  ///   <size>1000 1000</size>
  ///   <cell_count>50 50</cell_count>
  ///   <wave>
  ///     <model>PMS</model>
  ///     <period>5</period>
  ///     <number>3</number>
  ///     <scale>1.5</scale>
  ///     <gain>0.1</gain>
  ///     <direction>0.0</direction>
  ///     <angle>0.4</angle>
  ///     <tau>2.0</tau>
  ///     <amplitude>0.0</amplitude>
  ///     <steepness>0.0</steepness>
  ///   </wave>
  /// </wavefield>
  ///
  /// ## Example (secondary mode)
  /// <wavefield>
  ///   <topic>/keship/wavefield/parameters</topic>
  /// </wavefield>
  class Wavefield
  {
    /// \brief Shared ownership alias (波浪场为全局共享对象，见
    ///        notes/waves_global_refactor.md — gz-waves ECM 组件模式)。
    public: using Ptr = std::shared_ptr<Wavefield>;
    public: using ConstPtr = std::shared_ptr<const Wavefield>;

    /// \brief Constructor.
    public: Wavefield();

    /// \brief Destructor.
    public: virtual ~Wavefield();

    /// \brief Set the parameters from an SDF Element tree.
    ///
    /// \param[in] _sdf The SDF Element tree containing the wavefield parameters
    public: void Load(const std::shared_ptr<const sdf::Element> &_sdf);

    /// \brief Set the parameters from a message.
    ///
    /// \param[in] _msg The SDF Element tree containing the wavefield parameters
    public: void Load(const gz::msgs::Param &_msg);

    /// \brief Set all wave parameters from a WaveParams struct
    /// (JSON 水区配置通道，WavesModel 使用)。
    ///
    /// \param[in] _wp The wave parameters.
    public: void SetWaveParams(const keship::WaveParams &_wp);

    /// \brief Is the wavefield loaded.
    /// \return True when the wavefield has been loaded or false otherwise.
    public: bool Active() const;

    /// \brief The wavefield topic to publish/receive updates.
    /// \return The topic name.
    public: std::string Topic() const;

    /// \brief The number of wave components (max 8, limited by WaveVisual shader).
    public: size_t Number() const;

    /// \brief The angle between the mean wave direction and the
    ///        largest / smallest component waves.
    public: double Angle() const;

    /// \brief The scale between the mean and largest / smallest
    ///        component waves.
    public: double Scale() const;

    /// \brief A parameter in [0, 1] controlling the wave steepness
    ///        with 1 being steepest.
    public: double Steepness() const;

    /// \brief The angular frequency
    public: double AngularFrequency() const;

    /// \brief The amplitude of the mean wave in [m].
    public: double Amplitude() const;

    /// \brief The period of the mean wave in [s].
    public: double Period() const;

    /// \brief The phase of the mean wave.
    public: double Phase() const;

    /// \brief The mean wavelength.
    public: double Wavelength() const;

    /// \brief The mean wavenumber.
    public: double Wavenumber() const;

    /// \brief Time-constant for starting waves.
    public: float Tau() const;

    /// \brief Amplitude multiplier for PMS.
    public: float Gain() const;

    /// \brief A double specifiying the mean wave direction.
    public: double Direction() const;

    /// \brief Set the number of wave components (max 8).
    ///
    /// \param[in] _number The number of component waves.
    public: void SetNumber(size_t _number);

    /// \brief Set the angle parameter controlling
    /// the direction of the component waves.
    ///
    /// \param[in] _angle  The angle parameter.
    public: void SetAngle(double _angle);

    /// \brief Set the scale parameter controlling
    /// the range of amplitudes of the component waves.
    ///
    /// \param[in] _scale The scale parameter.
    public: void SetScale(double _scale);

    /// \brief Set the steepness parameter controlling
    /// the steepness of the waves. In [0, 1].
    ///
    /// \param[in] _steepness The steepness parameter.
    public: void SetSteepness(double _steepness);

    /// \brief Set the mean wave amplitude. Must be positive.
    ///
    /// \param[in] _amplitude The amplitude parameter.
    public: void SetAmplitude(double _amplitude);

    /// \brief Set the mean wave period. Must be positive.
    ///
    /// \param[in] _period The period parameter.
    public: void SetPeriod(double _period);

    /// \brief Set the mean wave phase.
    ///
    /// \param[in] _phase The phase parameter.
    public: void SetPhase(double _phase);

    /// \brief Set the time constant.
    ///
    /// \param[in] _tau The time constant.
    public: void SetTau(double _tau);

    /// \brief Set the PMS amplitude multiplier
    ///
    /// \param[in] _gain The multiplier
    public: void SetGain(double _gain);

    /// \brief Set the mean wave direction.
    ///
    /// \param[in] _direction The direction parameter, a double.
    public: void SetDirection(double _direction);

    /// \brief Access the component angular frequencies.
    public: const std::vector<double> &AngularFrequency_V() const;

    /// \brief Access the component amplitudes.
    public: const std::vector<double> &Amplitude_V() const;

    /// \brief Access the component phases.
    public: const std::vector<double> &Phase_V() const;

    /// \brief Access the steepness components.
    public: const std::vector<double> &Steepness_V() const;

    /// \brief Access the component wavenumbers.
    public: const std::vector<double> &Wavenumber_V() const;

    /// \brief Access the component directions.
    public: const std::vector<gz::math::Vector2d> &Direction_V() const;

    /// \brief Print a summary of the wave parameters to the gzmsg stream.
    public: void DebugPrint() const;

    /// A simpler version of determining wave height at a point.
    /// This method enforces that q (steepness) = 0 which allows us
    /// to caculate the wave height exactly for a given 2D point without the
    /// need to interatively solve for the position/height.
    /// \param[in] _point       The point at which we want the depth.
    /// \param[in] _time        The time at which we want the depth.
    /// \param[in] _timeInit    The time at which we want the wavefield to start
    /// \return                 The depth 'h' at the point.
    public: double ComputeDepthSimply(const gz::math::Vector3d &_point,
                                      double _time,
                                      double _timeInit = 0);

    /// \brief Compute the wave height and its **analytic** gradient at a point.
    ///
    /// 与 ComputeDepthSimply 同一波形（q = 0，无 Gerstner 水平位移）、同一缓起因子，
    /// 但梯度取解析式
    ///     ∂h/∂x = −Σ a_i k_i d_ix sin(θ_i)， ∂h/∂y = −Σ a_i k_i d_iy sin(θ_i)
    /// 而非有限差分。ds = 1 m 的前向差分会给每个分量引入
    /// 幅值 × 2sin(k·ds/2)/(k·ds) 与**相位滞后 k·ds/2**：
    /// 默认参数（Tp=2.5 s）下滞后 11°~30°，k·ds > π（λ < 2 m）时甚至**反号**。
    /// 见 doc/audit/water_wave_audit_2026-09-21.md §W-7。
    ///
    /// \param[in]  _point     The point at which we want the height.
    /// \param[in]  _time      The time at which we want the height.
    /// \param[in]  _timeInit  The time at which we want the wavefield to start
    /// \param[out] _height    The wave height at the point (may be nullptr).
    /// \param[out] _dhdx      ∂h/∂x (may be nullptr).
    /// \param[out] _dhdy      ∂h/∂y (may be nullptr).
    public: void ComputeHeightAndGradient(const gz::math::Vector3d &_point,
                                          double _time,
                                          double _timeInit,
                                          double *_height,
                                          double *_dhdx,
                                          double *_dhdy);

    /// \brief Compute the depth at a point directly
    /// (no sampling or interpolation).
    ///
    /// This method solves for (x, y) that when input into the
    /// Gerstner wave function
    /// gives the coordinates of the supplied parameter
    /// _point (_point.x(), _point.y()),
    /// and also computes the wave height pz at this point.
    /// The depth h = pz - point.z().
    /// This is a numerical method that uses a multi-variate
    /// Newton solver to solve
    /// the two dimensional non-linear system. In general it is not as fast as
    /// sampling from a discretised wave field with an efficient
    /// line intersection algorithm.
    ///
    /// \param[in] _point       The point at which we want the depth.
    /// \param[in] _time        The time at which we want the depth.
    /// \param[in] _timeInit    The time at which we want the wavefield to start
    /// \return                 The depth 'h' at the point.
    ///
    /// ⚠️ 2026-09-21 审核（缺陷 W-4）：**当前全项目无调用者**；且实际返回的是
    /// **高度**而非"深度"、内部缺 Gerstner 的 `q` 因子（与顶点着色器不一致）、
    /// F 与返回值对缓起因子的处理自相矛盾 —— 详见 `.cc` 中同名函数的标注。
    /// 生产路径请用 `ComputeHeightAndGradient` / `ComputeDepthSimply`。
    public: double ComputeDepthDirectly(const gz::math::Vector3d &_point,
                                        double _time,
                                        double _timeInit = 0);

    /// \brief Get all the parameters in a single message.
    /// \return A message with all the parameters.
    public: gz::msgs::Param Parameters() const;

    /// \internal
    /// \brief Pointer to the class private data.
    private: std::unique_ptr<WavefieldPrivate> data;
  };
}

#endif
