/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * 离屏 GUI 测试 v2：MS-06（MS-03 同款测量 + 学生算法 + 隐藏验收）
 */
// 离屏 GUI 测试 v2：MS-06（MS-03 同款测量 + 学生算法 + 隐藏验收）
// 断言重点：① 运行前不出现内置 RAO；② 运行后本轮内置值出现；③ 曲线可绘制；
//           ④ 提交验收后 5 组全部公开且通过（正确算法）
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
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
  WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");

  auto *edit = dlg.findChild<QPlainTextEdit *>("algoCodeEdit");
  auto *runBtn = dlg.findChild<QPushButton *>("algoRunBtn");
  auto *roundLabel = dlg.findChild<QLabel *>("roundResultLabel");
  auto *submitBtn = dlg.findChild<QPushButton *>("submitValidationBtn");
  auto *valStatus = dlg.findChild<QLabel *>("validationStatusLabel");
  auto *valTable = dlg.findChild<QTableWidget *>("validationTable");
  auto *curve = dlg.findChild<QWidget *>("raoCurve");

  check(edit && runBtn && roundLabel && submitBtn && valStatus && valTable && curve,
        "v2 控件齐全（算法面板/本轮结果/验收/曲线）");
  if (g_fails) return 1;

  // 用生成器造一轮“现场数据”（等价于记录一轮）
  const auto cases = keship::GenerateHiddenCases(1, 777u);
  dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);

  const QString tpl = edit->toPlainText();
  check(tpl.contains("load_data(data_path)") && tpl.contains("data_path"),
        "模板含本轮时程数组");
  check(!roundLabel->text().contains("内置 RAO"),
        "★ 运行前不出现内置 RAO（防作弊）", roundLabel->text());

  // 注入正确算法并运行
  QString code = tpl;
  code.replace("    return 0.0\n",
               "    mid = (t_s[0] + t_s[-1]) / 2.0\n"
               "    seg = [r for t, r in zip(t_s, roll_deg) if t >= mid]\n"
               "    return (max(seg) - min(seg)) / 2.0 / wave_amp_m\n");
  edit->setPlainText(code);
  runBtn->click();
  waitUntil([&]() { return roundLabel->text().contains("内置 RAO"); }, 30000);
  const QString after = roundLabel->text();
  // 注：「不通过」包含子串「通过」，只写 contains("通过")会恒为真。
  check(after.contains("内置 RAO") && after.contains("通过") && !after.contains("不通过"),
        "运行后公开内置值并判定通过", after);

  QPixmap pm = curve->grab();
  check(!pm.isNull() && pm.width() > 10, "RAO–T 曲线可绘制");

  // 提交验收（5 组隐藏数据）
  submitBtn->click();
  const bool done = waitUntil([&]() { return valStatus->text().contains("验收完成"); }, 120000);
  check(done, "验收完成", valStatus->text());
  check(valTable->rowCount() == 5, "验收表公开 5 组", QString::number(valTable->rowCount()));
  check(valStatus->text().contains("PASS"), "正确算法 5/5 通过", valStatus->text());
  if (valTable->rowCount() > 0 && valTable->item(0, 3)) {
    check(!valTable->item(0, 3)->text().isEmpty(), "验收表含标准 RAO（结果公开）",
          valTable->item(0, 3)->text());
  }

  std::cout << "OFFSCREEN_V2 " << (g_fails ? "FAIL" : "PASS")
            << " (fails=" << g_fails << ")" << std::endl;
  return g_fails ? 1 : 0;
}
