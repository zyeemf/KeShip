/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 42 项教学实验的统一离屏驱动（2026-10-28）
 *
 * 两个用途，同一个二进制：
 *   ① 冒烟：`--exp HS-08`            构造 → show → 退出码 0 即"能打开"
 *   ② 演练：`--exp RP-01 --spin 0 5 --click 计算 --result`
 *      设参数 → 点按钮 → 读表格/标签 → 输出可核对的原始数据
 *
 * 为什么一进程一实验：对话框构造期会读配置、建 gz 节点、算默认船型，任一项崩溃都会
 * 带走整个进程；拆成一进程后，崩溃只影响该项，驱动脚本能继续跑完 42 项并如实记退出码。
 *
 * 控件寻址用**索引**（第 n 个 QDoubleSpinBox / QComboBox），因为项目里绝大多数控件没有
 * objectName；索引顺序与 `--dump` 输出顺序一致，所以先 dump 再写演练参数即可复现。
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QTableWidget>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QWidget>

#include <gz/transport/Node.hh>

#include "editors/InertiaCalculatorDialog.hh"
#include "editors/ProxyGeometryDialog.hh"
#include "experiments/BuoyancyLayoutDialog.hh"
#include "experiments/CodeRunnerDialog.hh"
#include "experiments/CompartmentDialog.hh"
#include "experiments/ControlExpDialog.hh"
#include "experiments/DamageStabilityDialog.hh"
#include "experiments/DisplacementVerifyDialog.hh"
#include "experiments/GzStabilityDialog.hh"
#include "experiments/HullStabilityDialog.hh"
#include "experiments/IncliningExperimentDialog.hh"
#include "experiments/LargeAngleStabilityDialog.hh"
#include "experiments/LinesReadingDialog.hh"
#include "experiments/LongitudinalStrengthDialog.hh"
#include "experiments/MidshipSectionDialog.hh"
#include "experiments/OperabilityDialog.hh"
#include "experiments/ParametricHullDialog.hh"
#include "experiments/PropellerDialog.hh"
#include "experiments/ResistanceDialog.hh"
#include "experiments/SensorEstimationDialog.hh"
#include "experiments/StabilityExperimentDialog.hh"
#include "experiments/StructureCognitionDialog.hh"
#include "experiments/TurningCircleDialog.hh"
#include "experiments/WaveResponseDialog.hh"
#include "experiments/ZigzagDialog.hh"
#include "shared/ShipHullDialog.hh"

namespace {

const char *kWorld = "keship_world";

QDialog *Make(const std::string &_exp, gz::transport::Node &_node,
              const QStringList &_models)
{
  const QString exp = QString::fromStdString(_exp);
  const QStringList &models = _models;

  // ---- 船舶静力学 HS ----
  if (exp == "HS-01") return new ShipHullDialog(models, nullptr);
  if (exp == "HS-02") return new DisplacementVerifyDialog(_node, kWorld, nullptr);
  if (exp == "HS-03") return new StabilityExperimentDialog(_node, kWorld, nullptr);
  if (exp == "HS-04") return new IncliningExperimentDialog(_node, kWorld, nullptr);
  if (exp == "HS-05") return new HullStabilityDialog(_node, kWorld, nullptr);
  if (exp == "HS-06") return new LargeAngleStabilityDialog(nullptr);
  if (exp == "HS-07") return new BuoyancyLayoutDialog(nullptr);
  if (exp == "HS-08") return new GzStabilityDialog(nullptr);
  if (exp == "HS-09") return new CodeRunnerDialog(nullptr);

  // ---- 阻力与推进 RP（同一对话框按 expId 切模式）----
  if (exp.startsWith("RP-")) {
    if (exp == "RP-03" || exp == "RP-04")
      return new PropellerDialog(nullptr, exp);
    return new ResistanceDialog(nullptr, exp);
  }

  // ---- 操纵性与耐波性 MS ----
  if (exp == "MS-01") return new TurningCircleDialog(_node, kWorld, models, nullptr);
  if (exp == "MS-02")
    return new WaveResponseDialog(_node, kWorld, models, "ms02", nullptr);
  if (exp == "MS-03")
    return new WaveResponseDialog(_node, kWorld, models, "ms03", nullptr);
  if (exp == "MS-06")
    return new WaveResponseDialog(_node, kWorld, models, "ms06", nullptr);
  if (exp == "MS-04") return new ZigzagDialog(_node, kWorld, models, nullptr);
  if (exp == "MS-05")
    return new OperabilityDialog(_node, kWorld, models, nullptr);

  // ---- 智能控制 SC ----
  if (exp == "SC-08") return new SensorEstimationDialog(_node, kWorld, models, nullptr);
  if (exp.startsWith("SC-"))
    return new ControlExpDialog(_node, kWorld, models, exp, nullptr);

  // ---- 船体结构与制图 SD ----
  if (exp == "SD-01") return new StructureCognitionDialog(nullptr);
  if (exp == "SD-02") return new LinesReadingDialog(nullptr);
  if (exp == "SD-03") return new MidshipSectionDialog(nullptr);
  if (exp == "SD-04") return new CompartmentDialog(nullptr);
  if (exp == "SD-05") return new InertiaCalculatorDialog(nullptr);
  if (exp == "SD-06") return new ProxyGeometryDialog(nullptr);
  if (exp == "SD-07") return new DamageStabilityDialog(nullptr);
  if (exp == "SD-08") return new ParametricHullDialog(nullptr);
  if (exp == "SD-09") return new LongitudinalStrengthDialog(nullptr);

  return nullptr;
}

void Pump(QApplication &_app, int _ms)
{
  QElapsedTimer t;
  t.start();
  while (t.elapsed() < _ms) {
    _app.processEvents();
    QThread::msleep(10);
  }
}

void PrintUi(QDialog *_dlg)
{
  for (auto *b : _dlg->findChildren<QPushButton *>()) {
    std::printf("B\t%s\n", b->text().toUtf8().constData());
  }
  for (auto *c : _dlg->findChildren<QComboBox *>()) {
    QStringList it;
    for (int i = 0; i < c->count(); ++i) it << c->itemText(i);
    std::printf("C\tcur=%s\t%s\n", c->currentText().toUtf8().constData(),
                it.join("|").toUtf8().constData());
  }
  for (auto *s : _dlg->findChildren<QDoubleSpinBox *>()) {
    std::printf("S\tval=%.6g\tmin=%.6g\tmax=%.6g\tunit=%s\tname=%s\n", s->value(),
                s->minimum(), s->maximum(), s->suffix().toUtf8().constData(),
                s->objectName().toUtf8().constData());
  }
  for (auto *l : _dlg->findChildren<QLabel *>()) {
    const QString t = l->text().trimmed();
    if (!t.isEmpty())
      std::printf("L\t%s\n", t.left(400).replace("\n", " ").toUtf8().constData());
  }
  for (auto *t : _dlg->findChildren<QTableWidget *>()) {
    QString h;
    for (int c = 0; c < t->columnCount(); ++c)
      h += (t->horizontalHeaderItem(c) ? t->horizontalHeaderItem(c)->text() : "?") + "|";
    std::printf("T\thdr=%s\trows=%d\n", h.toUtf8().constData(), t->rowCount());
  }
  for (auto *e : _dlg->findChildren<QTextEdit *>()) {
    const QString t = e->toPlainText().trimmed();
    if (!t.isEmpty())
      std::printf("X\t%s\n", t.left(200).replace("\n", " ").toUtf8().constData());
  }
  std::fflush(stdout);
}

void PrintResults(QDialog *_dlg)
{
  for (auto *t : _dlg->findChildren<QTableWidget *>()) {
    for (int r = 0; r < t->rowCount(); ++r) {
      QString row;
      for (int c = 0; c < t->columnCount(); ++c)
        row += (t->item(r, c) ? t->item(r, c)->text() : "") + "|";
      std::printf("ROW\t%d\t%s\n", r, row.toUtf8().constData());
    }
  }
  for (auto *l : _dlg->findChildren<QLabel *>()) {
    const QString t = l->text().trimmed();
    if (!t.isEmpty())
      std::printf("LB\t%s\n", t.left(600).replace("\n", " ").toUtf8().constData());
  }
  for (auto *e : _dlg->findChildren<QTextEdit *>()) {
    const QString t = e->toPlainText().trimmed();
    if (!t.isEmpty())
      std::printf("X\t%s\n", t.left(400).replace("\n", " ").toUtf8().constData());
  }
  std::fflush(stdout);
}

}  // namespace

int main(int _argc, char **_argv)
{
  QApplication app(_argc, _argv);

  std::string exp;
  bool dump = false;
  bool result = false;
  int waitMs = 1000;
  std::vector<std::string> clicks;
  std::vector<std::pair<int, double>> spins;
  std::vector<std::pair<int, std::string>> combos;
  QString injectFile;

  for (int i = 1; i < _argc; ++i) {
    const char *a = _argv[i];
    if (std::strcmp(a, "--exp") == 0 && i + 1 < _argc) {
      exp = _argv[++i];
    } else if (std::strcmp(a, "--dump") == 0) {
      dump = true;
    } else if (std::strcmp(a, "--result") == 0) {
      result = true;
    } else if (std::strcmp(a, "--file") == 0 && i + 1 < _argc) {
      injectFile = QString::fromLocal8Bit(_argv[++i]);
    } else if (std::strcmp(a, "--click") == 0 && i + 1 < _argc) {
      clicks.push_back(_argv[++i]);
    } else if (std::strcmp(a, "--wait") == 0 && i + 1 < _argc) {
      waitMs = std::atoi(_argv[++i]);
    } else if (std::strcmp(a, "--spin") == 0 && i + 2 < _argc) {
      // 注意：不能写成 spins.emplace_back(atoi(argv[++i]), atof(argv[++i]))——
      // 函数实参求值顺序不确定，两个 ++i 会抢跑（2026-10-28 实测踩到：索引与值互换）。
      const int idx = std::atoi(_argv[i + 1]);
      const double val = std::atof(_argv[i + 2]);
      i += 2;
      spins.emplace_back(idx, val);
    } else if (std::strcmp(a, "--combo") == 0 && i + 2 < _argc) {
      const int idx = std::atoi(_argv[i + 1]);
      const std::string text = _argv[i + 2];
      i += 2;
      combos.emplace_back(idx, text);
    }
  }

  if (exp.empty()) {
    std::printf(
        "usage: exp_smoke_test --exp <EXP-ID> [--dump] [--result] "
        "[--spin i v]... [--combo i text]... [--click text]... [--wait ms]\n");
    return 2;
  }

  gz::transport::Node node;
  const QStringList models{"ke0812_1"};

  QDialog *dlg = Make(exp, node, models);
  if (!dlg) {
    std::printf("UNKNOWN %s\n", exp.c_str());
    std::fflush(stdout);
    return 2;
  }

  dlg->show();
  app.processEvents();

  /// 文件注入：多数实验的「浏览/导入」会弹 QFileDialog::getOpenFileName（模态阻塞），
  /// 驱动没法在点击返回后补操作。这里用 200 ms 轮询把顶层 QFileDialog 找出来、填路径并 accept，
  /// 从而把「必须人工选文件」的实验也纳入自动演练（--file <path>）。
  QTimer *injectTimer = nullptr;
  if (!injectFile.isEmpty()) {
    injectTimer = new QTimer(&app);
    QObject::connect(injectTimer, &QTimer::timeout, [&injectFile, injectTimer]() {
      for (auto *w : QApplication::topLevelWidgets()) {
        auto *fd = qobject_cast<QFileDialog *>(w);
        if (fd && fd->isVisible()) {
          const QFileInfo fi(injectFile);
          /// 先切目录再选文件名：只给绝对路径时，离屏对话框的列表还没刷新到该目录，
          /// accept 会因"文件不存在于当前视图"而拒绝关闭（2026-10-28 实测）。
          fd->setDirectory(fi.absolutePath());
          fd->selectFile(fi.fileName());
          QMetaObject::invokeMethod(fd, "accept", Qt::DirectConnection);
          continue;
        }
        /// 载入失败常常弹 QMessageBox（模态），不处理会把驱动卡死在这里。
        auto *mb = qobject_cast<QMessageBox *>(w);
        if (mb && mb->isVisible()) {
          std::fprintf(stderr, "MSGBOX %s\n", mb->text().toUtf8().constData());
          mb->accept();
        }
      }
    });
    injectTimer->start(200);
  }

  const int widgets = static_cast<int>(dlg->findChildren<QWidget *>().size());
  const int buttons = static_cast<int>(dlg->findChildren<QPushButton *>().size());
  std::printf("OK %s title=%s widgets=%d buttons=%d size=%dx%d\n", exp.c_str(),
              dlg->windowTitle().toUtf8().constData(), widgets, buttons,
              dlg->width(), dlg->height());
  std::fflush(stdout);

  // ---- 设参数（按 dump 的索引顺序）----
  const auto spinList = dlg->findChildren<QDoubleSpinBox *>();
  for (const auto &s : spins) {
    if (s.first < 0 || s.first >= static_cast<int>(spinList.size())) {
      std::printf("NOSPIN %d\n", s.first);
      continue;
    }
    spinList[static_cast<size_t>(s.first)]->setValue(s.second);
    std::printf("SPIN\t%d\t%.6g\n", s.first,
                spinList[static_cast<size_t>(s.first)]->value());
  }
  const auto comboList = dlg->findChildren<QComboBox *>();
  for (const auto &c : combos) {
    if (c.first < 0 || c.first >= static_cast<int>(comboList.size())) {
      std::printf("NOCOMBO %d\n", c.first);
      continue;
    }
    auto *cb = comboList[static_cast<size_t>(c.first)];
    const int idx = cb->findText(QString::fromStdString(c.second), Qt::MatchContains);
    if (idx < 0) {
      std::printf("COMBOMISS\t%d\t%s\n", c.first, c.second.c_str());
      continue;
    }
    cb->setCurrentIndex(idx);
    std::printf("COMBO\t%d\t%s\n", c.first,
                cb->currentText().toUtf8().constData());
  }
  if (!spins.empty() || !combos.empty())
    Pump(app, 300);

  if (dump) PrintUi(dlg);

  // ---- 点按钮 ----
  for (const auto &c : clicks) {
    QPushButton *hit = nullptr;
    for (auto *b : dlg->findChildren<QPushButton *>()) {
      if (b->text().contains(QString::fromStdString(c), Qt::CaseInsensitive) &&
          b->isEnabled()) {
        hit = b;
        break;
      }
    }
    if (!hit) {
      std::printf("NOCLICK\t%s\n", c.c_str());
      std::fflush(stdout);
      continue;
    }
    hit->click();
    Pump(app, waitMs);
    std::printf("CLICKED\t%s\n", c.c_str());
    std::fflush(stdout);
  }

  if (result) PrintResults(dlg);

  dlg->close();
  delete dlg;
  return 0;
}
