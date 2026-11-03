/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 学生代码执行器（HS-09 与 MS-06 共用）：沙箱调用 + JSON 解析 + 判定 */

#ifndef KESHIP_STUDENT_CODE_RUNNER_HH_
#define KESHIP_STUDENT_CODE_RUNNER_HH_

#include <QObject>
#include <QString>

#include <functional>

namespace keship {

/// 学生代码执行结果
struct StudentRunResult {
  bool ok = false;        ///< 沙箱执行成功且输出可解析
  QString stderrText;     ///< 编译/运行错误（或沙箱错误）
  QString stdoutText;     ///< 学生程序 stdout（已由沙箱从 JSON 取出）
  double value = 0.0;     ///< 从 stdout 解析出的数值
  bool valueOk = false;   ///< 数值是否解析成功
};

/// 学生代码执行器：QProcess → scripts/keship_code_runner.py（沙箱，自带超时）
class StudentCodeRunner {
 public:
  /// 异步执行；_done 在主线程回调（QProcess 事件驱动，不阻塞界面）
  static void RunAsync(QObject *_parent, const QString &_lang, const QString &_code,
                       std::function<void(const StudentRunResult &)> _done);

  /// 解析沙箱 JSON 输出（ok/stdout/stderr）并提取数值
  static StudentRunResult Parse(const QByteArray &_stdout, const QByteArray &_stderr);

  /// 误差百分比（%）；参考值 ≤1e-9 时返回 -1
  static double ErrorPct(double _student, double _ref);

  /// 判定：0=通过(<5%)、1=接近(<10%)、2=不通过
  static int Verdict(double _student, double _ref);

  /// 沙箱脚本路径（ament share/keship/scripts/…；找不到时回退源码路径）
  static QString RunnerPath();
};

}  // namespace keship

#endif
