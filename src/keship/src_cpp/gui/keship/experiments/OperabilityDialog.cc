/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-05 海况等级下的操限评估 —— 实现 */

#include "OperabilityDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"
#include "TurningExperimentCore.hh"
#include "WaveResponseCore.hh"

namespace {
/// 海况等级 → 有义波高 / 峰值周期 / 风速（教学取值，按 ITTC 海况表 + 蒲福风级内插）
/// 取"该等级代表值"而非区间端点，便于复现与对齐指导书。
void SeaStateTable(int _level, double &_hs, double &_tp, double &_wind)
{
  switch (_level) {
    case 3: _hs = 0.9; _tp = 5.0; _wind = 4.0; break;
    case 4: _hs = 1.8; _tp = 6.5; _wind = 6.0; break;
    case 5: _hs = 3.2; _tp = 8.0; _wind = 9.0; break;
    default: _hs = 0.0; _tp = 5.0; _wind = 0.0; break;  // 0 = 静水基准
  }
}
}  // namespace

//-----------------------------------------------------------------------------
OperabilityDialog::OperabilityDialog(gz::transport::Node &_transportNode,
                                     const std::string &_worldName,
                                     const QStringList &_modelNames,
                                     QWidget *_parent)
    : QDialog(_parent), transportNode_(_transportNode), worldName_(_worldName)
{
  setWindowTitle("MS-05 海况等级下的操限评估");
  resize(980, 700);
  turn_ = std::make_unique<keship::TurningExperimentCore>(transportNode_, worldName_);
  wave_ = std::make_unique<keship::WaveResponseCore>(transportNode_, worldName_);
  buildUi();
  modelCombo_->addItems(_modelNames);
}

OperabilityDialog::~OperabilityDialog()
{
  if (turn_ && turn_->running()) turn_->stop();
}

//-----------------------------------------------------------------------------
void OperabilityDialog::buildUi()
{
  auto *layout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>同一套操纵动作在不同海况下的**能力退化**。<br>"
      "① 先跑 <b>0 级（静水）</b>得到基准；② 再依次跑 3/4/5 级，比较失速比、"
      "航向保持偏差与回转指标的变化。<br>"
      "<b>交付物：</b>「导出操限评估」生成的 <code>ms05_operability.json</code>，"
      "含每个海况的工况与全部指标（可机器复核）。", this);
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto *paramBox = new QGroupBox("参数（⚠ 平台不含舵：转向由双推进器差动实现）", this);
  auto *paramForm = new QFormLayout(paramBox);

  modelCombo_ = new QComboBox(paramBox);
  paramForm->addRow("模型：", modelCombo_);

  leftNameEdit_ = new QLineEdit("thrust_left", paramBox);
  rightNameEdit_ = new QLineEdit("thrust_right", paramBox);
  auto *thrRow = new QHBoxLayout();
  thrRow->addWidget(new QLabel("左推进器：", paramBox));
  thrRow->addWidget(leftNameEdit_);
  thrRow->addSpacing(12);
  thrRow->addWidget(new QLabel("右推进器：", paramBox));
  thrRow->addWidget(rightNameEdit_);
  paramForm->addRow(thrRow);

  levelCombo_ = new QComboBox(paramBox);
  levelCombo_->addItem("0 级 · 静水（基准）", 0);
  levelCombo_->addItem("3 级 · 有义波高 0.9 m", 3);
  levelCombo_->addItem("4 级 · 有义波高 1.8 m", 4);
  levelCombo_->addItem("5 级 · 有义波高 3.2 m", 5);
  paramForm->addRow("海况等级：", levelCombo_);

  periodSpin_ = new QDoubleSpinBox(paramBox);
  periodSpin_->setRange(0.02, 1.0); periodSpin_->setValue(0.1);
  periodSpin_->setSingleStep(0.02); periodSpin_->setDecimals(3);
  periodSpin_->setSuffix(" s");
  paramForm->addRow("采样周期：", periodSpin_);

  seaStateLabel_ = new QLabel(paramBox);
  seaStateLabel_->setStyleSheet("color:#555;");
  paramForm->addRow(seaStateLabel_);

  auto *btnRow = new QHBoxLayout();
  applyWaveBtn_ = new QPushButton("① 只施加海况（不跑操纵）", paramBox);
  runBtn_ = new QPushButton("② 跑当前海况（直航 30 s + 回转 90 s）", paramBox);
  exportBtn_ = new QPushButton("导出操限评估（MS-05 交付物）", paramBox);
  btnRow->addWidget(applyWaveBtn_);
  btnRow->addWidget(runBtn_);
  btnRow->addWidget(exportBtn_);
  paramForm->addRow(btnRow);

  layout->addWidget(paramBox);

  table_ = new QTableWidget(this);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setColumnCount(9);
  table_->setHorizontalHeaderLabels({"海况", "Hs (m)", "Tp (s)", "风 (m/s)",
                                     "平均航速 (m/s)", "航向标准差 (°)",
                                     "战术直径 (m)", "进距 (m)", "失速比"});
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  layout->addWidget(table_, 1);

  statusLabel_ = new QLabel("就绪。建议顺序：0 级 → 3 级 → 4 级 → 5 级。", this);
  statusLabel_->setWordWrap(true);
  layout->addWidget(statusLabel_);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &OperabilityDialog::onTick);
  connect(applyWaveBtn_, &QPushButton::clicked, this, &OperabilityDialog::onApplySeaState);
  connect(runBtn_, &QPushButton::clicked, this, &OperabilityDialog::onRun);
  connect(exportBtn_, &QPushButton::clicked, this, &OperabilityDialog::onExport);
  connect(levelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { onLevelChanged(); });
  levelCombo_->setCurrentIndex(0);
  onLevelChanged();
}

//-----------------------------------------------------------------------------
void OperabilityDialog::seaStateOf(int _level, double &_hs, double &_tp, double &_wind)
{
  SeaStateTable(_level, _hs, _tp, _wind);
}

//-----------------------------------------------------------------------------
void OperabilityDialog::onLevelChanged()
{
  double hs = 0.0, tp = 0.0, wind = 0.0;
  seaStateOf(levelCombo_->currentData().toInt(), hs, tp, wind);
  seaStateLabel_->setText(
      hs <= 0.0 ? "静水：不施加波浪（作基准用）"
                : QString("有义波高 %1 m（波幅 %2 m）· 峰值周期 %3 s · 风速 %4 m/s")
                      .arg(hs, 0, 'f', 2).arg(hs / 2.0, 0, 'f', 2)
                      .arg(tp, 0, 'f', 1).arg(wind, 0, 'f', 1));
}

//-----------------------------------------------------------------------------
void OperabilityDialog::onApplySeaState()
{
  double hs = 0.0, tp = 0.0, wind = 0.0;
  seaStateOf(levelCombo_->currentData().toInt(), hs, tp, wind);
  if (hs <= 0.0) {
    statusLabel_->setText("静水基准：不施加波浪，直接「跑当前海况」。");
    return;
  }
  wave_->setTarget(modelCombo_->currentText().toStdString());
  // 横浪（90°）是回转操纵最不利方向，作为操限评估的固定激励方向
  wave_->sendWave(hs / 2.0, tp, 1.0, M_PI / 2.0);
  statusLabel_->setText(QString("已施加 %1 级海况：波幅 %2 m / 周期 %3 s / 横浪")
                            .arg(levelCombo_->currentData().toInt())
                            .arg(hs / 2.0, 0, 'f', 2).arg(tp, 0, 'f', 1));
  std::cout << "MS-05: 已施加海况 " << levelCombo_->currentData().toInt()
            << "（波幅 " << hs / 2.0 << " m / 周期 " << tp << " s）" << std::endl;
}

//-----------------------------------------------------------------------------
void OperabilityDialog::onRun()
{
  if (running_) return;
  const int level = levelCombo_->currentData().toInt();
  double hs = 0.0, tp = 0.0, wind = 0.0;
  seaStateOf(level, hs, tp, wind);

  turn_->setTarget(modelCombo_->currentText().toStdString(),
                   leftNameEdit_->text().toStdString(),
                   rightNameEdit_->text().toStdString());
  turn_->setSamplePeriod(periodSpin_->value());

  // 两段：① 直航 30 s（等推力，量航速与航向保持）② 回转 90 s（差动，量回转指标）
  std::vector<keship::TurningStage> stages;
  {
    keship::TurningStage s;
    s.durationSec = 30.0; s.leftN = 100.0; s.rightN = 100.0;
    s.name = "直航（等推力）";
    stages.push_back(s);
  }
  {
    keship::TurningStage s;
    s.durationSec = 90.0; s.leftN = 150.0; s.rightN = 50.0;
    s.measurementStart = true;
    s.name = "回转（差动）";
    stages.push_back(s);
  }
  std::string err;
  if (!turn_->setStages(stages, &err)) {
    QMessageBox::warning(this, "MS-05", QString::fromStdString("阶段设置失败：" + err));
    return;
  }

  if (hs > 0.0) {
    wave_->setTarget(modelCombo_->currentText().toStdString());
    wave_->sendWave(hs / 2.0, tp, 1.0, M_PI / 2.0);
  }

  turn_->clear();
  if (!turn_->start()) {
    QMessageBox::warning(this, "MS-05",
        "启动失败：请确认模型与推进器名正确，且仿真世界正在运行。");
    return;
  }

  running_ = true;
  runBtn_->setEnabled(false);
  applyWaveBtn_->setEnabled(false);
  exportBtn_->setEnabled(false);
  statusLabel_->setText(QString("正在跑 %1 级海况…（直航 30 s + 回转 90 s 仿真时间）")
                            .arg(level));
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
}

//-----------------------------------------------------------------------------
void OperabilityDialog::onTick()
{
  if (!turn_->sample()) return;

  // 全部阶段跑完 ⇒ 结算
  if (!turn_->running()) {
    timer_->stop();
    running_ = false;
    runBtn_->setEnabled(true);
    applyWaveBtn_->setEnabled(true);
    exportBtn_->setEnabled(true);

    const int level = levelCombo_->currentData().toInt();
    double hs = 0.0, tp = 0.0, wind = 0.0;
    seaStateOf(level, hs, tp, wind);

    Row r;
    r.level = level; r.hsM = hs; r.tpS = tp; r.windMs = wind;

    // 直航段（stage 0）的航速与航向标准差 —— 由轨迹直接算，不用核心的瞬时值
    const auto &trail = turn_->trail();
    std::vector<const keship::TurningSample *> leg;
    for (const auto &p : trail) if (p.stage == 0) leg.push_back(&p);

    if (leg.size() >= 3) {
      double dist = 0.0;
      for (size_t i = 1; i < leg.size(); ++i) {
        const double dx = leg[i]->x - leg[i - 1]->x;
        const double dy = leg[i]->y - leg[i - 1]->y;
        dist += std::sqrt(dx * dx + dy * dy);
      }
      const double dt = leg.back()->t - leg.front()->t;
      r.avgSpeedMs = (dt > 1e-6) ? dist / dt : -1.0;

      double sum = 0.0, sum2 = 0.0;
      for (auto *p : leg) { sum += p->yawDeg; sum2 += p->yawDeg * p->yawDeg; }
      const double n = static_cast<double>(leg.size());
      const double mean = sum / n;
      const double var = std::max(0.0, sum2 / n - mean * mean);
      r.headingStdDeg = std::sqrt(var);
    }

    const auto m = turn_->metrics();
    if (m.valid) { r.tacticalM = m.tactical; r.advanceM = m.advance; }

    // 失速比：与静水基准比（若无基准则留空，不编数）
    if (level != 0) {
      const Row *base = calmBaseline();
      if (base && base->avgSpeedMs > 1e-6 && r.avgSpeedMs > 0.0)
        r.speedLossRatio = r.avgSpeedMs / base->avgSpeedMs;
    } else if (r.avgSpeedMs > 0.0) {
      r.speedLossRatio = 1.0;  // 静水自身为基准
    }

    rows_.push_back(r);
    refreshTable();

    statusLabel_->setText(
        QString("%1 级完成：平均航速 %2 m/s · 航向标准差 %3° · 战术直径 %4 m%5")
            .arg(level)
            .arg(r.avgSpeedMs, 0, 'f', 3)
            .arg(r.headingStdDeg, 0, 'f', 3)
            .arg(r.tacticalM, 0, 'f', 2)
            .arg(level != 0 && r.speedLossRatio < 0.0
                     ? "（缺 0 级基准，失速比留空）" : ""));
    std::cout << "MS-05: " << level << " 级完成 avgSpeed=" << r.avgSpeedMs
              << " headingStd=" << r.headingStdDeg << " tactical=" << r.tacticalM
              << " speedLoss=" << r.speedLossRatio << std::endl;
  }
}

//-----------------------------------------------------------------------------
const OperabilityDialog::Row *OperabilityDialog::calmBaseline() const
{
  for (auto it = rows_.rbegin(); it != rows_.rend(); ++it)
    if (it->level == 0) return &(*it);
  return nullptr;
}

//-----------------------------------------------------------------------------
void OperabilityDialog::appendRow(const Row &_r) { rows_.push_back(_r); refreshTable(); }

//-----------------------------------------------------------------------------
void OperabilityDialog::refreshTable()
{
  auto fmt = [](double v, int dec) {
    return v < 0.0 ? QString("—") : QString::number(v, 'f', dec);
  };
  table_->setRowCount(static_cast<int>(rows_.size()));
  for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
    const Row &r = rows_[i];
    auto set = [&](int c, const QString &t) {
      auto *it = new QTableWidgetItem(t);
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      table_->setItem(i, c, it);
    };
    set(0, QString("%1 级").arg(r.level));
    set(1, QString::number(r.hsM, 'f', 2));
    set(2, QString::number(r.tpS, 'f', 1));
    set(3, QString::number(r.windMs, 'f', 1));
    set(4, fmt(r.avgSpeedMs, 3));
    set(5, fmt(r.headingStdDeg, 3));
    set(6, fmt(r.tacticalM, 2));
    set(7, fmt(r.advanceM, 2));
    set(8, fmt(r.speedLossRatio, 3));
  }
}

//-----------------------------------------------------------------------------
void OperabilityDialog::onExport()
{
  if (rows_.empty()) {
    QMessageBox::information(this, "导出操限评估",
        "还没有任何结果。请先跑至少一个海况（建议从 0 级开始）。");
    return;
  }

  const QString path = QFileDialog::getSaveFileName(
      this, "导出 MS-05 操限评估", "ms05_operability.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  keship::ExpDeliverable d;
  d.expId = "MS-05";
  d.createdAt = keship::NowIso8601();
  d.object["type"] = "ksm";
  d.object["name"] = modelCombo_->currentText();
  d.params["thrusterLeft"] = leftNameEdit_->text();
  d.params["thrusterRight"] = rightNameEdit_->text();
  d.params["straightThrustN"] = 100.0;
  d.params["turningThrustN"] = 150.0;
  d.params["diffThrustN"] = 50.0;
  d.params["waveDirectionDeg"] = 90.0;

  QJsonArray arr;
  for (const auto &r : rows_) {
    QJsonObject o;
    o["level"] = r.level;
    o["hsM"] = r.hsM;
    o["tpS"] = r.tpS;
    o["windMs"] = r.windMs;
    o["avgSpeedMs"] = r.avgSpeedMs;
    o["headingStdDeg"] = r.headingStdDeg;
    o["tacticalM"] = r.tacticalM;
    o["advanceM"] = r.advanceM;
    o["speedLossRatio"] = r.speedLossRatio;
    arr.append(o);
  }
  d.extra["rows"] = arr;

  // metrics 放"最恶劣海况"的汇总，便于下游直接引用
  const Row *worst = nullptr;
  for (const auto &r : rows_) if (!worst || r.level > worst->level) worst = &r;
  if (worst) {
    d.metrics["worstLevel"] = worst->level;
    d.metrics["worstTacticalM"] = worst->tacticalM;
    d.metrics["worstHeadingStdDeg"] = worst->headingStdDeg;
    d.metrics["worstSpeedLossRatio"] = worst->speedLossRatio;
    d.metrics["runs"] = static_cast<int>(rows_.size());
  }

  std::string err;
  if (!keship::WriteExpDeliverable(path.toStdString(), d, &err)) {
    QMessageBox::warning(this, "导出操限评估", QString::fromStdString("导出失败：" + err));
    return;
  }
  QMessageBox::information(this, "导出操限评估", QString("已导出：%1").arg(path));
  std::cout << "MS-05: 已导出交付物 " << path.toStdString() << std::endl;
}
