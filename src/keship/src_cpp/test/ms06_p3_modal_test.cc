/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * MS-06 P3 离屏测试：两个模态（提示 / 导出验收数据）+ 验收导出纯函数
 */
// MS-06 P3 离屏测试：两个模态（提示 / 导出验收数据）+ 验收导出纯函数
//
// 背景：这两个模态此前是 MS-06 唯一未被自动化覆盖的交互路径 ——
//   QMessageBox::information 是**模态阻塞**调用，常规 click() 会卡在 exec() 里。
// 做法：在 click() 之前装一个「模态捕获器」（QTimer 轮询 activeModalWidget），
//   抓到 QMessageBox 后记录标题/正文并 accept()，从而既能断言内容又能返回。
//
// 判据全部落在「用户看到什么 + 文件写了什么」上，不依赖内部状态。
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QThread>
#include <QTimer>
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

// ── 模态捕获器 ──────────────────────────────────────────────
static QString g_modalTitle;
static QString g_modalText;
static bool g_modalSeen = false;

static void resetModal() {
  g_modalTitle.clear();
  g_modalText.clear();
  g_modalSeen = false;
}

/// 在会弹模态的 click() 之前调用；模态关闭后 click() 才返回
static void armModalCapture(int _timeoutMs = 5000) {
  auto *t = new QTimer();
  t->setInterval(15);
  QObject::connect(t, &QTimer::timeout, [t]() {
    QWidget *w = QApplication::activeModalWidget();
    if (!w) {
      for (QWidget *tw : QApplication::topLevelWidgets())
        if (auto *b = qobject_cast<QMessageBox *>(tw)) { w = b; break; }
    }
    auto *box = qobject_cast<QMessageBox *>(w);
    if (!box) return;
    g_modalTitle = box->windowTitle();
    g_modalText = box->text();
    g_modalSeen = true;
    t->stop();
    t->deleteLater();
    box->accept();
  });
  t->start();
  QTimer::singleShot(_timeoutMs, t, [t]() {  // 兜底：不弹也不永久挂住定时器
    if (t->isActive()) { t->stop(); t->deleteLater(); }
  });
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  gz::transport::Node node;

  // ── A. 「提示」模态：标题 + 三步要点 ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *hint = dlg.findChild<QPushButton *>("algoHintBtn");
    check(hint != nullptr, "A1 学生算法面板有「提示」按钮");
    if (hint) {
      resetModal();
      armModalCapture();
      hint->click();  // 模态关闭后返回
      check(g_modalSeen, "A2 点「提示」弹出模态对话框");
      check(g_modalTitle == QString::fromUtf8("提示"), "A3 模态标题 = 提示", g_modalTitle);
      const bool hasSteps = g_modalText.contains(QString::fromUtf8("稳态段")) &&
                            g_modalText.contains(QString::fromUtf8("求幅值")) &&
                            g_modalText.contains(QString::fromUtf8("算 RAO"));
      check(hasSteps, "A4 正文含三步要点（稳态段 / 求幅值 / 算 RAO）", g_modalText);
      check(g_modalText.contains(QString::fromUtf8("不要写死")),
            "A5 正文提示代码不得写死数值（防作弊说明）");
    }
  }

  // ── B. 「导出验收数据」无数据分支：应提示先提交验收，且不弹目录选择 ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    auto *exp = dlg.findChild<QPushButton *>("exportValidationBtn");
    check(exp != nullptr, "B1 有「导出验收数据」按钮");
    if (exp) {
      resetModal();
      armModalCapture();
      exp->click();
      check(g_modalSeen, "B2 无数据时点击仍给出模态提示");
      check(g_modalTitle == QString::fromUtf8("导出验收数据"),
            "B3 模态标题 = 导出验收数据", g_modalTitle);
      check(g_modalText.contains(QString::fromUtf8("尚无验收数据")),
            "B4 正文提示「尚无验收数据」", g_modalText);
    }
  }

  // ── C. WriteValidationCases 纯函数：3 组全部写出，内容与行数正确 ──
  const QString dir = "/tmp/ms06_p3_export_test";
  {
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    const auto cases = keship::GenerateHiddenCases(3, 4242u);
    const int n = WaveResponseDialog::WriteValidationCases(dir.toStdString(), cases);
    check(n == 3, "C1 写出文件数 = 3",
          QString("实际 %1").arg(n));
    for (int i = 0; i < 3 && i < static_cast<int>(cases.size()); ++i) {
      const QString p = QString("%1/hidden_case_%2.csv").arg(dir).arg(i + 1);
      QFile f(p);
      const bool opened = f.open(QIODevice::ReadOnly);
      check(opened, QString("C2.%1 文件存在可读 hidden_case_%2.csv").arg(i + 1).arg(i + 1).toStdString());
      if (!opened) continue;
      const QStringList lines =
          QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
      check(!lines.isEmpty() && lines.first() == "t_s,roll_deg",
            QString("C3.%1 首行为表头 t_s,roll_deg").arg(i + 1).toStdString(),
            lines.isEmpty() ? QString("<空文件>") : lines.first());
      const int expectRows = static_cast<int>(cases[i].trail.size());
      check(lines.size() - 1 == expectRows,
            QString("C4.%1 数据行数 = 时程点数（%2）").arg(i + 1).arg(expectRows).toStdString(),
            QString("实际 %1 行").arg(lines.size() - 1));
    }
  }

  // ── D. 空输入 → 0（不抛异常、不建文件）──
  {
    const auto empty = std::vector<keship::HiddenWaveCase>{};
    check(WaveResponseDialog::WriteValidationCases(dir.toStdString(), empty) == 0,
          "D1 空用例返回 0");
  }

  // ── E. 不可写目录 → 0（失败跳过，不抛异常）──
  {
    const auto cases = keship::GenerateHiddenCases(2, 7u);
    check(WaveResponseDialog::WriteValidationCases("/nonexistent_keship_dir_p3", cases) == 0,
          "E1 目标目录不存在时返回 0（失败不抛异常）");
  }

  // ── F. 界面文案一致性：不得出现 v2 已删除的按钮名 ──
  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{}, "ms06");
    QStringList texts;
    for (QLabel *l : dlg.findChildren<QLabel *>()) texts << l->text();
    for (QPushButton *b : dlg.findChildren<QPushButton *>()) texts << b->text();
    const QString joined = texts.join("\n");
    const bool clean = !joined.contains(QString::fromUtf8("导出练习数据包")) &&
                       !joined.contains(QString::fromUtf8("提交学生结果")) &&
                       !joined.contains(QString::fromUtf8("student_rao"));
    check(clean,
          "F1 界面文案不含 v2 已删除的按钮名（导出练习数据包/提交学生结果/student_rao）",
          clean ? QString() : joined.left(400));
  }

  QDir(dir).removeRecursively();

  std::cout << "OFFSCREEN_MS06_P3 " << (g_fails ? "FAIL" : "PASS")
            << " (fails=" << g_fails << ")" << std::endl;
  return g_fails ? 1 : 0;
}
