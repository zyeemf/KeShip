/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— RP-01 / RP-02 / RP-05 / RP-06 阻力实验对话框（实现）
 *
 * 本文件只做三件事：① 采集参数；② 调 keship::ResistanceCore 的纯函数；
 * ③ 把结果画成表/曲线并导出 keship::ExpDeliverable。**不复制任何阻力公式**——
 * 一旦在 GUI 里重写 Cf、1+k、Ct，界面数与无头判据（keship_ctl）就会各算一套。
 */

#include "ResistanceDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QCheckBox>
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

/// 海水密度与运动黏度：与 ResistanceInput 默认值（15℃ 海水）一致。
/// 不放到界面上调，是因为本链的四个实验要横向对比，必须共用同一流体口径；
/// 学生若真需要改，走内核默认值而不是在 GUI 里各填一个数。
constexpr double kRhoWater = 1025.0;
constexpr double kNuWater = 1.139e-6;
constexpr double kRhoAir = 1.225;

/// 各模式的标题与建议文件名（文件名固定，下游测试才能按名找文件）
struct ModeSpec {
  const char *id;
  const char *title;
  const char *file;
};

const ModeSpec kModes[] = {
    {"RP-01", "RP-01 阻力成分分解", "rp01_resistance.json"},
    {"RP-02", "RP-02 阻力曲线测定", "rp02_curve.json"},
    {"RP-05", "RP-05 船型阻力优化", "rp05_optimization.json"},
    {"RP-06", "RP-06 尺度换算与实船外推", "rp06_extrapolation.json"},
};
constexpr int kModeCount = 4;

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

/// 表单行随字段一起隐藏（QFormLayout 的行标签是内部建的，必须单独取出来藏）
void HideFormRow(QFormLayout *_form, QWidget *_field, bool _hide)
{
  if (!_form || !_field) return;
  _field->setVisible(!_hide);
  if (QWidget *label = _form->labelForField(_field)) label->setVisible(!_hide);
}

/// 在 [_vMin,_vMax] 上取 _n 点的总阻力平均值。
/// 与 CbForMinResistance 的目标函数同口径（11 点等距），否则表里的
/// 「平均阻力」列与最优 Cb 那一行会对不上账。
double MeanTotal(const keship::ResistanceInput &_in, double _vMin, double _vMax,
                 int _n)
{
  double lo = std::max(_vMin, 0.0);
  double hi = std::max(_vMax, 0.0);
  if (hi < lo) std::swap(lo, hi);
  const int n = std::max(_n, 2);
  double sum = 0.0;
  for (int i = 0; i < n; ++i) {
    const double v = lo + (hi - lo) * static_cast<double>(i) /
                              static_cast<double>(n - 1);
    sum += keship::ResistanceAtSpeed(_in, v).Rtotal;
  }
  return sum / static_cast<double>(n);
}

/// log–log 最小二乘斜率：R ≈ k·V^m 的 m。
/// RP-02 的教学落点就是「这条幂律的指数在 2（摩擦主导）到 4（兴波主导）之间移动」。
double LogLogSlope(const std::vector<double> &_x, const std::vector<double> &_y)
{
  double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
  int n = 0;
  for (size_t i = 0; i < _x.size() && i < _y.size(); ++i) {
    if (!(_x[i] > 1e-9) || !(_y[i] > 1e-9)) continue;
    const double lx = std::log(_x[i]);
    const double ly = std::log(_y[i]);
    sx += lx; sy += ly; sxx += lx * lx; sxy += lx * ly;
    ++n;
  }
  if (n < 2) return 0.0;
  const double den = static_cast<double>(n) * sxx - sx * sx;
  if (std::abs(den) < 1e-12) return 0.0;
  return (static_cast<double>(n) * sxy - sx * sy) / den;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// 曲线控件
// ═══════════════════════════════════════════════════════════════════════════
ResistanceChartWidget::ResistanceChartWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumHeight(280);
  setAutoFillBackground(false);
}

void ResistanceChartWidget::SetAxes(const QString &_title, const QString &_xLabel,
                                    const QString &_yLabel)
{
  title_ = _title;
  xLabel_ = _xLabel;
  yLabel_ = _yLabel;
  update();
}

void ResistanceChartWidget::SetSeries(const std::vector<Series> &_series)
{
  series_ = _series;
  barNames_.clear();
  barValues_.clear();
  update();
}

void ResistanceChartWidget::AddMarker(double _x, double _y, const QString &_label,
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

void ResistanceChartWidget::SetBars(const QStringList &_names,
                                    const std::vector<double> &_values,
                                    const QString &_unit)
{
  barNames_ = _names;
  barValues_ = _values;
  barUnit_ = _unit;
  series_.clear();
  markers_.clear();
  update();
}

void ResistanceChartWidget::Clear()
{
  series_.clear();
  markers_.clear();
  barNames_.clear();
  barValues_.clear();
  update();
}

void ResistanceChartWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(200, 205, 215));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  // 左边界要给「空气 Rwind」这类标签留位；画曲线时 66 px 够，画占比条不够
  const int L = barNames_.isEmpty() ? 66 : 104;
  const int R = 18, T = 34, B = 46;
  const QRectF plot(L, T, std::max(10, width() - L - R),
                    std::max(10, height() - T - B));

  p.setPen(QColor("#333"));
  QFont titleFont("sans", 9);
  titleFont.setBold(true);
  p.setFont(titleFont);
  p.drawText(QRectF(0, 6, width(), 18), Qt::AlignCenter, title_);

  p.setFont(QFont("sans", 9));
  if (!barNames_.empty()) {
    paintBars(p, plot);
    return;
  }
  if (series_.empty()) {
    p.setPen(QColor("#999"));
    p.drawText(plot, Qt::AlignCenter, "尚无数据：设置参数后点「计算」");
    return;
  }
  paintPlot(p, plot);
}

void ResistanceChartWidget::paintPlot(QPainter &_p, const QRectF &_plot)
{
  double xMin = 1e18, xMax = -1e18, yMin = 1e18, yMax = -1e18;
  for (const auto &s : series_) {
    for (const auto &pt : s.pts) {
      xMin = std::min(xMin, pt.x());
      xMax = std::max(xMax, pt.x());
      yMin = std::min(yMin, pt.y());
      yMax = std::max(yMax, pt.y());
    }
  }
  if (!(xMax > xMin)) { xMin = 0.0; xMax = std::max(1.0, xMax); }
  if (!(yMax > yMin)) { yMax = yMin + 1.0; }
  // 纵轴一律从 0 起：力/功率类曲线若自动缩放下限，学生会误判「低航速无阻力」
  yMin = std::min(yMin, 0.0);
  const double yPad = 0.12 * std::max(yMax - yMin, 1e-9);
  yMax += yPad;
  const double xPad = 0.02 * std::max(xMax - xMin, 1e-9);
  xMin -= xPad;
  xMax += xPad;

  auto X = [&](double _x) {
    return _plot.left() + (_x - xMin) / (xMax - xMin) * _plot.width();
  };
  auto Y = [&](double _y) {
    return _plot.bottom() - (_y - yMin) / (yMax - yMin) * _plot.height();
  };

  // 网格 + 纵轴刻度：小数位按量级统一，避免同一轴出现 0.000 / 86.822 / 174 混排
  const int yDec = (yMax >= 100.0) ? 0 : (yMax >= 10.0 ? 1 : 3);
  const int nY = 4;
  for (int i = 0; i <= nY; ++i) {
    const double v = yMin + (yMax - yMin) * i / nY;
    const double y = Y(v);
    _p.setPen(QPen(QColor("#e6e6ee"), 1));
    _p.drawLine(QPointF(_plot.left(), y), QPointF(_plot.right(), y));
    _p.setPen(QColor("#666"));
    _p.drawText(QRectF(4, y - 8, _plot.left() - 10, 16),
                Qt::AlignRight | Qt::AlignVCenter, Num(v, yDec));
  }
  const int nX = 6;
  for (int i = 0; i <= nX; ++i) {
    const double v = xMin + (xMax - xMin) * i / nX;
    const double x = X(v);
    _p.setPen(QPen(QColor("#f0f0f5"), 1));
    _p.drawLine(QPointF(x, _plot.top()), QPointF(x, _plot.bottom()));
    _p.setPen(QColor("#666"));
    _p.drawText(QRectF(x - 22, _plot.bottom() + 4, 44, 16), Qt::AlignCenter,
                Num(v, 2));
  }
  _p.setPen(QPen(QColor("#333"), 1.2));
  _p.drawLine(_plot.bottomLeft(), _plot.bottomRight());
  _p.drawLine(_plot.bottomLeft(), _plot.topLeft());

  _p.setPen(QColor("#444"));
  _p.drawText(QRectF(_plot.left(), _plot.bottom() + 22, _plot.width(), 16),
              Qt::AlignCenter, xLabel_);
  _p.drawText(QRectF(_plot.left(), _plot.top() - 16, 220, 14),
              Qt::AlignLeft | Qt::AlignVCenter, yLabel_);

  // 图例（横排右对齐，不遮曲线主体）
  int legendX = static_cast<int>(_plot.right());
  const QFontMetrics fm(_p.font());
  for (auto it = series_.rbegin(); it != series_.rend(); ++it) {
    const int textW = fm.horizontalAdvance(it->name) + 26;
    legendX -= textW;
    const double y = _plot.top() - 9;
    QPen pen(it->color, 2.2);
    if (it->dashed) pen.setStyle(Qt::DashLine);
    _p.setPen(pen);
    _p.drawLine(QPointF(legendX, y), QPointF(legendX + 16, y));
    _p.setPen(QColor("#444"));
    _p.drawText(QRectF(legendX + 20, y - 8, textW, 16),
                Qt::AlignLeft | Qt::AlignVCenter, it->name);
  }

  // 曲线
  for (const auto &s : series_) {
    if (s.pts.size() < 1) continue;
    auto sorted = s.pts;
    std::sort(sorted.begin(), sorted.end(),
              [](const QPointF &a, const QPointF &b) { return a.x() < b.x(); });
    QPen pen(s.color, 2.0);
    if (s.dashed) pen.setStyle(Qt::DashLine);
    _p.setPen(pen);
    // 必须显式清掉画刷：上一轮画点留下的实心画刷会把折线路径**填充成多边形**
    _p.setBrush(Qt::NoBrush);
    if (sorted.size() == 1) {
      _p.setBrush(s.color);
      _p.drawEllipse(sorted[0], 4.0, 4.0);
      continue;
    }
    QPainterPath path;
    path.moveTo(X(sorted[0].x()), Y(sorted[0].y()));
    for (size_t i = 1; i < sorted.size(); ++i)
      path.lineTo(X(sorted[i].x()), Y(sorted[i].y()));
    _p.drawPath(path);
    _p.setBrush(s.color);
    _p.setPen(Qt::NoPen);
    for (const auto &pt : sorted) _p.drawEllipse(QPointF(X(pt.x()), Y(pt.y())), 2.6, 2.6);
  }
  _p.setBrush(Qt::NoBrush);

  // 标记点（峰值 / 最优点 / 工作点）
  for (const auto &m : markers_) {
    if (m.x < xMin || m.x > xMax) continue;
    const double x = X(m.x);
    const double y = Y(std::max(m.y, yMin));
    _p.setPen(QPen(m.color, 1.0, Qt::DashLine));
    _p.drawLine(QPointF(x, _plot.bottom()), QPointF(x, y));
    _p.setBrush(Qt::NoBrush);
    _p.setPen(QPen(m.color, 1.6));
    _p.drawEllipse(QPointF(x, y), 6.0, 6.0);
    _p.setPen(m.color);
    // 标记点靠右时把标签放到左侧，靠顶时放到下方：
    // 否则 RP-02 的 Vmax 点（右上角）会把标签压在图例上
    const double labelY = (y - _plot.top() < 40.0) ? (y + 12.0) : (y - 26.0);
    QRectF labelRect;
    Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter;
    if (x > _plot.right() - 190.0) {
      labelRect = QRectF(x - 152.0, labelY, 144.0, 16.0);
      align = Qt::AlignRight | Qt::AlignVCenter;
    } else {
      labelRect = QRectF(x + 8.0, labelY, 144.0, 16.0);
    }
    _p.drawText(labelRect, align, m.label);
  }
}

void ResistanceChartWidget::paintBars(QPainter &_p, const QRectF &_plot)
{
  double total = 0.0;
  for (double v : barValues_) total += std::max(v, 0.0);
  if (total <= 1e-12) {
    _p.setPen(QColor("#999"));
    _p.drawText(_plot, Qt::AlignCenter, "三项阻力均为 0（航速为 0 且无风）");
    return;
  }

  const QColor colors[3] = {QColor("#1f6feb"), QColor("#c0392b"), QColor("#7f8c8d")};
  const int n = std::min<int>(static_cast<int>(barNames_.size()),
                              static_cast<int>(barValues_.size()));
  const double rowH = _plot.height() / std::max(n, 1);
  for (int i = 0; i < n; ++i) {
    const double v = std::max(barValues_[i], 0.0);
    const double share = v / total;
    const double y = _plot.top() + rowH * i + rowH * 0.22;
    const double h = rowH * 0.56;

    _p.setPen(QColor("#444"));
    _p.drawText(QRectF(4, y, _plot.left() - 10, h),
                Qt::AlignRight | Qt::AlignVCenter, barNames_[i]);

    const double x0 = _plot.left();
    const double wFull = _plot.width() * 0.72;
    _p.setPen(Qt::NoPen);
    _p.setBrush(QColor("#eceff4"));
    _p.drawRect(QRectF(x0, y, wFull, h));
    _p.setBrush(colors[i % 3]);
    _p.drawRect(QRectF(x0, y, wFull * share, h));

    _p.setPen(QColor("#333"));
    _p.drawText(QRectF(x0 + wFull + 8, y, _plot.width() - wFull - 8, h),
                Qt::AlignLeft | Qt::AlignVCenter,
                QString("%1 %2（%3%）")
                    .arg(Num(v, 1)).arg(barUnit_).arg(Num(share * 100.0, 2)));
  }
  _p.setPen(QColor("#666"));
  _p.drawText(QRectF(_plot.left(), _plot.bottom() - 16, _plot.width(), 16),
              Qt::AlignLeft | Qt::AlignVCenter,
              QString("合计（三项之和）= %1 %2").arg(Num(total, 1)).arg(barUnit_));
}

// ═══════════════════════════════════════════════════════════════════════════
// 对话框
// ═══════════════════════════════════════════════════════════════════════════
ResistanceDialog::ResistanceDialog(QWidget *_parent, const QString &_expId)
    : QDialog(_parent), expId_(_expId)
{
  if (SpecOf(expId_).id != expId_.toStdString()) expId_ = "RP-01";
  buildUi();
  setMode(expId_);
  resize(1160, 880);
}

//-----------------------------------------------------------------------------
void ResistanceDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>把总阻力拆成「黏性 R<sub>v</sub> / 兴波 R<sub>r</sub> / 空气 "
      "R<sub>wind</sub>」三项，观察它们随航速、方形系数与尺度的变化。<br>"
      "<span style='color:#666;'>术语对照：本平台黏性项含形状因子 (1+k)，按 ITTC 术语应记作"
      "<b>黏性阻力 R<sub>v</sub></b>；教科书 ITTC-57 的平板摩擦阻力 R<sub>f</sub> 不含 (1+k)，"
      "二者数值不同，对照时请勿混用。</span><br>"
      "顶部下拉切换：<b>RP-01</b> 单点分解 → <b>RP-02</b> R–V 曲线 → "
      "<b>RP-05</b> C<sub>b</sub> 寻优 → <b>RP-06</b> 模型→实船外推。"
      "四项共用同一船体输入与同一个内核（<code>ResistanceCore</code>）。<br>"
      "<b>交付物：</b>「导出交付物」按当前模式写 <code>rp01_resistance.json</code> / "
      "<code>rp02_curve.json</code> / <code>rp05_optimization.json</code> / "
      "<code>rp06_extrapolation.json</code>。",
      this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 模式选择 ===
  auto *modeRow = new QHBoxLayout();
  modeRow->addWidget(new QLabel("实验模式：", this));
  modeCombo_ = new QComboBox(this);
  for (int i = 0; i < kModeCount; ++i)
    modeCombo_->addItem(QString::fromUtf8(kModes[i].title),
                        QString::fromLatin1(kModes[i].id));
  modeRow->addWidget(modeCombo_, 1);
  mainLayout->addLayout(modeRow);

  // === 参数区 ===
  auto *paramBox = new QGroupBox("参数", this);
  auto *paramRow = new QHBoxLayout(paramBox);

  auto *hullForm = new QFormLayout();
  hullForm->setLabelAlignment(Qt::AlignRight);

  lengthSpin_ = new QDoubleSpinBox(paramBox);
  lengthSpin_->setRange(1.0, 400.0); lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5); lengthSpin_->setDecimals(3);
  lengthSpin_->setSuffix(" m");
  hullForm->addRow("水线长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(paramBox);
  breadthSpin_->setRange(0.2, 60.0); breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1); breadthSpin_->setDecimals(3);
  breadthSpin_->setSuffix(" m");
  hullForm->addRow("型宽 B：", breadthSpin_);

  draftSpin_ = new QDoubleSpinBox(paramBox);
  draftSpin_->setRange(0.05, 25.0); draftSpin_->setValue(0.3);
  draftSpin_->setSingleStep(0.05); draftSpin_->setDecimals(3);
  draftSpin_->setSuffix(" m");
  hullForm->addRow("吃水 T：", draftSpin_);

  cbSpin_ = new QDoubleSpinBox(paramBox);
  cbSpin_->setRange(0.30, 0.88); cbSpin_->setValue(0.60);
  cbSpin_->setSingleStep(0.01); cbSpin_->setDecimals(3);
  hullForm->addRow("方形系数 Cb：", cbSpin_);

  cmSpin_ = new QDoubleSpinBox(paramBox);
  cmSpin_->setRange(0.50, 0.99); cmSpin_->setValue(0.90);
  cmSpin_->setSingleStep(0.01); cmSpin_->setDecimals(3);
  hullForm->addRow("中横剖系数 Cm：", cmSpin_);

  cwpSpin_ = new QDoubleSpinBox(paramBox);
  cwpSpin_->setRange(0.50, 0.95); cwpSpin_->setValue(0.70);
  cwpSpin_->setSingleStep(0.01); cwpSpin_->setDecimals(3);
  hullForm->addRow("水线面系数 Cwp：", cwpSpin_);

  volumeSpin_ = new QDoubleSpinBox(paramBox);
  volumeSpin_->setRange(0.001, 200000.0); volumeSpin_->setValue(1.0);
  volumeSpin_->setSingleStep(0.1); volumeSpin_->setDecimals(4);
  volumeSpin_->setSuffix(" m³");
  hullForm->addRow("排水体积 ∇：", volumeSpin_);

  selfCheck_ = new QCheckBox("∇ 随 Cb·L·B·T 自洽", paramBox);
  selfCheck_->setChecked(true);
  selfCheck_->setToolTip(
      "内核的 (1+k) 回归式里含 (L/∇^(1/3)) 与 (L³/∇) 两项。\n"
      "若 ∇ 与 Cb·L·B·T 不自洽，改 Cb 时形状因子不跟着变，\n"
      "「最优 Cb」会退化成「湿面积最小的 Cb」，物理上讲不通。");
  hullForm->addRow(selfCheck_);

  paramRow->addLayout(hullForm);

  auto *condForm = new QFormLayout();
  condForm->setLabelAlignment(Qt::AlignRight);
  condForm_ = condForm;

  speedSpin_ = new QDoubleSpinBox(paramBox);
  speedSpin_->setRange(0.0, 20.0); speedSpin_->setValue(2.0);
  speedSpin_->setSingleStep(0.1); speedSpin_->setDecimals(3);
  speedSpin_->setSuffix(" m/s");
  condForm->addRow("设计航速 V：", speedSpin_);

  windSpin_ = new QDoubleSpinBox(paramBox);
  windSpin_->setRange(0.0, 40.0); windSpin_->setValue(0.0);
  windSpin_->setSingleStep(0.5); windSpin_->setDecimals(2);
  windSpin_->setSuffix(" m/s");
  windSpin_->setToolTip("相对风速（逆风时应加上船速）；0 = 无风");
  condForm->addRow("相对风速：", windSpin_);

  frontalSpin_ = new QDoubleSpinBox(paramBox);
  frontalSpin_->setRange(0.0, 5000.0); frontalSpin_->setValue(0.0);
  frontalSpin_->setSingleStep(0.5); frontalSpin_->setDecimals(3);
  frontalSpin_->setSuffix(" m²");
  frontalSpin_->setToolTip("水线以上正面受风面积；0 = 不计空气阻力");
  condForm->addRow("受风面积：", frontalSpin_);

  paramRow->addLayout(condForm);

  // --- 模式专有：扫速 / 扫 Cb ---
  rangeBox_ = new QGroupBox("扫描设置", paramBox);
  auto *rangeForm = new QFormLayout(rangeBox_);
  rangeForm->setLabelAlignment(Qt::AlignRight);

  vMinSpin_ = new QDoubleSpinBox(rangeBox_);
  vMinSpin_->setRange(0.0, 20.0); vMinSpin_->setValue(0.5);
  vMinSpin_->setSingleStep(0.1); vMinSpin_->setDecimals(2);
  vMinSpin_->setSuffix(" m/s");
  rangeForm->addRow("航速下限：", vMinSpin_);

  vMaxSpin_ = new QDoubleSpinBox(rangeBox_);
  vMaxSpin_->setRange(0.1, 20.0); vMaxSpin_->setValue(3.5);
  vMaxSpin_->setSingleStep(0.1); vMaxSpin_->setDecimals(2);
  vMaxSpin_->setSuffix(" m/s");
  rangeForm->addRow("航速上限：", vMaxSpin_);

  pointsSpin_ = new QSpinBox(rangeBox_);
  pointsSpin_->setRange(2, 200); pointsSpin_->setValue(15);
  rangeForm->addRow("曲线点数：", pointsSpin_);

  cbSamplesSpin_ = new QSpinBox(rangeBox_);
  cbSamplesSpin_->setRange(3, 71); cbSamplesSpin_->setValue(15);
  rangeForm->addRow("Cb 采样点数：", cbSamplesSpin_);

  paramRow->addWidget(rangeBox_);

  // --- 模式专有：尺度换算 ---
  scaleBox_ = new QGroupBox("尺度换算", paramBox);
  auto *scaleForm = new QFormLayout(scaleBox_);
  scaleForm->setLabelAlignment(Qt::AlignRight);

  lambdaSpin_ = new QDoubleSpinBox(scaleBox_);
  lambdaSpin_->setRange(1.5, 100.0); lambdaSpin_->setValue(25.0);
  lambdaSpin_->setSingleStep(1.0); lambdaSpin_->setDecimals(2);
  lambdaSpin_->setToolTip("缩尺比 λ = L_实船 / L_模型");
  scaleForm->addRow("缩尺比 λ：", lambdaSpin_);

  shipSpeedSpin_ = new QDoubleSpinBox(scaleBox_);
  shipSpeedSpin_->setRange(0.2, 30.0); shipSpeedSpin_->setValue(6.0);
  shipSpeedSpin_->setSingleStep(0.5); shipSpeedSpin_->setDecimals(2);
  shipSpeedSpin_->setSuffix(" m/s");
  scaleForm->addRow("实船航速 Vs：", shipSpeedSpin_);

  scaleNote_ = new QLabel(
      "拖曳试验无风：本模式把风阻按 0 计（与内核 ModelToShipResistance 同口径），\n"
      "否则 Ct 里会混进与 Fn 无关的空气阻力。", scaleBox_);
  scaleNote_->setStyleSheet("color:#666;");
  scaleNote_->setWordWrap(true);
  scaleForm->addRow(scaleNote_);

  paramRow->addWidget(scaleBox_);

  auto *btnColumn = new QVBoxLayout();
  computeBtn_ = new QPushButton("计算", paramBox);
  exportBtn_ = new QPushButton("导出交付物", paramBox);
  // 按钮文字不放文件名：模式切换时文件名会变长（rp05_optimization.json），
  // 长文字会把按钮撑出参数区。文件名放 tooltip + 状态栏。
  computeBtn_->setMaximumWidth(220);
  exportBtn_->setMaximumWidth(220);
  btnColumn->addWidget(computeBtn_);
  btnColumn->addWidget(exportBtn_);
  btnColumn->addStretch();
  paramRow->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  // === 结论 ===
  summaryLabel_ = new QLabel(this);
  summaryLabel_->setWordWrap(true);
  summaryLabel_->setTextFormat(Qt::RichText);
  summaryLabel_->setMinimumHeight(46);
  mainLayout->addWidget(summaryLabel_);

  // === 图 + 表 ===
  // 图表最小高度压到 230：本对话框纵向要同时容纳参数区 8 行 + 结论 + 图 + 表，
  // 图表若按 280 起算，整个布局会超高（Qt 会把表挤到只剩表头）。
  chart_ = new ResistanceChartWidget(this);
  chart_->setMinimumHeight(210);
  mainLayout->addWidget(chart_, 2);

  table_ = new QTableWidget(this);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  table_->setMinimumHeight(150);
  mainLayout->addWidget(table_, 3);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  statusLabel_->setWordWrap(true);
  mainLayout->addWidget(statusLabel_);

  connect(modeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ResistanceDialog::onModeChanged);
  connect(computeBtn_, &QPushButton::clicked, this, &ResistanceDialog::onCompute);
  connect(exportBtn_, &QPushButton::clicked, this, &ResistanceDialog::onExport);
  connect(selfCheck_, &QCheckBox::toggled, this, [this](bool) { syncVolume(); });
  for (QDoubleSpinBox *s : {lengthSpin_, breadthSpin_, draftSpin_, cbSpin_})
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { syncVolume(); });

  syncVolume();
}

//-----------------------------------------------------------------------------
void ResistanceDialog::setMode(const QString &_expId)
{
  const QString id = SpecOf(_expId).id;
  const int idx = modeCombo_->findData(id);
  if (idx >= 0 && idx != modeCombo_->currentIndex()) {
    modeCombo_->setCurrentIndex(idx);  // 触发 onModeChanged → applyMode
    return;
  }
  expId_ = id;
  applyMode();
}

//-----------------------------------------------------------------------------
void ResistanceDialog::onModeChanged(int _index)
{
  if (_index < 0) return;
  const QString id = modeCombo_->itemData(_index).toString();
  if (!id.isEmpty()) expId_ = SpecOf(id).id;
  applyMode();
}

//-----------------------------------------------------------------------------
void ResistanceDialog::applyMode()
{
  const bool isRange = (expId_ == "RP-02" || expId_ == "RP-05");
  const bool isScale = (expId_ == "RP-06");

  rangeBox_->setVisible(isRange);
  scaleBox_->setVisible(isScale);

  // RP-05 只扫 Cb，不需要「曲线点数」，但需要「Cb 采样点数」
  auto *rangeForm = qobject_cast<QFormLayout *>(rangeBox_->layout());
  HideFormRow(rangeForm, vMinSpin_, false);
  HideFormRow(rangeForm, vMaxSpin_, false);
  HideFormRow(rangeForm, pointsSpin_, expId_ == "RP-05");
  HideFormRow(rangeForm, cbSamplesSpin_, expId_ != "RP-05");

  // RP-06 用实船航速而非设计航速；风阻在核心里被强制清零，故连同输入一起藏掉，
  // 免得学生以为「填了风就能算进实船阻力」。
  HideFormRow(condForm_, speedSpin_, isScale);
  HideFormRow(condForm_, windSpin_, isScale);
  HideFormRow(condForm_, frontalSpin_, isScale);

  setWindowTitle(QString::fromUtf8(SpecOf(expId_).title) +
                 " —— 输入 / 曲线 / 交付物");
  exportBtn_->setToolTip(
      QString("将写出 %1（schema keship.exp.v1，expId = %2）")
          .arg(QString::fromLatin1(SpecOf(expId_).file)).arg(expId_));

  lastMetrics_ = QJsonObject();
  lastExtra_ = QJsonObject();
  table_->clear();
  table_->setRowCount(0);
  table_->setColumnCount(0);
  chart_->Clear();
  summaryLabel_->clear();

  computeCurrent();
}

//-----------------------------------------------------------------------------
void ResistanceDialog::syncVolume()
{
  const bool self = selfCheck_->isChecked();
  volumeSpin_->setEnabled(!self);
  if (!self) return;
  const double v = cbSpin_->value() * lengthSpin_->value() *
                   breadthSpin_->value() * draftSpin_->value();
  volumeSpin_->setValue(v);
}

//-----------------------------------------------------------------------------
keship::ResistanceInput ResistanceDialog::buildInput() const
{
  keship::ResistanceInput in;
  in.Lwl = lengthSpin_->value();
  in.B = breadthSpin_->value();
  in.T = draftSpin_->value();
  in.Cb = cbSpin_->value();
  in.Cm = cmSpin_->value();
  in.Cwp = cwpSpin_->value();
  in.volumeD = selfCheck_->isChecked()
                   ? in.Cb * in.Lwl * in.B * in.T
                   : volumeSpin_->value();
  in.rhoWater = kRhoWater;
  in.nu = kNuWater;
  in.rhoAir = kRhoAir;
  in.windSpeedMs = windSpin_->value();
  in.frontalAreaM2 = frontalSpin_->value();
  in.appendageFactor = 1.0;  // 教学船无附体；内核里 (1+k2) 只放大黏性项
  return in;
}

//-----------------------------------------------------------------------------
void ResistanceDialog::storeCommon(const keship::ResistanceInput &_in,
                                   const QString &_note)
{
  lastObject_ = QJsonObject();
  lastObject_["type"] = "teaching_hull";
  lastObject_["name"] = QString("L8 教学船 L=%1 m").arg(Num(_in.Lwl, 2));
  lastObject_["lengthM"] = _in.Lwl;
  lastObject_["breadthM"] = _in.B;
  lastObject_["draftM"] = _in.T;
  lastObject_["cb"] = _in.Cb;
  lastObject_["cm"] = _in.Cm;
  lastObject_["cwp"] = _in.Cwp;
  lastObject_["volumeM3"] = _in.volumeD;
  lastObject_["wettedSurfaceM2"] = keship::WettedSurfaceM2(_in);
  lastObject_["onePlusK"] = keship::FormFactorOnePlusK(_in);

  lastParams_ = QJsonObject();
  lastParams_["mode"] = expId_;
  lastParams_["rhoWater"] = _in.rhoWater;
  lastParams_["nu"] = _in.nu;
  lastParams_["rhoAir"] = _in.rhoAir;
  lastParams_["appendageFactor"] = _in.appendageFactor;
  lastParams_["volumeSelfConsistent"] = selfCheck_->isChecked();
  lastParams_["fluidNote"] = "15℃ 海水（与 ResistanceInput 默认值一致）";

  lastCondition_ = QJsonObject();
  lastCondition_["note"] = _note;
}

//-----------------------------------------------------------------------------
void ResistanceDialog::setTableShape(int _rows, int _cols,
                                     const QStringList &_headers)
{
  table_->clear();
  table_->setRowCount(_rows);
  table_->setColumnCount(_cols);
  table_->setHorizontalHeaderLabels(_headers);
}

void ResistanceDialog::setStatus(const QString &_text)
{
  statusLabel_->setText(_text);
}

void ResistanceDialog::showSummary(const QString &_richText)
{
  summaryLabel_->setText(_richText);
}

//-----------------------------------------------------------------------------
void ResistanceDialog::computeCurrent()
{
  if (expId_ == "RP-02") computeRp02();
  else if (expId_ == "RP-05") computeRp05();
  else if (expId_ == "RP-06") computeRp06();
  else computeRp01();
}

//-----------------------------------------------------------------------------
// RP-01 阻力成分分解：单航速下把 R_total 拆成三项并给占比
//-----------------------------------------------------------------------------
void ResistanceDialog::computeRp01()
{
  const auto in = buildInput();
  const double v = speedSpin_->value();
  const auto b = keship::ResistanceAtSpeed(in, v);
  const double total = b.Rtotal;
  const double den = std::max(total, 1e-12);
  const double sRf = 100.0 * b.Rf / den;
  const double sRr = 100.0 * b.Rr / den;
  const double sWind = 100.0 * b.Rwind / den;

  storeCommon(in, QString("单航速 %1 m/s（Fn=%2）").arg(Num(v, 3)).arg(Num(b.Fn, 4)));
  lastCondition_["speedMs"] = v;
  lastCondition_["windSpeedMs"] = in.windSpeedMs;
  lastCondition_["frontalAreaM2"] = in.frontalAreaM2;

  lastParams_["designSpeedMs"] = v;
  lastParams_["windSpeedMs"] = in.windSpeedMs;
  lastParams_["frontalAreaM2"] = in.frontalAreaM2;

  lastMetrics_ = QJsonObject();
  lastMetrics_["speedMs"] = b.speedMs;
  lastMetrics_["fn"] = b.Fn;
  lastMetrics_["re"] = b.Re;
  lastMetrics_["cf"] = b.Cf;
  lastMetrics_["onePlusK"] = b.onePlusK;
  lastMetrics_["wettedSurfaceM2"] = keship::WettedSurfaceM2(in);
  lastMetrics_["rfN"] = b.Rf;
  lastMetrics_["rrN"] = b.Rr;
  lastMetrics_["rwindN"] = b.Rwind;
  lastMetrics_["rtotalN"] = b.Rtotal;
  lastMetrics_["ct"] = b.Ct;
  lastMetrics_["rfSharePct"] = sRf;
  lastMetrics_["rrSharePct"] = sRr;
  lastMetrics_["rwindSharePct"] = sWind;

  lastExtra_ = QJsonObject();
  QJsonObject formulas;
  formulas["cf"] = "ITTC-57: Cf = 0.075/(lgRe-2)^2";
  formulas["onePlusK"] = "Holtrop-Mennen 1982 形状因子";
  formulas["rf"] = "Rv = 0.5*rho*V^2*S*(1+k)*Cf*(1+k2)";  ///< 显示用；交付物字段名仍为 rf/Rf
  formulas["rr"] = "Rr = 0.5*rho*V^2*S*Cr(Fn,Cb)，Cr 锚点 Fn=0.30 取 1.50e-3";
  formulas["rwind"] = "Rwind = 0.5*rho_air*Cd*A*Vwind^2，Cd=1.0";
  lastExtra_["formulas"] = formulas;

  setTableShape(11, 5, {"项目", "符号", "数值", "单位", "占 R_total"});
  int r = 0;
  SetCell(table_, r, 0, "傅汝德数", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Fn"); SetCell(table_, r, 2, Num(b.Fn, 5));
  SetCell(table_, r, 3, "—"); SetCell(table_, r, 4, "—", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "雷诺数", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Re"); SetCell(table_, r, 2, Sci(b.Re, 3));
  SetCell(table_, r, 3, "—"); SetCell(table_, r, 4, "—", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "平板摩擦系数（ITTC-57）", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Cf"); SetCell(table_, r, 2, Sci(b.Cf, 4));
  SetCell(table_, r, 3, "—"); SetCell(table_, r, 4, "—", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "形状因子（Holtrop）", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "(1+k)"); SetCell(table_, r, 2, Num(b.onePlusK, 4));
  SetCell(table_, r, 3, "—"); SetCell(table_, r, 4, "—", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "湿表面积", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "S"); SetCell(table_, r, 2, Num(keship::WettedSurfaceM2(in), 4));
  SetCell(table_, r, 3, "m²"); SetCell(table_, r, 4, "—", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "黏性阻力（含 1+k）", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Rv"); SetCell(table_, r, 2, Num(b.Rf, 3));
  SetCell(table_, r, 3, "N"); SetCell(table_, r, 4, Num(sRf, 2) + "%", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "兴波（剩余）阻力", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Rr"); SetCell(table_, r, 2, Num(b.Rr, 3));
  SetCell(table_, r, 3, "N"); SetCell(table_, r, 4, Num(sRr, 2) + "%", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "空气阻力", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Rwind"); SetCell(table_, r, 2, Num(b.Rwind, 3));
  SetCell(table_, r, 3, "N"); SetCell(table_, r, 4, Num(sWind, 2) + "%", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "总阻力", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Rtotal"); SetCell(table_, r, 2, Num(b.Rtotal, 3));
  SetCell(table_, r, 3, "N"); SetCell(table_, r, 4, "100.00%", Qt::AlignCenter);
  ++r;
  SetCell(table_, r, 0, "总阻力系数", Qt::AlignLeft | Qt::AlignVCenter);
  SetCell(table_, r, 1, "Ct"); SetCell(table_, r, 2, Sci(b.Ct, 4));
  SetCell(table_, r, 3, "—"); SetCell(table_, r, 4, "—", Qt::AlignCenter);

  chart_->Clear();
  chart_->SetAxes(QString("阻力成分占比 @ V = %1 m/s").arg(Num(v, 2)),
                  "", "");
  chart_->SetBars({"黏性 Rv", "兴波 Rr", "空气 Rwind"}, {b.Rf, b.Rr, b.Rwind}, "N");

  const QString dominant = (sRf >= sRr && sRf >= sWind)
                               ? "黏性阻力主导"
                               : (sRr >= sWind ? "兴波阻力主导" : "空气阻力主导");
  showSummary(QString(
                  "V = <b>%1</b> m/s（Fn = %2）　总阻力 R<sub>total</sub> = <b>%3</b> N　"
                  "C<sub>t</sub> = %4　(1+k) = %5<br>"
                  "R<sub>v</sub> = %6 N（%7%）· R<sub>r</sub> = %8 N（%9%）· "
                  "R<sub>wind</sub> = %10 N（%11%）⇒ <b>%12</b>")
                  .arg(Num(v, 2)).arg(Num(b.Fn, 4)).arg(Num(b.Rtotal, 2))
                  .arg(Sci(b.Ct, 3)).arg(Num(b.onePlusK, 3))
                  .arg(Num(b.Rf, 1)).arg(Num(sRf, 1))
                  .arg(Num(b.Rr, 1)).arg(Num(sRr, 1))
                  .arg(Num(b.Rwind, 1)).arg(Num(sWind, 1)).arg(dominant));

  setStatus(QString("RP-01 已算：低速摩擦主导、高速兴波主导——可把 V 从 %1 m/s 逐步调到 %2 m/s 观察占比翻转。")
                .arg(Num(0.5, 1)).arg(Num(4.0, 1)));
  std::cout << "RP-01: V=" << v << " Rtotal=" << b.Rtotal << " Ct=" << b.Ct
            << " Rf%=" << sRf << " Rr%=" << sRr << std::endl;
}

//-----------------------------------------------------------------------------
// RP-02 阻力曲线测定：扫航速，画 R–V 曲线（显式要求：曲线上图，不要堆在表里当"图"）
//-----------------------------------------------------------------------------
void ResistanceDialog::computeRp02()
{
  auto in = buildInput();
  double lo = vMinSpin_->value();
  double hi = vMaxSpin_->value();
  if (hi < lo) std::swap(lo, hi);
  const int n = pointsSpin_->value();
  const auto curve = keship::ResistanceCurve(in, lo, hi, n);

  storeCommon(in, QString("航速扫描 %1 ~ %2 m/s，%3 点")
                        .arg(Num(lo, 2)).arg(Num(hi, 2)).arg(n));
  lastCondition_["vMinMs"] = lo;
  lastCondition_["vMaxMs"] = hi;
  lastCondition_["samples"] = n;
  lastCondition_["windSpeedMs"] = in.windSpeedMs;
  lastCondition_["frontalAreaM2"] = in.frontalAreaM2;

  lastParams_["vMinMs"] = lo;
  lastParams_["vMaxMs"] = hi;
  lastParams_["samples"] = n;
  lastParams_["windSpeedMs"] = in.windSpeedMs;
  lastParams_["frontalAreaM2"] = in.frontalAreaM2;

  const auto &last = curve.back();
  std::vector<double> vs, rs;
  QJsonArray aV, aFn, aRe, aCf, aRf, aRr, aRw, aRt, aCt;
  for (const auto &p : curve) {
    vs.push_back(p.speedMs);
    rs.push_back(p.Rtotal);
    aV.append(p.speedMs); aFn.append(p.Fn); aRe.append(p.Re); aCf.append(p.Cf);
    aRf.append(p.Rf); aRr.append(p.Rr); aRw.append(p.Rwind);
    aRt.append(p.Rtotal); aCt.append(p.Ct);
  }
  // 总阻力的 log–log 幂指数：R ∝ V^m，m 从 2（摩擦主导）滑向 4（V⁴ 兴波律）
  const double m = LogLogSlope(vs, rs);

  lastMetrics_ = QJsonObject();
  lastMetrics_["vMinMs"] = lo;
  lastMetrics_["vMaxMs"] = hi;
  lastMetrics_["samples"] = n;
  lastMetrics_["rtotalAtVmaxN"] = last.Rtotal;
  lastMetrics_["rfAtVmaxN"] = last.Rf;
  lastMetrics_["rrAtVmaxN"] = last.Rr;
  lastMetrics_["rwindAtVmaxN"] = last.Rwind;
  lastMetrics_["ctAtVmax"] = last.Ct;
  lastMetrics_["fnAtVmax"] = last.Fn;
  lastMetrics_["powerExponent"] = m;

  lastExtra_ = QJsonObject();
  lastExtra_["speedMs"] = aV;
  lastExtra_["fn"] = aFn;
  lastExtra_["re"] = aRe;
  lastExtra_["cf"] = aCf;
  lastExtra_["rfN"] = aRf;
  lastExtra_["rrN"] = aRr;
  lastExtra_["rwindN"] = aRw;
  lastExtra_["rtotalN"] = aRt;
  lastExtra_["ct"] = aCt;
  QJsonObject fit;
  fit["model"] = "Rtotal = k * V^m";
  fit["m"] = m;
  QJsonArray fitRange;
  fitRange.append(lo);
  fitRange.append(hi);
  fit["vRangeMs"] = fitRange;
  lastExtra_["fit"] = fit;

  setTableShape(static_cast<int>(curve.size()), 10,
                {"V (m/s)", "V (kn)", "Fn", "Re", "Cf",
                 "Rv (N)", "Rr (N)", "Rwind (N)", "Rtotal (N)", "Ct"});
  for (int i = 0; i < static_cast<int>(curve.size()); ++i) {
    const auto &p = curve[i];
    SetCell(table_, i, 0, Num(p.speedMs, 3));
    SetCell(table_, i, 1, Num(p.speedMs * 1.9438445, 2));
    SetCell(table_, i, 2, Num(p.Fn, 4));
    SetCell(table_, i, 3, Sci(p.Re, 3));
    SetCell(table_, i, 4, Sci(p.Cf, 4));
    SetCell(table_, i, 5, Num(p.Rf, 2));
    SetCell(table_, i, 6, Num(p.Rr, 2));
    SetCell(table_, i, 7, Num(p.Rwind, 2));
    SetCell(table_, i, 8, Num(p.Rtotal, 2));
    SetCell(table_, i, 9, Sci(p.Ct, 4));
  }

  ResistanceChartWidget::Series sRf, sRr, sRw, sRt;
  sRf.name = "Rv 黏性";  sRf.color = QColor("#1f6feb");
  sRr.name = "Rr 兴波";  sRr.color = QColor("#c0392b");
  sRw.name = "Rwind 空气"; sRw.color = QColor("#7f8c8d"); sRw.dashed = true;
  sRt.name = "Rtotal 总阻力"; sRt.color = QColor("#2c3e50");
  for (const auto &p : curve) {
    sRf.pts.push_back(QPointF(p.speedMs, p.Rf));
    sRr.pts.push_back(QPointF(p.speedMs, p.Rr));
    sRw.pts.push_back(QPointF(p.speedMs, p.Rwind));
    sRt.pts.push_back(QPointF(p.speedMs, p.Rtotal));
  }
  chart_->Clear();
  chart_->SetAxes(QString("R–V 阻力曲线（%1 点，%2 ~ %3 m/s）")
                      .arg(n).arg(Num(lo, 2)).arg(Num(hi, 2)),
                  "航速 V (m/s)", "阻力 R (N)");
  chart_->SetSeries({sRf, sRr, sRw, sRt});
  chart_->AddMarker(last.speedMs, last.Rtotal,
                    QString("Vmax: %1 N").arg(Num(last.Rtotal, 1)),
                    QColor("#8e44ad"));

  showSummary(QString(
                  "扫描 %1 点：R<sub>total</sub> 从 %2 N 增至 <b>%3</b> N；"
                  "末点 Fn = %4、C<sub>t</sub> = %5<br>"
                  "log–log 拟合 R ∝ V<sup>m</sup> 得 <b>m = %6</b>"
                  "（m→2 说明摩擦主导，m→4 说明兴波 V⁴ 律主导）")
                  .arg(n).arg(Num(curve.front().Rtotal, 1))
                  .arg(Num(last.Rtotal, 1)).arg(Num(last.Fn, 4))
                  .arg(Sci(last.Ct, 3)).arg(Num(m, 3)));

  setStatus("RP-02 已算：曲线越陡说明兴波占比越大；改 Cb 或 L 再看 m 往哪边走。");
  std::cout << "RP-02: " << n << " 点，m=" << m << " Rtotal(max)=" << last.Rtotal
            << std::endl;
}

//-----------------------------------------------------------------------------
// RP-05 船型阻力优化：扫 Cb（0.5~0.85），找平均总阻力最小的方形系数
//-----------------------------------------------------------------------------
void ResistanceDialog::computeRp05()
{
  const auto in = buildInput();
  double lo = vMinSpin_->value();
  double hi = vMaxSpin_->value();
  if (hi < lo) std::swap(lo, hi);
  const int n = std::max(cbSamplesSpin_->value(), 3);
  const double vDesign = speedSpin_->value();

  // 最优 Cb 由内核给（目标函数是 [vMin,vMax] 上的平均总阻力），GUI 不重算目标函数
  bestCb_ = keship::CbForMinResistance(in, lo, hi);

  struct Row {
    double cb = 0.0;
    double vol = 0.0;
    double s = 0.0;
    double onePlusK = 0.0;
    double rf = 0.0;
    double rr = 0.0;
    double rDesign = 0.0;
    double rMean = 0.0;
    bool best = false;
  };
  std::vector<Row> rows;

  auto evalCb = [&](double _cb) {
    Row r;
    keship::ResistanceInput probe = in;
    probe.Cb = _cb;
    // Cb 与 ∇ 必须自洽（∇ = Cb·L·B·T，即 Cb 的定义式）：
    // 否则 (1+k) 里的 (L/∇^(1/3)) 与 Cb 脱钩，扫描出来的「最优 Cb」只是
    // 湿面积在变，物理上讲不通。内核 CbForMinResistance 也是这么做的。
    probe.volumeD = _cb * in.Lwl * in.B * in.T;
    const auto b = keship::ResistanceAtSpeed(probe, vDesign);
    r.cb = _cb;
    r.vol = probe.volumeD;
    r.s = keship::WettedSurfaceM2(probe);
    r.onePlusK = b.onePlusK;
    r.rf = b.Rf;
    r.rr = b.Rr;
    r.rDesign = b.Rtotal;
    r.rMean = MeanTotal(probe, lo, hi, 11);  // 与内核目标函数同口径（11 点）
    return r;
  };

  for (int i = 0; i < n; ++i) {
    const double cb = 0.50 + 0.35 * static_cast<double>(i) /
                                 static_cast<double>(n - 1);
    rows.push_back(evalCb(cb));
  }
  // 最优点不一定落在采样网格上，单独补一行，否则「最优 Cb」在表里查不到
  const Row best = evalCb(bestCb_);
  const Row base = evalCb(in.Cb);
  rows.push_back(best);
  rows.back().best = true;
  std::sort(rows.begin(), rows.end(),
            [](const Row &a, const Row &b) { return a.cb < b.cb; });

  storeCommon(in, QString("Cb 扫描 0.50~0.85（%1 点），评定航速 %2 m/s，"
                          "平均阻力区间 %3~%4 m/s")
                        .arg(n).arg(Num(vDesign, 2))
                        .arg(Num(lo, 2)).arg(Num(hi, 2)));
  lastCondition_["designSpeedMs"] = vDesign;
  lastCondition_["vMinMs"] = lo;
  lastCondition_["vMaxMs"] = hi;

  // 扫描时 ∇ 已按 Cb·L·B·T 重算 ⇒ object 段也要用「基准行」的 ∇/S/(1+k)，
  // 否则交付物里 ∇ 与表中 Cb=基准那一行的 ∇ 不一致，下游复算会对不上账。
  lastObject_["volumeM3"] = base.vol;
  lastObject_["wettedSurfaceM2"] = base.s;
  lastObject_["onePlusK"] = base.onePlusK;
  lastObject_["volumeSelfConsistent"] = true;

  lastParams_["cbScanMin"] = 0.50;
  lastParams_["cbScanMax"] = 0.85;
  lastParams_["cbSamples"] = n;
  lastParams_["designSpeedMs"] = vDesign;
  lastParams_["vMinMs"] = lo;
  lastParams_["vMaxMs"] = hi;
  lastParams_["baseCb"] = in.Cb;

  const double totalSavingPct =
      (base.rDesign > 1e-12) ? 100.0 * (base.rDesign - best.rDesign) / base.rDesign : 0.0;
  const double meanSavingPct =
      (base.rMean > 1e-12) ? 100.0 * (base.rMean - best.rMean) / base.rMean : 0.0;

  lastMetrics_ = QJsonObject();
  lastMetrics_["cbMin"] = 0.50;
  lastMetrics_["cbMax"] = 0.85;
  lastMetrics_["cbSamples"] = n;
  lastMetrics_["designSpeedMs"] = vDesign;
  lastMetrics_["vMinMs"] = lo;
  lastMetrics_["vMaxMs"] = hi;
  lastMetrics_["bestCb"] = bestCb_;
  lastMetrics_["bestTotalAtDesignN"] = best.rDesign;
  lastMetrics_["bestMeanTotalN"] = best.rMean;
  lastMetrics_["bestWettedSurfaceM2"] = best.s;
  lastMetrics_["baseCb"] = in.Cb;
  lastMetrics_["baseTotalAtDesignN"] = base.rDesign;
  lastMetrics_["baseMeanTotalN"] = base.rMean;
  lastMetrics_["totalSavingPct"] = totalSavingPct;
  lastMetrics_["meanSavingPct"] = meanSavingPct;

  QJsonArray mkern, mmean, mcb;
  for (const auto &r : rows) { mcb.append(r.cb); mkern.append(r.rDesign); mmean.append(r.rMean); }
  lastExtra_ = QJsonObject();
  lastExtra_["cb"] = mcb;
  lastExtra_["rtotalAtDesignN"] = mkern;
  lastExtra_["rtotalMeanN"] = mmean;
  lastExtra_["objective"] = "min(mean Rtotal over [vMin,vMax])";
  lastExtra_["volumeRule"] = "volumeD = Cb * Lwl * B * T";

  setTableShape(static_cast<int>(rows.size()), 8,
                {"Cb", "∇ (m³)", "S (m²)", "1+k", "Rv (N)", "Rr (N)",
                 "Rtotal @V (N)", "平均 Rtotal (N)"});
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const auto &r = rows[i];
    const QString mark = r.best ? "★ " : "";
    SetCell(table_, i, 0, mark + Num(r.cb, 3));
    SetCell(table_, i, 1, Num(r.vol, 4));
    SetCell(table_, i, 2, Num(r.s, 4));
    SetCell(table_, i, 3, Num(r.onePlusK, 4));
    SetCell(table_, i, 4, Num(r.rf, 2));
    SetCell(table_, i, 5, Num(r.rr, 2));
    SetCell(table_, i, 6, Num(r.rDesign, 2));
    SetCell(table_, i, 7, Num(r.rMean, 2));
  }

  ResistanceChartWidget::Series sDesign, sMean;
  sDesign.name = QString("Rtotal @ V=%1 m/s").arg(Num(vDesign, 2));
  sDesign.color = QColor("#1f6feb");
  sMean.name = "平均 Rtotal（扫速区间）";
  sMean.color = QColor("#c0392b");
  sMean.dashed = true;
  for (const auto &r : rows) {
    sDesign.pts.push_back(QPointF(r.cb, r.rDesign));
    sMean.pts.push_back(QPointF(r.cb, r.rMean));
  }
  chart_->Clear();
  chart_->SetAxes("R–Cb 曲线（★ = 内核给出的最优方形系数）",
                  "方形系数 Cb", "阻力 R (N)");
  chart_->SetSeries({sDesign, sMean});
  chart_->AddMarker(bestCb_, best.rDesign,
                    QString("最优 Cb=%1").arg(Num(bestCb_, 3)),
                    QColor("#8e44ad"));

  showSummary(QString(
                  "最优方形系数 <b>Cb = %1</b>（内核 CbForMinResistance，目标 = "
                  "平均总阻力最小）<br>"
                  "该 Cb 下 R<sub>total</sub>@V = %2 N（基准 Cb=%3 时为 %4 N，"
                  "降 <b>%5%</b>）；平均总阻力 %6 N → %7 N（降 %8%）")
                  .arg(Num(bestCb_, 3))
                  .arg(Num(best.rDesign, 2)).arg(Num(in.Cb, 3))
                  .arg(Num(base.rDesign, 2)).arg(Num(totalSavingPct, 2))
                  .arg(Num(base.rMean, 2)).arg(Num(best.rMean, 2))
                  .arg(Num(meanSavingPct, 2)));

  setStatus("RP-05 已算：扫的是 Cb，∇ 随 Cb·L·B·T 同步变化（见参数区勾选框）；"
            "曲线最低点即最省力的方形系数。");
  std::cout << "RP-05: bestCb=" << bestCb_ << " R@V(best)=" << best.rDesign
            << " base=" << base.rDesign << std::endl;
}

//-----------------------------------------------------------------------------
// RP-06 尺度换算与实船外推
//
// 为什么用**三因次法**而不是二因次法（传统傅汝德比较律）：
//   两种方法都要做「模型 → 实船」的摩擦修正，差别只在摩擦项要不要乘 (1+k)：
//     · 二因次法：Cts = Ctm − (Cfm − Cfs)       —— 把黏性阻力当纯平板，隐含 1+k = 1
//     · 三因次法：Cts = Ctm − (1+k)(Cfm − Cfs)  —— ITTC-78，形状效应随 Re 一起缩放
//   模型 Re 比实船低 2~3 个数量级，Cfm 明显大于 Cfs，ΔCf = Cfm − Cfs 本身就不小。
//   二因次法只减掉 ΔCf，形状因子那一份（k·ΔCf）被留在阻力里 ⇒ 实船阻力**偏高**
//   （默认工况约高 10%~25%，见下表两行对照）。
//   物理上讲得通：形状效应 (1+k) 是黏性现象，实船 Re 下它同样要按 Cfs 缩小；
//   二因次法把它当成了「不随尺度变的剩余阻力」，属于摩擦口径用错，而不是"更保守"。
//   同一份模型试验能得出差 20% 的实船阻力 —— 正是本实验要学生看清的地方。
//   故本实验**以三因次结果为准**（交付物里的 rShipN 就是三因次值）。
//-----------------------------------------------------------------------------
void ResistanceDialog::computeRp06()
{
  auto model = buildInput();
  // 拖曳试验无风：与内核 ModelToShipResistance 内部的处理保持一致，
  // 否则「模型阻力」里混进风阻、「实船阻力」里没有，两列不可比。
  model.windSpeedMs = 0.0;
  model.frontalAreaM2 = 0.0;

  const double lambda = std::max(lambdaSpin_->value(), 1.01);
  const double vs = shipSpeedSpin_->value();
  const double vm = vs / std::sqrt(lambda);  ///< 同 Fn：Vm = Vs/√λ

  const auto bm = keship::ResistanceAtSpeed(model, vm);
  const double rs3 = keship::ModelToShipResistance(model, lambda, vs);

  const double sm = keship::WettedSurfaceM2(model);
  const double ss = sm * lambda * lambda;  ///< 几何相似 ⇒ 湿面积按 λ² 缩放
  const double rho = model.rhoWater;
  const double qs = 0.5 * rho * ss * vs * vs;
  const double ct3 = (qs > 1e-12) ? rs3 / qs : 0.0;

  // 实船 Re / Cf：把几何整体放大 λ（比值不变 ⇒ 1+k 不变），
  // 再用内核同一条 ITTC-57 线算 Cf_s —— 不在 GUI 里另写一个 Cf 公式。
  keship::ResistanceInput ship = model;
  ship.Lwl = model.Lwl * lambda;
  ship.B = model.B * lambda;
  ship.T = model.T * lambda;
  ship.volumeD = model.volumeD * lambda * lambda * lambda;
  const auto bs = keship::ResistanceAtSpeed(ship, vs);

  // 二因次法（对照）：只减掉平板摩擦差 ΔCf，不乘 (1+k)。
  // 注意这里不是「Ct 与尺度无关」——那是更粗的假设，教材上的二因次法
  // 仍然做摩擦修正，只是把黏性项当纯平板（1+k = 1）。
  const double ct2 = bm.Ct - (bm.Cf - bs.Cf);
  const double rs2 = ct2 * qs;

  storeCommon(model, QString("缩尺比 λ=%1，实船航速 %2 m/s（模型 %3 m/s），拖曳无风")
                        .arg(Num(lambda, 2)).arg(Num(vs, 3)).arg(Num(vm, 4)));
  lastCondition_["lambda"] = lambda;
  lastCondition_["shipSpeedMs"] = vs;
  lastCondition_["modelSpeedMs"] = vm;
  lastCondition_["windSpeedMs"] = 0.0;
  lastCondition_["note"] = "拖曳试验无风：模型与实船两侧风阻均按 0 计";

  lastParams_["lambda"] = lambda;
  lastParams_["shipSpeedMs"] = vs;
  lastParams_["extrapolationMethod"] = "ITTC-78 三因次法（简化式，结果为准）";
  lastParams_["comparisonMethod"] = "二因次法（1+k=1 的摩擦修正，仅作对照）";

  // 二因次法相对三因次的偏高量（正 = 二因次更高，预期恒正）
  const double overPct = (rs3 > 1e-12) ? 100.0 * (rs2 - rs3) / rs3 : 0.0;

  lastMetrics_ = QJsonObject();
  lastMetrics_["lambda"] = lambda;
  lastMetrics_["shipSpeedMs"] = vs;
  lastMetrics_["modelSpeedMs"] = vm;
  lastMetrics_["fnModel"] = bm.Fn;
  lastMetrics_["reModel"] = bm.Re;
  lastMetrics_["reShip"] = bs.Re;
  lastMetrics_["cfModel"] = bm.Cf;
  lastMetrics_["cfShip"] = bs.Cf;
  lastMetrics_["deltaCf"] = bm.Cf - bs.Cf;
  lastMetrics_["onePlusK"] = bm.onePlusK;
  lastMetrics_["wettedSurfaceModelM2"] = sm;
  lastMetrics_["wettedSurfaceShipM2"] = ss;
  lastMetrics_["rModelN"] = bm.Rtotal;
  lastMetrics_["ctModel"] = bm.Ct;
  lastMetrics_["ctShipThreeDim"] = ct3;
  lastMetrics_["ctShipTwoDim"] = ct2;
  lastMetrics_["rShipThreeDimN"] = rs3;
  lastMetrics_["rShipTwoDimN"] = rs2;
  lastMetrics_["rShipN"] = rs3;  ///< 交付基准值 = 三因次结果
  lastMetrics_["twoDimOverestimatePct"] = overPct;
  lastMetrics_["scaleRatioResistance"] = (bm.Rtotal > 1e-12) ? rs3 / bm.Rtotal : 0.0;

  // 对照曲线：实船航速 0.4~1.3 Vs，两种换算方法各一条，设计点打标
  QJsonArray aVs, aR3, aR2;
  ResistanceChartWidget::Series s3, s2;
  s3.name = "三因次法（ITTC-78）"; s3.color = QColor("#c0392b");
  s2.name = "二因次法（对照）";     s2.color = QColor("#7f8c8d"); s2.dashed = true;
  for (int i = 0; i <= 10; ++i) {
    const double v = vs * (0.4 + 0.9 * static_cast<double>(i) / 10.0);
    const double vmi = v / std::sqrt(lambda);
    const auto bmi = keship::ResistanceAtSpeed(model, vmi);
    const auto bsi = keship::ResistanceAtSpeed(ship, v);
    const double qsi = 0.5 * rho * ss * v * v;
    const double r3 = keship::ModelToShipResistance(model, lambda, v);
    const double r2 = (bmi.Ct - (bmi.Cf - bsi.Cf)) * qsi;
    aVs.append(v); aR3.append(r3); aR2.append(r2);
    s3.pts.push_back(QPointF(v, r3));
    s2.pts.push_back(QPointF(v, r2));
  }
  lastExtra_ = QJsonObject();
  lastExtra_["shipSpeedMs"] = aVs;
  lastExtra_["rShipThreeDimN"] = aR3;
  lastExtra_["rShipTwoDimN"] = aR2;
  lastExtra_["formulaThreeDim"] = "Cts = Ctm - (1+k)(Cfm - Cfs)";
  lastExtra_["formulaTwoDim"] = "Cts = Ctm - (Cfm - Cfs)";

  setTableShape(11, 4, {"项目", "模型", "实船", "说明"});
  int r = 0;
  auto row = [&](const QString &_name, const QString &_m, const QString &_s,
                 const QString &_note) {
    SetCell(table_, r, 0, _name, Qt::AlignLeft | Qt::AlignVCenter);
    SetCell(table_, r, 1, _m);
    SetCell(table_, r, 2, _s);
    SetCell(table_, r, 3, _note, Qt::AlignLeft | Qt::AlignVCenter);
    ++r;
  };
  row("航速 V (m/s)", Num(vm, 4), Num(vs, 3), "同 Fn：Vm = Vs/√λ");
  row("傅汝德数 Fn", Num(bm.Fn, 5), Num(bs.Fn, 5), "相似准则，两侧相等");
  row("雷诺数 Re", Sci(bm.Re, 3), Sci(bs.Re, 3), "差 λ^1.5 倍");
  row("平板摩擦系数 Cf", Sci(bm.Cf, 4), Sci(bs.Cf, 4), "ITTC-57");
  row("形状因子 (1+k)", Num(bm.onePlusK, 4), Num(bs.onePlusK, 4), "只用比值，与尺度无关");
  row("湿表面积 S (m²)", Num(sm, 4), Num(ss, 2), "S ∝ λ²");
  row("总阻力系数 Ct", Sci(bm.Ct, 4), Sci(ct3, 4), "实船列＝三因次法");
  row("实船总阻力（三因次）", "—", Num(rs3, 2) + " N", "本实验采用");
  row("实船总阻力（二因次）", "—", Num(rs2, 2) + " N",
      QString("比三因次高 %1%").arg(Num(overPct, 1)));
  row("模型总阻力", Num(bm.Rtotal, 3) + " N", "—", "拖曳试验可直接测得");
  row("阻力尺度比 Rs/Rm", "—", Num(rs3 / std::max(bm.Rtotal, 1e-12), 1),
      "远小于 λ³");

  chart_->Clear();
  chart_->SetAxes(QString("实船阻力 R–V（λ=%1）：三因次 vs 二因次")
                      .arg(Num(lambda, 1)),
                  "实船航速 Vs (m/s)", "实船阻力 Rs (N)");
  chart_->SetSeries({s3, s2});
  chart_->AddMarker(vs, rs3, QString("设计点 %1 N").arg(Num(rs3, 0)),
                    QColor("#8e44ad"));

  showSummary(QString(
                  "λ = <b>%1</b>　V<sub>s</sub> = %2 m/s（V<sub>m</sub> = %3 m/s，"
                  "Fn = %4）<br>"
                  "C<sub>fm</sub> = %5 → C<sub>fs</sub> = %6（降 %7%）　"
                  "(1+k) = %8 不变<br>"
                  "实船阻力：三因次法 <b>%9 N</b>　二因次法 %10 N（"
                  "二因次偏高 %11%）")
                  .arg(Num(lambda, 2)).arg(Num(vs, 2)).arg(Num(vm, 3))
                  .arg(Num(bm.Fn, 4)).arg(Sci(bm.Cf, 3)).arg(Sci(bs.Cf, 3))
                  .arg(Num(100.0 * (bm.Cf - bs.Cf) / std::max(bm.Cf, 1e-12), 1))
                  .arg(Num(bm.onePlusK, 3)).arg(Num(rs3, 1)).arg(Num(rs2, 1))
                  .arg(Num(overPct, 1)));

  setStatus("RP-06 已算：以三因次法为准（剩余阻力系数不变，摩擦项按 (1+k) 修正到实船 Re）；"
            "二因次法把形状因子当成 1，ΔCf 里那份 k·ΔCf 没被减掉，故实船阻力偏高。");
  std::cout << "RP-06: lambda=" << lambda << " Vs=" << vs << " Rs3=" << rs3
            << " Rs2=" << rs2 << std::endl;
}

//-----------------------------------------------------------------------------
void ResistanceDialog::onCompute()
{
  computeCurrent();
}

//-----------------------------------------------------------------------------
// 无交互导出：给离屏测试用（QFileDialog 在无头环境取不到路径，测试无法覆盖导出）
bool ResistanceDialog::exportTo(const QString &_path)
{
  if (lastMetrics_.isEmpty() || _path.isEmpty()) return false;

  keship::ExpDeliverable d;
  d.expId = expId_.toStdString();
  d.createdAt = keship::NowIso8601();
  // 交付物写的是**这次计算用的**参数（不是导出瞬间界面上可能已被改动的值），
  // 否则文件里的 params 与 metrics 不同源，下游复算必然对不上。
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cerr << "ResistanceDialog: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << expId_.toStdString() << ": 已导出交付物 "
            << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void ResistanceDialog::onExport()
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
