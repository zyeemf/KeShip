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
#include <QDialog>
#include <QEventLoop>
#include <QImage>
#include <QTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPlainTextEdit>
#include <QTextBrowser>
#include <QTextDocument>
#include <QUrl>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "OffsetsHullDialog.hh"
#include "ShipHullGenerator.hh"
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


/// 按族索引构造规格（与对话框 familyVolume() 同口径）
keship::ShipHullSpec SpecOf(int _i, double _L, double _B, double _D, double _rho)
{
  switch (_i) {
    case 1: return keship::ShipHullSpec::WigleyFlat(_L, _B, _D, _rho);
    case 2: return keship::ShipHullSpec::WigleySharp(_L, _B, _D, _rho);
    case 3: return keship::ShipHullSpec::WigleyFine(_L, _B, _D, _rho);
    case 4: return keship::ShipHullSpec::LewisU(_L, _B, _D, _rho);
    case 5: return keship::ShipHullSpec::LewisV(_L, _B, _D, _rho);
    default: return keship::ShipHullSpec::WigleyClassic(_L, _B, _D, _rho);
  }
}

/// 把平台算出的型值表转成 CSV（模拟学生程序的 stdout）
std::string LinesToCsv(const keship::LinesData &_d)
{
  std::string s;
  char buf[128];
  for (std::size_t i = 0; i < _d.stations.size(); ++i) {
    for (std::size_t j = 0; j < _d.waterlines.size(); ++j) {
      std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f\n",
                    _d.stations[i], _d.waterlines[j], _d.halfBreadths[i][j]);
      s += buf;
    }
  }
  return s;
}


/// 资料（Journée 1992）归一化坐标 CSV：x∈[-1,1]、z∈[0,1] 向下、y=Y/(B/2)
/// alpha=0 -> Model III/IV（Cm=0.667）；alpha=1 -> Model I/II（Cm=0.909）
std::string MakeDocCsv(int _nx, int _nz, double _alpha)
{
  std::string s;
  char buf[128];
  for (int i = 0; i <= _nx; ++i) {
    const double x = -1.0 + 2.0 * i / _nx;
    for (int j = 0; j <= _nz; ++j) {
      const double z = 1.0 * j / _nz;
      const double y = (1.0 - z * z) * (1.0 - x * x) * (1.0 + 0.2 * x * x) +
                       _alpha * z * z * (1.0 - std::pow(z, 8.0)) * std::pow(1.0 - x * x, 4.0);
      std::snprintf(buf, sizeof(buf), "%.6f,%.6f,%.6f\n", x, z, y);
      s += buf;
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

  // ⑥ 六族全通过：每个族用自己的解析体积做 V6 基准（问题 1 的回归）
  {
    static const char *kNames[] = {"Wigley 经典", "Wigley 平底", "Wigley 尖底",
                                   "Wigley 瘦削", "Lewis U", "Lewis V"};
    for (int fam = 0; fam < 6; ++fam) {
      dlg.setFamilyIndex(fam);
      const auto spec = SpecOf(fam, L, B, D, 500.0);
      keship::ShipHullGenerator g(spec);
      const std::string csv = LinesToCsv(g.lines());
      std::string err;
      const bool ok = dlg.applyCsvText(QString::fromStdString(csv), &err);
      check(ok, std::string("族 ") + kNames[fam] + " 通过校验" +
                    (err.empty() ? std::string() : "：" + err));
      if (ok && dlg.hull()) {
        const double dV = dlg.hull()->hydroProps().volume;
        const double dErr = 100.0 * (dlg.report().volumeFromTable - dV) / dV;
        check(std::abs(dErr) < 2.0,
              std::string("族 ") + kNames[fam] + " 体积偏差 " +
                  std::to_string(dErr) + "% < 2%");
      }
    }
    dlg.setFamilyIndex(0);   // 复位
  }

  // ⑦ 资料坐标自动转换（张老师要求"坐标系统一"）
  {
    dlg.setFamilyIndex(8);       // 自定义（不对拍体积）
    dlg.setDocCoords(true);      // 输入按资料归一化坐标
    std::string err;
    const bool ok = dlg.applyCsvText(QString::fromStdString(MakeDocCsv(20, 10, 0.0)), &err);
    check(ok, "资料坐标（Model III）转换后通过 V1~V5" +
                  (err.empty() ? std::string() : "：" + err));
    // 资料 Model III 体积系数 0.46222*L*B*D，对 6/1.5/1 = 4.1599
    const double v = dlg.report().volumeFromTable;
    check(std::abs(v - 4.1599) / 4.1599 < 0.01,
          "转换后体积 " + std::to_string(v) + " ≈ 4.1599（资料 Model III，容差 1%）");
    check(dlg.report().exactVolume < 0.0, "自定义模式：V6 无参考值（不判定）");
    check(dlg.hull() != nullptr, "资料坐标也能建出船体");
    if (dlg.hull()) {
      const double draft = dlg.hull()->hydroProps().draft;
      check(draft > 0.3 && draft < 1.0,
            "设计吃水 " + std::to_string(draft) + " 落在 (0.3, 1.0) 内");
    }
    dlg.setDocCoords(false);
    dlg.setFamilyIndex(0);
  }

  // ⑧ 资料公式族（Journée 1992）：Model I/II 与 III/IV 各自对拍通过
  {
    struct Case { int idx; bool cm909; const char *name; };
    const Case cases[] = {{6, true, "Model I/II"}, {7, false, "Model III/IV"}};
    for (const auto &c : cases) {
      dlg.setDocCoords(false);
      dlg.setFamilyIndex(c.idx);
      const auto spec = keship::ShipHullSpec::WigleyDoc(L, B, D, c.cm909, 500.0);
      keship::ShipHullGenerator g(spec);
      const std::string csv = LinesToCsv(g.lines());
      std::string err;
      const bool ok = dlg.applyCsvText(QString::fromStdString(csv), &err);
      check(ok, std::string("资料族 ") + c.name + " 通过校验" +
                    (err.empty() ? std::string() : "：" + err));
      if (ok) {
        check(std::abs(dlg.report().volumeErrPct) < 1.0,
              std::string("资料族 ") + c.name + " 体积偏差 " +
                  std::to_string(dlg.report().volumeErrPct) + "% < 1%");
        // 与资料表 1 的口径交叉验证：L3/B0.3/d0.1875 时 Model I=0.0946, III=0.0780
        const auto s2 = keship::ShipHullSpec::WigleyDoc(3.0, 0.3, 0.1875, c.cm909, 500.0);
        const double v2 = keship::ShipHullGenerator(s2).hydroProps().volume;
        const double ref = c.cm909 ? 0.0946 : 0.0780;
        check(std::abs(v2 - ref) / ref < 0.01,
              std::string("资料族 ") + c.name + " 复现资料表 1 体积 " +
                  std::to_string(v2) + " ≈ " + std::to_string(ref));
      }
    }
    dlg.setFamilyIndex(0);
  }

  // ⑨ 船型说明：内容完整性（纯文本层，不涉及渲染）
  {
    const QString h = dlg.familyHelpHtml();
    check(h.contains("数学船型族说明"), "说明 HTML 有标题");
    check(h.contains("Journée, J.M.J. (1992)"), "说明 HTML 引用原始文献");
    check(h.contains("<svg"), "说明 HTML 含 SVG 剖面图");
    check(h.count("<path") >= 8, "SVG 至少 8 条剖面曲线（6 族 + 资料 2 族）");
    // 公式排版：5 个核心公式以 SVG 内联（matplotlib mathtext 离线渲染，字形转 path），
    // 不再用 Unicode 上下标拼纯文本。断言「是矢量图，不是文本」。
    check(h.count("<svg") >= 6, "含 5 个公式 SVG + 1 个剖面图 SVG");
    check(!h.contains("ζ¹⁰") && !h.contains("ξ²") && !h.contains("ζ²"),
          "正文不再残留 Unicode 上下标拼的纯文本公式");
    check(h.contains("z_平台 = −z_资料 · D"), "说明 HTML 含坐标换算公式");
    check(h.contains("Gazebo Z_UP"), "说明 HTML 说明平台 z 向上");
    check(h.contains("资料 Model I/II"), "说明 HTML 含资料族");
  }

  // ⑩ 船型说明：**渲染路径**必须在无外部 launcher 的环境下可用
  //   （2026-11-03 缺陷：原实现写 /tmp/*.html 后交 QDesktopServices 打开系统浏览器；
  //     教学机 / WSL 无 xdg-open 也无浏览器 ⇒ Qt 只打一行 warning 就返回，
  //     学生点「船型说明」毫无反应。现改为 gz GUI 内 QTextBrowser 自渲染。）
  //   断言：能弹出内部窗口、HTML 已被解析、SVG 已转位图内嵌、正文无裸 SVG 标签。
  {
    check(dlg.findChild<QTextBrowser *>() == nullptr, "调用前对话框内无说明窗口");
    QMetaObject::invokeMethod(&dlg, "onShowFamilyHelp", Qt::DirectConnection);
    QApplication::processEvents();

    auto *view = dlg.findChild<QTextBrowser *>();
    check(view != nullptr, "点「船型说明」在 GUI 内部弹出窗口（不再依赖系统浏览器）");
    if (view) {
      const QString txt = view->toPlainText();
      check(txt.contains("数学船型族说明"), "渲染后正文含标题（HTML 已被解析）");
      check(txt.contains("Journée"), "渲染后正文含文献引用");
      check(!txt.contains("<svg"), "正文无裸 SVG 标签残留（已被替换为图片）");
      // 6 个内嵌图：1 个剖面曲线图 + 5 个公式，均应已转位图
      int figs = 0;
      for (int i = 1; i <= 6; ++i) {
        const QVariant r = view->document()->resource(
            QTextDocument::ImageResource,
            QUrl(QStringLiteral("keship://fig%1").arg(i)));
        if (r.isValid() && !r.value<QImage>().isNull()) ++figs;
      }
      check(figs >= 6, "6 个内嵌图（1 剖面 + 5 公式）均已转位图并注册");
      check(!view->document()->resource(QTextDocument::ImageResource,
                                        QUrl(QStringLiteral("keship://fig7"))).isValid(),
            "资源编号不超过实际图数（无多余注册）");
      check(view->isVisible() || view->parentWidget() != nullptr, "说明窗口已构建");
      // 落盘渲染结果供人工验收（与旧版 html 落盘同用途；失败不影响判定）
      {
        QTextDocument *d = view->document();
        d->setTextWidth(880);
        const int shotH = static_cast<int>(d->size().height()) + 8;
        QImage shot(880, shotH, QImage::Format_ARGB32);
        shot.fill(Qt::white);
        QPainter p(&shot);
        d->drawContents(&p);
        p.end();
        shot.save(QStringLiteral("/tmp/keship_sd10_help_preview.png"));
      }
      // 关掉，避免影响后续断言与退出
      if (auto *w = qobject_cast<QWidget *>(view->window())) w->close();
      QApplication::processEvents();
    }
  }

  std::printf("--- SD-10 对话框结果：%s（失败 %d 项）---\n",
              g_fail == 0 ? "PASS" : "FAIL", g_fail);
  return g_fail == 0 ? 0 : 1;
}
