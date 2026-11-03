/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 离屏 GUI 测试 v2 补充：错算法验收应 FAIL / C 档正确解 / MS-03 模式回归
 */
// 离屏 GUI 测试 v2 补充：错算法验收应 FAIL / C 档正确解 / MS-03 模式回归
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QThread>
#include <iostream>

#include "experiments/WaveResponseDialog.hh"
#include "HiddenCaseGenerator.hh"

static int g_fails = 0;
static void check(bool _ok, const std::string &_name, const QString &_detail = QString()) {
  std::cout << (_ok ? "  [PASS] " : "  [FAIL] ") << _name;
  if (!_detail.isEmpty()) std::cout << "  (" << _detail.toStdString() << ")";
  std::cout << std::endl;
  if (!_ok) ++g_fails;
}
static bool waitUntil(const std::function<bool()> &_pred, int _timeoutMs) {
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < _timeoutMs) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    if (_pred()) return true;
    QThread::msleep(20);
  }
  return _pred();
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  gz::transport::Node node;

  // ── A. 错算法：验收必须 FAIL ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *edit = dlg.findChild<QPlainTextEdit *>("algoCodeEdit");
    auto *runBtn = dlg.findChild<QPushButton *>("algoRunBtn");
    auto *roundLabel = dlg.findChild<QLabel *>("roundResultLabel");
    auto *submitBtn = dlg.findChild<QPushButton *>("submitValidationBtn");
    auto *valStatus = dlg.findChild<QLabel *>("validationStatusLabel");
    const auto cases = keship::GenerateHiddenCases(1, 555u);
    dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);
    dlg.findChild<QRadioButton *>("algoPythonRadio")->setChecked(true);  ///< 本用例用 Python
    edit->setPlainText(
        "print(f\"{0.0:.4f}\")\n");  // 硬编码 0：单轮不通过，验收应全挂
    runBtn->click();
    waitUntil([&]() { return roundLabel->text().contains("不通过"); }, 20000);
    check(roundLabel->text().contains("不通过"), "错算法单轮判不通过", roundLabel->text());
    submitBtn->click();
    waitUntil([&]() { return valStatus->text().contains("验收完成"); }, 120000);
    check(valStatus->text().contains("FAIL"), "错算法验收 FAIL（防作弊成立）",
          valStatus->text());
  }

  // ── B. C 档正确解：运行应通过 ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *edit = dlg.findChild<QPlainTextEdit *>("algoCodeEdit");
    auto *runBtn = dlg.findChild<QPushButton *>("algoRunBtn");
    auto *roundLabel = dlg.findChild<QLabel *>("roundResultLabel");
    auto *cRadio = dlg.findChild<QRadioButton *>("algoCRadio");
    const auto cases = keship::GenerateHiddenCases(1, 888u);
    dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);
    cRadio->setChecked(true);  // 切 C（模板重建）
    QString code = edit->toPlainText();
    code.replace("  double rao = 0.0;",
                 "  double mid = 0.5 * (t_s[0] + t_s[n-1]);\n"
                 "  double mn = 1e9, mx = -1e9;\n"
                 "  for (int i = 0; i < n; ++i) {\n"
                 "    if (t_s[i] < mid) continue;\n"
                 "    if (roll_deg[i] < mn) mn = roll_deg[i];\n"
                 "    if (roll_deg[i] > mx) mx = roll_deg[i];\n"
                 "  }\n"
                 "  double rao = ((mx - mn) / 2.0) / wave_amp_m;");
    edit->setPlainText(code);
    runBtn->click();
    waitUntil([&]() { return roundLabel->text().contains("内置 RAO"); }, 30000);
    // 注：必须排除「不通过」——「不通过」包含子串「通过」，
  // 只写 contains("通过")会恒为真（2026-10-24 复核实测：算法改坏仍 PASS）。
  const QString extraTxt = roundLabel->text();
  check(extraTxt.contains("通过") && !extraTxt.contains("不通过"),
        "C 档正确解 → 通过", extraTxt);
  }

  // ── B2. 默认 C 模板（未填 TODO）必须能编译并输出 0（2026-10-27 修） ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *runBtn = dlg.findChild<QPushButton *>("algoRunBtn");
    auto *roundLabel = dlg.findChild<QLabel *>("roundResultLabel");
    const auto cases = keship::GenerateHiddenCases(1, 4242u);
    dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);
    runBtn->click();
    waitUntil([&]() { return roundLabel->text().contains("内置 RAO"); }, 30000);
    check(roundLabel->text().contains("学生 RAO = 0.0000"),
          "默认 C 模板可编译并输出 0", roundLabel->text());
    check(roundLabel->text().contains("不通过"), "C 模板未填 → 不通过",
          roundLabel->text());
  }

  // ── B3. 学生代码带未用变量：不应再因 -Werror 编译失败（2026-10-27 定） ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *edit = dlg.findChild<QPlainTextEdit *>("algoCodeEdit");
    auto *runBtn = dlg.findChild<QPushButton *>("algoRunBtn");
    auto *roundLabel = dlg.findChild<QLabel *>("roundResultLabel");
    const auto cases = keship::GenerateHiddenCases(1, 909u);
    dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);
    QString code = edit->toPlainText();
    code.replace("  double rao = 0.0;",
                 "  int unused_var = 42;  /* 故意制造 -Wunused-variable */\n"
                 "  double rao = 0.0;");
    edit->setPlainText(code);
    runBtn->click();
    waitUntil([&]() { return roundLabel->text().contains("内置 RAO"); }, 30000);
    check(roundLabel->text().contains("学生 RAO = 0.0000"),
          "带未用变量仍能编译运行（-Werror 已去掉）", roundLabel->text());
  }

  // ── B4. 切语言 / 再记录一轮不得清空学生代码（2026-10-27 反馈） ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *edit = dlg.findChild<QPlainTextEdit *>("algoCodeEdit");
    auto *cRadio = dlg.findChild<QRadioButton *>("algoCRadio");
    auto *pyRadio = dlg.findChild<QRadioButton *>("algoPythonRadio");
    const auto cases = keship::GenerateHiddenCases(1, 111u);
    dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);
    edit->setPlainText("// MY_C_CODE\nint main(){return 0;}\n");
    pyRadio->setChecked(true);
    cRadio->setChecked(true);
    check(edit->toPlainText().contains("MY_C_CODE"),
          "切 Python 再切回 C，代码保留", edit->toPlainText().left(40));
    edit->setPlainText("// MY_C_CODE_2\nint main(){return 0;}\n");
    dlg.AddRoundForTest(cases[0].trail, cases[0].period + 1.0, cases[0].waveAmp);
    check(edit->toPlainText().contains("MY_C_CODE_2"),
          "再记录一轮不清空代码", edit->toPlainText().left(40));
  }

  // ── C. MS-03 回归：无算法面板、RAO 表在、记录按钮在 ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms03");
    auto *algo = dlg.findChild<QWidget *>("algoCodeEdit");
    auto *curve = dlg.findChild<QWidget *>("raoCurve");
    check(algo == nullptr && curve == nullptr, "MS-03 无 MS-06 面板/曲线（模式隔离）");
    check(dlg.windowTitle().contains("MS-03"), "MS-03 标题正确", dlg.windowTitle());
  }

  std::cout << "OFFSCREEN_V2_EXTRA " << (g_fails ? "FAIL" : "PASS")
            << " (fails=" << g_fails << ")" << std::endl;
  return g_fails ? 1 : 0;
}
