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
#include "experiments/OffsetsHullDialog.hh"
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
  if (exp == "SD-10") return new OffsetsHullDialog(nullptr);

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

/// 无交互导入（D1）：直调各对话框已有的 public 入口，不经过 QFileDialog。
/// 第二个参数用于「一个实验要导两份上游」的情形（HS-08: HS-07 + SD-08；SD-07: SD-04 + HS-08）。
bool TryImport(QDialog *_dlg, const QString &_p1, const QString &_p2)
{
  if (_p1.isEmpty()) return false;
  if (auto *x = qobject_cast<MidshipSectionDialog *>(_dlg)) {
    std::string err; return x->importFrom(_p1, &err);
  }
  if (auto *x = qobject_cast<LongitudinalStrengthDialog *>(_dlg)) {
    std::string err; return x->importFrom(_p1, &err);
  }
  if (auto *x = qobject_cast<LinesReadingDialog *>(_dlg)) {
    return x->importSd08(_p1);
  }
  if (auto *x = qobject_cast<BuoyancyLayoutDialog *>(_dlg)) {
    return x->importSd04(_p1);
  }
  if (auto *x = qobject_cast<GzStabilityDialog *>(_dlg)) {
    bool ok = x->importHs07(_p1);                       ///< 取 KG
    if (!_p2.isEmpty()) ok = x->importSd08(_p2) && ok;  ///< 再切站表
    return ok;
  }
  if (auto *x = qobject_cast<DamageStabilityDialog *>(_dlg)) {
    bool ok = x->importSd04(_p1);                       ///< 舱室
    if (!_p2.isEmpty()) ok = x->importHs08(_p2) && ok;  ///< 完整船 GM
    return ok;
  }
  if (auto *x = qobject_cast<InertiaCalculatorDialog *>(_dlg)) {
    x->loadMeshFileForTest(_p1.toStdString()); return true;
  }
  if (auto *x = qobject_cast<ProxyGeometryDialog *>(_dlg)) {
    x->loadMeshFileForTest(_p1.toStdString()); return true;
  }
  if (auto *x = qobject_cast<OffsetsHullDialog *>(_dlg)) {
    QFile f(_p1);
    if (!f.open(QIODevice::ReadOnly)) return false;
    std::string err;
    return x->applyCsvText(QString::fromUtf8(f.readAll()), &err);
  }
  return false;
}

/// 无交互导出（D1）：同上。带 err 的与不带 err 的两组签名分别处理。
bool TryExport(QDialog *_dlg, const QString &_p)
{
  if (_p.isEmpty()) return false;
  std::string err;
  if (auto *x = qobject_cast<MidshipSectionDialog *>(_dlg))        return x->exportTo(_p, &err);
  if (auto *x = qobject_cast<LongitudinalStrengthDialog *>(_dlg))  return x->exportTo(_p, &err);
  if (auto *x = qobject_cast<StructureCognitionDialog *>(_dlg))    return x->exportTo(_p, &err);
  if (auto *x = qobject_cast<ParametricHullDialog *>(_dlg))        return x->exportTo(_p, &err);
  if (auto *x = qobject_cast<OffsetsHullDialog *>(_dlg))          return x->exportTo(_p, &err);
  if (auto *x = qobject_cast<LinesReadingDialog *>(_dlg))          return x->exportTo(_p);
  if (auto *x = qobject_cast<BuoyancyLayoutDialog *>(_dlg))        return x->exportTo(_p);
  if (auto *x = qobject_cast<GzStabilityDialog *>(_dlg))           return x->exportTo(_p);
  if (auto *x = qobject_cast<DamageStabilityDialog *>(_dlg))       return x->exportTo(_p);
  if (auto *x = qobject_cast<CompartmentDialog *>(_dlg))           return x->exportTo(_p);
  if (auto *x = qobject_cast<ResistanceDialog *>(_dlg))            return x->exportTo(_p);
  if (auto *x = qobject_cast<PropellerDialog *>(_dlg))             return x->exportTo(_p);
  if (auto *x = qobject_cast<LargeAngleStabilityDialog *>(_dlg))   return x->exportTo(_p);
  return false;
}

}  // namespace

int main(int _argc, char **_argv)
{
  QApplication app(_argc, _argv);

  std::string exp;
  bool dump = false;
  bool result = false;
  int waitMs = 1000;
  /// ★ 2026-10-29（D5 配套）：动作按**命令行顺序**执行。
  /// 旧机制是「先全部 spin/combo、后全部 click」，无法表达
  /// HS-04/05「生成驳船 → 设 y → 等稳定 → 记录 → 再设下一组 y」这类
  /// 生成动作会重置参数的序列。新增 --pause ms 显式等待（判稳需要）。
  /// kind：'C'=click 'S'=spin 'O'=combo 'P'=pause
  struct Action { char kind; int idx; double val; std::string text; };
  std::vector<Action> actions;
  QString injectFile;
  QString importPath;    ///< --import：直调 importFrom/importSd04/...（不弹文件框）
  QString importPath2;   ///< --import2：第二个上游（HS-08 / SD-07 用）
  QString exportPath;    ///< --export：直调 exportTo（不弹文件框）

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
    } else if (std::strcmp(a, "--import") == 0 && i + 1 < _argc) {
      importPath = QString::fromLocal8Bit(_argv[++i]);
    } else if (std::strcmp(a, "--import2") == 0 && i + 1 < _argc) {
      importPath2 = QString::fromLocal8Bit(_argv[++i]);
    } else if (std::strcmp(a, "--export") == 0 && i + 1 < _argc) {
      exportPath = QString::fromLocal8Bit(_argv[++i]);
    } else if (std::strcmp(a, "--click") == 0 && i + 1 < _argc) {
      actions.push_back({'C', 0, 0.0, _argv[++i]});
    } else if (std::strcmp(a, "--wait") == 0 && i + 1 < _argc) {
      waitMs = std::atoi(_argv[++i]);
    } else if (std::strcmp(a, "--pause") == 0 && i + 1 < _argc) {
      actions.push_back({'P', 0, std::atof(_argv[++i]), ""});
    } else if (std::strcmp(a, "--spin") == 0 && i + 2 < _argc) {
      // 注意：不能把两个 ++i 写进同一条 push 的实参表——
      // 函数实参求值顺序不确定，两个 ++i 会抢跑（2026-10-28 实测踩到：索引与值互换）。
      const int idx = std::atoi(_argv[i + 1]);
      const double val = std::atof(_argv[i + 2]);
      i += 2;
      actions.push_back({'S', idx, val, ""});
    } else if (std::strcmp(a, "--combo") == 0 && i + 2 < _argc) {
      const int idx = std::atoi(_argv[i + 1]);
      const std::string text = _argv[i + 2];
      i += 2;
      actions.push_back({'O', idx, 0.0, text});
    }
  }

  if (exp.empty()) {
    std::printf(
        "usage: exp_smoke_test --exp <EXP-ID> [--dump] [--result] "
        "[--spin i v]... [--combo i text]... [--click text]... [--pause ms]... "
        "[--wait ms] [--file p] [--import p] [--import2 p] [--export p]\n"
        "  动作（--spin/--combo/--click/--pause）按命令行顺序执行；"
        "--wait 是每次 --click 后的默认等待；--dump 快照在动作执行前\n");
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

  /// 模态框兜底：定时器**无条件**创建（2026-10-28 D2）。
  /// ① 给了 --file 时：把顶层 QFileDialog 找出来、填路径并 accept（原逻辑）；
  /// ② 无论有没有 --file：自动 accept 顶层 QMessageBox，并把文字打到 stderr。
  ///    缺了 ② 时，「未勾软系泊」「未设目标 Cb」「导入失败」这类提示框会把驱动
  ///    永久阻塞 —— 实测 MS-02/03/06、SD-08 反解全部卡到 exit=124。
  QTimer *injectTimer = new QTimer(&app);
  QObject::connect(injectTimer, &QTimer::timeout, [&injectFile]() {
    for (auto *w : QApplication::topLevelWidgets()) {
      auto *fd = qobject_cast<QFileDialog *>(w);
      if (fd && fd->isVisible()) {
        if (injectFile.isEmpty()) continue;   ///< 没给 --file 就不去动文件框
        const QFileInfo fi(injectFile);
        /// 先切目录再选文件名：只给绝对路径时，离屏对话框的列表还没刷新到该目录，
        /// accept 会因"文件不存在于当前视图"而拒绝关闭（2026-10-28 实测）。
        fd->setDirectory(fi.absolutePath());
        fd->selectFile(fi.fileName());
        QMetaObject::invokeMethod(fd, "accept", Qt::DirectConnection);
        continue;
      }
      auto *mb = qobject_cast<QMessageBox *>(w);
      if (mb && mb->isVisible()) {
        std::fprintf(stderr, "MSGBOX %s\n", mb->text().toUtf8().constData());
        /// 有「是」就点「是」：测试要的是「继续」路径。直接 accept() 会激活默认按钮，
        /// 而 QMessageBox::question(Yes|No, 默认 No) 的默认按钮是 No —— 实测 MS-03
        /// 会因此静默 return，记录不启动（幅值全「—」）。
        if (QAbstractButton *yes = mb->button(QMessageBox::Yes)) {
          std::fprintf(stderr, "MSGBOX->Yes\n");
          yes->click();
        } else {
          std::fprintf(stderr, "MSGBOX->accept\n");
          mb->accept();
        }
      }
    }
  });
  injectTimer->start(200);

  const int widgets = static_cast<int>(dlg->findChildren<QWidget *>().size());
  const int buttons = static_cast<int>(dlg->findChildren<QPushButton *>().size());
  std::printf("OK %s title=%s widgets=%d buttons=%d size=%dx%d\n", exp.c_str(),
              dlg->windowTitle().toUtf8().constData(), widgets, buttons,
              dlg->width(), dlg->height());
  std::fflush(stdout);

  // ---- 无交互导入（D1）：不弹 QFileDialog，直调对话框的 public 入口 ----
  if (!importPath.isEmpty()) {
    const bool ok = TryImport(dlg, importPath, importPath2);
    std::printf("IMPORT\t%s\t%s\n", ok ? "OK" : "FAIL",
                importPath.toUtf8().constData());
    std::fflush(stdout);
    Pump(app, 300);
  }

  if (dump) PrintUi(dlg);

  // ---- 按命令行顺序执行全部动作（2026-10-29 D5 配套；--dump 快照在动作之前）----
  const auto spinList = dlg->findChildren<QDoubleSpinBox *>();
  const auto comboList = dlg->findChildren<QComboBox *>();
  for (const auto &a : actions) {
    if (a.kind == 'S') {
      if (a.idx < 0 || a.idx >= static_cast<int>(spinList.size())) {
        std::printf("NOSPIN %d\n", a.idx);
      } else {
        spinList[static_cast<size_t>(a.idx)]->setValue(a.val);
        std::printf("SPIN\t%d\t%.6g\n", a.idx,
                    spinList[static_cast<size_t>(a.idx)]->value());
        Pump(app, 300);
      }
      std::fflush(stdout);
    } else if (a.kind == 'O') {
      if (a.idx < 0 || a.idx >= static_cast<int>(comboList.size())) {
        std::printf("NOCOMBO %d\n", a.idx);
      } else {
        auto *cb = comboList[static_cast<size_t>(a.idx)];
        const int ci =
            cb->findText(QString::fromStdString(a.text), Qt::MatchContains);
        if (ci < 0) {
          std::printf("COMBOMISS\t%d\t%s\n", a.idx, a.text.c_str());
        } else {
          cb->setCurrentIndex(ci);
          std::printf("COMBO\t%d\t%s\n", a.idx,
                      cb->currentText().toUtf8().constData());
          Pump(app, 300);
        }
      }
      std::fflush(stdout);
    } else if (a.kind == 'C') {
      QPushButton *hit = nullptr;
      for (auto *b : dlg->findChildren<QPushButton *>()) {
        if (b->text().contains(QString::fromStdString(a.text), Qt::CaseInsensitive) &&
            b->isEnabled()) {
          hit = b;
          break;
        }
      }
      if (!hit) {
        std::printf("NOCLICK\t%s\n", a.text.c_str());
      } else {
        hit->click();
        Pump(app, waitMs);
        std::printf("CLICKED\t%s\n", a.text.c_str());
      }
      std::fflush(stdout);
    } else if (a.kind == 'P') {
      Pump(app, static_cast<int>(a.val));
      std::printf("PAUSE\t%d\n", static_cast<int>(a.val));
      std::fflush(stdout);
    }
  }

  // ---- 无交互导出（D1）：不弹 QFileDialog ----
  if (!exportPath.isEmpty()) {
    const bool ok = TryExport(dlg, exportPath);
    std::printf("EXPORT\t%s\t%s\n", ok ? "OK" : "FAIL",
                exportPath.toUtf8().constData());
    std::fflush(stdout);
  }

  if (result) PrintResults(dlg);

  dlg->close();
  delete dlg;
  return 0;
}
