/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-04 舱室划分 —— 实现 */

#include "CompartmentDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

constexpr double kWaterDensity = 1000.0;

QString Fmt(double _v, int _n = 3) { return QString::number(_v, 'f', _n); }

/// 单调去重（舱壁位置重复会让相邻舱段厚度为 0，绘出零宽舱室）
std::vector<double> SortedUnique(std::vector<double> _v)
{
  std::sort(_v.begin(), _v.end());
  std::vector<double> out;
  for (double x : _v) {
    if (out.empty() || std::abs(x - out.back()) > 1e-6) out.push_back(x);
  }
  return out;
}

/// 舱室几何键：重建时用它把"旧渗透率"带回同位置的舱室，
/// 否则每次加一道舱壁，学生填过的渗透率就全被重置，实验结果无法对比。
QString GeomKey(double _x1, double _x2, double _y1, double _y2)
{
  return QString("%1|%2|%3|%4")
      .arg(_x1, 0, 'f', 4).arg(_x2, 0, 'f', 4)
      .arg(_y1, 0, 'f', 4).arg(_y2, 0, 'f', 4);
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// CompartmentChartWidget
// ═══════════════════════════════════════════════════════════════════════════

CompartmentChartWidget::CompartmentChartWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumHeight(240);
}

void CompartmentChartWidget::setCurve(const std::vector<double> &_xs,
                                     const std::vector<double> &_vs,
                                     const std::vector<double> &_vps,
                                     double _L, double _totalV)
{
  xs_ = _xs;
  vs_ = _vs;
  vps_ = _vps;
  L_ = (_L > 1e-9) ? _L : 1.0;
  totalV_ = _totalV;
  update();
}

void CompartmentChartWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  // 上边距留 34 px：给"沿船长累积舱容"标题让位，否则标题会压住纵轴最高刻度
  const QRectF plot(66.0, 34.0, std::max(60.0, width() - 96.0),
                    std::max(60.0, height() - 86.0));

  p.setPen(QPen(QColor("#d8d8d8"), 1.0));
  p.drawRect(plot);

  if (xs_.size() < 2) {
    p.setPen(QColor("#888"));
    p.drawText(plot, Qt::AlignCenter, "尚无舱室数据");
    return;
  }

  double vMax = std::max(totalV_, 1e-9);
  for (double v : vs_) vMax = std::max(vMax, v);
  vMax *= 1.08;

  auto toPx = [&](double _x, double _v) {
    const double fx = _x / L_;
    const double fy = _v / vMax;
    return QPointF(plot.left() + fx * plot.width(),
                   plot.bottom() - fy * plot.height());
  };

  // 纵轴刻度取"整齐"步长（1/2/5×10^k）：舱容常是 9.0 这类整数，
  // 用 vMax/5 等分会出现 1.944、3.888 这种读不出意义的刻度
  const double raw = vMax / 5.0;
  const double mag = std::pow(10.0, std::floor(std::log10(std::max(raw, 1e-9))));
  const double nn = raw / mag;
  const double step = ((nn <= 1.0) ? 1.0 : (nn <= 2.0) ? 2.0 : (nn <= 5.0) ? 5.0 : 10.0) * mag;
  for (double v = 0.0; v <= vMax + 1e-9; v += step) {
    const QPointF q = toPx(0.0, v);
    p.setPen(QPen(QColor("#ececec"), 1.0));
    p.drawLine(QPointF(plot.left(), q.y()), QPointF(plot.right(), q.y()));
    p.setPen(QColor("#333"));
    p.drawText(QRectF(plot.left() - 60, q.y() - 8, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, Fmt(v, 2));
  }
  for (int i = 0; i <= 6; ++i) {
    const double frac = i / 6.0;
    p.drawText(QRectF(plot.left() + frac * plot.width() - 30, plot.bottom() + 4,
                      60, 16),
               Qt::AlignHCenter | Qt::AlignTop, Fmt(L_ * frac, 2));
  }
  p.drawText(QRectF(plot.right() - 90, plot.bottom() + 22, 90, 16),
             Qt::AlignRight, "x / m");
  p.drawText(QRectF(plot.left(), 8, plot.width(), 18), Qt::AlignLeft,
             "沿船长累积舱容 / m³");

  // 累积可浸舱容（填充，面积能看出"哪一段分舱最密"）
  QPolygonF areaPerm;
  areaPerm << QPointF(plot.left(), plot.bottom());
  for (std::size_t i = 0; i < xs_.size(); ++i) areaPerm << toPx(xs_[i], vps_[i]);
  areaPerm << QPointF(plot.right(), plot.bottom());
  p.setPen(Qt::NoPen);
  p.setBrush(QColor(70, 130, 200, 60));
  p.drawPolygon(areaPerm);

  // 累积总舱容（实线）
  QPolygonF lineV;
  for (std::size_t i = 0; i < xs_.size(); ++i) lineV << toPx(xs_[i], vs_[i]);
  p.setBrush(Qt::NoBrush);
  p.setPen(QPen(QColor("#1f4e79"), 2.0));
  p.drawPolyline(lineV);

  // 可浸舱容（虚线）
  QPolygonF lineVp;
  for (std::size_t i = 0; i < xs_.size(); ++i) lineVp << toPx(xs_[i], vps_[i]);
  p.setPen(QPen(QColor("#c0504d"), 1.6, Qt::DashLine));
  p.drawPolyline(lineVp);

  // 图例
  p.setPen(QPen(QColor("#1f4e79"), 2.0));
  p.drawLine(QPointF(plot.left() + 8, plot.top() + 10),
             QPointF(plot.left() + 40, plot.top() + 10));
  p.setPen(QColor("#333"));
  p.drawText(QRectF(plot.left() + 46, plot.top() + 2, 200, 16),
             Qt::AlignLeft, "总舱容 ΣV");
  p.setPen(QPen(QColor("#c0504d"), 1.6, Qt::DashLine));
  p.drawLine(QPointF(plot.left() + 158, plot.top() + 10),
             QPointF(plot.left() + 190, plot.top() + 10));
  p.setPen(QColor("#333"));
  p.drawText(QRectF(plot.left() + 196, plot.top() + 2, 240, 16),
             Qt::AlignLeft, "可浸舱容 Σ(μ·V)");
}

// ═══════════════════════════════════════════════════════════════════════════
// CompartmentDialog
// ═══════════════════════════════════════════════════════════════════════════

CompartmentDialog::CompartmentDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("SD-04 舱室划分 —— 舱壁布置 / 舱容曲线 / 渗透率");
  resize(1180, 820);
  buildUi();
  refresh();
}

void CompartmentDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>用横舱壁与纵舱壁把船体内部划分为若干矩形舱室，观察<b>沿船长的舱容"
      "累积曲线</b>如何随分舱数变化，并为每个舱室指定<b>渗透率 μ</b>（0=不进水，1=完全可浸）。<br>"
      "<b>交付物：</b>「导出舱室表」生成 <code>sd04_compartments.json</code>，HS-07 用它布置浮力体、"
      "SD-07 用它逐舱试破 —— 下游读的是这里真正算出的体积与渗透率，不是界面上抄的数字。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 参数 ===
  auto *paramBox = new QGroupBox("船体主尺度与默认渗透率", this);
  auto *paramLayout = new QHBoxLayout(paramBox);

  auto *form = new QFormLayout();
  lengthSpin_ = new QDoubleSpinBox(paramBox);
  lengthSpin_->setRange(2.0, 60.0);
  lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5);
  lengthSpin_->setDecimals(3);
  lengthSpin_->setSuffix(" m");
  form->addRow("船长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(paramBox);
  breadthSpin_->setRange(0.5, 20.0);
  breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1);
  breadthSpin_->setDecimals(3);
  breadthSpin_->setSuffix(" m");
  form->addRow("型宽 B：", breadthSpin_);

  depthSpin_ = new QDoubleSpinBox(paramBox);
  depthSpin_->setRange(0.3, 12.0);
  depthSpin_->setValue(1.0);
  depthSpin_->setSingleStep(0.05);
  depthSpin_->setDecimals(3);
  depthSpin_->setSuffix(" m");
  form->addRow("型深 D：", depthSpin_);

  permSpin_ = new QDoubleSpinBox(paramBox);
  permSpin_->setRange(0.0, 1.0);
  permSpin_->setValue(0.95);
  permSpin_->setSingleStep(0.05);
  permSpin_->setDecimals(2);
  form->addRow("默认渗透率 μ：", permSpin_);

  paramLayout->addLayout(form, 1);

  auto *btnColumn = new QVBoxLayout();
  auto *rebuildBtn = new QPushButton("按当前舱壁重新划分（重建舱室表）", paramBox);
  auto *exportBtn = new QPushButton("导出舱室表（SD-04 交付物）", paramBox);
  btnColumn->addWidget(rebuildBtn);
  btnColumn->addWidget(exportBtn);
  btnColumn->addStretch();
  paramLayout->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  // === 舱壁 + 舱室表 ===
  auto *midLayout = new QHBoxLayout();

  auto *bulkBox = new QGroupBox("舱壁位置（m）", this);
  auto *bulkLayout = new QVBoxLayout(bulkBox);

  bulkLayout->addWidget(new QLabel("横向舱壁（0 与 L 为隐含端点）：", bulkBox));
  xList_ = new QListWidget(bulkBox);
  xList_->setMaximumHeight(130);
  bulkLayout->addWidget(xList_);
  auto *xBtnRow = new QHBoxLayout();
  auto *addXBtn = new QPushButton("添加", bulkBox);
  auto *delXBtn = new QPushButton("删除所选", bulkBox);
  xBtnRow->addWidget(addXBtn);
  xBtnRow->addWidget(delXBtn);
  bulkLayout->addLayout(xBtnRow);

  bulkLayout->addSpacing(8);
  bulkLayout->addWidget(new QLabel("纵向舱壁（±B/2 为隐含舷侧）：", bulkBox));
  yList_ = new QListWidget(bulkBox);
  yList_->setMaximumHeight(130);
  bulkLayout->addWidget(yList_);
  auto *yBtnRow = new QHBoxLayout();
  auto *addYBtn = new QPushButton("添加", bulkBox);
  auto *delYBtn = new QPushButton("删除所选", bulkBox);
  yBtnRow->addWidget(addYBtn);
  yBtnRow->addWidget(delYBtn);
  bulkLayout->addLayout(yBtnRow);

  bulkLayout->addSpacing(8);
  bulkLayout->addWidget(new QLabel("新舱壁位置：", bulkBox));
  bulkheadSpin_ = new QDoubleSpinBox(bulkBox);
  bulkheadSpin_->setRange(0.0, 60.0);
  bulkheadSpin_->setValue(2.0);
  bulkheadSpin_->setDecimals(3);
  bulkheadSpin_->setSuffix(" m");
  bulkLayout->addWidget(bulkheadSpin_);
  bulkLayout->addStretch();
  bulkBox->setMaximumWidth(320);

  midLayout->addWidget(bulkBox);

  auto *compBox = new QGroupBox("舱室表（渗透率可直接编辑；其余为几何量）", this);
  auto *compLayout = new QVBoxLayout(compBox);
  compTable_ = new QTableWidget(compBox);
  compTable_->setColumnCount(10);
  compTable_->setHorizontalHeaderLabels(
      QStringList() << "名称" << "x1" << "x2" << "y1" << "y2" << "z1" << "z2"
                    << "体积 m³" << "渗透率 μ" << "可浸体积 m³");
  compTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  compTable_->verticalHeader()->setVisible(false);
  compLayout->addWidget(compTable_);
  midLayout->addWidget(compBox, 1);

  mainLayout->addLayout(midLayout, 1);

  // === 舱容曲线 ===
  auto *chartBox = new QGroupBox("沿船长的舱容累积曲线", this);
  auto *chartLayout = new QVBoxLayout(chartBox);
  chart_ = new CompartmentChartWidget(chartBox);
  chartLayout->addWidget(chart_);
  mainLayout->addWidget(chartBox, 1);

  summaryLabel_ = new QLabel(this);
  summaryLabel_->setTextFormat(Qt::RichText);
  summaryLabel_->setWordWrap(true);
  mainLayout->addWidget(summaryLabel_);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  statusLabel_->setWordWrap(true);
  mainLayout->addWidget(statusLabel_);

  connect(addXBtn, &QPushButton::clicked, this, &CompartmentDialog::onAddTransverse);
  connect(delXBtn, &QPushButton::clicked, this, &CompartmentDialog::onRemoveTransverse);
  connect(addYBtn, &QPushButton::clicked, this, &CompartmentDialog::onAddLongitudinal);
  connect(delYBtn, &QPushButton::clicked, this, &CompartmentDialog::onRemoveLongitudinal);
  connect(rebuildBtn, &QPushButton::clicked, this, &CompartmentDialog::onRebuild);
  connect(exportBtn, &QPushButton::clicked, this, &CompartmentDialog::onExport);
  connect(compTable_, &QTableWidget::cellChanged, this, &CompartmentDialog::onCellChanged);
}

//-----------------------------------------------------------------------------
std::vector<double> CompartmentDialog::bulkheadPositions(bool _transverse) const
{
  std::vector<double> v;
  QListWidget *list = _transverse ? xList_ : yList_;
  for (int i = 0; i < list->count(); ++i) v.push_back(list->item(i)->text().toDouble());
  return SortedUnique(v);
}

//-----------------------------------------------------------------------------
bool CompartmentDialog::refresh()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double mu0 = permSpin_->value();

  if (!(L > 1e-6) || !(B > 1e-6) || !(D > 1e-6)) {
    errMsg_ = "主尺度必须为正（L、B、D > 0）。";
    return false;
  }

  // 纵向舱壁必须落在船宽内，否则会生成"船体外"的舱室，SD-07 的破舱内核会直接判不适用
  for (double y : bulkheadPositions(false)) {
    if (y <= -0.5 * B + 1e-6 || y >= 0.5 * B - 1e-6) {
      errMsg_ = QString("纵向舱壁 y = %1 m 超出船宽范围（应在 −B/2 与 +B/2 之间）。")
                    .arg(y, 0, 'f', 3);
      return false;
    }
  }
  for (double x : bulkheadPositions(true)) {
    if (x <= 1e-6 || x >= L - 1e-6) {
      errMsg_ = QString("横向舱壁 x = %1 m 超出船长范围（应在 0 与 L 之间）。")
                    .arg(x, 0, 'f', 3);
      return false;
    }
  }

  std::vector<double> xs = SortedUnique(bulkheadPositions(true));
  std::vector<double> ys = SortedUnique(bulkheadPositions(false));
  xs.insert(xs.begin(), 0.0);
  xs.push_back(L);
  ys.insert(ys.begin(), -0.5 * B);
  ys.push_back(0.5 * B);

  // 旧舱室的渗透率按几何键带入（见 GeomKey 说明）
  std::vector<std::pair<QString, double>> oldPerm;
  for (const auto &c : comps_) {
    oldPerm.emplace_back(GeomKey(c.x1, c.x2, c.y1, c.y2), c.permeability);
  }
  auto permOf = [&](const QString &_key) {
    for (const auto &kv : oldPerm) {
      if (kv.first == _key) return kv.second;
    }
    return mu0;
  };

  comps_.clear();
  for (std::size_t ix = 0; ix + 1 < xs.size(); ++ix) {
    for (std::size_t iy = 0; iy + 1 < ys.size(); ++iy) {
      keship::Compartment c;
      c.name = QString("C%1-%2").arg(static_cast<int>(ix) + 1)
                   .arg(static_cast<int>(iy) + 1)
                   .toStdString();
      c.x1 = xs[ix];
      c.x2 = xs[ix + 1];
      c.y1 = ys[iy];
      c.y2 = ys[iy + 1];
      c.z1 = 0.0;
      c.z2 = D;
      c.permeability = permOf(GeomKey(c.x1, c.x2, c.y1, c.y2));
      comps_.push_back(c);
    }
  }

  if (comps_.empty()) {
    errMsg_ = "没有生成任何舱室，请检查舱壁位置。";
    return false;
  }

  refillTable();
  recomputeVolumes();
  return true;
}

//-----------------------------------------------------------------------------
void CompartmentDialog::refillTable()
{
  QSignalBlocker blocker(compTable_);   // 重建期间禁止 cellChanged 递归
  compTable_->setRowCount(static_cast<int>(comps_.size()));
  for (int r = 0; r < static_cast<int>(comps_.size()); ++r) {
    const auto &c = comps_[static_cast<std::size_t>(r)];
    const double vals[6] = {c.x1, c.x2, c.y1, c.y2, c.z1, c.z2};
    auto *nameItem = new QTableWidgetItem(QString::fromStdString(c.name));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
    compTable_->setItem(r, 0, nameItem);
    for (int k = 0; k < 6; ++k) {
      auto *it = new QTableWidgetItem(Fmt(vals[k], 3));
      it->setFlags(it->flags() & ~Qt::ItemIsEditable);
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      compTable_->setItem(r, k + 1, it);
    }
    // 体积列（7）与可浸体积列（9）在 recomputeVolumes() 里填
    auto *volItem = new QTableWidgetItem("0");
    volItem->setFlags(volItem->flags() & ~Qt::ItemIsEditable);
    volItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    compTable_->setItem(r, 7, volItem);

    auto *muItem = new QTableWidgetItem(Fmt(c.permeability, 2));
    muItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    compTable_->setItem(r, 8, muItem);

    auto *permItem = new QTableWidgetItem("0");
    permItem->setFlags(permItem->flags() & ~Qt::ItemIsEditable);
    permItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    compTable_->setItem(r, 9, permItem);
  }
}

//-----------------------------------------------------------------------------
void CompartmentDialog::recomputeVolumes()
{
  QSignalBlocker blocker(compTable_);
  totalVolume_ = 0.0;
  totalPermVolume_ = 0.0;

  for (int r = 0; r < static_cast<int>(comps_.size()); ++r) {
    auto &c = comps_[static_cast<std::size_t>(r)];
    const double v = (c.x2 - c.x1) * (c.y2 - c.y1) * (c.z2 - c.z1);
    totalVolume_ += v;
    totalPermVolume_ += v * c.permeability;
    if (auto *it = compTable_->item(r, 7)) it->setText(Fmt(v, 4));
    if (auto *it = compTable_->item(r, 9)) it->setText(Fmt(v * c.permeability, 4));
  }

  updateChartAndSummary();
  updateCache();
}

//-----------------------------------------------------------------------------
void CompartmentDialog::updateChartAndSummary()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();

  // 曲线采样 161 点：足够让舱壁处的折角清晰，又不至于在表格刷新时拖慢界面
  constexpr int kN = 160;
  curveX_.clear();
  curveV_.clear();
  curveVp_.clear();
  curveX_.reserve(kN + 1);
  curveV_.reserve(kN + 1);
  curveVp_.reserve(kN + 1);

  for (int i = 0; i <= kN; ++i) {
    const double x = L * i / static_cast<double>(kN);
    double v = 0.0, vp = 0.0;
    for (const auto &c : comps_) {
      // 舱室从 x1 起被水线/剖面"切"到 x 处：只累加已经过去的长度
      const double len = std::min(std::max(x - c.x1, 0.0), c.x2 - c.x1);
      const double vv = len * (c.y2 - c.y1) * (c.z2 - c.z1);
      v += vv;
      vp += vv * c.permeability;
    }
    curveX_.push_back(x);
    curveV_.push_back(v);
    curveVp_.push_back(vp);
  }

  chart_->setCurve(curveX_, curveV_, curveVp_, L, totalVolume_);

  summaryLabel_->setText(
      QString("舱室数 <b>%1</b>　｜　总舱容 <b>%2</b> m³　｜　可浸舱容 Σ(μ·V) <b>%3</b> m³"
              "（占 %4%）　｜　船体包络 L·B·D = %5 m³<br>"
              "平均舱长 %6 m　｜　单舱最大体积 %7 m³　｜　"
              "<span style='color:#555'>累积曲线的斜率就是该处的横剖面积；"
              "舱壁处斜率突变说明相邻舱段截面不同（等分舱时是一条直线）。"
              "分舱越细，破舱进水越容易被限制在一个小舱内。</span>")
          .arg(comps_.size())
          .arg(Fmt(totalVolume_, 4))
          .arg(Fmt(totalPermVolume_, 4))
          .arg(totalVolume_ > 1e-9 ? Fmt(100.0 * totalPermVolume_ / totalVolume_, 1) : "0")
          .arg(Fmt(L * B * D, 4))
          .arg(Fmt(L / std::max<std::size_t>(1, comps_.size()), 3))
          .arg(Fmt([&] {
                 double m = 0.0;
                 for (const auto &c : comps_) {
                   m = std::max(m, (c.x2 - c.x1) * (c.y2 - c.y1) * (c.z2 - c.z1));
                 }
                 return m;
               }(), 4)));

  statusLabel_->setText(
      QString("提示：本页舱室全部是轴对齐长方体（z1=0、z2=D），不含双层底与舷侧舱；"
              "渗透率只影响「可浸体积」与下游破舱计算，不影响本页总舱容。"));
}

//-----------------------------------------------------------------------------
void CompartmentDialog::updateCache()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();

  lastObject_ = QJsonObject();
  lastObject_["type"] = "box_barge";
  lastObject_["name"] = "SD-04 分舱驳船";
  lastObject_["lengthM"] = L;
  lastObject_["breadthM"] = B;
  lastObject_["depthM"] = D;
  lastObject_["hullEnvelopeM3"] = L * B * D;

  lastCondition_ = QJsonObject();
  lastCondition_["waterDensity"] = kWaterDensity;

  QJsonArray xb, yb;
  for (double x : bulkheadPositions(true)) xb.append(x);
  for (double y : bulkheadPositions(false)) yb.append(y);
  lastParams_ = QJsonObject();
  lastParams_["transverseBulkheads"] = xb;
  lastParams_["longitudinalBulkheads"] = yb;
  lastParams_["defaultPermeability"] = permSpin_->value();
  lastParams_["compartmentModel"] = "axis_aligned_box";

  lastMetrics_ = QJsonObject();
  lastMetrics_["totalVolumeM3"] = totalVolume_;
  lastMetrics_["totalPermeableVolumeM3"] = totalPermVolume_;
  lastMetrics_["nCompartments"] = static_cast<int>(comps_.size());

  QJsonArray arr;
  for (const auto &c : comps_) {
    const double v = (c.x2 - c.x1) * (c.y2 - c.y1) * (c.z2 - c.z1);
    QJsonObject o;
    o["name"] = QString::fromStdString(c.name);
    o["x1"] = c.x1;
    o["x2"] = c.x2;
    o["y1"] = c.y1;
    o["y2"] = c.y2;
    o["z1"] = c.z1;
    o["z2"] = c.z2;
    o["volumeM3"] = v;
    o["permeability"] = c.permeability;
    o["permeableVolumeM3"] = v * c.permeability;
    o["centroidX"] = 0.5 * (c.x1 + c.x2);
    o["centroidY"] = 0.5 * (c.y1 + c.y2);
    o["centroidZ"] = 0.5 * (c.z1 + c.z2);
    arr.append(o);
  }
  lastExtra_ = QJsonObject();
  lastExtra_["compartments"] = arr;
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onAddTransverse()
{
  const double L = lengthSpin_->value();
  const double x = bulkheadSpin_->value();
  if (x <= 1e-6 || x >= L - 1e-6) {
    QMessageBox::information(this, "添加横向舱壁",
        QString("横向舱壁应位于 0 < x < L（当前 L = %1 m）。").arg(Fmt(L, 3)));
    return;
  }
  for (int i = 0; i < xList_->count(); ++i) {
    if (std::abs(xList_->item(i)->text().toDouble() - x) < 1e-6) return;  // 已存在
  }
  xList_->addItem(Fmt(x, 3));
  onRebuild();
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onRemoveTransverse()
{
  const int row = xList_->currentRow();
  if (row < 0) return;
  delete xList_->takeItem(row);
  onRebuild();
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onAddLongitudinal()
{
  const double B = breadthSpin_->value();
  const double y = bulkheadSpin_->value();
  // 中纵舱壁（y = 0）是合法且常见的分舱方式，故 0 不被排除
  if (y <= -0.5 * B + 1e-6 || y >= 0.5 * B - 1e-6) {
    QMessageBox::information(this, "添加纵向舱壁",
        QString("纵向舱壁应位于 −B/2 与 +B/2 之间（当前 B = %1 m）。").arg(Fmt(B, 3)));
    return;
  }
  for (int i = 0; i < yList_->count(); ++i) {
    if (std::abs(yList_->item(i)->text().toDouble() - y) < 1e-6) return;
  }
  yList_->addItem(Fmt(y, 3));
  onRebuild();
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onRemoveLongitudinal()
{
  const int row = yList_->currentRow();
  if (row < 0) return;
  delete yList_->takeItem(row);
  onRebuild();
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onRebuild()
{
  bulkheadSpin_->setRange(0.0, std::max(1.0, lengthSpin_->value()));
  if (!refresh()) {
    QMessageBox::warning(this, "舱室划分", errMsg_);
    return;
  }
  std::cout << "SD-04: 已重建 " << comps_.size() << " 个舱室，总舱容 "
            << totalVolume_ << " m3" << std::endl;
}

//-----------------------------------------------------------------------------
bool CompartmentDialog::setBulkheads(const std::vector<double> &_transverse,
                                    const std::vector<double> &_longitudinal)
{
  xList_->clear();
  for (double x : _transverse) {
    if (x > 1e-6 && x < lengthSpin_->value() - 1e-6) {
      xList_->addItem(Fmt(x, 3));
    }
  }
  yList_->clear();
  for (double y : _longitudinal) {
    if (y > -0.5 * breadthSpin_->value() + 1e-6 &&
        y < 0.5 * breadthSpin_->value() - 1e-6) {
      yList_->addItem(Fmt(y, 3));
    }
  }
  return refresh();
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onCellChanged(int _row, int _col)
{
  // 只处理渗透率列（8）；其余列已设为不可编辑，这里再判一次防程序性写入触发
  if (_col != 8) return;
  if (_row < 0 || _row >= static_cast<int>(comps_.size())) return;
  auto *item = compTable_->item(_row, _col);
  if (!item) return;

  bool ok = false;
  double mu = item->text().toDouble(&ok);
  if (!ok) {
    mu = comps_[static_cast<std::size_t>(_row)].permeability;
  }
  mu = std::min(std::max(mu, 0.0), 1.0);  // 渗透率越界钳位：外推没有物理意义
  comps_[static_cast<std::size_t>(_row)].permeability = mu;

  {
    QSignalBlocker blocker(compTable_);
    item->setText(Fmt(mu, 2));
  }
  recomputeVolumes();
}

//-----------------------------------------------------------------------------
bool CompartmentDialog::exportTo(const QString &_path)
{
  if (_path.isEmpty()) return false;
  // 导出前重算一次：保证文件里的 metrics 与 extra 出自同一版舱室表
  if (!refresh()) return false;

  keship::ExpDeliverable d;
  d.expId = "SD-04";
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cerr << "SD-04: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << "SD-04: 已导出交付物 " << _path.toStdString()
            << "（" << comps_.size() << " 舱）" << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void CompartmentDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-04 舱室表", "sd04_compartments.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出舱室表",
        QString("导出失败：%1\n请检查目录是否可写，或先修正舱壁输入。").arg(path));
    return;
  }
  QMessageBox::information(this, "导出舱室表",
      QString("已导出：%1\n\n共 %2 个舱室，总舱容 %3 m³。\n"
              "该文件是 HS-07（浮体布局）与 SD-07（破舱稳性）的输入。")
          .arg(path).arg(comps_.size()).arg(Fmt(totalVolume_, 4)));
}
