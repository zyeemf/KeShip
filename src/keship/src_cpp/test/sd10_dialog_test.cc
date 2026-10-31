/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* SD-10 型值表对话框：离屏回归
 *
 * 目的：把四要素里的"对话框可定位 + 无交互入口可用"变成机器证据。
 *   ① 构造成功、模板含输出契约注释
 *   ② applyCsvText 正例：21x11 Wigley → 校验通过 → 建船 → 静水力 draft≈0.6527
 *   ③ 负例：缺格 / 半宽超 B/2 必须被拒，且错误文案指向正确原因
 *   ④ exportTo：写出的 JSON expId=SD-10、含型值表、往返可读
 * 运行：ctest -R sd10_dialog --output-on-failure（CMake 已设 QT_QPA_PLATFORM=offscreen）
 */

#include <QApplication>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "OffsetsHullDialog.hh"
#include "StudentCodeRunner.hh"

namespace {

int g_fail = 0;

void check(bool _ok, const std::string &_what)
{
  std::printf("%s  %s\n", _ok ? "[PASS]" : "[FAIL]", _what.c_str());
  if (!_ok) ++g_fail;
}

constexpr double L = 6.0, B = 1.5, D = 1.0;

double Hb(double _x, double _z)
{
  const double xi = 2.0 * _x / L - 1.0;
  const double zt = -_z / D;
  return 0.5 * B * (1.0 - xi * xi) * (1.0 - zt * zt);
}

/// 生成完整型值表 CSV 文本
std::string MakeCsv(int _nx, int _nz, double _scaleY = 1.0, bool _dropOne = false)
{
  std::string s;
  int n = 0;
  for (int i = 0; i <= _nx; ++i) {
    const double x = L * i / _nx;
    for (int j = 0; j <= _nz; ++j) {
      const double z = -D * j / _nz;
      if (_dropOne && n == 55) { ++n; continue; }
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f\n", x, z, _scaleY * Hb(x, z));
      s += buf;
      ++n;
    }
  }
  return s;
}

}  // namespace

int main(int argc, char **argv)
{
  QApplication app(argc, argv);

  OffsetsHullDialog dlg;

  // ① 构造与模板
  check(true, "对话框可构造（offscreen）");
  {
    auto *code = dlg.findChild<QPlainTextEdit *>();
    check(code != nullptr, "找到代码编辑区");
    if (code) {
      const QString t = code->toPlainText();
      check(t.contains("x,z,halfBreadth"), "模板含输出契约（x,z,halfBreadth）");
      check(t.contains("右舷"), "模板说明只打右舷");
      check(t.contains("Nx") && t.contains("Nz"), "模板给出网格常量");
    }
  }

  // ② 正例
  {
    std::string err;
    const bool ok = dlg.applyCsvText(QString::fromStdString(MakeCsv(20, 10)), &err);
    check(ok, "正例 21x11 校验通过" + (err.empty() ? std::string() : "：" + err));
    check(dlg.report().ok, "report.ok = true");
    check(dlg.report().nStations == 21 && dlg.report().nWaterlines == 11,
          "网格 21 站 x 11 水线");
    check(std::abs(dlg.report().volumeErrPct) < 2.0,
          "体积偏差 " + std::to_string(dlg.report().volumeErrPct) + "% < 2%");
    check(dlg.hull() != nullptr, "已建船体（hull 非空）");
    if (dlg.hull()) {
      const auto &hp = dlg.hull()->hydroProps();
      check(std::abs(hp.draft - 0.652674) < 0.01,
            "设计吃水 " + std::to_string(hp.draft) + " ≈ 0.6527");
      check(std::abs(hp.volumeD - 1.998438) < 0.02,
            "排水体积 " + std::to_string(hp.volumeD) + " ≈ 1.9984");
    }
  }

  // ③ 负例：缺格
  {
    std::string err;
    const bool ok = dlg.applyCsvText(QString::fromStdString(MakeCsv(20, 10, 1.0, true)), &err);
    check(!ok, "负例：缺一个点必须被拒");
    check(err.find("缺格") != std::string::npos,
          "错误文案指向缺格（" + err + "）");
    check(dlg.hull() == nullptr, "被拒后不保留旧船体");
  }

  // ③ 负例：半宽超 B/2（漏了 /2）
  {
    std::string err;
    const bool ok = dlg.applyCsvText(QString::fromStdString(MakeCsv(20, 10, 2.0)), &err);
    check(!ok, "负例：整表乘 2 必须被拒");
    check(err.find("B/2") != std::string::npos,
          "错误文案提示 B/2（" + err + "）");
  }

  // ③ 负例：网格过疏
  {
    std::string err;
    const bool ok = dlg.applyCsvText(QString::fromStdString(MakeCsv(10, 5)), &err);
    check(!ok, "负例：11x6 网格必须因密度不足被拒");
  }

  // ④ 导出（先恢复正例状态）
  {
    std::string err;
    check(dlg.applyCsvText(QString::fromStdString(MakeCsv(20, 10)), &err),
          "导出前恢复正例状态");
    const QString path = QStringLiteral("/tmp/sd10_dialog_deliverable.json");
    check(dlg.exportTo(path, &err), "exportTo 成功" + (err.empty() ? std::string() : "：" + err));

    QFile f(path);
    check(f.open(QIODevice::ReadOnly), "导出文件可打开");
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    check(doc.isObject(), "导出内容是 JSON 对象");
    const QJsonObject o = doc.object();
    check(o.value("expId").toString() == "SD-10", "expId = SD-10");
    check(o.value("schema").toString() == "keship.exp.v1", "schema = keship.exp.v1");

    const QJsonObject m = o.value("metrics").toObject();
    check(std::abs(m.value("draftM").toDouble() - 0.652674) < 0.01,
          "交付物 draftM ≈ 0.6527");
    check(m.value("nStations").toInt() == 21 && m.value("nWaterlines").toInt() == 11,
          "交付物含网格尺寸");

    const QJsonObject ex = o.value("extra").toObject();
    check(ex.value("stations").toArray().size() == 21, "extra.stations 21 项");
    check(ex.value("waterlines").toArray().size() == 11, "extra.waterlines 11 项");
    check(ex.value("halfBreadths").toArray().size() == 21, "extra.halfBreadths 21 行");
  }

  // ⑤ 沙箱端到端：学生代码 -> 沙箱执行 -> 解析校验（学生真实使用路径）
  {
    const char *py =
        "L,B,D=6.0,1.5,1.0\n"
        "Nx,Nz=20,10\n"
        "for i in range(Nx+1):\n"
        "    x=L*i/Nx\n"
        "    for j in range(Nz+1):\n"
        "        z=-D*j/Nz\n"
        "        xi=2.0*x/L-1.0\n"
        "        zt=-z/D\n"
        "        y=0.5*B*(1.0-xi*xi)*(1.0-zt*zt)\n"
        "        print('%.6f,%.6f,%.6f' % (x,z,y))\n";

    QEventLoop loop;
    bool done = false;
    bool runOk = false;
    QString out;
    keship::StudentCodeRunner::RunAsync(&dlg, "python", QString::fromUtf8(py),
        [&](const keship::StudentRunResult &_r) {
          done = true;
          runOk = _r.ok;
          out = _r.stdoutText;
          loop.quit();
        });
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();

    check(done, "沙箱在 30 s 内返回（学生真实路径）");
    check(runOk, "沙箱执行学生 Python 代码成功");
    if (done && runOk) {
      check(out.count('\n') >= 200, "沙箱输出行数 >= 200（21x11 = 231 行）");
      std::string err;
      const bool ok = dlg.applyCsvText(out, &err);
      check(ok, "沙箱输出直接通过六项校验（全链路贯通）" +
                    (err.empty() ? std::string() : "：" + err));
      check(dlg.hull() != nullptr, "全链路后端到端建出船体");
      if (dlg.hull()) {
        const auto &hp = dlg.hull()->hydroProps();
        check(std::abs(hp.draft - 0.652674) < 0.01,
              "全链路吃水 " + std::to_string(hp.draft) + " ~= 0.6527");
      }
    }
  }

  std::printf("--- SD-10 对话框结果：%s（失败 %d 项）---\n",
              g_fail == 0 ? "PASS" : "FAIL", g_fail);
  return g_fail == 0 ? 0 : 1;
}
