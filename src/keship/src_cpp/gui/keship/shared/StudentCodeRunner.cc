/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 学生代码执行器实现：沙箱 QProcess + JSON 解析 + 判定 */

#include "StudentCodeRunner.hh"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include <cmath>
#include <memory>

namespace keship {

QString StudentCodeRunner::RunnerPath() {
  try {
    const std::string shareDir = ament_index_cpp::get_package_share_directory("keship");
    return QString::fromStdString(shareDir) + "/scripts/keship_code_runner.py";
  } catch (...) {
    return QCoreApplication::applicationDirPath()
        + "/../../src/keship/scripts/keship_code_runner.py";
  }
}

double StudentCodeRunner::ErrorPct(double _student, double _ref) {
  if (std::abs(_ref) <= 1e-9) return -1.0;
  return std::abs(_student - _ref) / std::abs(_ref) * 100.0;
}

int StudentCodeRunner::Verdict(double _student, double _ref) {
  const double err = ErrorPct(_student, _ref);
  if (err < 0.0) return 2;
  if (err < 5.0) return 0;
  if (err < 10.0) return 1;
  return 2;
}

StudentRunResult StudentCodeRunner::Parse(const QByteArray &_stdout,
                                          const QByteArray &_stderr) {
  StudentRunResult r;
  const QJsonDocument doc = QJsonDocument::fromJson(_stdout);
  if (!doc.isObject()) {
    r.ok = false;
    r.stderrText = QString::fromUtf8(_stderr);
    if (r.stderrText.isEmpty()) r.stderrText = QString::fromUtf8(_stdout);
    return r;
  }
  const QJsonObject obj = doc.object();
  r.stdoutText = obj.value("stdout").toString();
  r.stderrText = obj.value("stderr").toString();
  if (!obj.value("ok").toBool()) {
    r.ok = false;
    return r;
  }
  // 学生程序 stdout 约定输出一个数值；先按整串解析，失败再扫第一个数字 token
  bool ok = false;
  const double v = r.stdoutText.trimmed().toDouble(&ok);
  if (ok) {
    r.value = v;
    r.valueOk = true;
    r.ok = true;
    return r;
  }
  static const QRegularExpression re(
      QStringLiteral("[-+]?\\d+(?:\\.\\d+)?(?:[eE][-+]?\\d+)?"));
  const auto m = re.match(r.stdoutText);
  if (m.hasMatch()) {
    r.value = m.captured(0).toDouble(&ok);
    r.valueOk = ok;
    r.ok = ok;
  }
  return r;
}

void StudentCodeRunner::RunAsync(QObject *_parent, const QString &_lang,
                                 const QString &_code,
                                 std::function<void(const StudentRunResult &)> _done) {
  auto *proc = new QProcess(_parent);
  auto fired = std::make_shared<bool>(false);
  auto finish = [proc, _done, fired](const QByteArray &_out, const QByteArray &_err) {
    if (*fired) return;
    *fired = true;
    const StudentRunResult r = Parse(_out, _err);
    proc->deleteLater();
    _done(r);
  };
  QObject::connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                   proc, [proc, finish](int, QProcess::ExitStatus) {
                     finish(proc->readAllStandardOutput(),
                            proc->readAllStandardError());
                   });
  QObject::connect(proc, &QProcess::errorOccurred, proc,
                   [proc, finish](QProcess::ProcessError) {
                     QByteArray err = proc->readAllStandardError();
                     if (err.isEmpty())
                       err = QByteArrayLiteral("无法启动代码运行器");
                     finish(proc->readAllStandardOutput(), err);
                   });
  proc->start("python3", {RunnerPath(), _lang, _code});
}

}  // namespace keship
