/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-08 完整稳性综合校核 —— 实现 */

#include "GzStabilityDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"
#include "ShipHullGenerator.hh"

namespace {

constexpr double kGravity = 9.81;

QString Fmt(double _v, int _n = 4) { return QString::number(_v, 'f', _n); }

const char *kSourceBox = "box";
const char *kSourceGen = "generator";
const char *kSourceSd08 = "sd08";

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// GzChartWidget
// ═══════════════════════════════════════════════════════════════════════════

GzChartWidget::GzChartWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumHeight(300);
}

void GzChartWidget::setCurve(const std::vector<keship::GzPoint> &_pts, double _gzMax,
                            double _angleGzMax, double _vanishingDeg)
{
  pts_ = _pts;
  gzMax_ = _gzMax;
  angleGzMax_ = _angleGzMax;
  vanishDeg_ = _vanishingDeg;
  update();
}

void GzChartWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  const QRectF plot(70.0, 22.0, std::max(60.0, width() - 110.0),
                    std::max(60.0, height() - 78.0));
  p.setPen(QPen(QColor("#d0d0d0"), 1.0));
  p.drawRect(plot);

  if (pts_.size() < 2) {
    p.setPen(QColor("#888"));
    p.drawText(plot, Qt::AlignCenter, "尚无 GZ 曲线（请点「计算」）");
    return;
  }

  double yMin = 0.0, yMax = std::max(gzMax_, 1e-6);
  for (const auto &q : pts_) {
    yMin = std::min(yMin, q.gzM);
    yMax = std::max(yMax, q.gzM);
  }
  // 上方多留 22%：曲线峰值到框顶之间要放"GZmax / 消失角"两行标注，
  // 直接画在马克点旁边会与曲线或彼此重叠（曾经把 48° 标注压到 90° 标注上）
  const double span = std::max(yMax - yMin, 1e-6);
  yMin -= std::max(0.01, 0.10 * span);
  yMax += std::max(0.02, 0.22 * span);

  auto toPx = [&](double _phi, double _gz) {
    return QPointF(plot.left() + (_phi / 90.0) * plot.width(),
                   plot.bottom() - ((_gz - yMin) / (yMax - yMin)) * plot.height());
  };

  // 网格（每 10°）
  p.setPen(QPen(QColor("#efefef"), 1.0));
  for (int d = 10; d < 90; d += 10) {
    p.drawLine(toPx(d, yMin), toPx(d, yMax));
  }
  p.setPen(QColor("#444"));
  for (int d = 0; d <= 90; d += 15) {
    p.drawText(QRectF(plot.left() + (d / 90.0) * plot.width() - 20, plot.bottom() + 4,
                      40, 16),
               Qt::AlignHCenter | Qt::AlignTop, QString::number(d));
  }
  p.drawText(QRectF(plot.right() - 70, plot.bottom() + 22, 70, 16), Qt::AlignRight,
             "φ / °");
  p.drawText(QRectF(6, 4, plot.width(), 16), Qt::AlignLeft, "GZ / m");

  // 纵轴刻度：取"整齐"步长（1/2/5×10^k），5 格左右，便于读写数与手算对照
  {
    const double raw = (yMax - yMin) / 5.0;
    const double mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
    const double n = raw / mag;
    const double step = ((n <= 1.0) ? 1.0 : (n <= 2.0) ? 2.0 : (n <= 5.0) ? 5.0 : 10.0) * mag;
    for (double v = std::ceil(yMin / step) * step; v <= yMax + 1e-12; v += step) {
      const QPointF q = toPx(0.0, v);
      if (std::abs(v) > step * 0.01) {   // 0 由下方零轴单独标注
        p.setPen(QPen(QColor("#f2f2f2"), 1.0));
        p.drawLine(QPointF(plot.left(), q.y()), QPointF(plot.right(), q.y()));
      }
      p.setPen(QColor("#444"));
      p.drawText(QRectF(plot.left() - 58, q.y() - 8, 54, 16),
                 Qt::AlignRight | Qt::AlignVCenter, Fmt(v, 3));
    }
  }

  // 零轴（GZ=0 是"有没有复原能力"的分界）
  p.setPen(QPen(QColor("#999"), 1.4));
  p.drawLine(toPx(0.0, 0.0), toPx(90.0, 0.0));

  // 曲线
  QPolygonF line;
  for (const auto &q : pts_) line << toPx(q.phiDeg, q.gzM);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(QColor("#1f4e79"), 2.2));
  p.drawPolyline(line);

  // GZmax 标记（标注放在框顶标注行，不贴在点上，避免与曲线/消失角标注打架）
  const QPointF pm = toPx(angleGzMax_, gzMax_);
  p.setPen(QPen(QColor("#c0392b"), 1.4, Qt::DashLine));
  p.drawLine(QPointF(pm.x(), plot.top()), pm);
  p.setPen(QPen(QColor("#c0392b"), 2.0));
  p.setBrush(QColor("#c0392b"));
  p.drawEllipse(pm, 4.0, 4.0);

  p.setPen(QColor("#c0392b"));
  p.drawText(QRectF(plot.left() + 8, plot.top() + 4, plot.width() * 0.55, 16),
             Qt::AlignLeft | Qt::AlignVCenter,
             QString("GZmax = %1 m @ %2°").arg(Fmt(gzMax_, 3)).arg(Fmt(angleGzMax_, 1)));

  // 消失角竖线
  const QPointF pv = toPx(std::min(vanishDeg_, 90.0), yMin);
  p.setPen(QPen(QColor("#e67e22"), 1.4, Qt::DashLine));
  p.drawLine(QPointF(pv.x(), plot.top()), QPointF(pv.x(), plot.bottom()));
  p.setPen(QColor("#e67e22"));
  p.drawText(QRectF(plot.right() - plot.width() * 0.40 - 8, plot.top() + 4,
                    plot.width() * 0.40, 16),
             Qt::AlignRight | Qt::AlignVCenter,
             QString("消失角 %1°").arg(Fmt(vanishDeg_, 1)));
}

// ═══════════════════════════════════════════════════════════════════════════
// GzStabilityDialog
// ═══════════════════════════════════════════════════════════════════════════

GzStabilityDialog::GzStabilityDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("HS-08 完整稳性综合校核 —— GZ 曲线 / IMO 五项判据");
  resize(1180, 860);
  buildUi();
  refresh();
}

void GzStabilityDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>给定主尺度、吃水与重心高 KG，算出静稳性曲线 <b>GZ(φ)</b>，"
      "并逐项判定 IMO A.749(18) / IS Code 2008 的五项完整稳性衡准。<br>"
      "<b>上游依赖：</b>「导入 HS-07」取 <code>hs07_buoyancy.json</code> 的 <code>metrics.kgM</code> "
      "作为本页 KG；站表可由 L/B/D 生成，也可导入 SD-08 的 <code>sd08_lines.json</code>。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 输入 ===
  auto *paramBox = new QGroupBox("输入（主尺度 / 吃水 / 重心 / 站表来源）", this);
  auto *paramLayout = new QHBoxLayout(paramBox);

  auto *formL = new QFormLayout();
  lengthSpin_ = new QDoubleSpinBox(paramBox);
  lengthSpin_->setRange(2.0, 60.0); lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5); lengthSpin_->setDecimals(3); lengthSpin_->setSuffix(" m");
  formL->addRow("船长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(paramBox);
  breadthSpin_->setRange(0.5, 20.0); breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1); breadthSpin_->setDecimals(3); breadthSpin_->setSuffix(" m");
  formL->addRow("型宽 B：", breadthSpin_);

  depthSpin_ = new QDoubleSpinBox(paramBox);
  depthSpin_->setRange(0.3, 12.0); depthSpin_->setValue(1.0);
  depthSpin_->setSingleStep(0.05); depthSpin_->setDecimals(3); depthSpin_->setSuffix(" m");
  formL->addRow("型深 D：", depthSpin_);

  auto *formR = new QFormLayout();
  draftSpin_ = new QDoubleSpinBox(paramBox);
  draftSpin_->setRange(0.02, 12.0); draftSpin_->setValue(0.3);
  draftSpin_->setSingleStep(0.05); draftSpin_->setDecimals(3); draftSpin_->setSuffix(" m");
  formR->addRow("吃水 T：", draftSpin_);

  kgSpin_ = new QDoubleSpinBox(paramBox);
  kgSpin_->setRange(0.0, 20.0); kgSpin_->setValue(0.5);
  kgSpin_->setSingleStep(0.05); kgSpin_->setDecimals(3); kgSpin_->setSuffix(" m");
  formR->addRow("重心高 KG：", kgSpin_);

  densitySpin_ = new QDoubleSpinBox(paramBox);
  densitySpin_->setRange(900.0, 1100.0); densitySpin_->setValue(1000.0);
  densitySpin_->setSingleStep(10.0); densitySpin_->setDecimals(1);
  densitySpin_->setSuffix(" kg/m³");
  formR->addRow("水密度 ρ：", densitySpin_);

  auto *formS = new QFormLayout();
  sourceCombo_ = new QComboBox(paramBox);
  sourceCombo_->addItem("矩形驳船（boxSection = true，基准）");
  sourceCombo_->addItem("数学船型（按 L/B/D 生成型值表）");
  sourceCombo_->addItem("SD-08 型线交付物（导入文件）");
  formS->addRow("站表来源：", sourceCombo_);

  paramLayout->addLayout(formL);
  paramLayout->addSpacing(18);
  paramLayout->addLayout(formR);
  paramLayout->addSpacing(18);
  paramLayout->addLayout(formS);

  auto *btnColumn = new QVBoxLayout();
  auto *importHs07Btn = new QPushButton("导入 HS-07（取 KG）", paramBox);
  auto *importSd08Btn = new QPushButton("导入 SD-08 型线", paramBox);
  auto *computeBtn = new QPushButton("计算 GZ 曲线与衡准", paramBox);
  auto *exportBtn = new QPushButton("导出 GZ 结果（HS-08 交付物）", paramBox);
  btnColumn->addWidget(importHs07Btn);
  btnColumn->addWidget(importSd08Btn);
  btnColumn->addWidget(computeBtn);
  btnColumn->addWidget(exportBtn);
  btnColumn->addStretch();
  paramLayout->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  // === 假设声明（常驻，不允许被折叠/隐藏） ===
  assumptionLabel_ = new QLabel(this);
  assumptionLabel_->setWordWrap(true);
  assumptionLabel_->setTextFormat(Qt::RichText);
  assumptionLabel_->setStyleSheet(
      "color:#8a4b00;background:#fff8e6;border:1px solid #f0d9a8;padding:6px;");
  mainLayout->addWidget(assumptionLabel_);

  upstreamLabel_ = new QLabel(this);
  upstreamLabel_->setWordWrap(true);
  upstreamLabel_->setTextFormat(Qt::RichText);
  upstreamLabel_->setStyleSheet("color:#555;");
  mainLayout->addWidget(upstreamLabel_);

  // === 曲线 + 判据表 ===
  auto *midLayout = new QHBoxLayout();

  auto *chartBox = new QGroupBox("静稳性曲线 GZ(φ)", this);
  auto *chartLayout = new QVBoxLayout(chartBox);
  chart_ = new GzChartWidget(chartBox);
  chartLayout->addWidget(chart_);
  midLayout->addWidget(chartBox, 3);

  auto *critBox = new QGroupBox("IMO 五项衡准", this);
  auto *critLayout = new QVBoxLayout(critBox);
  criteriaTable_ = new QTableWidget(critBox);
  criteriaTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  criteriaTable_->setColumnCount(4);
  criteriaTable_->setHorizontalHeaderLabels(
      QStringList() << "判据" << "阈值" << "实算" << "结论");
  // 列宽策略：判据列固定 148 px、阈值按内容、实算列吃掉剩余、结论列固定 64 px。
  // 若四列都 ResizeToContents，表会比分组框宽，PASS/FAIL 与实算值被挤进滚动区看不见。
  criteriaTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  criteriaTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  criteriaTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  criteriaTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
  criteriaTable_->setColumnWidth(0, 148);
  criteriaTable_->setColumnWidth(3, 64);
  criteriaTable_->verticalHeader()->setVisible(false);
  criteriaTable_->setRowCount(5);
  critLayout->addWidget(criteriaTable_);
  critBox->setMinimumWidth(520);
  midLayout->addWidget(critBox, 2);

  mainLayout->addLayout(midLayout, 3);

  resultLabel_ = new QLabel(this);
  resultLabel_->setWordWrap(true);
  resultLabel_->setTextFormat(Qt::RichText);
  mainLayout->addWidget(resultLabel_);

  connect(importHs07Btn, &QPushButton::clicked, this, &GzStabilityDialog::onImportHs07);
  connect(importSd08Btn, &QPushButton::clicked, this, &GzStabilityDialog::onImportSd08);
  connect(computeBtn, &QPushButton::clicked, this, &GzStabilityDialog::onCompute);
  connect(exportBtn, &QPushButton::clicked, this, &GzStabilityDialog::onExport);
  connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &GzStabilityDialog::onSourceChanged);
}

//-----------------------------------------------------------------------------
std::vector<keship::HullStation> GzStabilityDialog::buildStationsFromLines(
    const std::vector<double> &_st, const std::vector<double> &_wl,
    const std::vector<std::vector<double>> &_hb, double _draft, double _B,
    int *_wlIndex) const
{
  std::vector<keship::HullStation> out;
  if (_st.empty() || _wl.empty() || _hb.size() != _st.size()) return out;

  // 为什么取"吃水处水线"而不是 z=0 水线：
  //   型值表的 z=0 是**甲板/水线面**（Wigley 船体在 z=0 处最宽），而 GZ 关心的是
  //   实际吃水处的水线宽。吃水 0.3 m 而 D=1.0 m 时，若误用 z=0 的宽度，
  //   等于把船当成"吃水等于型深"来算 GZ，GZmax 会被系统性高估。
  int best = 0;
  double bestD = 1e18;
  for (int j = 0; j < static_cast<int>(_wl.size()); ++j) {
    // 型值表 z 以水线面为 0、向龙骨为负；吃水对应 z = −T
    const double d = std::abs(_wl[static_cast<std::size_t>(j)] + _draft);
    if (d < bestD) { bestD = d; best = j; }
  }
  if (_wlIndex) *_wlIndex = best;

  std::vector<double> b(_st.size(), 0.0);
  double bMax = 0.0;
  for (std::size_t i = 0; i < _st.size(); ++i) {
    const auto &row = _hb[i];
    b[i] = (best < static_cast<int>(row.size())) ? row[static_cast<std::size_t>(best)] : 0.0;
    bMax = std::max(bMax, b[i]);
  }
  if (!(bMax > 1e-9)) return out;

  // 归一：把该水线的**最大半宽**映射到 B/2。
  // 理由：型值表给出的是"半宽随 x 变化"的形状，但 GZ 内核要求半宽表与输入 B 同尺度
  // （boxSection=false 时不再强制 b=B/2，但横剖面仍以 B/2 为宽度基准）。
  // 归一后中横剖吃水半宽 = B/2，学生改 B 才能真的看到"船变宽 → GZ 变大"。
  const double scale = (0.5 * _B) / bMax;
  out.reserve(_st.size());
  for (std::size_t i = 0; i < _st.size(); ++i) {
    out.push_back({_st[i], b[i] * scale});
  }
  return out;
}

//-----------------------------------------------------------------------------
bool GzStabilityDialog::refresh()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double T = draftSpin_->value();
  const double KG = kgSpin_->value();
  const double rho = densitySpin_->value();

  if (!(L > 1e-6) || !(B > 1e-6) || !(D > 1e-6)) {
    errMsg_ = "主尺度必须为正。";
    return false;
  }
  if (!(T > 1e-6) || T > D) {
    errMsg_ = QString("吃水 T = %1 m 必须在 (0, D] 内（D = %2 m）。").arg(Fmt(T), Fmt(D));
    return false;
  }
  if (!(KG >= 0.0)) {
    errMsg_ = "重心高 KG 不能为负。";
    return false;
  }

  const int src = sourceCombo_->currentIndex();
  stations_.clear();
  waterlineIndex_ = -1;

  if (src == 0) {
    boxSection_ = true;
    stationSource_ = kSourceBox;
  } else if (src == 1) {
    boxSection_ = false;
    stationSource_ = kSourceGen;
    // 用数学船型生成器造型值表：它给出"水线面收敛 + 横剖面收窄"的真实三维船型
    keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(L, B, D, 500.0);
    keship::ShipHullGenerator gen(spec);
    const auto &ld = gen.lines();
    stations_ = buildStationsFromLines(ld.stations, ld.waterlines, ld.halfBreadths,
                                       T, B, &waterlineIndex_);
    if (stations_.size() < 2) {
      errMsg_ = "数学船型生成的型值表为空，无法构造站表。";
      return false;
    }
  } else {
    boxSection_ = false;
    stationSource_ = kSourceSd08;
    if (!hasSd08_) {
      errMsg_ = "尚未导入 SD-08 型线交付物：请先点「导入 SD-08 型线」，"
                "或把站表来源改回前面两项。";
      return false;
    }
    std::vector<double> st, wl;
    std::vector<std::vector<double>> hb;
    for (const auto &v : sd08Stations_) st.push_back(v.toDouble());
    for (const auto &v : sd08Waterlines_) wl.push_back(v.toDouble());
    for (const auto &rv : sd08HalfB_) {
      std::vector<double> row;
      for (const auto &v : rv.toArray()) row.push_back(v.toDouble());
      hb.push_back(row);
    }
    stations_ = buildStationsFromLines(st, wl, hb, T, B, &waterlineIndex_);
    if (stations_.size() < 2) {
      errMsg_ = "SD-08 型线文件里没有可用的 stations/halfBreadths。";
      return false;
    }
  }

  keship::GzInput in;
  in.stations = stations_;
  in.L = L;
  in.B = B;
  in.D = D;
  in.draft = T;
  in.KG = KG;
  in.rhoWater = rho;
  in.boxSection = boxSection_;

  curve_ = keship::GzCurve(in, 90.0, 1.0);
  summary_ = keship::Summarize(in);
  if (curve_.size() < 2) {
    errMsg_ = "GZ 曲线为空：站表或吃水组合无法形成有效排水体积。";
    return false;
  }

  // 排水体积（仅用于与上游浮力对账）：矩形驳船解析，变半宽船梯形积分
  if (boxSection_) {
    displacementM3_ = L * B * T;
  } else {
    double vol = 0.0;
    for (std::size_t i = 0; i + 1 < stations_.size(); ++i) {
      const double dx = stations_[i + 1].x - stations_[i].x;
      vol += 0.5 * (2.0 * stations_[i].halfBreadth + 2.0 * stations_[i + 1].halfBreadth) *
             dx * T;
    }
    displacementM3_ = vol;
  }

  chart_->setCurve(curve_, summary_.gzMaxM, summary_.angleGzMaxDeg,
                   summary_.vanishingAngleDeg);
  fillCriteriaTable();
  updateCache();

  // 假设声明：这是本页最容易被误读的地方，逐字写清"算的是什么船、算什么假设"
  if (boxSection_) {
    assumptionLabel_->setText(
        QString("<b>当前假设：boxSection = true</b> —— 计算对象是<b>矩形驳船</b>"
                "（横剖面恒为宽 2b = B、舷侧直立，b = B/2 由 B 强制给定，站表不参与）。"
                "该口径有 wall-sided 解析式可手算校核，但它<b>不是</b>真实船型："
                "真实船的舭部圆弧、外飘、艏艉收窄都会改变 GZ，尤其是 20° 以后。"));
  } else {
    assumptionLabel_->setText(
        QString("<b>当前假设：boxSection = false</b> —— 站表共 <b>%1</b> 站，"
                "取自 z = −%2 m 水线（第 %3 条水线）沿船长的半宽，已按该水线最大半宽"
                "归一到 B/2。剖面仍按<b>舷侧直立</b>处理（内核只有「单一半宽」这一项几何输入，"
                "无法还原舭部圆弧与外飘），故结果是「同一水线宽度下最保守的直立舷侧等价船」，"
                "<b>不可</b>当作真实船型 GZ 用于送审计算。")
            .arg(stations_.size())
            .arg(Fmt(T, 3))
            .arg(waterlineIndex_ >= 0 ? QString::number(waterlineIndex_) : QString("—")));
  }

  QString up;
  if (hs07Imported_) {
    const double myF = rho * kGravity * displacementM3_;
    const double rel = (upstreamBuoyancyN_ > 1e-9)
                           ? std::abs(myF - upstreamBuoyancyN_) / upstreamBuoyancyN_ : 0.0;
    up = QString("HS-07 交付：KG = <b>%1</b> m、总浮力 <b>%2</b> N　→　本页：KG = <b>%3</b> m、"
                 "ρ·g·∇ = <b>%4</b> N（浮力相对偏差 <b>%5%</b>）")
             .arg(Fmt(upstreamKgM_, 4)).arg(Fmt(upstreamBuoyancyN_, 2))
             .arg(Fmt(KG, 4)).arg(Fmt(myF, 2)).arg(Fmt(rel * 100.0, 3));
    upstreamLabel_->setStyleSheet(rel > 0.05 ? "color:#b00;" : "color:#338833;");
  } else {
    up = "未导入上游交付物：KG 为手工输入值。（HS-07 的 metrics.kgM 可直接导入）";
    upstreamLabel_->setStyleSheet("color:#555;");
  }
  upstreamLabel_->setText(up);

  const double gm = summary_.gm0M;
  resultLabel_->setText(
      QString("GZmax = <b>%1</b> m @ <b>%2°</b>　｜　消失角 <b>%3°</b>　｜　GM0 = <b>%4</b> m　｜　"
              "0–30° 面积 %5 m·rad　｜　30–40° 面积 %6　｜　40°–消失角 面积 %7　｜　"
              "排水体积 ∇ = %8 m³<br>"
              "<b style='color:%9'>五项衡准：%10</b>")
          .arg(Fmt(summary_.gzMaxM, 4))
          .arg(Fmt(summary_.angleGzMaxDeg, 2))
          .arg(Fmt(summary_.vanishingAngleDeg, 2))
          .arg(Fmt(gm, 4))
          .arg(Fmt(summary_.area0to30, 5))
          .arg(Fmt(summary_.area30to40, 5))
          .arg(Fmt(summary_.area40toVanishing, 5))
          .arg(Fmt(displacementM3_, 5))
          .arg(summary_.allPass ? "#1a7f37" : "#b00")
          .arg(summary_.allPass ? "全部 PASS" : "存在 FAIL（见右侧逐项表）"));

  return true;
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::fillCriteriaTable()
{
  struct Row {
    QString name;
    QString threshold;
    QString actual;
    bool pass;
  };
  const std::vector<Row> rows = {
      {"0–30° 面积", "≥ 0.055 m·rad", Fmt(summary_.area0to30, 5), summary_.passArea0to30},
      {"30–40° 面积", "≥ 0.030 m·rad", Fmt(summary_.area30to40, 5), summary_.passArea30to40},
      {"40°–消失角 面积", "≥ 0.090 m·rad", Fmt(summary_.area40toVanishing, 5),
       summary_.passArea40toVan},
      {"GZmax / 对应角", "≥ 0.20 m 且 ∠ ≥ 30°",
       QString("%1 m @ %2°").arg(Fmt(summary_.gzMaxM, 3))
           .arg(Fmt(summary_.angleGzMaxDeg, 1)),
       summary_.passGzMax},
      {"初稳性高 GM0", "≥ 0.150 m", Fmt(summary_.gm0M, 4), summary_.passGm0},
  };

  for (int r = 0; r < static_cast<int>(rows.size()) && r < 5; ++r) {
    const auto &row = rows[static_cast<std::size_t>(r)];
    const QString texts[3] = {row.name, row.threshold, row.actual};
    for (int c = 0; c < 3; ++c) {
      auto *it = new QTableWidgetItem(texts[c]);
      it->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
      it->setToolTip(texts[c]);   // 列宽不够时可悬停看全值
      criteriaTable_->setItem(r, c, it);
    }
    auto *verdict = new QTableWidgetItem(row.pass ? "PASS" : "FAIL");
    verdict->setTextAlignment(Qt::AlignCenter);
    verdict->setForeground(QBrush(QColor(row.pass ? "#1a7f37" : "#c0392b")));
    criteriaTable_->setItem(r, 3, verdict);
  }
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::updateCache()
{
  lastObject_ = QJsonObject();
  lastObject_["type"] = "stability_gz";
  lastObject_["name"] = "HS-08 完整稳性";
  lastObject_["lengthM"] = lengthSpin_->value();
  lastObject_["breadthM"] = breadthSpin_->value();
  lastObject_["depthM"] = depthSpin_->value();
  lastObject_["stationSource"] = stationSource_;
  lastObject_["boxSection"] = boxSection_;
  lastObject_["waterlineIndex"] = waterlineIndex_;

  lastCondition_ = QJsonObject();
  lastCondition_["draftM"] = draftSpin_->value();
  lastCondition_["kgM"] = kgSpin_->value();
  lastCondition_["waterDensity"] = densitySpin_->value();

  lastParams_ = QJsonObject();
  lastParams_["maxDeg"] = 90.0;
  lastParams_["stepDeg"] = 1.0;
  lastParams_["nStations"] = static_cast<int>(stations_.size());
  lastParams_["sectionModel"] = boxSection_ ? "box_rectangular" : "wall_sided_from_halfbreadth";
  lastParams_["areaIntegration"] = "trapezoid_0.05deg";

  lastMetrics_ = QJsonObject();
  lastMetrics_["gzMaxM"] = summary_.gzMaxM;
  lastMetrics_["angleGzMaxDeg"] = summary_.angleGzMaxDeg;
  lastMetrics_["vanishingAngleDeg"] = summary_.vanishingAngleDeg;
  lastMetrics_["gm0M"] = summary_.gm0M;
  lastMetrics_["area0to30"] = summary_.area0to30;
  lastMetrics_["area30to40"] = summary_.area30to40;
  lastMetrics_["area40toVanishing"] = summary_.area40toVanishing;
  lastMetrics_["allPass"] = summary_.allPass;

  QJsonArray curve;
  for (const auto &q : curve_) {
    QJsonObject o;
    o["phiDeg"] = q.phiDeg;
    o["gzM"] = q.gzM;
    curve.append(o);
  }

  QJsonObject crit;
  crit["area0to30"] = summary_.passArea0to30;
  crit["area30to40"] = summary_.passArea30to40;
  crit["area40toVanishing"] = summary_.passArea40toVan;
  crit["gzMax"] = summary_.passGzMax;
  crit["gm0"] = summary_.passGm0;

  QJsonObject thr;
  thr["area0to30"] = 0.055;
  thr["area30to40"] = 0.030;
  thr["area40toVanishing"] = 0.090;
  thr["gzMaxM"] = 0.20;
  thr["angleGzMaxDeg"] = 30.0;
  thr["gm0M"] = 0.15;

  lastExtra_ = QJsonObject();
  lastExtra_["curve"] = curve;
  lastExtra_["criteria"] = crit;
  lastExtra_["imoThresholds"] = thr;
  lastExtra_["boxSection"] = boxSection_;
  lastExtra_["stationSource"] = stationSource_;
  lastExtra_["displacementM3"] = displacementM3_;
  lastExtra_["area0to40"] = summary_.area0to40;

  QJsonArray st;
  for (const auto &s : stations_) {
    QJsonObject o;
    o["x"] = s.x;
    o["halfBreadth"] = s.halfBreadth;
    st.append(o);
  }
  lastExtra_["stations"] = st;
}

//-----------------------------------------------------------------------------
bool GzStabilityDialog::setStationSource(int _index)
{
  if (_index < 0 || _index > 2) return false;
  {
    QSignalBlocker blocker(sourceCombo_);
    sourceCombo_->setCurrentIndex(_index);
  }
  return refresh();
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::onSourceChanged(int _index)
{
  // 切到"矩形驳船"时不需要站表；切到 SD-08 但没导入时，先把话说清楚，
  // 否则用户点「计算」才看到报错，会以为是程序坏了
  if (_index == 2 && !hasSd08_) {
    upstreamLabel_->setText(
        "站表来源已切到「SD-08 型线交付物」，但尚未导入文件 —— 请点「导入 SD-08 型线」。");
    upstreamLabel_->setStyleSheet("color:#b00;");
  }
  if (!refresh()) {
    assumptionLabel_->setText(
        QString("<b style='color:#b00'>当前无法计算：</b>%1").arg(errMsg_));
  }
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::onImportHs07()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 HS-07 浮体布局（取 KG）", "hs07_buoyancy.json", "JSON (*.json)");
  if (path.isEmpty()) return;
  if (!importHs07(path)) {
    QMessageBox::warning(this, "导入 HS-07",
        QString::fromStdString("无法使用该文件：\n" + errMsg_.toStdString()));
  }
}

//-----------------------------------------------------------------------------
bool GzStabilityDialog::importHs07(const QString &_path)
{
  keship::ExpDeliverable d;
  std::string err;
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "HS-07", &err)) {
    errMsg_ = QString::fromStdString(err);
    return false;
  }

  if (!d.metrics.contains("kgM")) {
    errMsg_ = "该文件的 metrics 里没有 kgM，无法作为重心高来源。";
    return false;
  }

  upstreamKgM_ = d.metrics["kgM"].toDouble();
  upstreamBuoyancyN_ = d.metrics["totalBuoyancyN"].toDouble();
  hs07Imported_ = true;

  {
    QSignalBlocker b1(kgSpin_);
    QSignalBlocker b2(lengthSpin_);
    QSignalBlocker b3(breadthSpin_);
    QSignalBlocker b4(depthSpin_);
    QSignalBlocker b5(draftSpin_);
    kgSpin_->setValue(upstreamKgM_);
    // 主尺度与吃水必须跟着上游走：KG 是"距龙骨"的绝对高度，
    // 换了 D 却沿用同一个 KG，等于换了一条船在算
    lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
    breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));
    depthSpin_->setValue(d.object["depthM"].toDouble(depthSpin_->value()));
    draftSpin_->setValue(d.condition["draftM"].toDouble(draftSpin_->value()));
  }

  if (!refresh()) return false;
  std::cout << "HS-08: 已导入 HS-07，KG=" << upstreamKgM_ << " m" << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::onImportSd08()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 SD-08 型线交付物", "sd08_lines.json", "JSON (*.json)");
  if (path.isEmpty()) return;
  if (!importSd08(path)) {
    QMessageBox::warning(this, "导入 SD-08 型线",
        QString::fromStdString("无法使用该文件：\n" + errMsg_.toStdString()));
  }
}

//-----------------------------------------------------------------------------
bool GzStabilityDialog::importSd08(const QString &_path)
{
  keship::ExpDeliverable d;
  std::string err;
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "SD-08", &err)) {
    errMsg_ = QString::fromStdString(err);
    return false;
  }

  sd08Stations_ = d.extra["stations"].toArray();
  sd08Waterlines_ = d.extra["waterlines"].toArray();
  sd08HalfB_ = d.extra["halfBreadths"].toArray();
  if (sd08Stations_.isEmpty() || sd08Waterlines_.isEmpty() || sd08HalfB_.isEmpty()) {
    errMsg_ = "文件缺少 extra.stations / waterlines / halfBreadths，无法构造站表。";
    return false;
  }
  hasSd08_ = true;

  {
    QSignalBlocker b1(sourceCombo_);
    QSignalBlocker b2(lengthSpin_);
    QSignalBlocker b3(breadthSpin_);
    QSignalBlocker b4(depthSpin_);
    sourceCombo_->setCurrentIndex(2);   // 站表来源切到 SD-08
    lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
    breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));
    depthSpin_->setValue(d.object["depthM"].toDouble(depthSpin_->value()));
  }

  if (!refresh()) return false;

  const double wantGm = d.metrics["GMd"].toDouble();
  upstreamLabel_->setText(
      QString("已导入 SD-08 型线：<code>%1</code>（%2 站 × %3 水线）<br>"
              "上游交付 GM<sub>D</sub> = %4 m（静水力法：设计吃水水线面惯性矩）　→　"
              "本页按 GZ 曲线求得 GM0 = %5 m。两者口径不同（等体积倾斜的 dGZ/dφ），"
              "不必逐位相等，但量级应一致。")
          .arg(_path).arg(sd08Stations_.size()).arg(sd08Waterlines_.size())
          .arg(Fmt(wantGm, 4)).arg(Fmt(summary_.gm0M, 4)));
  upstreamLabel_->setStyleSheet("color:#338833;");

  std::cout << "HS-08: 已导入 SD-08 型线，站数 " << sd08Stations_.size() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::onCompute()
{
  if (!refresh()) {
    QMessageBox::warning(this, "完整稳性校核", errMsg_);
    return;
  }
  std::cout << "HS-08: GZmax=" << summary_.gzMaxM << " m @ " << summary_.angleGzMaxDeg
            << " deg, GM0=" << summary_.gm0M << " m, allPass="
            << (summary_.allPass ? "true" : "false") << std::endl;
}

//-----------------------------------------------------------------------------
bool GzStabilityDialog::exportTo(const QString &_path)
{
  if (_path.isEmpty()) return false;
  if (!refresh()) return false;

  keship::ExpDeliverable d;
  d.expId = "HS-08";
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cerr << "HS-08: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << "HS-08: 已导出交付物 " << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void GzStabilityDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 HS-08 GZ 结果", "hs08_gz.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出 GZ 结果",
        QString("导出失败：%1\n请检查输入（%2）").arg(path).arg(errMsg_));
    return;
  }
  QMessageBox::information(this, "导出 GZ 结果",
      QString("已导出：%1\n\nGZmax = %2 m @ %3°　消失角 %4°　GM0 = %5 m\n五项衡准：%6\n"
              "该文件是 SD-07 的输入（SD-07 读 metrics.gm0M 作完整船基准 GM）。")
          .arg(path).arg(Fmt(summary_.gzMaxM, 4)).arg(Fmt(summary_.angleGzMaxDeg, 1))
          .arg(Fmt(summary_.vanishingAngleDeg, 1)).arg(Fmt(summary_.gm0M, 4))
          .arg(summary_.allPass ? "全部 PASS" : "存在 FAIL"));
}
