/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: TaskScoring 实现 */

#include "TaskScoring.hh"

#include <algorithm>
#include <cmath>

namespace keship {

/// 任务最短执行时间比例——低于此时间，任务视为"未真正执行"
static constexpr double kMinElapsedRatio = 0.03;  // 3% of timeLimit (e.g., 9s for 300s task)

double TaskScoring::normScore(double value, double normalizer) {
  if (normalizer <= 0.0) return 0.0;
  return std::max(0.0, 1.0 - value / normalizer);
}

double TaskScoring::timeScore(double elapsed, double limit) {
  if (limit <= 0.0) return 0.0;
  // 如果执行时间极短（< 3% 时限），时间得分为 0——任务未真正执行
  if (elapsed < limit * kMinElapsedRatio) return 0.0;
  return std::max(0.0, (limit - elapsed) / limit);
}

double TaskScoring::effortScore(double meanThrust, double maxThrust) {
  if (maxThrust <= 0.0) return 0.5;  // 无推进器信息，给中等分
  return 1.0 / (1.0 + meanThrust / maxThrust);
}

// ═══════════════════════════════════════════════════════════
// Waypoint
// ═══════════════════════════════════════════════════════════

ScoreResult TaskScoring::computeWaypoint(const TaskMetrics &m,
                                          const TaskWeights &w) {
  ScoreResult r;
  r.valid = true;

  // 如果未抵达任何航路点，任务失败——总分强制为 0
  if (m.totalWaypoints > 0 && m.waypointsReached == 0) {
    r.individual["waypoint_progress"] = 0.0;
    r.individual["position_error"] = 0.0;
    r.individual["time"] = 0.0;
    r.total = 0.0;
    return r;
  }

  double s1 = (m.totalWaypoints > 0)
      ? static_cast<double>(m.waypointsReached) / m.totalWaypoints : 0.0;
  r.individual["waypoint_progress"] = s1;

  double s2 = normScore(m.meanPosError, m.arrivalRadius);
  r.individual["position_error"] = s2;

  double s3 = timeScore(m.elapsedSec, m.timeLimit);
  r.individual["time"] = s3;

  r.total = w.w1 * s1 + w.w2 * s2 + w.w3 * s3;
  return r;
}

// ═══════════════════════════════════════════════════════════
// Tracking
// ═══════════════════════════════════════════════════════════

ScoreResult TaskScoring::computeTracking(const TaskMetrics &m,
                                          const TaskWeights &w) {
  ScoreResult r;
  r.valid = true;

  // 如果执行时间极短（< 3% 时限），任务未真正执行
  if (m.elapsedSec < m.timeLimit * kMinElapsedRatio) {
    r.individual["tracking_deviation"] = 0.0;
    r.individual["time"] = 0.0;
    r.total = 0.05;
    return r;
  }

  double s1 = normScore(m.maxLateralDev, m.tolerance);
  r.individual["tracking_deviation"] = s1;

  double s2 = timeScore(m.elapsedSec, m.timeLimit);
  r.individual["time"] = s2;

  // tracking 只评估偏差和时间两个维度（能耗在该场景无意义）
  r.total = w.w1 * s1 + w.w2 * s2;
  return r;
}

// ═══════════════════════════════════════════════════════════
// DP
// ═══════════════════════════════════════════════════════════

ScoreResult TaskScoring::computeDp(const TaskMetrics &m,
                                    const TaskWeights &w) {
  ScoreResult r;
  r.valid = true;

  // 如果执行时间极短（< 3% 时限），任务未真正执行
  if (m.elapsedSec < m.timeLimit * kMinElapsedRatio) {
    r.individual["dp_drift"] = 0.0;
    r.individual["heading_error"] = 0.0;
    r.individual["control_effort"] = 0.0;
    r.total = 0.05;
    return r;
  }

  double s1 = normScore(m.rmsDrift, m.tolerance);
  r.individual["dp_drift"] = s1;

  double s2 = normScore(m.meanHeadingErr, 90.0);
  r.individual["heading_error"] = s2;

  double s3 = effortScore(m.meanThrust, m.maxThrust);
  r.individual["control_effort"] = s3;

  r.total = w.w1 * s1 + w.w2 * s2 + w.w3 * s3;
  return r;
}

// ═══════════════════════════════════════════════════════════
// Goto
// ═══════════════════════════════════════════════════════════

ScoreResult TaskScoring::computeGoto(const TaskMetrics &m,
                                      const TaskWeights &w) {
  ScoreResult r;
  r.valid = true;

  if (m.elapsedSec < m.timeLimit * kMinElapsedRatio) {
    r.individual["arrival_accuracy"] = 0.0;
    r.individual["time"] = 0.0;
    r.total = 0.05;
    return r;
  }

  double s1 = normScore(m.finalDistance, m.gotoRadius);
  r.individual["arrival_accuracy"] = s1;

  double s2 = timeScore(m.elapsedSec, m.timeLimit);
  r.individual["time"] = s2;

  // goto 2 指标：精度 + 时间
  r.total = w.w1 * s1 + w.w2 * s2;
  return r;
}

}  // namespace keship
