/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-02 / MS-03 对话框实现（薄壳：界面 + 委托核心层） */

#include "WaveResponseDialog.hh"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>

#include <QAbstractItemView>
#include <QTableWidget>

// ══════════════════════════════════════════════════════════════════
// 响应时程图（三曲线）
// ══════════════════════════════════════════════════════════════════
WaveChartWidget::WaveChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(320);
  setAutoFillBackground(true);
}

void WaveChartWidget::setTrail(const std::vector<keship::WaveSample> &_pts) {
  pts_ = _pts;
  update();
}
void WaveChartWidget::clearTrail() { pts_.clear(); update(); }

void WaveChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (pts_.size() < 2) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "尚无数据 — 点「开始」施加规则波");
    return;
  }

  const double tMax = pts_.back().t;
  double yMin = 0, yMax = 0;
  for (const auto &s : pts_) {
    yMin = std::min({yMin, s.rollDeg, s.pitchDeg, s.heave});
    yMax = std::max({yMax, s.rollDeg, s.pitchDeg, s.heave});
  }
  if (yMax - yMin < 1e-6) { yMax += 1; yMin -= 1; }
  const double pad = (yMax - yMin) * 0.1;
  yMin -= pad; yMax += pad;

  const double W = width() - 50.0, H = height() - 30.0;
  auto toPix = [&](double t, double y) {
    return QPointF(40 + (t / std::max(1e-6, tMax)) * W,
                   15 + H - ((y - yMin) / (yMax - yMin)) * H);
  };

  p.setPen(QColor(232, 235, 242));
  for (int i = 1; i < 5; ++i) {
    const double gy = 15 + H * i / 5.0;
    p.drawLine(QPointF(40, gy), QPointF(40 + W, gy));
  }

  auto drawCurve = [&](auto getter, const QColor &_c) {
    QPainterPath path;
    path.moveTo(toPix(pts_[0].t, getter(pts_[0])));
    for (size_t i = 1; i < pts_.size(); ++i)
      path.lineTo(toPix(pts_[i].t, getter(pts_[i])));
    p.setPen(QPen(_c, 2));
    p.drawPath(path);
  };
  drawCurve([](const keship::WaveSample &s) { return s.rollDeg; },  QColor(210, 80, 70));
  drawCurve([](const keship::WaveSample &s) { return s.pitchDeg; }, QColor(60, 110, 200));
  drawCurve([](const keship::WaveSample &s) { return s.heave; },    QColor(50, 160, 90));

  p.setPen(QColor(90, 95, 105));
  p.drawText(12, 16, QString("红=横摇(°)  蓝=纵摇(°)  绿=升沉(m) · %1 点").arg(pts_.size()));
  p.drawText(6, 28, QString::number(yMax, 'f', 1));
  p.drawText(6, static_cast<int>(15 + H), QString::number(yMin, 'f', 1));
}

// ══════════════════════════════════════════════════════════════════
// 对话框
// ══════════════════════════════════════════════════════════════════
WaveResponseDialog::WaveResponseDialog(gz::transport::Node &_transportNode,
                                       const std::string &_worldName,
                                       const QStringList &_models,
                                       const std::string &_mode,
                                       QWidget *_parent)
    : QDialog(_parent), mode_(_mode) {
  const bool ms03 = (mode_ == "ms03");
  setWindowTitle(ms03 ? "MS-03 横摇 RAO 测量" : "MS-02 升沉与纵摇响应");
  resize(920, 880);

  core_ = std::make_unique<keship::WaveResponseCore>(_transportNode, _worldName);

  auto *root = new QVBoxLayout(this);
  auto *paramBox = new QGroupBox("规则波参数（与「水域环境参数」同一通道）", this);
  auto *form = new QFormLayout(paramBox);

  modelCombo_ = new QComboBox(paramBox);
  modelCombo_->addItems(_models.isEmpty() ? QStringList{"GT1"} : _models);
  form->addRow("船模：", modelCombo_);

  waveAmpSpin_ = new QDoubleSpinBox(paramBox);
  waveAmpSpin_->setRange(0.0, 5.0); waveAmpSpin_->setValue(0.5);
  waveAmpSpin_->setSingleStep(0.1); waveAmpSpin_->setSuffix(" m");
  form->addRow("波幅：", waveAmpSpin_);

  wavePeriodSpin_ = new QDoubleSpinBox(paramBox);
  wavePeriodSpin_->setRange(0.5, 20.0); wavePeriodSpin_->setValue(5.0);
  wavePeriodSpin_->setSingleStep(0.5); wavePeriodSpin_->setSuffix(" s");
  form->addRow("波浪周期：", wavePeriodSpin_);

  gainSpin_ = new QDoubleSpinBox(paramBox);
  gainSpin_->setRange(0.0, 2.0); gainSpin_->setValue(1.0);
  gainSpin_->setSingleStep(0.1);
  form->addRow("增益（波幅倍率）：", gainSpin_);

  periodSpin_ = new QDoubleSpinBox(paramBox);
  periodSpin_->setRange(0.02, 1.0); periodSpin_->setValue(0.05);
  periodSpin_->setSingleStep(0.01); periodSpin_->setSuffix(" s");
  form->addRow("采样轮询周期：", periodSpin_);
  form->addRow("波型：", new QLabel("CWR 单频规则波（实验固定）", paramBox));
  root->addWidget(paramBox);

  auto *ctrlRow = new QHBoxLayout();
  applyWaveBtn_ = new QPushButton("施加波浪", this);
  startBtn_ = new QPushButton("开始记录", this);
  stopBtn_ = new QPushButton("停止", this);
  clearBtn_ = new QPushButton("清零", this);
  exportBtn_ = new QPushButton("导出 CSV", this);
  stopBtn_->setEnabled(false);
  ctrlRow->addWidget(applyWaveBtn_);
  ctrlRow->addWidget(startBtn_);
  ctrlRow->addWidget(stopBtn_);
  ctrlRow->addWidget(clearBtn_);
  ctrlRow->addStretch();
  ctrlRow->addWidget(exportBtn_);
  root->addLayout(ctrlRow);

  readoutLabel_ = new QLabel("仿真 t = —   横摇 = —   纵摇 = —   升沉 = —", this);
  resultLabel_ = ms03
      ? new QLabel("横摇幅值 = —   横摇 RAO = —   受迫周期 = —", this)
      : new QLabel("横摇幅值 = —   纵摇幅值 = —   升沉幅值 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  // ── MS-03 专用：RAO 累积记录表（操作类实验）──
  if (ms03) {
    guideLabel_ = new QLabel(
        "<b>操作步骤（MS-03 为操作类实验）</b>："
        "① 设定波浪周期与波幅 →「施加波浪」→「开始记录」，仿真时钟累计 ≥40 s 后「停止」；"
        "② 点「记录本次结果」，将本次 (周期, RAO) 存入下表；"
        "③ 改变波浪周期，重复 ①② 约 5~8 次；"
        "④ 从表中找横摇 RAO 的峰值（共振峰），对应周期即横摇固有周期的估计，"
        "可与 HS-04 的 GM 理论周期对照。", this);
    guideLabel_->setWordWrap(true);
    root->addWidget(guideLabel_);

    raoTable_ = new QTableWidget(0, 4, this);
    raoTable_->setHorizontalHeaderLabels(
        {"波浪周期 (s)", "横摇幅值 (°)", "横摇 RAO (°/m)", "受迫周期 (s)"});
    raoTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    raoTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    raoTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    raoTable_->setMaximumHeight(170);
    root->addWidget(raoTable_);

    auto *raoRow = new QHBoxLayout();
    recordBtn_ = new QPushButton("记录本次结果", this);
    deleteRowBtn_ = new QPushButton("删除选中行", this);
    exportRaoBtn_ = new QPushButton("导出 RAO 表", this);
    recordBtn_->setEnabled(false);   ///< 停止后启用（一次停止只记一行）
    raoRow->addWidget(recordBtn_);
    raoRow->addWidget(deleteRowBtn_);
    raoRow->addStretch();
    raoRow->addWidget(exportRaoBtn_);
    root->addLayout(raoRow);
  }

  chart_ = new WaveChartWidget(this);
  root->addWidget(chart_, 1);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &WaveResponseDialog::onTick);
  connect(applyWaveBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onApplyWave);
  connect(startBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onStop);
  connect(clearBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onClear);
  connect(exportBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onExport);
  if (recordBtn_) {
    connect(recordBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onRecordResult);
    connect(deleteRowBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onDeleteRow);
    connect(exportRaoBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onExportRao);
  }

  std::cout << (ms03 ? "MS-03" : "MS-02")
            << ": 波浪响应对话框已创建（核心层 WaveResponseCore）" << std::endl;
}

WaveResponseDialog::~WaveResponseDialog() = default;

void WaveResponseDialog::syncParamsToCore() {
  core_->setTarget(modelCombo_->currentText().toStdString());
}

void WaveResponseDialog::onApplyWave() {
  syncParamsToCore();
  core_->sendWave(waveAmpSpin_->value(), wavePeriodSpin_->value(), gainSpin_->value());
  std::cout << (mode_ == "ms03" ? "MS-03" : "MS-02")
            << ": 已施加规则波（波幅 " << waveAmpSpin_->value()
            << " m / 周期 " << wavePeriodSpin_->value() << " s）" << std::endl;
}

void WaveResponseDialog::onStart() {
  if (core_->running()) return;
  syncParamsToCore();
  if (!core_->start()) {
    QMessageBox::warning(this, "实验",
        "未能读到船模位姿或仿真时钟 —— 请确认「船模」名与场景一致，且仿真正在运行。");
    return;
  }
  core_->sendWave(waveAmpSpin_->value(), wavePeriodSpin_->value(), gainSpin_->value());
  chart_->clearTrail();
  startBtn_->setEnabled(false);
  stopBtn_->setEnabled(true);
  if (recordBtn_) recordBtn_->setEnabled(false);
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
}

void WaveResponseDialog::onStop() {
  if (!core_->running()) return;
  core_->stop();
  timer_->stop();
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  showResults();
  if (recordBtn_) recordBtn_->setEnabled(core_->metrics().valid);
}

void WaveResponseDialog::onClear() {
  core_->clear();
  chart_->clearTrail();
  readoutLabel_->setText("仿真 t = —   横摇 = —   纵摇 = —   升沉 = —");
  resultLabel_->setText(mode_ == "ms03"
      ? "横摇幅值 = —   横摇 RAO = —   受迫周期 = —"
      : "横摇幅值 = —   纵摇幅值 = —   升沉幅值 = —");
  if (raoTable_) {
    raoTable_->setRowCount(0);
    recordBtn_->setEnabled(false);
  }
}

void WaveResponseDialog::onTick() {
  if (!core_->sample()) {
    if (core_->clockReset()) {
      core_->stop();
      timer_->stop();
      startBtn_->setEnabled(true);
      stopBtn_->setEnabled(false);
      if (recordBtn_) recordBtn_->setEnabled(false);
      QMessageBox::warning(this, "仿真时间已重置",
          "仿真时钟发生回退，本次响应记录已停止。请清零后重新开始。");
    }
    return;
  }
  chart_->setTrail(core_->trail());
  updateReadout();
}

void WaveResponseDialog::updateReadout() {
  const auto &t = core_->trail();
  if (t.empty()) return;
  const auto &s = t.back();
  readoutLabel_->setText(QString("仿真 t = %1 s   横摇 = %2°   纵摇 = %3°   升沉 = %4 m")
      .arg(s.t, 0, 'f', 1).arg(s.rollDeg, 0, 'f', 2)
      .arg(s.pitchDeg, 0, 'f', 2).arg(s.heave, 0, 'f', 3));
}

void WaveResponseDialog::showResults() {
  const auto m = core_->metrics();
  auto fmt = [](double v) { return v < 0 ? QString("—") : QString::number(v, 'f', 3); };
  if (!m.valid) {
    resultLabel_->setText("数据不足");
    return;
  }
  if (mode_ == "ms03") {
    resultLabel_->setText(QString("横摇幅值 = %1°   横摇 RAO = %2 °/m   受迫周期 = %3 s")
        .arg(fmt(m.rollAmp)).arg(fmt(m.rollRAO)).arg(fmt(m.rollPeriod)));
  } else {
    resultLabel_->setText(QString("横摇幅值 = %1°   纵摇幅值 = %2°   升沉幅值 = %3 m")
        .arg(fmt(m.rollAmp)).arg(fmt(m.pitchAmp)).arg(fmt(m.heaveAmp)));
  }
}

void WaveResponseDialog::onRecordResult() {
  const auto m = core_->metrics();
  if (!m.valid) {
    QMessageBox::information(this, "记录", "当前无有效结果。");
    return;
  }
  const int row = raoTable_->rowCount();
  raoTable_->insertRow(row);
  auto num = [](double v) { return QString::number(v, 'f', 3); };
  raoTable_->setItem(row, 0, new QTableWidgetItem(num(m.wavePeriod)));
  raoTable_->setItem(row, 1, new QTableWidgetItem(num(m.rollAmp)));
  raoTable_->setItem(row, 2, new QTableWidgetItem(num(m.rollRAO)));
  raoTable_->setItem(row, 3, new QTableWidgetItem(num(m.rollPeriod)));
  recordBtn_->setEnabled(false);   ///< 一次停止只记一行，防重复
  std::cout << "MS-03: 已记录 RAO 点（周期 " << m.wavePeriod
            << " s，RAO " << m.rollRAO << " °/m）" << std::endl;
}

void WaveResponseDialog::onDeleteRow() {
  const int row = raoTable_->currentRow();
  if (row < 0) {
    QMessageBox::information(this, "删除", "请先在表中选中一行。");
    return;
  }
  raoTable_->removeRow(row);
  std::cout << "MS-03: 已删除 RAO 表第 " << row + 1 << " 行" << std::endl;
}

void WaveResponseDialog::onExportRao() {
  if (!raoTable_ || raoTable_->rowCount() == 0) {
    QMessageBox::information(this, "导出 RAO 表", "尚无记录。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出 RAO–周期表 CSV",
      "ms03_rao.csv", "CSV (*.csv)");
  if (path.isEmpty()) return;
  std::ofstream f(path.toStdString());
  if (!f) {
    QMessageBox::warning(this, "导出 RAO 表", "无法写入文件。");
    return;
  }
  f << "wave_period_s,roll_amp_deg,roll_rao_deg_per_m,forced_period_s\n";
  for (int r = 0; r < raoTable_->rowCount(); ++r) {
    f << raoTable_->item(r, 0)->text().toStdString() << ","
      << raoTable_->item(r, 1)->text().toStdString() << ","
      << raoTable_->item(r, 2)->text().toStdString() << ","
      << raoTable_->item(r, 3)->text().toStdString() << "\n";
  }
  std::cout << "MS-03: RAO 表已导出（" << raoTable_->rowCount()
            << " 行）-> " << path.toStdString() << std::endl;
}

void WaveResponseDialog::onExport() {
  if (core_->trail().empty()) {
    QMessageBox::information(this, "导出", "尚无数据。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出响应时程 CSV",
      QString::fromStdString(mode_) + "_wave.csv", "CSV (*.csv)");
  if (path.isEmpty()) return;
  if (!core_->exportCsv(path.toStdString()))
    QMessageBox::warning(this, "导出", "无法写入文件。");
}
