/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 诊断日志文件输出 — 实现 */
/// 2026.07.24 创建

#include "DiagnosticLog.hh"

#include <ctime>
#include <sys/stat.h>
#include <sys/types.h>

namespace keship
{

namespace
{
void mkdirRecursive(const std::string &_path)
{
  if (_path.empty() || _path == "/")
    return;
  std::string parent = _path.substr(0, _path.rfind('/'));
  if (!parent.empty() && parent != "/")
    mkdirRecursive(parent);
  mkdir(_path.c_str(), 0755);
}
} // namespace

std::mutex DiagnosticLog::mtx_;
std::string DiagnosticLog::logDir_;
std::unordered_map<std::string, std::ofstream> DiagnosticLog::files_;

void DiagnosticLog::init(const std::string &_baseDir)
{
  std::lock_guard<std::mutex> lock(mtx_);
  if (!logDir_.empty())
    return;

  std::time_t now = std::time(nullptr);
  std::tm tm;
  localtime_r(&now, &tm);
  char ts[32];
  std::strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", &tm);

  logDir_ = _baseDir + "/" + std::string(ts);
  mkdirRecursive(logDir_);
}

void DiagnosticLog::write(const std::string &_filename,
                          const std::string &_header,
                          const std::string &_csvLine)
{
  std::string path = openFile(_filename, _header);
  if (path.empty())
    return;

  std::lock_guard<std::mutex> lock(mtx_);
  auto it = files_.find(path);
  if (it != files_.end())
  {
    it->second << _csvLine << std::endl;
    it->second.flush();
  }
}

std::string DiagnosticLog::dir()
{
  std::lock_guard<std::mutex> lock(mtx_);
  return logDir_;
}

std::string DiagnosticLog::openFile(const std::string &_filename,
                                    const std::string &_header)
{
  std::lock_guard<std::mutex> lock(mtx_);
  if (logDir_.empty())
    return {};

  std::string path = logDir_ + "/" + _filename;

  auto it = files_.find(path);
  if (it == files_.end())
  {
    files_[path] = std::ofstream(path, std::ios::out | std::ios::ate);
    if (!_header.empty())
      files_[path] << _header << std::endl;
  }
  return path;
}

} // namespace keship
