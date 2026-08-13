/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: TaskScoring — 任务评分引擎 */

#ifndef KESHIP_TASK_SCORING_HH_
#define KESHIP_TASK_SCORING_HH_

#include <map>
#include <string>

namespace keship {

/// 任务执行期间累积的测量数据
struct TaskMetrics {
  // waypoint
  int waypointsReached = 0;
  int totalWaypoints = 0;
  double meanPosError = 0.0;   // 到达各航路点时的平均距离偏差 (m)
  double arrivalRadius = 2.0;  // waypoint 抵达半径 (m)

  // tracking
  double maxLateralDev = 0.0;  // 最大横向偏差 (m)
  double tolerance = 3.0;      // tracking 容许偏差 / dp 容许半径 (m)

  // dp
  double rmsDrift = 0.0;       // RMS 位置漂移 (m)
  double meanHeadingErr = 0.0; // 平均航向误差 (°)

  // goto
  double finalDistance = 0.0;  // 任务结束时刻距目标的距离 (m)
  double gotoRadius = 3.0;     // 抵达半径 (m)

  // 通用
  double meanThrust = 0.0;     // 平均指令推力 (N)
  double maxThrust = 1.0;      // 推进器最大推力 (N)，防止除零
  double elapsedSec = 0.0;     // 已用时间 (s)
  double timeLimit = 300.0;    // 时间限制 (s)
};

/// 评分权重（每种任务类型 3 个指标）
struct TaskWeights {
  double w1 = 0.35;
  double w2 = 0.40;
  double w3 = 0.25;

  static TaskWeights waypointDefaults() { return {0.35, 0.40, 0.25}; }
  static TaskWeights trackingDefaults() { return {0.70, 0.30, 0.0}; }
  static TaskWeights dpDefaults() { return {0.50, 0.20, 0.30}; }
};

/// 评分结果
struct ScoreResult {
  std::map<std::string, double> individual;  // 指标名 → 得分 [0, 1]
  double total = 0.0;                         // 总分 [0, 1]
  bool valid = false;
};

/// 评分计算器
struct TaskScoring {
  static ScoreResult computeWaypoint(const TaskMetrics &m,
      const TaskWeights &w = TaskWeights::waypointDefaults());
  static ScoreResult computeTracking(const TaskMetrics &m,
      const TaskWeights &w = TaskWeights::trackingDefaults());
  static ScoreResult computeDp(const TaskMetrics &m,
      const TaskWeights &w = TaskWeights::dpDefaults());
  static ScoreResult computeGoto(const TaskMetrics &m,
      const TaskWeights &w = TaskWeights::waypointDefaults());

private:
  /// 安全归一化：max(0, 1 - value / normalizer)，normalizer <= 0 时返回 0
  static double normScore(double value, double normalizer);
  /// 时间得分
  static double timeScore(double elapsed, double limit);
  /// 推力效率得分
  static double effortScore(double meanThrust, double maxThrust);
};

}  // namespace keship

#endif  // KESHIP_TASK_SCORING_HH_
