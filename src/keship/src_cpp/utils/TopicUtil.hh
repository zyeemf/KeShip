/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* ROS 2 主题名净化工具 */

#ifndef KESHIP_TOPIC_UTIL_HH_
#define KESHIP_TOPIC_UTIL_HH_

#include <string>

namespace keship
{

/// @brief 净化主题名：仅保留 [A-Za-z0-9_/~{}]、'/'，其余字符（中文等）替换为 '_'
/// @details ROS 2 主题不允许含中文字符；模型实例名/命名空间可能含中文，
///          构造 rclcpp 主题前统一净化，避免 InvalidTopicNameError 崩溃
inline std::string SanitizeRosTopic(const std::string &_in)
{
  std::string out = _in;
  for (char &c : out)
  {
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '_' || c == '/' ||
                    c == '~' || c == '{' || c == '}';
    if (!ok) c = '_';
  }
  return out;
}

}  // namespace keship

#endif
