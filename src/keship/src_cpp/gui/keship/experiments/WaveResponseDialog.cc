/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-02 / MS-03 对话框实现（薄壳：界面 + 委托核心层） */

#include "WaveResponseDialog.hh"
#include <gz/msgs/entity_factory.pb.h>
#include "../shared/BargeFactory.hh"

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
#include <random>
#include <cmath>
#include <fstream>
#include <iostream>

#include <QAbstractItemView>
#include <QHeaderView>
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

  /// ★ 2026-10-17：显示最近 60 s 时间窗。
  ///   原先总是把整段记录压缩到画布：记录到 3114 s 时约 620 个波周期挤进
  ///   850 px（每周期 <1.4 px），必然糊成实心色块且看不出波形。
  ///   窗口内仍逐像素抽稀，兼顾可读性与开销。
  constexpr double kWindowSec = 60.0;
  const double tEnd = pts_.back().t;
  const double tStart = std::max(0.0, tEnd - kWindowSec);
  size_t firstIdx = 0;
  while (firstIdx + 1 < pts_.size() && pts_[firstIdx].t < tStart) ++firstIdx;
  const double tSpan = std::max(1e-6, tEnd - pts_[firstIdx].t);

  double yMin = 0, yMax = 0;
  for (size_t i = firstIdx; i < pts_.size(); ++i) {
    const auto &s = pts_[i];
    yMin = std::min({yMin, s.rollDeg, s.pitchDeg, s.heave});
    yMax = std::max({yMax, s.rollDeg, s.pitchDeg, s.heave});
  }
  if (yMax - yMin < 1e-6) { yMax += 1; yMin -= 1; }
  const double pad = (yMax - yMin) * 0.1;
  yMin -= pad; yMax += pad;

  const double W = width() - 50.0, H = height() - 30.0;
  auto toPix = [&](double t, double y) {
    return QPointF(40 + ((t - pts_[firstIdx].t) / tSpan) * W,
                   15 + H - ((y - yMin) / (yMax - yMin)) * H);
  };

  p.setPen(QColor(232, 235, 242));
  for (int i = 1; i < 5; ++i) {
    const double gy = 15 + H * i / 5.0;
    p.drawLine(QPointF(40, gy), QPointF(40 + W, gy));
  }

  /// ★ 2026-10-17：按像素抽稀。原先逐点 lineTo（长记录可达上万点），
  ///   抗锯齿下既慢又把曲线糊成实心色块（张老师 12504 点截图）。
  ///   每个像素列最多画 2 个点，并强制包含末点，保证形状与最新值不失真。
  const size_t visible = pts_.size() - firstIdx;
  const size_t maxDraw = static_cast<size_t>(std::max(2.0, W * 2.0));
  const size_t step = std::max<size_t>(1, visible / maxDraw);
  auto drawCurve = [&](auto getter, const QColor &_c) {
    QPainterPath path;
    path.moveTo(toPix(pts_[firstIdx].t, getter(pts_[firstIdx])));
    for (size_t i = firstIdx + step; i < pts_.size(); i += step)
      path.lineTo(toPix(pts_[i].t, getter(pts_[i])));
    if ((pts_.size() - 1 - firstIdx) % step != 0)
      path.lineTo(toPix(pts_.back().t, getter(pts_.back())));
    p.setPen(QPen(_c, 2));
    p.drawPath(path);
  };
  drawCurve([](const keship::WaveSample &s) { return s.rollDeg; },  QColor(210, 80, 70));
  drawCurve([](const keship::WaveSample &s) { return s.pitchDeg; }, QColor(60, 110, 200));
  drawCurve([](const keship::WaveSample &s) { return s.heave; },    QColor(50, 160, 90));

  p.setPen(QColor(90, 95, 105));
  p.drawText(12, 16, QString("红=横摇(°)  蓝=纵摇(°)  绿=升沉(m) · 显示最近 %1 s / 共 %2 点")
      .arg(kWindowSec, 0, 'f', 0).arg(pts_.size()));
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
    : QDialog(_parent), mode_(_mode), node_(_transportNode),
      worldName_(_worldName) {
  const bool ms03 = (mode_ == "ms03");
  const bool ms06 = (mode_ == "ms06");
  setWindowTitle(ms06 ? "MS-06 RAO 编程计算实践"
                      : (ms03 ? "MS-03 横摇 RAO 测量"
                              : "MS-02 升沉与纵摇响应"));
  const char *tag = ms06 ? "MS-06" : (ms03 ? "MS-03" : "MS-02");
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

  /// 波浪方向：横摇 RAO（ms03/ms06）需横浪（90°）——顺浪下横摇被对称性
  /// 严格抵消（2026-10-16 端到端验证：驳船顺浪横摇 4e-15°）；
  /// 升沉/纵摇（ms02）用顺浪（0°）。
  directionSpin_ = new QDoubleSpinBox(paramBox);
  directionSpin_->setRange(0.0, 360.0);
  directionSpin_->setValue((ms03 || ms06) ? 90.0 : 0.0);
  directionSpin_->setSingleStep(15.0);
  directionSpin_->setSuffix(" °");
  form->addRow("波浪方向：", directionSpin_);
  form->addRow("波型：", new QLabel("CWR 单频规则波（实验固定）", paramBox));

  /// 软系泊：只约束水平位置与航向，保留 roll/pitch/heave 自由。
  /// 默认不勾选（不静默改变物理模型）；勾选后由物理插件在仿真步内施加 PD 约束。
  mooringCheck_ = new QCheckBox(
      "启用水平软系泊（保持平均位置与航向；横摇/纵摇/升沉仍自由）", paramBox);
  mooringCheck_->setChecked(false);
  form->addRow("实验边界：", mooringCheck_);
  mooringStatusLabel_ = new QLabel(
      "系泊状态：未启用（船模可自由漂移/偏航）", paramBox);
  mooringStatusLabel_->setStyleSheet("color: #8a6d00;");
  mooringStatusLabel_->setWordWrap(true);
  form->addRow("", mooringStatusLabel_);
  root->addWidget(paramBox);

  mooringTimer_ = new QTimer(this);
  mooringTimer_->setInterval(200);
  connect(mooringTimer_, &QTimer::timeout, this,
          &WaveResponseDialog::onMooringHeartbeat);
  connect(mooringCheck_, &QCheckBox::toggled, this,
          &WaveResponseDialog::onMooringToggled);

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
  helpBtn_ = new QPushButton("RAO 说明 (?)", this);
  ctrlRow->addWidget(helpBtn_);
  ctrlRow->addWidget(exportBtn_);
  connect(helpBtn_, &QPushButton::clicked, this,
          &WaveResponseDialog::onShowRaoHelp);
  root->addLayout(ctrlRow);

  readoutLabel_ = new QLabel("仿真 t = —   横摇 = —   纵摇 = —   升沉 = —", this);
  resultLabel_ = (ms03 || ms06)
      ? new QLabel("横摇幅值 = —   横摇 RAO = —   受迫周期 = —", this)
      : new QLabel("横摇幅值 = —   纵摇幅值 = —   升沉幅值 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  // ── MS-03/MS-06 专用：RAO 累积记录表 ──
  if (ms03 || ms06) {
    guideLabel_ = new QLabel(ms06
        ? "<b>操作步骤（MS-06 为编程实践类实验）</b>："
          "① 逐周期：设定周期与波幅 →「施加波浪」→「开始记录」，仿真时钟 ≥40 s 后「停止」"
          "→「记录本次结果」；② 改变波浪周期重复 ① 约 5~8 次；"
          "③ 点「导出练习数据包」保存位姿 CSV + 参数 + 任务书；"
          "④ 学生按任务书编程计算各周期 RAO（amp/波幅）；"
          "⑤ 点「提交学生结果」提交 student_rao.csv，平台自动与参考值比对。"
        : "<b>操作步骤（MS-03 为操作类实验）</b>："
          "① 设定波浪周期与波幅 →「施加波浪」→「开始记录」，仿真时钟累计 ≥40 s 后「停止」；"
          "② 点「记录本次结果」，将本次 (周期, RAO) 存入下表；"
          "③ 改变波浪周期，重复 ①② 约 5~8 次；"
          "④ 从表中找横摇 RAO 的峰值（通常提示共振区），峰值周期可与 HS-04 的"
          "简化理论 T₀ 对照；两者不必相等（阻尼、附加质量、耦合均会影响）。",
        this);
    guideLabel_->setWordWrap(true);
    root->addWidget(guideLabel_);

    raoTable_ = new QTableWidget(0, 5, this);
    raoTable_->setHorizontalHeaderLabels(
        {"波浪周期 (s)", "横摇幅值 (°)", "横摇 RAO (°/m)", "受迫周期 (s)", "系泊状态"});
    raoTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    raoTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    raoTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    raoTable_->setMaximumHeight(170);
    raoTable_->horizontalHeader()->setSectionResizeMode(
        QHeaderView::Stretch);
    root->addWidget(raoTable_);

    auto *raoRow = new QHBoxLayout();
    recordBtn_ = new QPushButton("记录本次结果", this);
    deleteRowBtn_ = new QPushButton("删除选中行", this);
    exportRaoBtn_ = new QPushButton("导出 RAO 表", this);
    importGmBtn_ = new QPushButton("导入 GM 交付物", this);
    recordBtn_->setEnabled(false);   ///< 停止后启用（一次停止只记一行）
    raoRow->addWidget(recordBtn_);
    raoRow->addWidget(deleteRowBtn_);
    raoRow->addStretch();
    raoRow->addWidget(exportRaoBtn_);
    raoRow->addWidget(importGmBtn_);
    root->addLayout(raoRow);

    // T₀ 对照行：ms03（与实测峰值周期对照）与 ms06（任务书第 5 步理论对照）共用
    gmCompareLabel_ = new QLabel(
        "理论横摇固有周期 T₀：尚未导入 HS-04 的 GM 交付物\n"
        "（点「导入 GM 交付物」选择 hs04_gm.json；"
        "T₀ = 2π√(Ixx/(Δ·GM)) 由 HS-04 的初稳性高、排水量与纵向转动惯量算出）",
        this);
    gmCompareLabel_->setStyleSheet("font-family: monospace;");
    root->addWidget(gmCompareLabel_);

    // ── MS-06 v2：学生算法面板 + 本轮结果 + 隐藏验收 + RAO–T 曲线 ──
    if (ms06) {
      resize(1120, 920);
      auto *algoRow = new QHBoxLayout();
      algoPanel_ = new StudentAlgorithmPanel(this);
      algoRow->addWidget(algoPanel_, 3);

      auto *rightCol = new QVBoxLayout();
      roundResultLabel_ = new QLabel("本轮：运行学生代码后显示（内置值运行前不显示）", this);
      roundResultLabel_->setWordWrap(true);
      roundResultLabel_->setStyleSheet("font-family: monospace; color:#333;");
      rightCol->addWidget(roundResultLabel_);
      submitValidationBtn_ = new QPushButton("提交验收（隐藏数据 5 组）", this);
      rightCol->addWidget(submitValidationBtn_);
      validationStatusLabel_ = new QLabel("", this);
      validationStatusLabel_->setWordWrap(true);
      rightCol->addWidget(validationStatusLabel_);
      validationTable_ = new QTableWidget(0, 5, this);
      validationTable_->setHorizontalHeaderLabels(
          {"组", "T (s)", "波幅 (m)", "标准 RAO", "学生 RAO"});
      validationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
      validationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
      validationTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
      validationTable_->setMaximumHeight(150);
      rightCol->addWidget(validationTable_);
      exportValidationBtn_ = new QPushButton("导出验收数据", this);
      rightCol->addWidget(exportValidationBtn_);
      rightCol->addStretch();
      algoRow->addLayout(rightCol, 2);
      root->addLayout(algoRow);

      raoCurve_ = new RaoCurveWidget(this);
      root->addWidget(raoCurve_);

      roundResultLabel_->setObjectName("roundResultLabel");
      submitValidationBtn_->setObjectName("submitValidationBtn");
      validationStatusLabel_->setObjectName("validationStatusLabel");
      validationTable_->setObjectName("validationTable");
      exportValidationBtn_->setObjectName("exportValidationBtn");
      raoCurve_->setObjectName("raoCurve");
      connect(algoPanel_, &StudentAlgorithmPanel::studentRaoReady, this,
              &WaveResponseDialog::onStudentRaoReady);
      connect(submitValidationBtn_, &QPushButton::clicked, this,
              &WaveResponseDialog::onSubmitValidation);
      connect(exportValidationBtn_, &QPushButton::clicked, this,
              &WaveResponseDialog::onExportValidation);
    }
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
    connect(importGmBtn_, &QPushButton::clicked, this, &WaveResponseDialog::onImportGm);
  }

  std::cout << tag
            << ": 波浪响应对话框已创建（核心层 WaveResponseCore）" << std::endl;
}

WaveResponseDialog::~WaveResponseDialog() = default;

void WaveResponseDialog::syncParamsToCore() {
  core_->setTarget(modelCombo_->currentText().toStdString());
}

void WaveResponseDialog::onMooringToggled(bool _on) {
  syncParamsToCore();
  core_->setMooringEnabled(_on);
  if (_on) {
    mooringTimer_->start();
    mooringStatusLabel_->setText("系泊状态：请求中（等待物理端确认）…");
    mooringStatusLabel_->setStyleSheet("color: #8a6d00;");
  } else {
    mooringTimer_->stop();
    mooringStatusLabel_->setText("系泊状态：未启用（船模可自由漂移/偏航）");
    mooringStatusLabel_->setStyleSheet("color: #8a6d00;");
  }
}

void WaveResponseDialog::onMooringHeartbeat() {
  core_->publishMooringHeartbeat();
  updateMooringStatusLabel();
}

void WaveResponseDialog::updateMooringStatusLabel() {
  if (!mooringCheck_ || !mooringStatusLabel_) return;
  if (!mooringCheck_->isChecked()) {
    mooringStatusLabel_->setText("系泊状态：未启用（船模可自由漂移/偏航）");
    mooringStatusLabel_->setStyleSheet("color: #8a6d00;");
    return;
  }
  const auto st = core_->mooringStatus();
  if (!st.found) {
    mooringStatusLabel_->setText("系泊状态：请求中（等待物理端确认）…");
    mooringStatusLabel_->setStyleSheet("color: #8a6d00;");
    return;
  }
  if (!core_->mooringApplied()) {
    mooringStatusLabel_->setText(
        QString("系泊状态：未生效（%1）。请确认目标船模已加载且仿真正在运行。")
            .arg(QString::fromStdString(st.reason)));
    mooringStatusLabel_->setStyleSheet("color: #b03030;");
    return;
  }
  mooringStatusLabel_->setText(
      QString("系泊状态：已生效（位置误差 %1 m，航向误差 %2°%3）")
          .arg(st.positionError, 0, 'f', 3)
          .arg(st.yawError * 180.0 / M_PI, 0, 'f', 2)
          .arg(st.forceSaturated || st.yawSaturated ? "，已限幅" : ""));
  mooringStatusLabel_->setStyleSheet("color: #2e7d32;");
}

void WaveResponseDialog::onShowRaoHelp() {
  const bool ms03 = (mode_ == "ms03");
  const bool ms06 = (mode_ == "ms06");
  QString title = ms06 ? "MS-06 · RAO 说明"
                       : (ms03 ? "MS-03 · RAO 说明" : "MS-02 · 响应与 RAO 说明");
  QString modeText;
  if (ms03 || ms06) {
    modeText = "本实验测的是<b>横摇</b>响应（波浪方向默认 90° 横浪）。";
  } else {
    modeText = "本实验主要观察<b>升沉与纵摇</b>响应（波浪方向默认 0° 顺浪）；"
               "主结果区显示幅值，不直接显示 RAO 数值（命令行入口会输出纵摇/升沉 RAO）。";
  }
  const QString html = QString(
      "<h3>教学直观</h3>"
      "<p>RAO（响应幅值算子）回答一个问题：<b>单位波幅的规则波，能让船体产生多大响应？</b>"
      "把波浪周期逐次改变，就能看出船在哪个周期附近“晃得最厉害”。</p>"
      "%1"
      "<p><b>为什么需要软系泊？</b>船在波浪里会缓慢漂移、甚至被推着转向。"
      "船头一转向，同一列波浪相对船的入射角就变了，测量的已经不是同一个工况，"
      "多轮数据无法互相比较。勾选软系泊后，平台只把<b>平均水平位置与航向</b>拉回参考值，"
      "<b>横摇、纵摇、升沉仍然自由</b>——要观察的响应不会被锁死。</p>"
      "<p><b>注意</b>：软系泊会轻微改变船体动力学，系泊与自由漂移两种条件下的数据"
      "应分别标注、不要混在一起比较。</p>"
      "<h3>技术定义</h3>"
      "<p>对指定自由度 j、航向条件 β 与遭遇频率 ω<sub>e</sub>，复传递函数可写为"
      " H<sub>j</sub>(ω<sub>e</sub>,β)=X̂<sub>j</sub>/â；本平台给出的 RAO 是"
      "<b>幅值型</b> RAO，即稳态响应幅值与有效输入波幅之比："
      " roll/pitch 为 °/m，heave 为 m/m。<b>本界面不输出相位</b>。</p>"
      "<p>测量前提：① 记录段处于<b>稳态</b>（排除起振瞬态）；② 波幅不宜大到明显进入"
      "强非线性；③ <b>入射方向与航向固定</b>——自由漂移/偏航会使 β 随时间变化，"
      "破坏“单一工况”假设。</p>"
      "<p><b>关于峰值</b>：RAO 峰值表示在该模型、航向、阻尼与频率条件下响应达到最大，"
      "通常提示<b>共振区</b>；但峰值周期<b>不等于</b>简化理论固有周期 "
      "T₀=2π√(Ixx/(Δ·GM))。T₀ 只能作为对照量，差异还可能来自阻尼、附加质量与耦合。</p>"
      "<p>记录建议：每轮至少 max(40 s, 6×输入周期) 仿真秒，指标取末半段；"
      "同一周期建议重复 2~3 次并取中位。</p>")
      .arg(modeText);
  QMessageBox box(this);
  box.setWindowTitle(title);
  box.setTextFormat(Qt::RichText);
  box.setText(html);
  box.setStandardButtons(QMessageBox::Ok);
  box.exec();
}

void WaveResponseDialog::onApplyWave() {
  syncParamsToCore();
  core_->sendWave(waveAmpSpin_->value(), wavePeriodSpin_->value(),
                  gainSpin_->value(),
                  directionSpin_->value() * M_PI / 180.0);
  std::cout << (mode_ == "ms06" ? "MS-06" : (mode_ == "ms03" ? "MS-03" : "MS-02"))
            << ": 已施加规则波（波幅 " << waveAmpSpin_->value()
            << " m / 周期 " << wavePeriodSpin_->value() << " s）" << std::endl;
}

void WaveResponseDialog::onStart() {
  if (core_->running()) return;
  syncParamsToCore();
  if (mooringCheck_ && mooringCheck_->isChecked() && !core_->mooringApplied()) {
    updateMooringStatusLabel();
    QMessageBox::information(this, "软系泊尚未生效",
        "已勾选软系泊，但物理端尚未确认生效。\n\n"
        "请确认：① 目标船模已加载（含浮力插件）；② 仿真未暂停；"
        "③ 状态行显示「系泊状态：已生效」。\n\n"
        "本次记录未启动。若只想做自由响应观察，可取消勾选后重新开始。");
    return;
  }
  if (!freeRunWarned_ && (mode_ == "ms03" || mode_ == "ms06") &&
      (!mooringCheck_ || !mooringCheck_->isChecked())) {
    const auto ret = QMessageBox::question(this, "未启用软系泊",
        "当前未启用软系泊：船模可能漂移或偏航，入射角会随航向改变。\n\n"
        "本轮适合观察自由响应；不保证跨轮定量 RAO 可比，"
        "也不应把该数据用于定量 T₀ 对照。\n\n是否继续记录？",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;
    freeRunWarned_ = true;
  }
  if (!core_->start()) {
    QMessageBox::warning(this, "实验",
        "未能读到船模位姿或仿真时钟 —— 请确认「船模」名与场景一致，且仿真正在运行。");
    return;
  }
  core_->sendWave(waveAmpSpin_->value(), wavePeriodSpin_->value(),
                  gainSpin_->value(),
                  directionSpin_->value() * M_PI / 180.0);
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
  resultLabel_->setText((mode_ == "ms03" || mode_ == "ms06")
      ? "横摇幅值 = —   横摇 RAO = —   受迫周期 = —"
      : "横摇幅值 = —   纵摇幅值 = —   升沉幅值 = —");
  if (raoTable_) {
    raoTable_->setRowCount(0);
    recordBtn_->setEnabled(false);
  }
  core_->resetMooringSession();
  rounds_.clear();
  roundPeriods_.clear();
  roundAmps_.clear();
  roundMooring_.clear();
  refRao_.clear();
  resetStudentState();
  updateGmCompare();
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
  if (mooringCheck_ && mooringCheck_->isChecked())
    updateMooringStatusLabel();
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
  if (mode_ == "ms03" || mode_ == "ms06") {
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
  const bool moored = core_->mooringApplied();
  raoTable_->setItem(row, 4, new QTableWidgetItem(moored ? "已系泊" : "自由"));
  recordBtn_->setEnabled(false);   ///< 一次停止只记一行，防重复
  std::cout << "MS-03: 已记录 RAO 点（周期 " << m.wavePeriod
            << " s，RAO " << m.rollRAO << " °/m）" << std::endl;
  if (mode_ == "ms06") {
    // 收集本轮数据（下一轮 start 会 clear trail，须先快照）
    addRound(core_->trail(), m.wavePeriod, m.waveAmp, core_->mooringApplied(),
             m.rollRAO);
  }
  updateGmCompare();
}

void WaveResponseDialog::onDeleteRow() {
  const int row = raoTable_->currentRow();
  if (row < 0) {
    QMessageBox::information(this, "删除", "请先在表中选中一行。");
    return;
  }
  raoTable_->removeRow(row);
  std::cout << "MS-03: 已删除 RAO 表第 " << row + 1 << " 行" << std::endl;
  updateGmCompare();
}

bool WaveResponseDialog::WriteRaoTableCsv(const QTableWidget *_table,
                                          const std::string &_path) {
  if (!_table || _table->rowCount() == 0) return false;
  std::ofstream f(_path);
  if (!f) return false;
  f << "wave_period_s,roll_amp_deg,roll_rao_deg_per_m,forced_period_s,mooring_enabled\n";
  for (int r = 0; r < _table->rowCount(); ++r) {
    f << _table->item(r, 0)->text().toStdString() << ","
      << _table->item(r, 1)->text().toStdString() << ","
      << _table->item(r, 2)->text().toStdString() << ","
      << _table->item(r, 3)->text().toStdString() << ","
      << (_table->columnCount() > 4 && _table->item(r, 4)
              ? (_table->item(r, 4)->text() == "已系泊" ? "true" : "false")
              : "unknown")
      << "\n";
  }
  return true;
}

void WaveResponseDialog::onExportRao() {
  if (!raoTable_ || raoTable_->rowCount() == 0) {
    QMessageBox::information(this, "导出 RAO 表", "尚无记录。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出 RAO–周期表 CSV",
      "ms03_rao.csv", "CSV (*.csv)");
  if (path.isEmpty()) return;
  if (!WriteRaoTableCsv(raoTable_, path.toStdString()))
    QMessageBox::warning(this, "导出 RAO 表", "无法写入文件。");
  else
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

void WaveResponseDialog::onImportGm()
{
  QString path = QFileDialog::getOpenFileName(this, "导入 HS-04 GM 交付物",
      "hs04_gm.json", "JSON (*.json)");
  if (path.isEmpty()) return;
  keship::GmDeliverable d;
  std::string err;
  if (!keship::ReadGmDeliverable(path.toStdString(), d, &err) ||
      !keship::ValidGmDeliverable(d, &err)) {
    QMessageBox::warning(this, "导入 GM 交付物",
        QString::fromStdString("导入失败：" + err));
    return;
  }
  gmDeliverable_ = d;
  hasGm_ = true;
  ensureObjectFromDeliverable(d);
  std::cout << "MS-03: 已导入 GM 交付物（模型 \"" << d.model
            << "\"，GM实验 " << d.gmExperiment << " m，Ixx "
            << d.ixxKgM2 << " kg·m²）" << std::endl;
  updateGmCompare();
}

bool WaveResponseDialog::ensureObjectFromDeliverable(
    const keship::GmDeliverable &_d) {
  const auto &obj = _d.object;
  if (obj.type == "ksm_model") {
    const std::string f = obj.ksmFile.empty() ? obj.name : obj.ksmFile;
    QMessageBox::information(this, "交付物船模",
        QString("该交付物的实验对象是 ksm 船模「%1」。\n"
                "请先用主界面「加载 ▾ → 载入模型」加载它，"
                "再重新打开本对话框并选择该模型。")
            .arg(QString::fromStdString(f)));
    return false;
  }
  // 数学船型：按交付物参数重建同一对象（场景已有同名对象则直接使用）
  double L = obj.lengthM, B = obj.breadthM, H = obj.heightM;
  double rho = obj.densityKgM3;
  if (!(L > 0) || !(B > 0) || !(H > 0)) {
    L = _d.lengthM; B = _d.breadthM; H = _d.heightM;
  }
  if (!(rho > 0)) rho = _d.densityKgM3;
  if (!(L > 0) || !(B > 0) || !(H > 0) || !(rho > 0)) {
    std::cout << "MS-03: 交付物缺船型参数，无法重建对象（沿用当前选择）"
              << std::endl;
    return false;
  }
  const std::string name = obj.name.empty() ? "SP02_barge" : obj.name;
  const std::string sdf =
      keship::MakeBargeSDF(name, L, B, H, rho, 1000.0, 9.81);
  gz::msgs::EntityFactory req;
  req.set_name(name);
  req.set_allow_renaming(false);
  req.set_sdf(sdf);
  gz::msgs::Boolean rep;
  bool result = false;
  const bool ok = node_.Request(
      "/world/" + worldName_ + "/create/blocking", req, 20000, rep, result);
  if (ok && result) {
    std::cout << "MS-03: 已按交付物生成实验对象「" << name << "」（L="
              << L << " B=" << B << " H=" << H << "）" << std::endl;
  } else {
    std::cout << "MS-03: 同名对象已存在或生成失败 —— 直接使用场景中的「"
              << name << "」" << std::endl;
  }
  objectName_ = name;
  if (modelCombo_) {
    int idx = modelCombo_->findText(QString::fromStdString(name));
    if (idx < 0) {
      modelCombo_->addItem(QString::fromStdString(name));
      idx = modelCombo_->findText(QString::fromStdString(name));
    }
    modelCombo_->setCurrentIndex(idx);
  }
  return true;
}

void WaveResponseDialog::updateGmCompare()
{
  if (!gmCompareLabel_) return;
  if (!hasGm_) {
    gmCompareLabel_->setText(
        "理论横摇固有周期 T₀：尚未导入 HS-04 的 GM 交付物\n"
        "（点「导入 GM 交付物」选择 hs04_gm.json；"
        "T₀ = 2π√(Ixx/(Δ·GM)) 由 HS-04 的初稳性高、排水量与纵向转动惯量算出）");
    return;
  }
  const double t0 = keship::TheoreticalRollPeriod(gmDeliverable_);
  if (t0 < 0.0) {
    gmCompareLabel_->setText(
        "理论横摇固有周期 T₀：交付物数据不足（缺 Ixx 或排水量），无法计算");
    return;
  }
  // 实测峰值周期：RAO 表中 RAO 最大行对应的波浪周期
  double peakPeriod = -1.0, peakRao = -1.0;
  for (int r = 0; r < raoTable_->rowCount(); ++r) {
    const double rao = raoTable_->item(r, 2)->text().toDouble();
    if (rao > peakRao) {
      peakRao = rao;
      peakPeriod = raoTable_->item(r, 0)->text().toDouble();
    }
  }
  const bool useExp = gmDeliverable_.gmExperiment > 0.0;
  const double gm = useExp ? gmDeliverable_.gmExperiment
                             : gmDeliverable_.gmTheory;
  const char *gmSrc = useExp ? "GM实验" : "GM理论";
  const QString objName = QString::fromStdString(
      objectName_.empty() ? gmDeliverable_.object.name : objectName_);
  QString txt = QString("理论横摇固有周期 T₀ = 2π√(Ixx/(Δ·GM)) = %1 s"
                        "%6\n"
                        "　　（来源：HS-04 交付物 %2 = %3 m，Δ = %4 kg，"
                        "Ixx = %5 kg·m²）")
                    .arg(t0, 0, 'f', 2)
                    .arg(gmSrc)
                    .arg(gm, 0, 'f', 4)
                    .arg(gmDeliverable_.displacementKg, 0, 'f', 0)
                    .arg(gmDeliverable_.ixxKgM2, 0, 'f', 1)
                    .arg(objName.isEmpty()
                             ? QString()
                             : QString("　实验对象：%1").arg(objName));
  if (peakPeriod > 0.0) {
    const double dev = (peakPeriod - t0) / t0 * 100.0;
    txt += QString("\n实测峰值周期 = %1 s（RAO 表最大值处）｜ 相对偏差 = %2%")
               .arg(peakPeriod, 0, 'f', 2)
               .arg(dev, 0, 'f', 1);
  } else {
    txt += "\n实测峰值周期：尚无 RAO 记录 —— 请先在下方逐周期记录"
           "（改波浪周期 → 记录 ≥40 仿真秒 → 停止 →「记录本次结果」），"
           "再回到本行看实测值与 T₀ 的对比";
  }
  gmCompareLabel_->setText(txt);
}

// ══════════════════════════════════════════════════════════════
// MS-06 v2：学生算法 / RAO–T 曲线 / 隐藏验收
// ══════════════════════════════════════════════════════════════

void WaveResponseDialog::addRound(const std::vector<keship::WaveSample> &_trail,
                                  double _period, double _waveAmp, bool _mooring,
                                  double _refRao) {
  rounds_.push_back(_trail);
  roundPeriods_.push_back(_period);
  roundAmps_.push_back(_waveAmp);
  roundMooring_.push_back(_mooring);
  refRao_.push_back(_refRao);
  studentRao_.push_back(-1.0);  // 本轮尚未运行
  if (algoPanel_) algoPanel_->SetRound(_trail, _waveAmp, _period);
  if (roundResultLabel_)
    roundResultLabel_->setText(
        QString("第 %1 轮已就绪（T = %2 s）：运行学生代码后显示学生/内置对比")
            .arg(static_cast<int>(rounds_.size()))
            .arg(_period, 0, 'f', 2));
  updateCurve();
  std::cout << "MS-06: 已收集第 " << rounds_.size() << " 轮（周期 " << _period
            << " s）" << std::endl;
}

void WaveResponseDialog::AddRoundForTest(const std::vector<keship::WaveSample> &_trail,
                                         double _period, double _waveAmp) {
  const auto m = keship::WaveResponseCore::computeMetrics(_trail, _waveAmp, _period);
  addRound(_trail, _period, _waveAmp, false, m.rollRAO);
}

void WaveResponseDialog::onStudentRaoReady(double _rao) {
  if (rounds_.empty()) return;
  const size_t idx = rounds_.size() - 1;
  if (idx < studentRao_.size()) studentRao_[idx] = _rao;
  const double ref = refRao_[idx];
  const double err = keship::StudentCodeRunner::ErrorPct(_rao, ref);
  const int v = keship::StudentCodeRunner::Verdict(_rao, ref);
  const QString verdict = (v == 0) ? "通过" : (v == 1 ? "接近" : "不通过");
  if (roundResultLabel_)
    roundResultLabel_->setText(
        QString("第 %1 轮：学生 RAO = %2 °/m ｜ 内置 RAO = %3 °/m ｜ 误差 %4% ｜ %5")
            .arg(static_cast<int>(idx + 1))
            .arg(_rao, 0, 'f', 4)
            .arg(ref, 0, 'f', 4)
            .arg(err, 0, 'f', 2)
            .arg(verdict));
  updateCurve();
}

void WaveResponseDialog::updateCurve() {
  if (!raoCurve_) return;
  std::vector<std::pair<double, double>> b, s;
  for (size_t i = 0; i < rounds_.size(); ++i) {
    if (i < refRao_.size()) b.emplace_back(roundPeriods_[i], refRao_[i]);
    if (i < studentRao_.size() && studentRao_[i] > 0)
      s.emplace_back(roundPeriods_[i], studentRao_[i]);
  }
  raoCurve_->SetData(b, s);
}

void WaveResponseDialog::resetStudentState() {
  studentRao_.clear();
  hiddenCases_.clear();
  hiddenStudent_.clear();
  validationRunning_ = false;
  validationIndex_ = 0;
  if (validationTable_) validationTable_->setRowCount(0);
  if (validationStatusLabel_) validationStatusLabel_->clear();
  if (roundResultLabel_)
    roundResultLabel_->setText("本轮：运行学生代码后显示（内置值运行前不显示）");
  if (raoCurve_) raoCurve_->Clear();
}

void WaveResponseDialog::onSubmitValidation() {
  if (validationRunning_) return;
  if (!algoPanel_ || !algoPanel_->HasRound()) {
    QMessageBox::information(this, "提交验收", "请先记录一轮数据并写好算法。");
    return;
  }
  const QString code = algoPanel_->CurrentCode();
  if (code.trimmed().isEmpty()) return;
  hiddenCases_ = keship::GenerateHiddenCases(
      5, static_cast<unsigned>(std::random_device{}()));
  hiddenStudent_.assign(hiddenCases_.size(), -1.0);
  validationIndex_ = 0;
  validationRunning_ = true;
  if (validationTable_) validationTable_->setRowCount(0);
  if (submitValidationBtn_) submitValidationBtn_->setEnabled(false);
  if (validationStatusLabel_)
    validationStatusLabel_->setText(
        QString("验收中…（共 %1 组，过程隐藏数据）")
            .arg(static_cast<int>(hiddenCases_.size())));
  startNextHiddenCase();
}

void WaveResponseDialog::startNextHiddenCase() {
  if (validationIndex_ >= static_cast<int>(hiddenCases_.size())) {
    // 完成：公开全部结果
    validationRunning_ = false;
    if (submitValidationBtn_) submitValidationBtn_->setEnabled(true);
    int pass = 0;
    if (validationTable_) validationTable_->setRowCount(0);
    for (size_t i = 0; i < hiddenCases_.size(); ++i) {
      const auto &c = hiddenCases_[i];
      const double stu = hiddenStudent_[i];
      const double err =
          stu > 0 ? keship::StudentCodeRunner::ErrorPct(stu, c.refRao) : -1.0;
      const bool ok = (err >= 0.0 && err <= 5.0);
      if (ok) ++pass;
      if (validationTable_) {
        const int row = validationTable_->rowCount();
        validationTable_->insertRow(row);
        auto set = [&](int col, const QString &s) {
          validationTable_->setItem(row, col, new QTableWidgetItem(s));
        };
        set(0, QString::number(static_cast<int>(i) + 1));
        set(1, QString::number(c.period, 'f', 2));
        set(2, QString::number(c.waveAmp, 'f', 2));
        set(3, QString::number(c.refRao, 'f', 3));
        set(4, stu > 0 ? QString::number(stu, 'f', 3) : QString("—"));
      }
    }
    if (validationStatusLabel_)
      validationStatusLabel_->setText(
          QString("验收完成：%1/%2 组通过（≤5%）%3")
              .arg(pass)
              .arg(static_cast<int>(hiddenCases_.size()))
              .arg(pass == static_cast<int>(hiddenCases_.size()) ? " PASS" : " FAIL"));
    return;
  }
  const auto &c = hiddenCases_[validationIndex_];
  if (validationStatusLabel_)
    validationStatusLabel_->setText(
        QString("验收中… 第 %1/%2 组（数据隐藏）")
            .arg(validationIndex_ + 1)
            .arg(static_cast<int>(hiddenCases_.size())));
  keship::WriteRoundDataCsv(c.trail, c.waveAmp, keship::kRoundDataPath);
  keship::StudentCodeRunner::RunAsync(
      this, algoPanel_->IsPython() ? "python" : "c", algoPanel_->CurrentCode(),
      [this](const keship::StudentRunResult &_r) {
        if (_r.ok && _r.valueOk &&
            validationIndex_ < static_cast<int>(hiddenStudent_.size()))
          hiddenStudent_[validationIndex_] = _r.value;
        ++validationIndex_;
        startNextHiddenCase();
      });
}

void WaveResponseDialog::onExportValidation() {
  if (hiddenCases_.empty()) {
    QMessageBox::information(this, "导出验收数据", "尚无验收数据（请先提交验收）。");
    return;
  }
  QString dir = QFileDialog::getExistingDirectory(this, "选择导出目录");
  if (dir.isEmpty()) return;
  const int written = WriteValidationCases(dir.toStdString(), hiddenCases_);
  const int total = static_cast<int>(hiddenCases_.size());
  QMessageBox::information(
      this, "导出验收数据",
      written == total
          ? QString("已导出各组时程 CSV。")
          : QString("已导出 %1/%2 组（部分写入失败）。")
                .arg(written)
                .arg(total));
}

int WaveResponseDialog::WriteValidationCases(
    const std::string &_dir,
    const std::vector<keship::HiddenWaveCase> &_cases) {
  int written = 0;
  for (size_t i = 0; i < _cases.size(); ++i) {
    const QString path = QString("%1/hidden_case_%2.csv")
                             .arg(QString::fromStdString(_dir))
                             .arg(static_cast<int>(i) + 1);
    std::ofstream f(path.toStdString());
    if (!f) continue;
    f << "t_s,roll_deg\n";
    for (const auto &s : _cases[i].trail) f << s.t << "," << s.rollDeg << "\n";
    ++written;
  }
  return written;
}
