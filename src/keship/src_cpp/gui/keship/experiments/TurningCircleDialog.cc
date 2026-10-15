/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-01 回转性观测 — 对话框实现（薄壳：界面 + 委托核心层） */

#include "TurningCircleDialog.hh"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {
constexpr double kDeg2Rad = M_PI / 180.0;
}

// ══════════════════════════════════════════════════════════════════
// 轨迹图
// ══════════════════════════════════════════════════════════════════
TurningChartWidget::TurningChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(320);
  setAutoFillBackground(true);
}

void TurningChartWidget::setTrail(const std::vector<keship::TurningSample> &_pts) {
  pts_ = _pts;
  if (!pts_.empty()) {
    xMin_ = xMax_ = pts_[0].x;
    yMin_ = yMax_ = pts_[0].y;
    for (const auto &p : pts_) {
      xMin_ = std::min(xMin_, p.x); xMax_ = std::max(xMax_, p.x);
      yMin_ = std::min(yMin_, p.y); yMax_ = std::max(yMax_, p.y);
    }
    const double dx = std::max(1.0, xMax_ - xMin_), dy = std::max(1.0, yMax_ - yMin_);
    const double d = std::max(dx, dy) * 0.1;
    xMin_ -= d; xMax_ += d; yMin_ -= d; yMax_ += d;
  }
  update();
}

void TurningChartWidget::clearTrail() {
  pts_.clear();
  xMin_ = yMin_ = -10; xMax_ = yMax_ = 10;
  update();
}

void TurningChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (pts_.size() < 2) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "尚无轨迹 — 点「开始」施加差动推力");
    return;
  }

  // ── 坐标区（四周留出刻度标签空间） ──────────────────────────
  const double mL = 62, mR = 14, mT = 26, mB = 50;
  const double W = width() - mL - mR, H = height() - mT - mB;
  if (W < 80 || H < 60) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter, "窗口过小 — 无法绘制坐标轴");
    return;
  }

  // ★ 长宽比封顶 4:1 —— 极扁轨迹（如等推力直航 74 m × 2 m）若直接按 min 缩放，
  //   坐标框会退化成细条、刻度全挤在一起。此时按需扩展"短边"范围（仍是**同比例**），
  //   刻度值仍如实反映真实坐标，不会被拉宽误导。
  double axMin = xMin_, axMax = xMax_, ayMin = yMin_, ayMax = yMax_;
  constexpr double kMaxAspect = 4.0;
  for (int it = 0; it < 8; ++it) {
    const double rx = axMax - axMin, ry = ayMax - ayMin;
    if (rx > ry * kMaxAspect) {
      const double cy = 0.5 * (ayMin + ayMax);
      const double half = 0.5 * rx / kMaxAspect;
      ayMin = cy - half; ayMax = cy + half;
    } else if (ry > rx * kMaxAspect) {
      const double cx = 0.5 * (axMin + axMax);
      const double half = 0.5 * ry / kMaxAspect;
      axMin = cx - half; axMax = cx + half;
    } else {
      break;
    }
  }

  const double rangeX = axMax - axMin, rangeY = ayMax - ayMin;
  // ★ X / Y **同比例**（min 缩放，框在坐标区内居中）
  const double sScale = std::min(W / rangeX, H / rangeY);
  const double pw = rangeX * sScale, ph = rangeY * sScale;
  const double boxL = mL + (W - pw) * 0.5;
  const double boxB = mT + H - (H - ph) * 0.5;
  const double boxT = boxB - ph;

  auto toPix = [&](double x, double y) {
    return QPointF(boxL + (x - axMin) * sScale, boxB - (y - ayMin) * sScale);
  };

  // ── "好看的"刻度步长（1 / 2 / 5 × 10ⁿ） ────────────────────
  auto niceStep = [](double range, int target) {
    if (!(range > 0.0) || !std::isfinite(range)) return 1.0;
    const double raw = range / static_cast<double>(target);
    const double mag = std::pow(10.0, std::floor(std::log10(raw)));
    const double n = raw / mag;
    double m = 1.0;
    if (n > 5.0) m = 10.0; else if (n > 2.0) m = 5.0; else if (n > 1.0) m = 2.0;
    return m * mag;
  };
  // ★ X/Y **同比例**：网格使用**同一步长** ⇒ 屏幕上的格子是正方形，
  //   回转轨迹（如定常圆）与真实几何一致，形状一眼可辨。
  const double stepCommon = niceStep(std::max(rangeX, rangeY), 6);
  const double stepX = stepCommon;
  const double stepY = stepCommon;
  auto fmtTick = [](double v, double step) {
    int dec = 0;
    if (step < 1.0)
      dec = std::min(3, static_cast<int>(std::ceil(-std::log10(step))));
    return QString::number(v, 'f', dec);
  };

  QFont tickFont = p.font();
  tickFont.setPointSize(8);

  // ── 网格线 + 刻度 + 刻度值标签 ─────────────────────────────
  {
    int guard = 0;
    for (double v = std::ceil(axMin / stepX) * stepX;
         v <= axMax + 1e-9 && guard < 200; v += stepX, ++guard) {
      const double gx = toPix(v, 0).x();
      p.setPen(QColor(232, 235, 242));
      p.drawLine(QPointF(gx, boxT), QPointF(gx, boxB));
      p.setPen(QColor(126, 133, 148));
      p.drawLine(QPointF(gx, boxB), QPointF(gx, boxB + 5));
      p.setFont(tickFont);
      p.setPen(QColor(90, 95, 105));
      p.drawText(QRectF(gx - 44, boxB + 6, 88, 16),
                 Qt::AlignHCenter | Qt::AlignTop, fmtTick(v, stepX));
    }
    guard = 0;
    for (double v = std::ceil(ayMin / stepY) * stepY;
         v <= ayMax + 1e-9 && guard < 200; v += stepY, ++guard) {
      const double gy = toPix(0, v).y();
      p.setPen(QColor(232, 235, 242));
      p.drawLine(QPointF(boxL, gy), QPointF(boxL + pw, gy));
      p.setPen(QColor(126, 133, 148));
      p.drawLine(QPointF(boxL - 5, gy), QPointF(boxL, gy));
      p.setFont(tickFont);
      p.setPen(QColor(90, 95, 105));
      p.drawText(QRectF(boxL - 52, gy - 8, 46, 16),
                 Qt::AlignRight | Qt::AlignVCenter, fmtTick(v, stepY));
    }
  }

  // ── 坐标轴外框 + x 轴标题（单位由表头统一给出，避免竖排溢出） ──
  p.setPen(QPen(QColor(120, 128, 142), 1));
  p.drawRect(QRectF(boxL, boxT, pw, ph).adjusted(0, 0, -1, -1));
  p.setFont(tickFont);
  p.setPen(QColor(70, 76, 88));
  p.drawText(QRectF(boxL, boxB + 24, pw, 14),
             Qt::AlignHCenter | Qt::AlignTop, "x (m)");

  QPainterPath path;
  path.moveTo(toPix(pts_[0].x, pts_[0].y));
  for (size_t i = 1; i < pts_.size(); ++i)
    path.lineTo(toPix(pts_[i].x, pts_[i].y));
  p.setPen(QPen(QColor(60, 110, 200), 2));
  p.drawPath(path);

  p.setBrush(QColor(50, 160, 90));
  p.setPen(Qt::NoPen);
  p.drawEllipse(toPix(pts_.front().x, pts_.front().y), 5, 5);
  p.setBrush(QColor(210, 80, 70));
  p.drawEllipse(toPix(pts_.back().x, pts_.back().y), 5, 5);

  const auto &last = pts_.back();
  const double yawRad = last.yawDeg * kDeg2Rad;
  const QPointF c = toPix(last.x, last.y);
  p.setPen(QPen(QColor(210, 80, 70), 2));
  p.drawLine(c, c + QPointF(std::cos(yawRad) * 22, -std::sin(yawRad) * 22));

  p.setPen(QColor(90, 95, 105));
  p.setFont(tickFont);
  p.drawText(10, 12, QString("回转轨迹 · %1 点 · 单位 m（X/Y 同比例）").arg(pts_.size()));
  p.drawText(QRectF(width() - 206, 12, 196, 14),
             Qt::AlignRight | Qt::AlignTop,
             QString("网格 每格 %1 m").arg(stepCommon, 0, 'g', 4));
}

// ══════════════════════════════════════════════════════════════════
// 对话框
// ══════════════════════════════════════════════════════════════════
TurningCircleDialog::TurningCircleDialog(gz::transport::Node &_transportNode,
                                         const std::string &_worldName,
                                         const QStringList &_models,
                                         QWidget *_parent)
    : QDialog(_parent) {
  setWindowTitle("MS-01 回转性观测");
  resize(980, 760);

  core_ = std::make_unique<keship::TurningExperimentCore>(_transportNode, _worldName);

  auto *root = new QVBoxLayout(this);

  auto *paramBox = new QGroupBox("回转参数（⚠ 平台不含舵：用双推进器差动替代舵角）", this);
  auto *form = new QFormLayout(paramBox);

  modelCombo_ = new QComboBox(paramBox);
  modelCombo_->addItems(_models.isEmpty() ? QStringList{"GT1"} : _models);
  form->addRow("船模：", modelCombo_);

  leftNameEdit_ = new QLineEdit("thrust_left", paramBox);
  rightNameEdit_ = new QLineEdit("thrust_right", paramBox);
  form->addRow("左推进器名：", leftNameEdit_);
  form->addRow("右推进器名：", rightNameEdit_);

  periodSpin_ = new QDoubleSpinBox(paramBox);
  periodSpin_->setRange(0.02, 2.0); periodSpin_->setValue(0.1);
  periodSpin_->setSingleStep(0.02); periodSpin_->setSuffix(" s");
  form->addRow("采样周期：", periodSpin_);
  root->addWidget(paramBox);

  auto *stageBox = new QGroupBox("分阶段推力计划", this);
  auto *stageLayout = new QVBoxLayout(stageBox);
  stageTable_ = new QTableWidget(stageBox);
  stageTable_->setColumnCount(5);
  stageTable_->setHorizontalHeaderLabels({"阶段", "时长 (仿真 s)", "左推力 (N)", "右推力 (N)", "指标起点"});
  stageTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  stageTable_->verticalHeader()->setVisible(false);
  stageTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  stageTable_->setSelectionMode(QAbstractItemView::SingleSelection);
  stageTable_->setMinimumHeight(110);
  // 「指标起点」术语较抽象 —— 表头加悬浮说明，表下再给一行常驻解释（2026-10-15 张老师反馈）
  if (auto *h = stageTable_->horizontalHeaderItem(4))
    h->setToolTip("指标起点 = 计量起点。\n"
                  "勾选的那一段开始，才计入「进距 / 战术直径 / 回转直径」；\n"
                  "它前面的阶段只作预备（例如先直航把船加到稳定航速），不计入指标。\n"
                  "必须且只能勾选一段。");
  stageLayout->addWidget(stageTable_);
  auto *stageHint = new QLabel(
      "「时长」填 0 = 持续到手动停止（只允许最后一段）。"
      "「指标起点」= 计量起点：勾选的那一段开始，才计入 进距 / 战术直径 / 回转直径；"
      "前面的阶段只作预备（例如先直航建立航速），不计入指标。必须且只能勾选一段。", stageBox);
  stageHint->setWordWrap(true);
  stageHint->setStyleSheet("color: #5a6070;");
  stageLayout->addWidget(stageHint);
  auto *stageButtons = new QHBoxLayout();
  addStageBtn_ = new QPushButton("增加阶段", stageBox);
  removeStageBtn_ = new QPushButton("删除选中阶段", stageBox);
  stageButtons->addWidget(addStageBtn_);
  stageButtons->addWidget(removeStageBtn_);
  stageButtons->addStretch();
  stageLayout->addLayout(stageButtons);
  root->addWidget(stageBox);
  syncTableFromStages();

  auto *ctrlRow = new QHBoxLayout();
  startBtn_ = new QPushButton("开始", this);
  stopBtn_ = new QPushButton("停止", this);
  clearBtn_ = new QPushButton("清零", this);
  exportBtn_ = new QPushButton("导出 CSV", this);
  stopBtn_->setEnabled(false);
  ctrlRow->addWidget(startBtn_); ctrlRow->addWidget(stopBtn_); ctrlRow->addWidget(clearBtn_);
  ctrlRow->addStretch(); ctrlRow->addWidget(exportBtn_);
  root->addLayout(ctrlRow);

  readoutLabel_ = new QLabel("t = —   x = —   y = —   航向 = —   速度 = —", this);
  resultLabel_ = new QLabel("进距 = —   战术直径 = —   回转直径 = —", this);
  readoutLabel_->setStyleSheet("font-family: monospace;");
  resultLabel_->setStyleSheet("font-family: monospace; font-weight: bold;");
  root->addWidget(readoutLabel_);
  root->addWidget(resultLabel_);

  chart_ = new TurningChartWidget(this);
  root->addWidget(chart_, 1);

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &TurningCircleDialog::onTick);
  connect(startBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onStop);
  connect(clearBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onClear);
  connect(exportBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onExport);
  connect(addStageBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onAddStage);
  connect(removeStageBtn_, &QPushButton::clicked, this, &TurningCircleDialog::onRemoveStage);

  std::cout << "MS-01: 回转性观测对话框已创建（核心层 TurningExperimentCore）" << std::endl;
}

TurningCircleDialog::~TurningCircleDialog() = default;

void TurningCircleDialog::syncTableFromStages() {
  if (!stageTable_) return;
  const auto &stages = core_->stages();
  stageTable_->setRowCount(static_cast<int>(stages.size()));
  for (int r = 0; r < static_cast<int>(stages.size()); ++r) {
    const auto &st = stages[static_cast<size_t>(r)];
    auto put = [&](int c, const QString &v) {
      auto *it = new QTableWidgetItem(v);
      if (c == 4) {
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        it->setCheckState(st.measurementStart ? Qt::Checked : Qt::Unchecked);
        it->setText(QString());
      } else {
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable);
      }
      stageTable_->setItem(r, c, it);
    };
    put(0, st.name.empty() ? QString("阶段 %1").arg(r + 1) : QString::fromStdString(st.name));
    put(1, QString::number(st.durationSec, 'f', st.durationSec == 0.0 ? 0 : 1));
    put(2, QString::number(st.leftN, 'f', 1));
    put(3, QString::number(st.rightN, 'f', 1));
    put(4, QString());
  }
  if (stageTable_->rowCount() > 0) stageTable_->selectRow(stageTable_->rowCount() - 1);
}

bool TurningCircleDialog::syncStagesFromTable(QString *_error) {
  std::vector<keship::TurningStage> stages;
  int measurementCount = 0;
  for (int r = 0; r < stageTable_->rowCount(); ++r) {
    auto cell = [&](int c) { return stageTable_->item(r, c) ? stageTable_->item(r, c)->text().trimmed() : QString(); };
    bool okD = false, okL = false, okR = false;
    const double duration = cell(1).toDouble(&okD);
    const double left = cell(2).toDouble(&okL);
    const double right = cell(3).toDouble(&okR);
    if (!okD || !okL || !okR || duration < 0.0) {
      if (_error) *_error = QString("第 %1 阶段的时长或推力不是有效数字").arg(r + 1);
      return false;
    }
    const bool measure = stageTable_->item(r, 4)
        && stageTable_->item(r, 4)->checkState() == Qt::Checked;
    measurementCount += measure ? 1 : 0;
    keship::TurningStage st;
    st.durationSec = duration;
    st.leftN = left;
    st.rightN = right;
    st.measurementStart = measure;
    st.name = cell(0).isEmpty() ? QString("阶段 %1").arg(r + 1).toStdString() : cell(0).toStdString();
    stages.push_back(st);
  }
  if (measurementCount != 1) {
    if (_error) *_error = "必须且只能勾选一个「指标起点」阶段";
    return false;
  }
  std::string coreError;
  if (!core_->setStages(stages, &coreError)) {
    if (_error) *_error = QString::fromStdString(coreError);
    return false;
  }
  if (_error) _error->clear();
  return true;
}

void TurningCircleDialog::onAddStage() {
  if (core_->running()) return;
  const int row = stageTable_->rowCount();
  if (row > 0) {
    bool ok = false;
    const double lastDuration = stageTable_->item(row - 1, 1)->text().toDouble(&ok);
    if (ok && lastDuration == 0.0) stageTable_->item(row - 1, 1)->setText("20");
  }
  const double left = row > 0 ? stageTable_->item(row - 1, 2)->text().toDouble() : 120.0;
  const double right = row > 0 ? stageTable_->item(row - 1, 3)->text().toDouble() : 60.0;
  stageTable_->insertRow(row);
  stageTable_->setItem(row, 0, new QTableWidgetItem(QString("阶段 %1").arg(row + 1)));
  stageTable_->setItem(row, 1, new QTableWidgetItem("0"));
  stageTable_->setItem(row, 2, new QTableWidgetItem(QString::number(left, 'f', 1)));
  stageTable_->setItem(row, 3, new QTableWidgetItem(QString::number(right, 'f', 1)));
  auto *check = new QTableWidgetItem();
  check->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
  check->setCheckState(Qt::Unchecked);
  stageTable_->setItem(row, 4, check);
  stageTable_->selectRow(row);
}

void TurningCircleDialog::onRemoveStage() {
  if (core_->running() || stageTable_->rowCount() <= 1) return;
  const int row = stageTable_->currentRow();
  if (row < 0) return;
  const bool removedMarker = stageTable_->item(row, 4)
      && stageTable_->item(row, 4)->checkState() == Qt::Checked;
  stageTable_->removeRow(row);
  if (removedMarker) stageTable_->item(0, 4)->setCheckState(Qt::Checked);
  for (int r = 0; r < stageTable_->rowCount(); ++r)
    if (stageTable_->item(r, 0)) stageTable_->item(r, 0)->setText(QString("阶段 %1").arg(r + 1));
  stageTable_->selectRow(std::min(row, stageTable_->rowCount() - 1));
}

void TurningCircleDialog::syncParamsToCore() {
  core_->setTarget(modelCombo_->currentText().toStdString(),
                   leftNameEdit_->text().toStdString(),
                   rightNameEdit_->text().toStdString());
  core_->setSamplePeriod(periodSpin_->value());
}

void TurningCircleDialog::onStart() {
  if (core_->running()) return;
  QString error;
  if (!syncStagesFromTable(&error)) {
    QMessageBox::warning(this, "MS-01 推力计划", error);
    return;
  }
  syncParamsToCore();
  if (!core_->start()) {
    QMessageBox::warning(this, "MS-01",
        "未能读到船模位姿 —— 请确认「船模」名与场景中一致，且仿真已启动。");
    return;
  }
  chart_->clearTrail();
  stageTable_->setEnabled(false);
  addStageBtn_->setEnabled(false);
  removeStageBtn_->setEnabled(false);
  startBtn_->setEnabled(false);
  stopBtn_->setEnabled(true);
  timer_->start(static_cast<int>(periodSpin_->value() * 1000));
  std::cout << "MS-01: 开始多阶段推力计划，共 " << stageTable_->rowCount()
            << " 段；指标起点阶段 " << core_->measurementStartStage() + 1 << std::endl;
}

void TurningCircleDialog::onStop() {
  if (!core_->running()) return;
  core_->stop();
  timer_->stop();
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  stageTable_->setEnabled(true);
  addStageBtn_->setEnabled(true);
  removeStageBtn_->setEnabled(true);
  showResults();
  std::cout << "MS-01: 已停止，推力归零" << std::endl;
}

void TurningCircleDialog::onClear() {
  if (core_->running()) {
    core_->stop();            // 若仍在运行先归零推力
    timer_->stop();
  }
  core_->clear();
  chart_->clearTrail();
  readoutLabel_->setText("计划 t = —   阶段 —   x = —   y = —   航向 = —   速度 = —");
  resultLabel_->setText("进距 = —   战术直径 = —   回转直径 = —");
  // 重置按钮与阶段表编辑权限（与 onStop 保持一致，防止 stop 未执行时的状态错乱）
  startBtn_->setEnabled(true);
  stopBtn_->setEnabled(false);
  stageTable_->setEnabled(true);
  addStageBtn_->setEnabled(true);
  removeStageBtn_->setEnabled(true);
  std::cout << "MS-01: 已清零" << std::endl;
}

void TurningCircleDialog::onTick() {
  if (!core_->sample()) return;
  chart_->setTrail(core_->trail());
  updateReadout();
}

void TurningCircleDialog::updateReadout() {
  const auto &t = core_->trail();
  if (t.empty()) return;
  const auto &p = t.back();
  readoutLabel_->setText(QString("计划 t = %1 s   阶段 %2   x = %3   y = %4   航向 = %5°   速度 = %6 m/s   推力 L/R = %7 / %8 N%9")
      .arg(p.t, 0, 'f', 1).arg(p.stage + 1).arg(p.x, 0, 'f', 2).arg(p.y, 0, 'f', 2)
      .arg(p.yawDeg, 0, 'f', 1).arg(core_->speed(), 0, 'f', 2)
      .arg(core_->currentLeftThrust(), 0, 'f', 1)
      .arg(core_->currentRightThrust(), 0, 'f', 1)
      .arg(p.measurementStart ? "   ← 指标起点" : ""));
}

void TurningCircleDialog::showResults() {
  const auto m = core_->metrics();
  auto fmt = [](double v) { return v < 0 ? QString("—") : QString::number(v, 'f', 2); };
  if (!m.valid) {
    resultLabel_->setText("进距 = —   战术直径 = —   回转直径 = —（轨迹点不足）");
    return;
  }
  if (m.noTurn) {
    resultLabel_->setText(QString("进距 = %1 m   未回转（全程转向角 %2° < 10°，船基本直航）"
                                  "—— 战术直径 / 回转直径无定义")
        .arg(fmt(m.advance)).arg(m.turnedDeg, 0, 'f', 1));
    return;
  }
  resultLabel_->setText(QString("进距 = %1 m   战术直径 = %2 m   回转直径 = %3 m（圆拟合 %4 点, R²=%5）")
      .arg(fmt(m.advance)).arg(fmt(m.tactical)).arg(fmt(m.turning))
      .arg(m.fitPoints).arg(m.fitR2, 0, 'f', 4));
}

void TurningCircleDialog::onExport() {
  if (core_->trail().empty()) {
    QMessageBox::information(this, "导出", "尚无轨迹数据。");
    return;
  }
  QString path = QFileDialog::getSaveFileName(this, "导出回转轨迹 CSV",
      QString::fromStdString(modelCombo_->currentText().toStdString()) + "_turning.csv",
      "CSV (*.csv)");
  if (path.isEmpty()) return;
  if (!core_->exportCsv(path.toStdString()))
    QMessageBox::warning(this, "导出", "无法写入文件。");
  else
    std::cout << "MS-01: 轨迹已导出 → " << path.toStdString() << std::endl;
}
