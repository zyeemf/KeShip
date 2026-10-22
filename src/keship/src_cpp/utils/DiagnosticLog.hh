/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 诊断日志文件输出 — 替代 gzmsg/std::cout，输出到 CSV */
/// 2026.07.24 创建：集中管理诊断日志文件写入
/// 2026-10-22 改造（批次三 ⑨）：**每文件独立锁 + 批量落盘**
///   原实现：单个全局 mutex 保护整个写入路径（write 与 openFile 各取一次锁），
///   且**每行 flush 一次**（系统调用）。写入频次高时（hydro_detail 每帧）开销显著。
///   现实现：全局锁只保护 files_ / logDir_（map 查找 + 首次建文件），
///   实际写入只锁**本文件自己的 mutex**；flush 改为每 kFlushEvery 行一次。

#ifndef KESHIP_UTILS_DIAGNOSTICLOG_HH_
#define KESHIP_UTILS_DIAGNOSTICLOG_HH_

#include <memory>
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

  /// 显式落盘所有文件（进程退出前调用；正常析构也会 flush）
  static void flushAll();

private:
  struct Entry;

  /// 取（或首次创建）某文件的 Entry；**只在此处持有全局锁**
  static Entry *getEntry(const std::string &_filename,
                         const std::string &_header);

  static std::mutex mtx_;  ///< 只保护 logDir_ 与 files_ 的结构
  static std::string logDir_;
  static std::unordered_map<std::string, std::unique_ptr<Entry>> files_;
};

} // namespace keship

#endif
