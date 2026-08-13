/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 诊断日志文件输出 — 替代 gzmsg/std::cout，输出到 CSV */
/// 2026.07.24 创建：集中管理诊断日志文件写入

#ifndef KESHIP_UTILS_DIAGNOSTICLOG_HH_
#define KESHIP_UTILS_DIAGNOSTICLOG_HH_

#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace keship
{

class DiagnosticLog
{
public:
  static void init(const std::string &_baseDir);

  static void write(const std::string &_filename,
                    const std::string &_header,
                    const std::string &_csvLine);

  static std::string dir();

private:
  static std::string openFile(const std::string &_filename,
                              const std::string &_header);

  static std::mutex mtx_;
  static std::string logDir_;
  static std::unordered_map<std::string, std::ofstream> files_;
};

} // namespace keship

#endif
