/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 诊断日志文件输出 — 实现 */
/// 2026.07.24 创建
/// 2026-10-22 改造（批次三 ⑨）：每文件独立锁 + 批量落盘，见 .hh 头注释

#include "DiagnosticLog.hh"

#include <ctime>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>

namespace keship
{

namespace
{
/// 批量落盘阈值：每 N 行 flush 一次（原实现是每行一次系统调用）。
/// 进程正常退出时 ofstream 析构会 flush 剩余行；**kill -9 最多丢 N-1 行**诊断数据。
constexpr int kFlushEvery = 20;

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

/// 每个日志文件一个 Entry：自带锁与未落盘行计数
struct DiagnosticLog::Entry
{
  std::mutex mtx;
  std::ofstream out;
  int pending = 0;  ///< 已写未 flush 的行数
};

std::mutex DiagnosticLog::mtx_;
std::string DiagnosticLog::logDir_;
std::unordered_map<std::string, std::unique_ptr<DiagnosticLog::Entry>>
    DiagnosticLog::files_;

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

DiagnosticLog::Entry *DiagnosticLog::getEntry(const std::string &_filename,
                                              const std::string &_header)
{
  std::lock_guard<std::mutex> lock(mtx_);

  auto it = files_.find(_filename);
  if (it != files_.end())
    return it->second.get();

  if (logDir_.empty())
    return nullptr;

  auto e = std::make_unique<Entry>();
  e->out.open(logDir_ + "/" + _filename, std::ios::out | std::ios::ate);
  if (!e->out)
    return nullptr;
  if (!_header.empty())
    e->out << _header << '\n';

  Entry *raw = e.get();
  files_[_filename] = std::move(e);
  return raw;
}

void DiagnosticLog::write(const std::string &_filename,
                          const std::string &_header,
                          const std::string &_csvLine)
{
  /// 全局锁只在 getEntry 内部持有（map 查找 / 首次建文件），随后只锁本文件。
  Entry *e = getEntry(_filename, _header);
  if (!e)
    return;

  std::lock_guard<std::mutex> lock(e->mtx);
  e->out << _csvLine << '\n';
  if (++e->pending >= kFlushEvery)
  {
    e->out.flush();
    e->pending = 0;
  }
}

void DiagnosticLog::flushAll()
{
  std::lock_guard<std::mutex> g(mtx_);
  for (auto &kv : files_)
  {
    std::lock_guard<std::mutex> l(kv.second->mtx);
    kv.second->out.flush();
    kv.second->pending = 0;
  }
}

std::string DiagnosticLog::dir()
{
  std::lock_guard<std::mutex> lock(mtx_);
  return logDir_;
}

} // namespace keship
