/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-06 大倾角稳性 —— 原理验证实验实现
 *
 * 数值曲线一律来自 StabilityGzCore（等体积倾斜，逐段解析积分）；
 * 解析曲线一律由 hs06::WallSidedGz 现算，**不**缓存中间值，
 * 保证表格、曲线、交付物三处是同一份数。
 */

#include "LargeAngleStabilityDialog.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <vector>

#include <QBrush>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFontMetricsF>
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

constexpr double kPi = 3.14159265358979323846;
constexpr double kRad2Deg = 180.0 / kPi;

/// 对比区间与判据：0..60° 覆盖教学关心的全部范围（60° 后 GZ 已回落）
constexpr double kMaxDeg = 60.0;
constexpr double kChartStepDeg = 0.5;   ///< 曲线采样步长
constexpr double kTableStepDeg = 5.0;   ///< 表格逐行步长
constexpr double kTolPct = 5.0;         ///< wall-sided "失效" 判据：相对偏差 > 5%

QString Fmt(double _v, int _n = 4) { return QString::number(_v, 'f', _n); }

}  // namespace

namespace keship {
namespace hs06 {

//-----------------------------------------------------------------------------
WallSidedModel BuildWallSidedModel(const std::vector<HullStation> &_stations,
                                   bool _boxSection, double _B, double _L,
                                   double _T, double _KG)
{
  WallSidedModel m;
  if (!(_B > 1e-9) || !(_T > 1e-9)) return m;

  // 半宽序列（沿 x）。矩形剖面直接用 b = B/2 覆盖 [0, L]。
  std::vector<double> xs, bs;
  if (_boxSection || _stations.size() < 2) {
    xs = {0.0, std::max(_L, 1e-9)};
    bs = {0.5 * _B, 0.5 * _B};
  } else {
    std::vector<HullStation> st = _stations;
    std::sort(st.begin(), st.end(),
              [](const HullStation &_a, const HullStation &_b) { return _a.x < _b.x; });
    for (const auto &s : st) {
      if (!std::isfinite(s.x) || !std::isfinite(s.halfBreadth)) continue;
      xs.push_back(s.x);
      bs.push_back(std::max(s.halfBreadth, 0.0));
    }
    if (xs.size() < 2) return m;
  }

  // ∇ = ∫2b·T dx、Ix = ∫(2/3)b³ dx —— 与 StabilityGzCore::BuildHullGeom 同一口径
  double vol = 0.0, ix = 0.0;
  for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
    const double dx = xs[i + 1] - xs[i];
    if (!(dx > 0.0)) continue;
    const double b0 = bs[i], b1 = bs[i + 1];
    vol += 0.5 * (2.0 * b0 * _T + 2.0 * b1 * _T) * dx;
    ix += 0.5 * ((2.0 / 3.0) * b0 * b0 * b0 + (2.0 / 3.0) * b1 * b1 * b1) * dx;
  }
  if (!(vol > 1e-12) || !(ix > 1e-15)) return m;

  m.volumeM3 = vol;
  m.ixM4 = ix;
  m.kbM = 0.5 * _T;
  m.bmM = ix / vol;
  m.gmM = m.kbM + m.bmM - _KG;
  m.valid = true;
  return m;
}

//-----------------------------------------------------------------------------
double WallSidedGz(double _phiDeg, const WallSidedModel &_m)
{
  if (!_m.valid) return 0.0;
  const double a = std::min(std::abs(_phiDeg), 89.9);
  const double r = a * kPi / 180.0;
  const double t = std::tan(r);
  double gz = std::sin(r) * (_m.gmM + 0.5 * _m.bmM * t * t);
  return (_phiDeg < 0.0) ? -gz : gz;
}

//-----------------------------------------------------------------------------
FeatureAngles ComputeFeatureAngles(double _B, double _T, double _D)
{
  FeatureAngles f;
  if (!(_B > 1e-9) || !(_T > 1e-9)) return f;
  f.bilgeDeg = std::atan(2.0 * _T / _B) * kRad2Deg;
  const double rise = _D - _T;
  f.deckDeg = (rise > 0.0) ? std::atan(2.0 * rise / _B) * kRad2Deg : 0.0;
  return f;
}

//-----------------------------------------------------------------------------
SampleTable SampleWallSided(const GzInput &_in, const WallSidedModel &_m,
                            double _maxDeg, double _stepDeg, double _tolPct)
{
  SampleTable t;
  if (!_m.valid || !(_maxDeg > 0.0)) return t;

  const double step = (_stepDeg > 1e-9) ? _stepDeg : 1.0;
  const int n = std::max(1, static_cast<int>(std::floor(_maxDeg / step + 1e-9)));
  t.rows.reserve(static_cast<std::size_t>(n) + 2);

  bool found = false;
  for (int i = 0; i <= n; ++i) {
    SampleRow r;
    r.phiDeg = static_cast<double>(i) * step;
    r.gzNumeric = GZ(r.phiDeg, _in);
    r.gzWallSided = WallSidedGz(r.phiDeg, _m);
    if (std::abs(r.gzWallSided) < 1e-12) {
      r.devPct = 0.0;
    } else {
      r.devPct = std::abs(r.gzNumeric - r.gzWallSided) / std::abs(r.gzWallSided) * 100.0;
    }
    r.valid = r.devPct <= _tolPct;
    t.maxDevPct = std::max(t.maxDevPct, r.devPct);
    // φ=0 两法同为 0，偏差恒为 0；失效点只在 φ>0 上找
    if (!found && r.phiDeg > 0.0 && r.devPct > _tolPct) {
      t.validUntilDeg = r.phiDeg;
      found = true;
    }
    t.rows.push_back(r);
  }

  if (n * step < _maxDeg - 1e-9) {
    SampleRow r;
    r.phiDeg = _maxDeg;
    r.gzNumeric = GZ(r.phiDeg, _in);
    r.gzWallSided = WallSidedGz(r.phiDeg, _m);
    r.devPct = (std::abs(r.gzWallSided) < 1e-12)
                   ? 0.0
                   : std::abs(r.gzNumeric - r.gzWallSided) / std::abs(r.gzWallSided) * 100.0;
    r.valid = r.devPct <= _tolPct;
    t.maxDevPct = std::max(t.maxDevPct, r.devPct);
    if (!found && r.phiDeg > 0.0 && r.devPct > _tolPct) {
      t.validUntilDeg = r.phiDeg;
      found = true;
    }
    t.rows.push_back(r);
  }

  if (!found) t.validUntilDeg = _maxDeg;
  return t;
}

}  // namespace hs06
}  // namespace keship

// ═══════════════════════════════════════════════════════════════════════════
// LargeAngleChartWidget
// ═══════════════════════════════════════════════════════════════════════════

LargeAngleChartWidget::LargeAngleChartWidget(QWidget *_parent) : QWidget(_parent)
{
  // 260 而非 320：窗口被压小时（笔记本 768 px 高），若最小高过大，布局会把画布
  // 挤到超出可见区，底部的图例与横轴刻度被裁掉——正是"曲线看得见、话读不全"的老毛病
  setMinimumHeight(260);
}

void LargeAngleChartWidget::setData(const std::vector<keship::hs06::SampleRow> &_rows,
                                   const keship::hs06::FeatureAngles &_feat,
                                   double _validUntilDeg)
{
  rows_ = _rows;
  feat_ = _feat;
  validUntilDeg_ = _validUntilDeg;
  update();
}

void LargeAngleChartWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  const QRectF plot(64.0, 34.0, std::max(80.0, width() - 96.0),
                    std::max(80.0, height() - 112.0));
  p.setPen(QPen(QColor("#d0d0d0"), 1.0));
  p.drawRect(plot);

  if (rows_.size() < 2) {
    p.setPen(QColor("#888"));
    p.drawText(plot, Qt::AlignCenter, "尚无对比曲线");
    return;
  }

  // ── 纵向范围：以"数值曲线 + 有效域内的解析曲线"为准 ──
  // 失效域内解析式会冲到 1 m 以上（60° 时约为数值的 7 倍），若把它也纳入范围，
  // 数值曲线会被压成一条近乎水平的细线，什么也看不出来。故按数值量级定标，
  // 解析曲线在失效域越界部分**裁剪**在画框内 —— 出顶本身就是失效的视觉证据。
  double maxNum = 0.0, maxWsValid = 0.0, minNum = 0.0;
  for (const auto &r : rows_) {
    maxNum = std::max(maxNum, r.gzNumeric);
    minNum = std::min(minNum, r.gzNumeric);
    if (r.valid) maxWsValid = std::max(maxWsValid, r.gzWallSided);
  }
  const double top = 1.30 * std::max({maxNum, maxWsValid, 1e-6});
  const double bot = std::min(0.0, minNum) - 0.10 * std::max(top, 1e-6);

  const double phiMax = std::max(rows_.back().phiDeg, 1.0);
  auto toPx = [&](double _phi, double _gz) {
    return QPointF(plot.left() + (_phi / phiMax) * plot.width(),
                   plot.bottom() - ((_gz - bot) / (top - bot)) * plot.height());
  };

  // ── 网格 ──
  p.setPen(QPen(QColor("#efefef"), 1.0));
  for (int d = 10; d < static_cast<int>(phiMax); d += 10) {
    p.drawLine(toPx(d, bot), toPx(d, top));
  }
  p.setPen(QColor("#444"));
  for (int d = 0; d <= static_cast<int>(phiMax); d += 10) {
    p.drawText(QRectF(plot.left() + (d / phiMax) * plot.width() - 18, plot.bottom() + 4,
                      36, 16),
               Qt::AlignHCenter | Qt::AlignTop, QString::number(d));
  }
  p.drawText(QRectF(plot.right() - 60, plot.bottom() + 22, 60, 16), Qt::AlignRight, "φ / °");
  p.drawText(QRectF(6, 4, 60, 16), Qt::AlignLeft, "GZ / m");

  // 纵轴刻度（1/2/5×10^k）
  {
    const double raw = (top - bot) / 5.0;
    const double mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
    const double q = raw / mag;
    const double step = ((q <= 1.0) ? 1.0 : (q <= 2.0) ? 2.0 : (q <= 5.0) ? 5.0 : 10.0) * mag;
    for (double v = std::ceil(bot / step) * step; v <= top + 1e-12; v += step) {
      const QPointF pt = toPx(0.0, v);
      p.setPen(QPen(QColor("#f4f4f4"), 1.0));
      p.drawLine(QPointF(plot.left(), pt.y()), QPointF(plot.right(), pt.y()));
      p.setPen(QColor("#444"));
      p.drawText(QRectF(plot.left() - 56, pt.y() - 8, 52, 16),
                 Qt::AlignRight | Qt::AlignVCenter, Fmt(v, 3));
    }
  }

  // 零轴
  p.setPen(QPen(QColor("#999"), 1.2));
  p.drawLine(toPx(0.0, 0.0), toPx(phiMax, 0.0));

  // ── 曲线（裁剪到画框） ──
  p.save();
  p.setClipRect(plot.adjusted(-1.0, -1.0, 1.0, 1.0));

  // 数值 GZ：实线
  QPolygonF line;
  for (const auto &r : rows_) line << toPx(r.phiDeg, r.gzNumeric);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(QColor("#1f4e79"), 2.2));
  p.drawPolyline(line);

  // wall-sided：虚线；有效域实色、失效域浅色（"公式还在算，但已经错了"）
  for (std::size_t i = 0; i + 1 < rows_.size(); ++i) {
    QColor c(192, 57, 43, rows_[i].valid ? 255 : 70);
    p.setPen(QPen(c, 1.8, Qt::DashLine));
    p.drawLine(toPx(rows_[i].phiDeg, rows_[i].gzWallSided),
               toPx(rows_[i + 1].phiDeg, rows_[i + 1].gzWallSided));
  }
  p.restore();

  // ── 三条特征角竖线（标签分层，避免互相压字） ──
  struct Mark {
    double deg;
    QString text;
    QColor color;
    int row;
  };
  const std::vector<Mark> marks = {
      {feat_.bilgeDeg, QString("舭部出水 %1°").arg(Fmt(feat_.bilgeDeg, 1)),
       QColor("#1a7f37"), 0},
      {validUntilDeg_, QString("失效点 %1°").arg(Fmt(validUntilDeg_, 1)),
       QColor("#c0392b"), 1},
      {feat_.deckDeg, QString("甲板入水 %1°").arg(Fmt(feat_.deckDeg, 1)),
       QColor("#e67e22"), 2},
  };
  for (const auto &m : marks) {
    if (!(m.deg > 0.0) || m.deg > phiMax + 1e-9) continue;
    const double x = toPx(m.deg, bot).x();
    p.setPen(QPen(m.color, 1.2, Qt::DashLine));
    p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    p.setPen(m.color);
    const double ty = 6.0 + 15.0 * m.row;
    const double tx = std::min(std::max(x - 20.0, plot.left()), plot.right() - 120.0);
    p.drawText(QRectF(tx, ty, 120.0, 14.0), Qt::AlignLeft | Qt::AlignVCenter, m.text);
  }

  // ── 图例（画在坐标框**下方**的留白里：框内左下角正好压在曲线上） ──
  // 逐项按 QFontMetrics 的实际字宽累加排布：中文字宽随字号变化很大，
  // 写死偏移量必然在某种字号下让两项图例叠在一起（本文件已因此返工过一次）。
  {
    const double ly = std::min(plot.bottom() + 40.0, static_cast<double>(height()) - 26.0);
    QFont lf = p.font();
    lf.setPointSizeF(8.0);
    p.setFont(lf);
    const QFontMetricsF fm(lf, this);
    double x = plot.left() + 2.0;

    auto item = [&](const QColor &_line, Qt::PenStyle _style, const QString &_text,
                    const QColor &_fg) {
      p.setPen(QPen(_line, 1.8, _style));
      p.drawLine(QPointF(x, ly + 6), QPointF(x + 20, ly + 6));
      p.setPen(_fg);
      const double w = fm.horizontalAdvance(_text) + 6.0;
      p.drawText(QRectF(x + 24, ly - 2, w, 16), Qt::AlignLeft | Qt::AlignVCenter, _text);
      x += 24 + w + 16;
    };

    item(QColor("#1f4e79"), Qt::SolidLine, "数值积分", QColor("#333"));
    item(QColor(192, 57, 43, 255), Qt::DashLine, "wall-sided（有效域）",
         QColor(192, 57, 43, 255));
    item(QColor(192, 57, 43, 70), Qt::DashLine, "（失效域：仍在算，但已错）",
         QColor(150, 110, 105));
  }
}

// ═══════════════════════════════════════════════════════════════════════════
// LargeAngleStabilityDialog
// ═══════════════════════════════════════════════════════════════════════════

LargeAngleStabilityDialog::LargeAngleStabilityDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("HS-06 大倾角稳性 —— wall-sided 解析式 vs 数值积分（原理验证）");
  resize(1180, 880);
  buildUi();
  refresh();
}

void LargeAngleStabilityDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的（原理验证）：</b>小倾角公式 GZ ≈ GM·sinφ 在大倾角下不够用，工程上常用 "
      "wall-sided 修正 <b>GZ = sinφ·(GM + ½·BM·tan²φ)</b>。本页把它与"
      "<b>等体积倾斜的数值积分</b>并排对比，回答一个具体问题：<b>这个公式到多少度还能用？</b><br>"
      "与 HS-08 的分工：HS-08 做 IMO 五项衡准的<b>综合校核</b>；本实验只做<b>公式适用性验证</b>，"
      "不做衡准判定。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 参数区 ===
  auto *paramBox = new QGroupBox("输入（主尺度 / 吃水 / 重心）", this);
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
  densitySpin_->setSingleStep(10.0); densitySpin_->setDecimals(1); densitySpin_->setSuffix(" kg/m³");
  formR->addRow("水密度 ρ：", densitySpin_);

  auto *formS = new QFormLayout();
  boxCheck_ = new QCheckBox("矩形剖面（舷侧直立，b = B/2）", paramBox);
  boxCheck_->setChecked(true);
  boxCheck_->setToolTip(
      "矩形剖面 = 解析式与数值积分严格同一船型，最适合验证公式本身；\n"
      "取消勾选则按 L/B/D 生成数学船型的型值表（各站半宽不同），\n"
      "此时三个特征角仍按最大宽度站（b = B/2）给出。");
  formS->addRow("剖面假设：", boxCheck_);

  paramLayout->addLayout(formL);
  paramLayout->addSpacing(18);
  paramLayout->addLayout(formR);
  paramLayout->addSpacing(18);
  paramLayout->addLayout(formS);

  auto *btnColumn = new QVBoxLayout();
  auto *computeBtn = new QPushButton("计算并对比", paramBox);
  auto *exportBtn = new QPushButton("导出对比结果（HS-06 交付物）", paramBox);
  btnColumn->addWidget(computeBtn);
  btnColumn->addWidget(exportBtn);
  btnColumn->addStretch();
  paramLayout->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  assumptionLabel_ = new QLabel(this);
  assumptionLabel_->setWordWrap(true);
  assumptionLabel_->setTextFormat(Qt::RichText);
  assumptionLabel_->setStyleSheet(
      "color:#8a4b00;background:#fff8e6;border:1px solid #f0d9a8;padding:6px;");
  assumptionLabel_->setText(
      QString("<b>模型假设：</b>封闭箱体（甲板水密）、舷侧直立、甲板入水后仍按水密箱体外推；"
              "数值解按等体积倾斜逐段解析积分，解析解按 KB = T/2、BM = Ix/∇。"
              "两者<b>只在「舭部未出水且甲板未入水」时理论上等价</b>。"));
  mainLayout->addWidget(assumptionLabel_);

  // === 曲线 + 表格 ===
  auto *mid = new QHBoxLayout();

  auto *chartBox = new QGroupBox("GZ(φ)：数值 vs wall-sided", this);
  auto *chartLayout = new QVBoxLayout(chartBox);
  chart_ = new LargeAngleChartWidget(chartBox);
  chartLayout->addWidget(chart_);
  mid->addWidget(chartBox, 3);

  auto *tableBox = new QGroupBox("逐角度对比（每 5°）", this);
  auto *tableLayout = new QVBoxLayout(tableBox);
  tableWidget_ = new QTableWidget(tableBox);
  tableWidget_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tableWidget_->setColumnCount(5);
  // 表头用短名：5 列挤在 440 px 时，"数值 GZ / m" 这类长表头会互相压字
  tableWidget_->setHorizontalHeaderLabels(
      QStringList() << "φ/°" << "数值 GZ" << "解析 GZ" << "偏差%" << "解析式");
  tableWidget_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  tableWidget_->verticalHeader()->setVisible(false);
  // 13 行（0…60°，每 5°）必须一屏看全：行高压到 22 px，整表不出现滚动条
  tableWidget_->verticalHeader()->setDefaultSectionSize(22);
  tableLayout->addWidget(tableWidget_);
  tableBox->setMinimumWidth(440);
  mid->addWidget(tableBox, 2);

  mainLayout->addLayout(mid, 3);

  conclusionLabel_ = new QLabel(this);
  conclusionLabel_->setWordWrap(true);
  conclusionLabel_->setTextFormat(Qt::RichText);
  mainLayout->addWidget(conclusionLabel_);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  mainLayout->addWidget(statusLabel_);

  connect(computeBtn, &QPushButton::clicked, this, &LargeAngleStabilityDialog::onCompute);
  connect(exportBtn, &QPushButton::clicked, this, &LargeAngleStabilityDialog::onExport);
  connect(boxCheck_, &QCheckBox::toggled, this, [this](bool) { refresh(); });
}

//-----------------------------------------------------------------------------
std::vector<keship::HullStation> LargeAngleStabilityDialog::buildStationsFromLines(
    double _draft, double _B) const
{
  std::vector<keship::HullStation> out;
  const keship::ShipHullSpec spec = keship::ShipHullSpec::WigleyClassic(
      lengthSpin_->value(), breadthSpin_->value(), depthSpin_->value(), 500.0);
  keship::ShipHullGenerator gen(spec);
  const auto &ld = gen.lines();
  if (ld.stations.empty() || ld.waterlines.empty() || ld.halfBreadths.size() != ld.stations.size()) {
    return out;
  }

  // 型值表的 z 以水线面为 0、向龙骨为负；吃水对应 z = −T。
  // 取最接近的那条水线（与 HS-08 同一口径），再按最大半宽归一到 B/2。
  int best = 0;
  double bestD = 1e18;
  for (int j = 0; j < static_cast<int>(ld.waterlines.size()); ++j) {
    const double d = std::abs(ld.waterlines[static_cast<std::size_t>(j)] + _draft);
    if (d < bestD) { bestD = d; best = j; }
  }

  std::vector<double> b(ld.stations.size(), 0.0);
  double bMax = 0.0;
  for (std::size_t i = 0; i < ld.stations.size(); ++i) {
    const auto &row = ld.halfBreadths[i];
    b[i] = (best < static_cast<int>(row.size())) ? row[static_cast<std::size_t>(best)] : 0.0;
    bMax = std::max(bMax, b[i]);
  }
  if (!(bMax > 1e-9)) return out;

  const double scale = (0.5 * _B) / bMax;
  out.reserve(ld.stations.size());
  for (std::size_t i = 0; i < ld.stations.size(); ++i) {
    out.push_back({ld.stations[i], b[i] * scale});
  }
  return out;
}

//-----------------------------------------------------------------------------
bool LargeAngleStabilityDialog::refresh()
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
  if (KG < 0.0) {
    errMsg_ = "重心高 KG 不能为负。";
    return false;
  }

  const bool box = boxCheck_->isChecked();
  stations_.clear();
  if (!box) {
    stations_ = buildStationsFromLines(T, B);
    if (stations_.size() < 2) {
      errMsg_ = "数学船型生成的型值表为空，无法构造站表。";
      return false;
    }
  }

  input_ = keship::GzInput();
  input_.stations = stations_;
  input_.L = L;
  input_.B = B;
  input_.D = D;
  input_.draft = T;
  input_.KG = KG;
  input_.rhoWater = rho;
  input_.boxSection = box;

  model_ = keship::hs06::BuildWallSidedModel(stations_, box, B, L, T, KG);
  if (!model_.valid) {
    errMsg_ = "无法构造 wall-sided 解析模型（排水体积或水线面惯性矩为 0）。";
    return false;
  }
  feat_ = keship::hs06::ComputeFeatureAngles(B, T, D);
  summary_ = keship::Summarize(input_);
  table_ = keship::hs06::SampleWallSided(input_, model_, kMaxDeg, kChartStepDeg, kTolPct);

  chart_->setData(table_.rows, feat_, table_.validUntilDeg);
  fillTable();
  updateCache();

  // 结论：一句话回答"本船型的 wall-sided 有效上限是多少度"
  const double devAtMax = table_.rows.empty() ? 0.0 : table_.rows.back().devPct;
  conclusionLabel_->setText(
      QString("<b>结论：</b>本船型 wall-sided 解析式的有效上限为 <b style='color:#c0392b'>%1°</b>"
              "（两法相对偏差首次超过 %2%）。<br>"
              "物理上舭部在 <b>%3°</b> 出水，公式前提（水线同时切两舷）即已破坏，"
              "但数值偏差要到 %1° 才到 %2%；甲板边缘在 <b>%4°</b> 入水后解析式彻底失真"
              "（%5° 时偏差已达 %6%）。<br>"
              "<b>教学要点：</b>「舭部出水 → 公式已不成立」与「偏差 > 5% → 工程可察觉」是两个不同的角，"
              "前者是物理前提，后者是精度判据。工程上 <b>%3° 以上一律用数值积分</b>。")
          .arg(Fmt(table_.validUntilDeg, 1))
          .arg(Fmt(kTolPct, 0))
          .arg(Fmt(feat_.bilgeDeg, 1))
          .arg(Fmt(feat_.deckDeg, 1))
          .arg(Fmt(kMaxDeg, 0))
          .arg(Fmt(devAtMax, 1)));

  statusLabel_->setText(
      QString("GM = KB + BM − KG = %1 + %2 − %3 = <b>%4</b> m　｜　∇ = %5 m³　｜　"
              "Ix = %6 m⁴　｜　GZmax = %7 m @ %8°　｜　采样 %9 点")
          .arg(Fmt(model_.kbM, 4)).arg(Fmt(model_.bmM, 4)).arg(Fmt(KG, 4))
          .arg(Fmt(model_.gmM, 4)).arg(Fmt(model_.volumeM3, 5)).arg(Fmt(model_.ixM4, 6))
          .arg(Fmt(summary_.gzMaxM, 4)).arg(Fmt(summary_.angleGzMaxDeg, 2))
          .arg(static_cast<int>(table_.rows.size())));

  assumptionLabel_->setText(
      box
          ? QString("<b>当前假设：矩形剖面（舷侧直立，b = B/2）。</b>封闭箱体、甲板水密、"
                    "甲板入水后仍按水密箱体外推。解析式与数值解<b>严格同一船型</b>，"
                    "两法在舭部出水角 %1° 之前应完全重合 —— 这正是本实验的对照基准。")
                .arg(Fmt(feat_.bilgeDeg, 1))
          : QString("<b>当前假设：数学船型（Wigley，%1 站）。</b>取 z = −%2 m 水线沿船长半宽并"
                    "归一到 B/2；剖面仍按舷侧直立矩形（内核只有单一半宽这一项几何输入，"
                    "无法还原舭部圆弧与外飘），故为「同一水线宽度下的直立舷侧等价船」。"
                    "三个特征角按最大宽度站给出。")
                .arg(static_cast<int>(stations_.size())).arg(Fmt(T, 3)));

  return true;
}

//-----------------------------------------------------------------------------
void LargeAngleStabilityDialog::fillTable()
{
  // 只取 5° 的整数倍行（曲线是 0.5° 密采样，表格不必逐行铺 121 行）
  std::vector<keship::hs06::SampleRow> rows;
  for (const auto &r : table_.rows) {
    const double m = std::fmod(r.phiDeg, kTableStepDeg);
    if (std::abs(m) < 1e-6 || std::abs(m - kTableStepDeg) < 1e-6) rows.push_back(r);
  }

  tableWidget_->clearContents();
  tableWidget_->setRowCount(static_cast<int>(rows.size()));
  for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
    const auto &row = rows[static_cast<std::size_t>(r)];
    const QString texts[4] = {
        QString::number(row.phiDeg, 'f', 0), QString::number(row.gzNumeric, 'f', 5),
        QString::number(row.gzWallSided, 'f', 5), QString::number(row.devPct, 'f', 2)};
    for (int c = 0; c < 4; ++c) {
      auto *it = new QTableWidgetItem(texts[c]);
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      tableWidget_->setItem(r, c, it);
    }
    auto *verdict = new QTableWidgetItem(row.valid ? "有效" : "失效");
    verdict->setTextAlignment(Qt::AlignCenter);
    verdict->setForeground(QBrush(QColor(row.valid ? "#1a7f37" : "#c0392b")));
    tableWidget_->setItem(r, 4, verdict);
  }
}

//-----------------------------------------------------------------------------
void LargeAngleStabilityDialog::updateCache()
{
  const bool box = boxCheck_->isChecked();

  lastObject_ = QJsonObject();
  lastObject_["type"] = "gz_wall_sided";
  lastObject_["name"] = "HS-06 大倾角稳性（原理验证）";
  lastObject_["lengthM"] = lengthSpin_->value();
  lastObject_["breadthM"] = breadthSpin_->value();
  lastObject_["depthM"] = depthSpin_->value();
  lastObject_["boxSection"] = box;
  lastObject_["nStations"] = static_cast<int>(stations_.size());

  lastCondition_ = QJsonObject();
  lastCondition_["draftM"] = draftSpin_->value();
  lastCondition_["kgM"] = kgSpin_->value();
  lastCondition_["waterDensity"] = densitySpin_->value();

  lastParams_ = QJsonObject();
  lastParams_["lengthM"] = lengthSpin_->value();
  lastParams_["breadthM"] = breadthSpin_->value();
  lastParams_["depthM"] = depthSpin_->value();
  lastParams_["draftM"] = draftSpin_->value();
  lastParams_["kgM"] = kgSpin_->value();
  lastParams_["waterDensity"] = densitySpin_->value();
  lastParams_["boxSection"] = box;
  lastParams_["maxDeg"] = kMaxDeg;
  lastParams_["stepDeg"] = kChartStepDeg;
  lastParams_["tableStepDeg"] = kTableStepDeg;
  lastParams_["deviationTolerancePct"] = kTolPct;
  lastParams_["deviationDefinition"] =
      QString("|GZ_numeric - GZ_wallSided| / |GZ_wallSided| * 100");
  lastParams_["assumptions"] =
      QString("封闭箱体（甲板水密）、舷侧直立（每站为矩形剖面，KB=T/2）、"
              "甲板入水后仍按水密箱体外推；数值解为等体积倾斜逐段解析积分");

  lastMetrics_ = QJsonObject();
  // gm0M 取**解析口径** GM = KB + BM − KG（wall-sided 式里用的就是它，可与手算逐位对账）；
  // 数值曲线在 0° 处的斜率另存 gm0NumericM，两者之差只反映中心差分误差（~1e-7），
  // 一并给出才能证明"两条曲线是同一条物理曲线"。
  lastMetrics_["gm0M"] = model_.gmM;
  lastMetrics_["gzMaxM"] = summary_.gzMaxM;
  lastMetrics_["angleGzMaxDeg"] = summary_.angleGzMaxDeg;
  lastMetrics_["bilgeEmergenceDeg"] = feat_.bilgeDeg;
  lastMetrics_["deckImmersionDeg"] = feat_.deckDeg;
  lastMetrics_["wallSidedValidUntilDeg"] = table_.validUntilDeg;
  lastMetrics_["maxDeviationPct"] = table_.maxDevPct;
  lastMetrics_["boxSection"] = box;

  QJsonArray tableArr;
  for (const auto &r : table_.rows) {
    QJsonObject o;
    o["phi"] = r.phiDeg;
    o["gzNumeric"] = r.gzNumeric;
    o["gzWallSided"] = r.gzWallSided;
    o["devPct"] = r.devPct;
    o["valid"] = r.valid;
    tableArr.append(o);
  }

  QJsonObject ws;
  ws["kbM"] = model_.kbM;
  ws["bmM"] = model_.bmM;
  ws["gmM"] = model_.gmM;
  ws["volumeM3"] = model_.volumeM3;
  ws["ixM4"] = model_.ixM4;
  /// 数值曲线在 0° 处的斜率（中心差分）—— 与解析 GM 对照，
  /// 二者之差只反映差分误差，可证明"两条曲线是同一条物理曲线"
  ws["gm0NumericM"] = summary_.gm0M;

  lastExtra_ = QJsonObject();
  lastExtra_["table"] = tableArr;
  lastExtra_["wallSided"] = ws;
  lastExtra_["tolerancePct"] = kTolPct;
}

//-----------------------------------------------------------------------------
void LargeAngleStabilityDialog::onCompute()
{
  if (!refresh()) {
    QMessageBox::warning(this, "大倾角稳性对比", errMsg_);
    return;
  }
  std::cout << "HS-06: GM=" << model_.gmM << " m, bilge=" << feat_.bilgeDeg
            << " deg, deck=" << feat_.deckDeg << " deg, wall-sided valid until "
            << table_.validUntilDeg << " deg, maxDev=" << table_.maxDevPct << "%" << std::endl;
}

//-----------------------------------------------------------------------------
bool LargeAngleStabilityDialog::exportTo(const QString &_path)
{
  if (_path.isEmpty()) return false;
  if (!refresh()) return false;

  keship::ExpDeliverable d;
  d.expId = "HS-06";
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    errMsg_ = QString::fromStdString(err);
    std::cerr << "HS-06: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << "HS-06: 已导出交付物 " << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
bool LargeAngleStabilityDialog::setBoxSection(bool _box)
{
  if (boxCheck_->isChecked() == _box) return refresh();
  {
    // 先屏蔽 toggled→refresh 的连接，避免"设置一次算两遍"
    const QSignalBlocker blocker(boxCheck_);
    boxCheck_->setChecked(_box);
  }
  return refresh();
}

//-----------------------------------------------------------------------------
void LargeAngleStabilityDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 HS-06 对比结果", "hs06_gz.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出对比结果",
        QString("导出失败：%1\n请检查输入（%2）").arg(path).arg(errMsg_));
    return;
  }

  QMessageBox::information(this, "导出对比结果",
      QString("已导出：%1\n\nGM0 = %2 m　｜　GZmax = %3 m @ %4°\n"
              "舭部出水 %5°　｜　甲板入水 %6°　｜　wall-sided 失效点 %7°\n"
              "最大偏差 %8%（0–60°）")
          .arg(path).arg(Fmt(model_.gmM, 4)).arg(Fmt(summary_.gzMaxM, 4))
          .arg(Fmt(summary_.angleGzMaxDeg, 1)).arg(Fmt(feat_.bilgeDeg, 1))
          .arg(Fmt(feat_.deckDeg, 1)).arg(Fmt(table_.validUntilDeg, 1))
          .arg(Fmt(table_.maxDevPct, 2)));
}
