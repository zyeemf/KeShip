/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— RP-03 敞水特性 / RP-04 推进匹配 对话框（实现）
 *
 * 本文件不复制任何 KT/KQ/平衡式公式：敞水与匹配全部走 keship::PropellerCore，
 * 船体阻力走 keship::ResistanceCore，与无头判据（keship_ctl）同源。
 */

#include "PropellerDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

constexpr double kRhoWater = 1025.0;   ///< 15℃ 海水（与 ResistanceInput 默认一致）
constexpr double kNuWater = 1.139e-6;
constexpr double kRhoAir = 1.225;

struct ModeSpec {
  const char *id;
  const char *title;
  const char *file;
};
const ModeSpec kModes[] = {
    {"RP-03", "RP-03 螺旋桨敞水特性", "rp03_openwater.json"},
    {"RP-04", "RP-04 推进性能匹配", "rp04_matching.json"},
};
constexpr int kModeCount = 2;

const ModeSpec &SpecOf(const QString &_id)
{
  for (int i = 0; i < kModeCount; ++i)
    if (_id == QString::fromLatin1(kModes[i].id)) return kModes[i];
  return kModes[0];
}

void SetCell(QTableWidget *_t, int _row, int _col, const QString &_text,
             Qt::Alignment _align = Qt::AlignRight | Qt::AlignVCenter)
{
  auto *item = new QTableWidgetItem(_text);
  item->setTextAlignment(_align);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  _t->setItem(_row, _col, item);
}

QString Num(double _v, int _dec) { return QString::number(_v, 'f', _dec); }
QString Sci(double _v, int _dec) { return QString::number(_v, 'e', _dec); }

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 敞水特性图
// ═══════════════════════════════════════════════════════════════════════════
PropellerChartWidget::PropellerChartWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumHeight(300);
  setAutoFillBackground(false);
}

void PropellerChartWidget::SetData(
    const std::vector<keship::OpenWaterPoint> &_pts)
{
  pts_ = _pts;
  update();
}

void PropellerChartWidget::AddMarker(double _x, double _y, const QString &_label,
                                     const QColor &_color)
{
  Marker m;
  m.x = _x;
  m.y = _y;
  m.label = _label;
  m.color = _color;
  markers_.push_back(m);
  update();
}

void PropellerChartWidget::Clear()
{
  pts_.clear();
  markers_.clear();
  update();
}

void PropellerChartWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  const int L = 68, R = 20, T = 40, B = 48;
  const QRectF plot(L, T, std::max(10, width() - L - R),
                    std::max(10, height() - T - B));

  QFont titleFont("sans", 9);
  titleFont.setBold(true);
  p.setFont(titleFont);
  p.setPen(QColor("#333"));
  p.drawText(QRectF(0, 6, width(), 18), Qt::AlignCenter,
             "螺旋桨敞水特性（KT / KQ / η0 – J）");

  p.setFont(QFont("sans", 9));
  if (pts_.size() < 2) {
    p.setPen(QColor("#999"));
    p.drawText(plot, Qt::AlignCenter, "尚无数据：设置桨几何后点「计算」");
    return;
  }

  double jMax = 0.0, yMax = 1e-9;
  for (const auto &q : pts_) {
    jMax = std::max(jMax, q.J);
    yMax = std::max(yMax, std::max(q.KT, std::max(q.KQ, q.eta0)));
  }
  if (!(jMax > 1e-9)) jMax = 1.0;
  yMax *= 1.15;

  auto X = [&](double _j) { return plot.left() + _j / jMax * plot.width(); };
  auto Y = [&](double _v) {
    return plot.bottom() - std::max(_v, 0.0) / yMax * plot.height();
  };

  // 网格 + 纵轴刻度（0 线单独画粗，η0 与 KT 都可能过 0）
  const int nY = 4;
  for (int i = 0; i <= nY; ++i) {
    const double v = yMax * i / nY;
    const double y = Y(v);
    p.setPen(QPen(QColor("#e6e6ee"), 1));
    p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    p.setPen(QColor("#666"));
    p.drawText(QRectF(4, y - 8, plot.left() - 10, 16),
               Qt::AlignRight | Qt::AlignVCenter, Num(v, 2));
  }
  const int nX = 6;
  for (int i = 0; i <= nX; ++i) {
    const double j = jMax * i / nX;
    const double x = X(j);
    p.setPen(QPen(QColor("#f0f0f5"), 1));
    p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    p.setPen(QColor("#666"));
    p.drawText(QRectF(x - 20, plot.bottom() + 4, 40, 16), Qt::AlignCenter,
               Num(j, 2));
  }
  p.setPen(QPen(QColor("#333"), 1.2));
  p.drawLine(plot.bottomLeft(), plot.bottomRight());
  p.drawLine(plot.bottomLeft(), plot.topLeft());

  p.setPen(QColor("#444"));
  p.drawText(QRectF(plot.left(), plot.bottom() + 24, plot.width(), 16),
             Qt::AlignCenter, "进速系数 J = Va / (nD)");
  p.drawText(QRectF(plot.left(), plot.top() - 18, 420, 14),
             Qt::AlignLeft | Qt::AlignVCenter,
             "系数 / 效率（KQ 约 1/10 KT，贴近横轴属正常）");

  // 三条曲线
  struct Curve {
    QString name;
    QColor color;
    std::vector<QPointF> pts;
    bool dashed;
  };
  std::vector<Curve> curves = {
      {"KT", QColor("#1f6feb"), {}, false},
      {"KQ", QColor("#e67e22"), {}, false},
      {"η0", QColor("#27ae60"), {}, false},
  };
  for (const auto &q : pts_) {
    curves[0].pts.push_back(QPointF(q.J, q.KT));
    curves[1].pts.push_back(QPointF(q.J, q.KQ));
    curves[2].pts.push_back(QPointF(q.J, q.eta0));
  }

  for (const auto &c : curves) {
    p.setPen(QPen(c.color, 2.0));
    // 必须显式清画刷：上一轮画点留下的实心画刷会把折线路径填充成多边形
    p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(X(c.pts[0].x()), Y(c.pts[0].y()));
    for (size_t i = 1; i < c.pts.size(); ++i)
      path.lineTo(X(c.pts[i].x()), Y(c.pts[i].y()));
    p.drawPath(path);
    p.setBrush(c.color);
    p.setPen(Qt::NoPen);
    for (const auto &pt : c.pts)
      p.drawEllipse(QPointF(X(pt.x()), Y(pt.y())), 2.4, 2.4);
    // 曲线右端标注名称：敞水图上三条线靠位置区分比靠图例快
    const auto &lastPt = c.pts.back();
    if (lastPt.y() > 0.0) {
      p.setPen(c.color);
      p.drawText(QPointF(X(lastPt.x()) - 30, Y(lastPt.y()) - 6), c.name);
    }
  }
  p.setBrush(Qt::NoBrush);

  // 标记点
  for (const auto &m : markers_) {
    const double x = X(std::min(m.x, jMax));
    const double y = Y(m.y);
    p.setPen(QPen(m.color, 1.0, Qt::DashLine));
    p.drawLine(QPointF(x, plot.bottom()), QPointF(x, y));
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(m.color, 1.6));
    p.drawEllipse(QPointF(x, y), 6.0, 6.0);
    p.setPen(m.color);
    // 靠右的标记点把标签放左侧，避免顶出绘图区
    QRectF labelRect;
    Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter;
    if (x > plot.right() - 200.0) {
      labelRect = QRectF(x - 162.0, y - 26.0, 154.0, 16.0);
      align = Qt::AlignRight | Qt::AlignVCenter;
    } else {
      labelRect = QRectF(x + 8.0, y - 26.0, 154.0, 16.0);
    }
    p.drawText(labelRect, align, m.label);
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// 对话框
// ═══════════════════════════════════════════════════════════════════════════
PropellerDialog::PropellerDialog(QWidget *_parent, const QString &_expId)
    : QDialog(_parent), expId_(_expId)
{
  if (SpecOf(expId_).id != expId_.toStdString()) expId_ = "RP-03";
  buildUi();
  setMode(expId_);
  resize(1120, 900);
}

//-----------------------------------------------------------------------------
void PropellerDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>螺旋桨几何（<i>D</i> / <i>P/D</i> / <i>A<sub>e</sub>/A<sub>0</sub></i> / "
      "<i>Z</i>）如何决定敞水特性 <i>K<sub>T</sub></i>、<i>K<sub>Q</sub></i>、"
      "<i>η<sub>0</sub></i>；再把桨放进船后，由平衡式 <i>T</i>(1−<i>t</i>)=<i>R</i> "
      "解出转速与推进质量系数 <i>η<sub>D</sub></i>。<br>"
      "顶部下拉切换：<b>RP-03</b> 敞水（只看桨）→ <b>RP-04</b> 匹配（桨 + 船 + 伴流）。<br>"
      "<b>交付物：</b>「导出交付物」写 <code>rp03_openwater.json</code> / "
      "<code>rp04_matching.json</code>（schema <code>keship.exp.v1</code>）。",
      this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  auto *modeRow = new QHBoxLayout();
  modeRow->addWidget(new QLabel("实验模式：", this));
  modeCombo_ = new QComboBox(this);
  for (int i = 0; i < kModeCount; ++i)
    modeCombo_->addItem(QString::fromUtf8(kModes[i].title),
                        QString::fromLatin1(kModes[i].id));
  modeRow->addWidget(modeCombo_, 1);
  mainLayout->addLayout(modeRow);

  auto *paramBox = new QGroupBox("参数", this);
  auto *paramRow = new QHBoxLayout(paramBox);

  // --- 桨几何（两个模式共用）---
  auto *propForm = new QFormLayout();
  propForm->setLabelAlignment(Qt::AlignRight);

  diameterSpin_ = new QDoubleSpinBox(paramBox);
  diameterSpin_->setRange(0.05, 10.0); diameterSpin_->setValue(0.15);
  diameterSpin_->setSingleStep(0.01); diameterSpin_->setDecimals(3);
  diameterSpin_->setSuffix(" m");
  propForm->addRow("直径 D：", diameterSpin_);

  pitchSpin_ = new QDoubleSpinBox(paramBox);
  pitchSpin_->setRange(0.5, 1.6); pitchSpin_->setValue(1.0);
  pitchSpin_->setSingleStep(0.05); pitchSpin_->setDecimals(3);
  propForm->addRow("螺距比 P/D：", pitchSpin_);

  areaSpin_ = new QDoubleSpinBox(paramBox);
  areaSpin_->setRange(0.30, 1.05); areaSpin_->setValue(0.50);
  areaSpin_->setSingleStep(0.05); areaSpin_->setDecimals(3);
  propForm->addRow("盘面比 Ae/A0：", areaSpin_);

  bladeSpin_ = new QDoubleSpinBox(paramBox);
  bladeSpin_->setRange(2.0, 6.0); bladeSpin_->setValue(3.0);
  bladeSpin_->setSingleStep(1.0); bladeSpin_->setDecimals(1);
  propForm->addRow("叶数 Z：", bladeSpin_);

  jMaxSpin_ = new QDoubleSpinBox(paramBox);
  jMaxSpin_->setRange(0.2, 2.0); jMaxSpin_->setValue(1.2);
  jMaxSpin_->setSingleStep(0.1); jMaxSpin_->setDecimals(2);
  propForm->addRow("J 上限：", jMaxSpin_);

  pointsSpin_ = new QSpinBox(paramBox);
  pointsSpin_->setRange(5, 200); pointsSpin_->setValue(41);
  pointsSpin_->setToolTip("点数越多，峰值与零推力点定位越精确（表格行数同步增加）");
  propForm->addRow("曲线点数：", pointsSpin_);

  paramRow->addLayout(propForm);

  // --- 船体（仅 RP-04）---
  hullBox_ = new QGroupBox("船体（模型尺度）", paramBox);
  hullForm_ = new QFormLayout(hullBox_);
  hullForm_->setLabelAlignment(Qt::AlignRight);

  lengthSpin_ = new QDoubleSpinBox(hullBox_);
  lengthSpin_->setRange(1.0, 400.0); lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5); lengthSpin_->setDecimals(3);
  lengthSpin_->setSuffix(" m");
  hullForm_->addRow("水线长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(hullBox_);
  breadthSpin_->setRange(0.2, 60.0); breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1); breadthSpin_->setDecimals(3);
  breadthSpin_->setSuffix(" m");
  hullForm_->addRow("型宽 B：", breadthSpin_);

  draftSpin_ = new QDoubleSpinBox(hullBox_);
  draftSpin_->setRange(0.05, 25.0); draftSpin_->setValue(0.3);
  draftSpin_->setSingleStep(0.05); draftSpin_->setDecimals(3);
  draftSpin_->setSuffix(" m");
  hullForm_->addRow("吃水 T：", draftSpin_);

  cbSpin_ = new QDoubleSpinBox(hullBox_);
  cbSpin_->setRange(0.30, 0.88); cbSpin_->setValue(0.60);
  cbSpin_->setSingleStep(0.01); cbSpin_->setDecimals(3);
  hullForm_->addRow("方形系数 Cb：", cbSpin_);

  cmSpin_ = new QDoubleSpinBox(hullBox_);
  cmSpin_->setRange(0.50, 0.99); cmSpin_->setValue(0.90);
  cmSpin_->setSingleStep(0.01); cmSpin_->setDecimals(3);
  hullForm_->addRow("中横剖系数 Cm：", cmSpin_);

  cwpSpin_ = new QDoubleSpinBox(hullBox_);
  cwpSpin_->setRange(0.50, 0.95); cwpSpin_->setValue(0.70);
  cwpSpin_->setSingleStep(0.01); cwpSpin_->setDecimals(3);
  hullForm_->addRow("水线面系数 Cwp：", cwpSpin_);

  paramRow->addWidget(hullBox_);

  // --- 匹配（仅 RP-04）---
  matchBox_ = new QGroupBox("船-桨匹配", paramBox);
  auto *matchForm = new QFormLayout(matchBox_);
  matchForm->setLabelAlignment(Qt::AlignRight);

  speedSpin_ = new QDoubleSpinBox(matchBox_);
  speedSpin_->setRange(0.1, 20.0); speedSpin_->setValue(2.0);
  speedSpin_->setSingleStep(0.1); speedSpin_->setDecimals(3);
  speedSpin_->setSuffix(" m/s");
  matchForm->addRow("航速 V：", speedSpin_);

  wakeSpin_ = new QDoubleSpinBox(matchBox_);
  wakeSpin_->setRange(0.0, 0.60); wakeSpin_->setValue(0.20);
  wakeSpin_->setSingleStep(0.01); wakeSpin_->setDecimals(3);
  wakeSpin_->setToolTip("伴流分数 w：Va = (1−w)V，单桨运输船典型 0.15~0.30");
  matchForm->addRow("伴流分数 w：", wakeSpin_);

  deductionSpin_ = new QDoubleSpinBox(matchBox_);
  deductionSpin_->setRange(0.0, 0.50); deductionSpin_->setValue(0.15);
  deductionSpin_->setSingleStep(0.01); deductionSpin_->setDecimals(3);
  deductionSpin_->setToolTip("推力减额 t：T(1−t) = R，单桨运输船典型 0.10~0.25");
  matchForm->addRow("推力减额 t：", deductionSpin_);

  windSpin_ = new QDoubleSpinBox(matchBox_);
  windSpin_->setRange(0.0, 40.0); windSpin_->setValue(0.0);
  windSpin_->setSingleStep(0.5); windSpin_->setDecimals(2);
  windSpin_->setSuffix(" m/s");
  matchForm->addRow("相对风速：", windSpin_);

  frontalSpin_ = new QDoubleSpinBox(matchBox_);
  frontalSpin_->setRange(0.0, 5000.0); frontalSpin_->setValue(0.0);
  frontalSpin_->setSingleStep(0.5); frontalSpin_->setDecimals(3);
  frontalSpin_->setSuffix(" m²");
  matchForm->addRow("受风面积：", frontalSpin_);

  paramRow->addWidget(matchBox_);

  auto *btnColumn = new QVBoxLayout();
  computeBtn_ = new QPushButton("计算", paramBox);
  exportBtn_ = new QPushButton("导出交付物", paramBox);
  // 按钮不写文件名（rp04_matching.json 之类的长串），文件名放 tooltip + 状态栏
  computeBtn_->setMaximumWidth(220);
  exportBtn_->setMaximumWidth(220);
  btnColumn->addWidget(computeBtn_);
  btnColumn->addWidget(exportBtn_);
  btnColumn->addStretch();
  paramRow->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  summaryLabel_ = new QLabel(this);
  summaryLabel_->setWordWrap(true);
  summaryLabel_->setTextFormat(Qt::RichText);
  summaryLabel_->setMinimumHeight(46);
  mainLayout->addWidget(summaryLabel_);

  chart_ = new PropellerChartWidget(this);
  chart_->setMinimumHeight(230);
  mainLayout->addWidget(chart_, 2);

  table_ = new QTableWidget(this);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setMinimumHeight(130);
  mainLayout->addWidget(table_, 3);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  statusLabel_->setWordWrap(true);
  mainLayout->addWidget(statusLabel_);

  connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &PropellerDialog::onModeChanged);
  connect(computeBtn_, &QPushButton::clicked, this, &PropellerDialog::onCompute);
  connect(exportBtn_, &QPushButton::clicked, this, &PropellerDialog::onExport);
}

//-----------------------------------------------------------------------------
void PropellerDialog::setMode(const QString &_expId)
{
  const QString id = SpecOf(_expId).id;
  const int idx = modeCombo_->findData(id);
  if (idx >= 0 && idx != modeCombo_->currentIndex()) {
    modeCombo_->setCurrentIndex(idx);
    return;
  }
  expId_ = id;
  applyMode();
}

//-----------------------------------------------------------------------------
void PropellerDialog::onModeChanged(int _index)
{
  if (_index < 0) return;
  const QString id = modeCombo_->itemData(_index).toString();
  if (!id.isEmpty()) expId_ = SpecOf(id).id;
  applyMode();
}

//-----------------------------------------------------------------------------
void PropellerDialog::applyMode()
{
  const bool isMatch = (expId_ == "RP-04");
  hullBox_->setVisible(isMatch);
  matchBox_->setVisible(isMatch);

  setWindowTitle(QString::fromUtf8(SpecOf(expId_).title) + " —— 输入 / 曲线 / 交付物");
  exportBtn_->setToolTip(
      QString("将写出 %1（schema keship.exp.v1，expId = %2）")
          .arg(QString::fromLatin1(SpecOf(expId_).file)).arg(expId_));

  lastMetrics_ = QJsonObject();
  lastExtra_ = QJsonObject();
  lastConverged_ = false;
  table_->clear();
  table_->setRowCount(0);
  table_->setColumnCount(0);
  chart_->Clear();
  summaryLabel_->clear();

  computeCurrent();
}

//-----------------------------------------------------------------------------
keship::PropellerInput PropellerDialog::buildPropeller() const
{
  keship::PropellerInput p;
  p.D = diameterSpin_->value();
  p.P_over_D = pitchSpin_->value();
  p.Ae_A0 = areaSpin_->value();
  p.Z = bladeSpin_->value();
  return p;
}

//-----------------------------------------------------------------------------
keship::ResistanceInput PropellerDialog::buildHull() const
{
  keship::ResistanceInput in;
  in.Lwl = lengthSpin_->value();
  in.B = breadthSpin_->value();
  in.T = draftSpin_->value();
  in.Cb = cbSpin_->value();
  in.Cm = cmSpin_->value();
  in.Cwp = cwpSpin_->value();
  // ∇ 由 Cb·L·B·T 反算（Cb 的定义式）：RP-04 的核心链路里 (1+k) 用到
  // L/∇^(1/3)，若 ∇ 与 Cb 不自洽，形状因子就与船型脱钩。
  in.volumeD = in.Cb * in.Lwl * in.B * in.T;
  in.rhoWater = kRhoWater;
  in.nu = kNuWater;
  in.rhoAir = kRhoAir;
  in.windSpeedMs = windSpin_->value();
  in.frontalAreaM2 = frontalSpin_->value();
  in.appendageFactor = 1.0;
  return in;
}

//-----------------------------------------------------------------------------
void PropellerDialog::storeCommon()
{
  const auto p = buildPropeller();
  lastObject_ = QJsonObject();
  lastObject_["type"] = "propeller";
  lastObject_["series"] = "Wageningen B（本平台按 B3-50 锚点标定的 s≤2 子集）";
  lastObject_["diameterM"] = p.D;
  lastObject_["pitchRatio"] = p.P_over_D;
  lastObject_["aeA0"] = p.Ae_A0;
  lastObject_["blades"] = p.Z;

  lastParams_ = QJsonObject();
  lastParams_["mode"] = expId_;
  lastParams_["diameterM"] = p.D;
  lastParams_["pitchRatio"] = p.P_over_D;
  lastParams_["aeA0"] = p.Ae_A0;
  lastParams_["blades"] = p.Z;

  lastCondition_ = QJsonObject();
}

void PropellerDialog::setTableShape(int _rows, int _cols,
                                    const QStringList &_headers)
{
  table_->clear();
  table_->setRowCount(_rows);
  table_->setColumnCount(_cols);
  table_->setHorizontalHeaderLabels(_headers);
}

void PropellerDialog::setStatus(const QString &_text)
{
  statusLabel_->setText(_text);
}

void PropellerDialog::showSummary(const QString &_richText)
{
  summaryLabel_->setText(_richText);
}

//-----------------------------------------------------------------------------
void PropellerDialog::computeCurrent()
{
  if (expId_ == "RP-04") computeRp04();
  else computeRp03();
}

//-----------------------------------------------------------------------------
// RP-03 敞水特性：KT(J)、KQ(J)、η0(J) 三条曲线 + η0 峰值
//-----------------------------------------------------------------------------
void PropellerDialog::computeRp03()
{
  const auto p = buildPropeller();
  const double jmax = jMaxSpin_->value();
  const int n = pointsSpin_->value();
  const auto curve = keship::OpenWaterCurve(p, jmax, n);

  storeCommon();
  lastCondition_["jMax"] = jmax;
  lastCondition_["samples"] = n;
  lastParams_["jMax"] = jmax;
  lastParams_["samples"] = n;

  int iPeak = 0;
  for (int i = 1; i < static_cast<int>(curve.size()); ++i)
    if (curve[i].eta0 > curve[iPeak].eta0) iPeak = i;
  const auto peak = curve[iPeak];

  // 零推力点 J0：KT 由正变负的位置线性插值。
  // J > J0 时桨反而产生阻力（风车状态），本平台不建模，但「J0 在哪」是
  // 学生理解 P/D 作用的关键读数，故显式给出。
  double jZero = -1.0;
  for (size_t i = 1; i < curve.size(); ++i) {
    if (curve[i - 1].KT > 0.0 && curve[i].KT <= 0.0) {
      const double k0 = curve[i - 1].KT;
      const double k1 = curve[i].KT;
      const double j0 = curve[i - 1].J;
      const double j1 = curve[i].J;
      const double w = (std::abs(k1 - k0) > 1e-12) ? k0 / (k0 - k1) : 0.0;
      jZero = j0 + (j1 - j0) * w;
      break;
    }
  }

  lastMetrics_ = QJsonObject();
  lastMetrics_["diameterM"] = p.D;
  lastMetrics_["pitchRatio"] = p.P_over_D;
  lastMetrics_["aeA0"] = p.Ae_A0;
  lastMetrics_["blades"] = p.Z;
  lastMetrics_["jMax"] = jmax;
  lastMetrics_["samples"] = n;
  lastMetrics_["kt0"] = curve.front().KT;
  lastMetrics_["kq0"] = curve.front().KQ;
  lastMetrics_["jZeroThrust"] = jZero;
  lastMetrics_["eta0Max"] = peak.eta0;
  lastMetrics_["jAtEta0Max"] = peak.J;
  lastMetrics_["ktAtEta0Max"] = peak.KT;
  lastMetrics_["kqAtEta0Max"] = peak.KQ;

  QJsonArray aJ, aKt, aKq, aEta;
  for (const auto &q : curve) {
    aJ.append(q.J); aKt.append(q.KT); aKq.append(q.KQ); aEta.append(q.eta0);
  }
  lastExtra_ = QJsonObject();
  lastExtra_["j"] = aJ;
  lastExtra_["kt"] = aKt;
  lastExtra_["kq"] = aKq;
  lastExtra_["eta0"] = aEta;
  lastExtra_["definitions"] = QJsonObject();  // 占位后逐条赋值（避免 initializer_list 兼容性）
  QJsonObject defs;
  defs["J"] = "J = Va/(nD)";
  defs["KT"] = "KT = T/(rho*n^2*D^4)";
  defs["KQ"] = "KQ = Q/(rho*n^2*D^5)";
  defs["eta0"] = "eta0 = J*KT/(2*pi*KQ)";
  lastExtra_["definitions"] = defs;

  setTableShape(static_cast<int>(curve.size()), 4, {"J", "KT", "KQ", "η0"});
  for (int i = 0; i < static_cast<int>(curve.size()); ++i) {
    SetCell(table_, i, 0, Num(curve[i].J, 3));
    SetCell(table_, i, 1, Num(curve[i].KT, 4));
    SetCell(table_, i, 2, Num(curve[i].KQ, 5));
    SetCell(table_, i, 3, Num(curve[i].eta0, 4));
  }

  chart_->Clear();
  chart_->SetData(curve);
  chart_->AddMarker(peak.J, peak.eta0,
                    QString("η0峰 %1 @ J=%2")
                        .arg(Num(peak.eta0, 3)).arg(Num(peak.J, 3)),
                    QColor("#c0392b"));

  showSummary(QString(
                  "敞水效率峰值 <b>η<sub>0,max</sub> = %1</b> @ J = %2　｜　"
                  "系柱 KT(0) = %3、KQ(0) = %4<br>"
                  "零推力点 <b>J<sub>0</sub> = %5</b>"
                  "（J &gt; J<sub>0</sub> 时 KT &lt; 0，桨变成阻力源）")
                  .arg(Num(peak.eta0, 3)).arg(Num(peak.J, 3))
                  .arg(Num(curve.front().KT, 3)).arg(Num(curve.front().KQ, 4))
                  .arg(jZero < 0.0 ? QString("> %1").arg(Num(jmax, 2))
                                   : Num(jZero, 3)));

  setStatus("RP-03 已算：P/D↑ 使 J0 与 η0 峰一起右移；Ae/A0↑ 使 KQ 增大 ⇒ η0 略降。"
            "建议每次只改一个几何量做对照。");
  std::cout << "RP-03: eta0Max=" << peak.eta0 << " J(peak)=" << peak.J
            << " J0=" << jZero << std::endl;
}

//-----------------------------------------------------------------------------
// RP-04 推进性能匹配：解 T(1−t) = R，给出 n / T / Q / P_D / QPC
//-----------------------------------------------------------------------------
void PropellerDialog::computeRp04()
{
  const auto p = buildPropeller();
  const auto hull = buildHull();
  const double v = speedSpin_->value();
  const double w = wakeSpin_->value();
  const double t = deductionSpin_->value();

  storeCommon();
  lastObject_["hullLengthM"] = hull.Lwl;
  lastObject_["hullBreadthM"] = hull.B;
  lastObject_["hullDraftM"] = hull.T;
  lastObject_["hullCb"] = hull.Cb;
  lastObject_["hullVolumeM3"] = hull.volumeD;

  const auto res = keship::ResistanceAtSpeed(hull, v);
  const auto m = keship::MatchOperatingPoint(p, hull, v, w, t);
  lastConverged_ = m.converged;

  const double va = (1.0 - w) * v;
  const double j = (m.n_rps > 1e-9) ? va / (m.n_rps * p.D) : 0.0;
  const auto ow = keship::OpenWaterAtJ(p, j);
  const double etaH = (w < 1.0) ? (1.0 - t) / (1.0 - w) : 0.0;  ///< 船身效率
  const double pe = res.Rtotal * v;                              ///< 有效功率
  const double residual = m.thrustN * (1.0 - t) - res.Rtotal;
  const double residualRel = (res.Rtotal > 1e-9) ? residual / res.Rtotal : 0.0;

  lastCondition_["speedMs"] = v;
  lastCondition_["wakeFraction"] = w;
  lastCondition_["thrustDeduction"] = t;
  lastCondition_["windSpeedMs"] = hull.windSpeedMs;
  lastCondition_["frontalAreaM2"] = hull.frontalAreaM2;
  lastCondition_["note"] = QString("V=%1 m/s，w=%2，t=%3")
                               .arg(Num(v, 3)).arg(Num(w, 3)).arg(Num(t, 3));

  lastParams_["speedMs"] = v;
  lastParams_["wakeFraction"] = w;
  lastParams_["thrustDeduction"] = t;
  lastParams_["windSpeedMs"] = hull.windSpeedMs;
  lastParams_["frontalAreaM2"] = hull.frontalAreaM2;
  lastParams_["hullVolumeM3"] = hull.volumeD;

  lastMetrics_ = QJsonObject();
  lastMetrics_["speedMs"] = v;
  lastMetrics_["wakeFraction"] = w;
  lastMetrics_["thrustDeduction"] = t;
  lastMetrics_["resistanceN"] = res.Rtotal;
  lastMetrics_["effectivePowerW"] = pe;
  lastMetrics_["nRps"] = m.n_rps;
  lastMetrics_["nRpm"] = m.n_rps * 60.0;
  lastMetrics_["thrustN"] = m.thrustN;
  lastMetrics_["torqueNm"] = m.torqueNm;
  lastMetrics_["powerDW"] = m.powerW;
  lastMetrics_["qpcEtaD"] = m.qpc;
  lastMetrics_["eta0AtJ"] = ow.eta0;
  lastMetrics_["hullEfficiency"] = etaH;
  lastMetrics_["advanceJ"] = j;
  lastMetrics_["ktAtJ"] = ow.KT;
  lastMetrics_["kqAtJ"] = ow.KQ;
  lastMetrics_["residualN"] = residual;
  lastMetrics_["residualRel"] = residualRel;
  lastMetrics_["converged"] = m.converged;

  // 曲线数组：工作点附近画 0~Jmax 的敞水特性，供下游直接复核 η0(J)
  const double jmax = std::max(jMaxSpin_->value(), j * 1.2 + 0.1);
  const int nCurve = pointsSpin_->value();
  const auto curve = keship::OpenWaterCurve(p, jmax, nCurve);
  QJsonArray aJ, aKt, aKq, aEta;
  for (const auto &q : curve) {
    aJ.append(q.J); aKt.append(q.KT); aKq.append(q.KQ); aEta.append(q.eta0);
  }
  lastExtra_ = QJsonObject();
  lastExtra_["j"] = aJ;
  lastExtra_["kt"] = aKt;
  lastExtra_["kq"] = aKq;
  lastExtra_["eta0"] = aEta;
  QJsonObject point;
  point["j"] = j;
  point["kt"] = ow.KT;
  point["kq"] = ow.KQ;
  point["eta0"] = ow.eta0;
  point["nRps"] = m.n_rps;
  point["vaMs"] = va;
  lastExtra_["operatingPoint"] = point;
  lastExtra_["balance"] = "T*(1-t) = Rtotal（含风阻，与界面同口径）";

  setTableShape(17, 4, {"项目", "数值", "单位", "说明"});
  int r = 0;
  auto row = [&](const QString &_name, const QString &_value, const QString &_unit,
                 const QString &_note) {
    SetCell(table_, r, 0, _name, Qt::AlignLeft | Qt::AlignVCenter);
    SetCell(table_, r, 1, _value);
    SetCell(table_, r, 2, _unit, Qt::AlignCenter);
    SetCell(table_, r, 3, _note, Qt::AlignLeft | Qt::AlignVCenter);
    ++r;
  };
  // 说明列宽度有限，注释一律控制在 12 个汉字以内（长了会被列宽截断）
  row("总阻力 R", Num(res.Rtotal, 2), "N", "含风阻，同 RP-02");
  row("实效伴流速度 Va", Num(va, 4), "m/s", "Va = (1−w)·V");
  row("进速系数 J", Num(j, 4), "—", "J = Va/(nD)");
  row("转速 n", Num(m.n_rps, 4), "r/s", "对 n 二分求解");
  row("转速 n", Num(m.n_rps * 60.0, 2), "r/min", "×60");
  row("推力 T", Num(m.thrustN, 3), "N", "T = ρn²D⁴·KT");
  row("扭矩 Q", Num(m.torqueNm, 4), "N·m", "Q = ρn²D⁵·KQ");
  row("收到功率 P_D", Num(m.powerW, 2), "W", "P_D = 2πnQ");
  row("有效功率 P_E", Num(pe, 2), "W", "P_E = R·V");
  row("敞水效率 η0", Num(ow.eta0, 4), "—", "工作点 J 处");
  row("船身效率 ηH", Num(etaH, 4), "—", "ηH = (1−t)/(1−w)");
  row("推进质量系数 ηD", Num(m.qpc, 4), "—", "ηD = η0·ηH（QPC）");
  row("KT(J)", Num(ow.KT, 4), "—", "工作点推力系数");
  row("KQ(J)", Num(ow.KQ, 5), "—", "工作点扭矩系数");
  row("平衡残差", Num(residual, 4), "N", "T(1−t) − R");
  row("相对残差", Sci(std::abs(residualRel), 2), "—", "判据 |·| ≤ 1e-4");
  row("是否收敛", m.converged ? "是" : "否", "—",
      m.converged ? "平衡式已闭合" : "未闭合，见状态栏");

  chart_->Clear();
  chart_->SetData(curve);
  chart_->AddMarker(j, ow.eta0,
                    QString("工作点 J=%1").arg(Num(j, 3)), QColor("#c0392b"));

  showSummary(QString(
                  "V = %1 m/s（w = %2、t = %3）　R = <b>%4</b> N<br>"
                  "平衡解：n = <b>%5 r/s</b>（%6 r/min）· T = %7 N · Q = %8 N·m · "
                  "P<sub>D</sub> = <b>%9 W</b><br>"
                  "η<sub>0</sub> = %10 · η<sub>H</sub> = %11 · "
                  "η<sub>D</sub>（QPC）= <b>%12</b> · 相对残差 %13")
                  .arg(Num(v, 2)).arg(Num(w, 3)).arg(Num(t, 3))
                  .arg(Num(res.Rtotal, 1))
                  .arg(Num(m.n_rps, 3)).arg(Num(m.n_rps * 60.0, 1))
                  .arg(Num(m.thrustN, 1)).arg(Num(m.torqueNm, 2))
                  .arg(Num(m.powerW, 1))
                  .arg(Num(ow.eta0, 3)).arg(Num(etaH, 3)).arg(Num(m.qpc, 3))
                  .arg(Sci(std::abs(residualRel), 2)));

  if (m.converged) {
    setStatus("RP-04 已算：工作点是 J 轴上的一个点，同时满足桨的 KT(J) 与船的阻力；"
              "改 w 或 t 会同时移动 ηH 与工作点 J。");
  } else {
    setStatus("RP-04 未收敛：航速或阻力为 0（无有效工作点），或推力始终补不上阻力。"
              "请提高航速/加大桨径，或检查 w、t 是否被填成极端值。");
  }
  std::cout << "RP-04: n=" << m.n_rps << " rps, T=" << m.thrustN
            << " P_D=" << m.powerW << " qpc=" << m.qpc
            << " converged=" << (m.converged ? 1 : 0) << std::endl;
}

//-----------------------------------------------------------------------------
void PropellerDialog::onCompute()
{
  computeCurrent();
}

//-----------------------------------------------------------------------------
// 无交互导出：给离屏测试用（QFileDialog 在无头环境取不到路径）
bool PropellerDialog::exportTo(const QString &_path)
{
  if (lastMetrics_.isEmpty() || _path.isEmpty()) return false;

  keship::ExpDeliverable d;
  d.expId = expId_.toStdString();
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cerr << "PropellerDialog: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << expId_.toStdString() << ": 已导出交付物 "
            << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void PropellerDialog::onExport()
{
  if (lastMetrics_.isEmpty()) {
    QMessageBox::information(this, "导出交付物",
        "还没有计算结果。请先点「计算」。");
    return;
  }

  const QString defaultName = QString::fromLatin1(SpecOf(expId_).file);
  const QString path = QFileDialog::getSaveFileName(
      this, QString("导出 %1 交付物").arg(expId_), defaultName, "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出交付物",
        QString("导出失败：%1\n（详见控制台输出）").arg(path));
    return;
  }
  QMessageBox::information(this, "导出交付物",
      QString("已导出：%1\n\nexpId = %2（schema keship.exp.v1）")
          .arg(path).arg(expId_));
}
