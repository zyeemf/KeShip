/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * Planar soft-mooring controller used by MS-02/MS-03/MS-06 RAO measurements.
 * Controls only horizontal translation and world-yaw; roll/pitch/heave remain free.
 */
#ifndef KESHIP_UTILS_PLANARMOORING_HH_
#define KESHIP_UTILS_PLANARMOORING_HH_

#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>

#include <gz/math/Vector3.hh>

namespace keship
{

struct PlanarMooringCommand
{
  std::string targetModelScoped;
  uint64_t sessionId{0};
  bool enabled{false};
};

struct PlanarMooringMailboxSnapshot
{
  bool found{false};
  PlanarMooringCommand command;
  std::chrono::steady_clock::time_point receivedAt{};
};

/// Thread-safe mailbox between gz-transport callbacks and PreUpdate.
class PlanarMooringMailbox
{
public:
  void Update(const PlanarMooringCommand &_command)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (found_ && _command.sessionId < command_.sessionId)
      return;  // stale/out-of-order command; do not revive an older session
    if (found_ && _command.sessionId == command_.sessionId &&
        (_command.targetModelScoped != command_.targetModelScoped ||
         _command.enabled != command_.enabled))
      return;  // state transition requires a fresh session ID
    command_ = _command;
    found_ = true;
    receivedAt_ = std::chrono::steady_clock::now();
  }

  PlanarMooringMailboxSnapshot Snapshot() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return {found_, command_, receivedAt_};
  }

private:
  mutable std::mutex mutex_;
  bool found_{false};
  PlanarMooringCommand command_;
  std::chrono::steady_clock::time_point receivedAt_{};
};

struct PlanarMooringReference
{
  double x{0.0};
  double y{0.0};
  double yaw{0.0};
};

struct PlanarMooringState
{
  double x{0.0};
  double y{0.0};
  double yaw{0.0};
  double vx{0.0};
  double vy{0.0};
  double yawRate{0.0};
};

struct PlanarMooringGains
{
  /// Provisional soft-mooring bandwidths; headless A/B calibration is required.
  double omegaXY{0.25};
  double omegaYaw{0.70};
  double dampingRatio{1.0};
  /// Equivalent maximum translational acceleration (0.25 g by default).
  double maxLinearAcceleration{0.25 * 9.81};
  /// Maximum yaw angular acceleration (rad/s^2).
  double maxYawAcceleration{0.5};
};

struct PlanarMooringOutput
{
  gz::math::Vector3d force{0.0, 0.0, 0.0};  ///< World-frame force at COM
  double yawTorque{0.0};                    ///< Pure world-Z torque about COM
  bool forceSaturated{false};
  bool yawSaturated{false};
};

/// Smallest signed angle difference in [-pi, pi].
inline double WrapPlanarMooringAngle(double _angle)
{
  if (!std::isfinite(_angle))
    return 0.0;
  return std::atan2(std::sin(_angle), std::cos(_angle));
}

/// Compute a critically damped planar spring-damper wrench.
/// Invalid physical values fail closed to a zero output.
inline PlanarMooringOutput ComputePlanarMooring(
    bool _enabled,
    const PlanarMooringReference &_reference,
    const PlanarMooringState &_state,
    double _massKg,
    double _izzWorldKgM2,
    const PlanarMooringGains &_gains = {})
{
  PlanarMooringOutput out;
  if (!_enabled || !std::isfinite(_massKg) || _massKg <= 0.0 ||
      !std::isfinite(_izzWorldKgM2) || _izzWorldKgM2 <= 0.0 ||
      !std::isfinite(_gains.omegaXY) || _gains.omegaXY <= 0.0 ||
      !std::isfinite(_gains.omegaYaw) || _gains.omegaYaw <= 0.0 ||
      !std::isfinite(_gains.dampingRatio) || _gains.dampingRatio < 0.0 ||
      !std::isfinite(_gains.maxLinearAcceleration) ||
      _gains.maxLinearAcceleration <= 0.0 ||
      !std::isfinite(_gains.maxYawAcceleration) ||
      _gains.maxYawAcceleration <= 0.0)
    return out;

  const double kXY = _massKg * _gains.omegaXY * _gains.omegaXY;
  const double cXY = 2.0 * _gains.dampingRatio * _massKg * _gains.omegaXY;
  double fx = -kXY * (_state.x - _reference.x) - cXY * _state.vx;
  double fy = -kXY * (_state.y - _reference.y) - cXY * _state.vy;
  const double forceMag = std::hypot(fx, fy);
  const double maxForce = _massKg * _gains.maxLinearAcceleration;
  if (forceMag > maxForce)
  {
    const double scale = maxForce / forceMag;
    fx *= scale;
    fy *= scale;
    out.forceSaturated = true;
  }
  out.force.Set(fx, fy, 0.0);

  const double yawError = WrapPlanarMooringAngle(_state.yaw - _reference.yaw);
  const double kYaw = _izzWorldKgM2 * _gains.omegaYaw * _gains.omegaYaw;
  const double cYaw = 2.0 * _gains.dampingRatio *
                       _izzWorldKgM2 * _gains.omegaYaw;
  double tauYaw = -kYaw * yawError - cYaw * _state.yawRate;
  const double maxTau = _izzWorldKgM2 * _gains.maxYawAcceleration;
  if (std::abs(tauYaw) > maxTau)
  {
    tauYaw = std::copysign(maxTau, tauYaw);
    out.yawSaturated = true;
  }
  out.yawTorque = tauYaw;
  return out;
}

}  // namespace keship

#endif  // KESHIP_UTILS_PLANARMOORING_HH_
