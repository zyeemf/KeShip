/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: TaskTomlParser — TOML 任务文件解析/序列化（基于 toml++） */

#ifndef KESHIP_TASK_TOML_PARSER_HH_
#define KESHIP_TASK_TOML_PARSER_HH_

#include <optional>
#include <string>
#include <vector>

namespace keship {

/// 单个航路点
struct TaskWaypoint {
  double x = 0.0;
  double y = 0.0;
  double heading = -1.0;  // < 0 = 无航向要求，[0, 360) = 目标航向（导航惯例：0=北/世界+Y）
};

/// 任务配置（waypoint / tracking / dp 三种类型共用）
struct TaskConfig {
  std::string name;
  std::string type;  // "waypoint" | "tracking" | "dp"
  int timeLimit = 300;

  // waypoint / tracking 共用
  std::vector<TaskWaypoint> waypoints;
  double arrivalRadius = 2.0;  // waypoint: 抵达半径 (m); tracking: tolerance (m)

  // dp 专用
  double holdX = 0.0;
  double holdY = 0.0;
  double holdHdg = -999.0;  // < -900 = 无航向要求
  double tolerance = 3.0;   // dp 容许半径 (m)

  // goto 专用
  double targetX = 0.0;
  double targetY = 0.0;
  double targetHeading = -1.0;  // < 0 = 无航向要求
  double gotoRadius = 3.0;      // 抵达半径 (m)
};

/// TOML 文件解析器
struct TaskTomlParser {
  /// 从文件加载任务配置
  /// @return nullopt 表示解析失败，通过 errOut 返回错误信息
  static std::optional<TaskConfig> load(const std::string &path,
                                        std::string *errOut = nullptr);

  /// 保存任务配置到 TOML 文件
  /// @return false 表示写入失败
  static bool save(const std::string &path, const TaskConfig &cfg,
                   std::string *errOut = nullptr);
};

}  // namespace keship

#endif  // KESHIP_TASK_TOML_PARSER_HH_
