/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: TaskTomlParser 实现 */

#include "TaskTomlParser.hh"

#include <fstream>
#include <iostream>
#include <toml.hpp>

namespace keship {

static bool writeTaskToml(std::ofstream &f, const TaskConfig &cfg) {
  auto esc = [](const std::string &s) -> std::string {
    // 简单字符串转义：反斜杠和双引号
    std::string out;
    for (char c : s) {
      if (c == '\\') out += "\\\\";
      else if (c == '"') out += "\\\"";
      else out += c;
    }
    return out;
  };

  f << "# KeShip 任务场景\n"
    << "name = \"" << esc(cfg.name) << "\"\n"
    << "time_limit = " << cfg.timeLimit << "\n\n"
    << "[task]\n"
    << "type = \"" << cfg.type << "\"\n";

  if (cfg.type == "waypoint") {
    f << "arrival_radius = " << cfg.arrivalRadius << "\n";
  } else if (cfg.type == "tracking") {
    f << "tolerance = " << cfg.arrivalRadius << "\n";  // arrivalRadius 复用为 tolerance
  } else if (cfg.type == "dp") {
    f << "hold_x = " << cfg.holdX << "\n"
      << "hold_y = " << cfg.holdY << "\n";
    if (cfg.holdHdg > -900.0)
      f << "hold_heading = " << cfg.holdHdg << "\n";
    f << "tolerance = " << cfg.tolerance << "\n";
  } else if (cfg.type == "goto") {
    f << "target_x = " << cfg.targetX << "\n"
      << "target_y = " << cfg.targetY << "\n";
    if (cfg.targetHeading >= 0)
      f << "target_heading = " << cfg.targetHeading << "\n";
    f << "arrival_radius = " << cfg.gotoRadius << "\n";
  }

  // waypoints（waypoint 和 tracking 共用）
  if (cfg.type == "waypoint" || cfg.type == "tracking") {
    for (const auto &wp : cfg.waypoints) {
      f << "\n[[task.waypoints]]\n"
        << "x = " << wp.x << "\n"
        << "y = " << wp.y;
      if (wp.heading >= 0.0)
        f << "\nheading = " << wp.heading;
      f << "\n";
    }
  }

  return true;
}

// ═══════════════════════════════════════════════════════════
// load
// ═══════════════════════════════════════════════════════════

std::optional<TaskConfig> TaskTomlParser::load(const std::string &path,
                                                std::string *errOut) {
  try {
    auto tbl = toml::parse_file(path);

    TaskConfig cfg;

    // 顶层字段
    cfg.name = tbl["name"].value_or("");
    cfg.timeLimit = static_cast<int>(tbl["time_limit"].value_or(300LL));

    // [task] 段
    auto task = tbl["task"];
    if (!task.is_table()) {
      if (errOut) *errOut = "缺少 [task] 段";
      return std::nullopt;
    }
    cfg.type = task.as_table()->at_path("type").value_or("");

    if (cfg.type.empty()) {
      if (errOut) *errOut = "[task] 段缺少 type 字段";
      return std::nullopt;
    }

    if (cfg.type == "waypoint") {
      cfg.arrivalRadius = task.as_table()->at_path("arrival_radius").value_or(2.0);
    } else if (cfg.type == "tracking") {
      cfg.arrivalRadius = task.as_table()->at_path("tolerance").value_or(3.0);
    } else if (cfg.type == "dp") {
      cfg.holdX = task.as_table()->at_path("hold_x").value_or(0.0);
      cfg.holdY = task.as_table()->at_path("hold_y").value_or(0.0);
      cfg.holdHdg = task.as_table()->at_path("hold_heading").value_or(-999.0);
      cfg.tolerance = task.as_table()->at_path("tolerance").value_or(3.0);
    } else if (cfg.type == "goto") {
      cfg.targetX = task.as_table()->at_path("target_x").value_or(0.0);
      cfg.targetY = task.as_table()->at_path("target_y").value_or(0.0);
      cfg.targetHeading = task.as_table()->at_path("target_heading").value_or(-1.0);
      cfg.gotoRadius = task.as_table()->at_path("arrival_radius").value_or(3.0);
    } else {
      if (errOut) *errOut = "未知任务类型: " + cfg.type;
      return std::nullopt;
    }

    // [[task.waypoints]] — waypoint 和 tracking 共用
    if (cfg.type == "waypoint" || cfg.type == "tracking") {
      auto wps = tbl["task"]["waypoints"];
      if (wps.is_array_of_tables()) {
        for (auto &wp : *wps.as_array()) {
          if (!wp.is_table()) continue;
          auto &t = *wp.as_table();
          TaskWaypoint twp;
          twp.x = t.at_path("x").value_or(0.0);
          twp.y = t.at_path("y").value_or(0.0);
          twp.heading = t.at_path("heading").value_or(-1.0);
          cfg.waypoints.push_back(twp);
        }
      }
    }

    if (cfg.name.empty())
      cfg.name = path.substr(path.find_last_of("/\\") + 1);

    return cfg;

  } catch (const toml::parse_error &e) {
    if (errOut) {
      *errOut = std::string("TOML 解析错误: ") + e.what()
              + " (行 " + std::to_string(e.source().begin.line) + ")";
    }
    return std::nullopt;
  } catch (const std::exception &e) {
    if (errOut)
      *errOut = std::string("解析错误: ") + e.what();
    return std::nullopt;
  }
}

// ═══════════════════════════════════════════════════════════
// save
// ═══════════════════════════════════════════════════════════

bool TaskTomlParser::save(const std::string &path, const TaskConfig &cfg,
                           std::string *errOut) {
  try {
    std::ofstream f(path);
    if (!f.is_open()) {
      if (errOut) *errOut = "无法写入文件: " + path;
      return false;
    }
    writeTaskToml(f, cfg);
    f.close();
    if (f.fail()) {
      if (errOut) *errOut = "写入文件失败: " + path;
      return false;
    }
    return true;
  } catch (const std::exception &e) {
    if (errOut)
      *errOut = std::string("保存错误: ") + e.what();
    return false;
  }
}

}  // namespace keship
