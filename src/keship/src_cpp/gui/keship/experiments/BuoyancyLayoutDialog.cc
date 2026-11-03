/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-07 浮体布局设计 —— 实现 */

#include "BuoyancyLayoutDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

#include <QCheckBox>
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
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

constexpr double kGravity = 9.81;
constexpr int kDefaultTanks = 4;   ///< 未导入 SD-04 时的示范分舱数

QString Fmt(double _v, int _n = 3) { return QString::number(_v, 'f', _n); }

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// BuoySideViewWidget
// ═══════════════════════════════════════════════════════════════════════════

BuoySideViewWidget::BuoySideViewWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumHeight(200);
}

void BuoySideViewWidget::setLayoutData(double _L, double _D, double _draft,
                                      const std::vector<Box> &_boxes)
{
  L_ = (_L > 1e-9) ? _L : 1.0;
  D_ = (_D > 1e-9) ? _D : 1.0;
  draft_ = _draft;
  boxes_ = _boxes;
  update();
}

void BuoySideViewWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  const QRectF plot(70.0, 34.0, std::max(60.0, width() - 110.0),
                    std::max(60.0, height() - 82.0));

  // 纵轴按"船体高度 + 一点余量"定标：吃水可能超出 D（超载），故取两者较大值
  const double zMax = std::max(D_, draft_) * 1.15 + 1e-6;
  auto toPx = [&](double _x, double _z) {
    return QPointF(plot.left() + (_x / L_) * plot.width(),
                   plot.bottom() - (_z / zMax) * plot.height());
  };
  auto rectOf = [&](double _x1, double _x2, double _z1, double _z2) {
    const QPointF a = toPx(_x1, _z2);
    const QPointF b = toPx(_x2, _z1);
    return QRectF(a, b);
  };

  p.setPen(QPen(QColor("#d0d0d0"), 1.0));
  p.drawRect(plot);

  // 船体轮廓
  p.setPen(QPen(QColor("#333"), 1.6));
  p.setBrush(QColor(235, 235, 235));
  p.drawRect(rectOf(0.0, L_, 0.0, D_));

  // 各浮力体：上部（未浸水）浅色，下部（浸水）蓝色 —— 一眼看出"多少体积在干活"
  for (const auto &b : boxes_) {
    p.setPen(QPen(QColor("#7f9bb5"), 1.2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rectOf(b.x1, b.x2, b.z1, b.z2));
    if (b.immersedTop > b.z1 + 1e-9) {
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(70, 130, 200, 110));
      p.drawRect(rectOf(b.x1, b.x2, b.z1, b.immersedTop));
    }
  }

  // 水线
  const QPointF wl0 = toPx(0.0, draft_);
  const QPointF wl1 = toPx(L_, draft_);
  p.setPen(QPen(QColor("#1f78b4"), 2.0, Qt::DashLine));
  p.drawLine(wl0, wl1);
  p.setPen(QColor("#1f78b4"));
  // 标注画在框内右端：画在框外会被窗口右边缘裁掉
  p.drawText(QRectF(plot.right() - 118, wl1.y() - 18, 112, 15),
             Qt::AlignRight | Qt::AlignVCenter,
             QString("水线 WL  T=%1 m").arg(Fmt(draft_, 2)));

  // 刻度
  p.setPen(QColor("#444"));
  p.drawText(QRectF(6, plot.bottom() - 10, 58, 18), Qt::AlignRight, "z=0");
  p.drawText(QRectF(6, plot.top() - 4, 58, 18), Qt::AlignRight, Fmt(zMax, 2));
  p.drawText(QRectF(plot.left() - 4, plot.bottom() + 4, 90, 16), Qt::AlignLeft, "x=0");
  p.drawText(QRectF(plot.right() - 60, plot.bottom() + 4, 60, 16), Qt::AlignRight,
             QString("x=%1 m").arg(Fmt(L_, 1)));
  p.drawText(QRectF(plot.left(), 8, plot.width(), 18), Qt::AlignLeft | Qt::AlignTop,
             "侧视示意（x–z 剖面；蓝色为浸水部分）");
}

// ═══════════════════════════════════════════════════════════════════════════
// BuoyancyLayoutDialog
// ═══════════════════════════════════════════════════════════════════════════

BuoyancyLayoutDialog::BuoyancyLayoutDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("HS-07 浮体布局设计 —— 舱室 → 浮力体 / 浮力 / 重心高估算");
  resize(1180, 840);
  buildUi();
  refresh();
}

void BuoyancyLayoutDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>把 SD-04 划分好的舱室当作<b>浮力体</b>布置到水下，核对其提供的总浮力"
      "能否托住设计吃水下的排水量，并估算<b>重心高 KG</b>（重量按舱容均匀分布）。<br>"
      "<b>上游依赖：</b>「导入 SD-04 舱室表」读取 <code>sd04_compartments.json</code>，"
      "舱室的位置/尺寸/数量全部来自该文件 —— 本页不重复录入。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 输入 ===
  auto *paramBox = new QGroupBox("主尺度 / 吃水 / 密度", this);
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

  densitySpin_ = new QDoubleSpinBox(paramBox);
  densitySpin_->setRange(900.0, 1100.0); densitySpin_->setValue(1000.0);
  densitySpin_->setSingleStep(10.0); densitySpin_->setDecimals(1);
  densitySpin_->setSuffix(" kg/m³");
  formR->addRow("水密度 ρ：", densitySpin_);

  kgSpin_ = new QDoubleSpinBox(paramBox);
  kgSpin_->setRange(0.0, 20.0); kgSpin_->setValue(0.5);
  kgSpin_->setSingleStep(0.05); kgSpin_->setDecimals(3); kgSpin_->setSuffix(" m");
  formR->addRow("重心高 KG：", kgSpin_);

  kgAutoCheck_ = new QCheckBox("使用估算 KG（按舱容均匀分布）", paramBox);
  kgAutoCheck_->setChecked(true);
  kgSpin_->setEnabled(false);
  formR->addRow("", kgAutoCheck_);

  paramLayout->addLayout(formL);
  paramLayout->addSpacing(20);
  paramLayout->addLayout(formR);

  auto *btnColumn = new QVBoxLayout();
  auto *importBtn = new QPushButton("导入 SD-04 舱室表", paramBox);
  auto *computeBtn = new QPushButton("按当前舱室布置浮力体", paramBox);
  auto *exportBtn = new QPushButton("导出浮体布局（HS-07 交付物）", paramBox);
  btnColumn->addWidget(importBtn);
  btnColumn->addWidget(computeBtn);
  btnColumn->addWidget(exportBtn);
  btnColumn->addStretch();
  paramLayout->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  upstreamLabel_ = new QLabel(this);
  upstreamLabel_->setWordWrap(true);
  upstreamLabel_->setTextFormat(Qt::RichText);
  upstreamLabel_->setStyleSheet("color:#555;");
  mainLayout->addWidget(upstreamLabel_);

  // === 浮力体表 ===
  auto *tableBox = new QGroupBox("浮力体清单（每个舱室对应一个浮力体，只取水线以下部分）", this);
  auto *tableLayout = new QVBoxLayout(tableBox);
  buoyTable_ = new QTableWidget(tableBox);
  buoyTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  buoyTable_->setColumnCount(10);
  buoyTable_->setHorizontalHeaderLabels(
      QStringList() << "浮力体" << "x1" << "x2" << "y1" << "y2" << "z1" << "z2"
                    << "浸深 m" << "浸水体积 m³" << "浮力 N");
  buoyTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  buoyTable_->verticalHeader()->setVisible(false);
  buoyTable_->setMinimumHeight(190);
  tableLayout->addWidget(buoyTable_);
  mainLayout->addWidget(tableBox, 3);

  // === 侧视示意图 ===
  auto *viewBox = new QGroupBox("侧视示意", this);
  auto *viewLayout = new QVBoxLayout(viewBox);
  sideView_ = new BuoySideViewWidget(viewBox);
  viewLayout->addWidget(sideView_);
  mainLayout->addWidget(viewBox, 2);

  summaryLabel_ = new QLabel(this);
  summaryLabel_->setWordWrap(true);
  summaryLabel_->setTextFormat(Qt::RichText);
  mainLayout->addWidget(summaryLabel_);

  kgLabel_ = new QLabel(this);
  kgLabel_->setWordWrap(true);
  kgLabel_->setTextFormat(Qt::RichText);
  mainLayout->addWidget(kgLabel_);

  connect(importBtn, &QPushButton::clicked, this, &BuoyancyLayoutDialog::onImportSd04);
  connect(computeBtn, &QPushButton::clicked, this, &BuoyancyLayoutDialog::onRecompute);
  connect(exportBtn, &QPushButton::clicked, this, &BuoyancyLayoutDialog::onExport);
  connect(kgAutoCheck_, &QCheckBox::toggled, this, [this](bool _on) {
    kgSpin_->setEnabled(!_on);
    if (_on) refresh();
  });
}

//-----------------------------------------------------------------------------
bool BuoyancyLayoutDialog::refresh()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double T = draftSpin_->value();
  const double rho = densitySpin_->value();

  if (!(L > 1e-6) || !(B > 1e-6) || !(D > 1e-6) || !(T > 1e-6) || !(rho > 1.0)) {
    errMsg_ = "主尺度、吃水与水密度必须为正。";
    return false;
  }
  if (T > D) {
    errMsg_ = QString("吃水 T = %1 m 大于型深 D = %2 m：船体已完全没入水中，"
                      "本布置的浮力体口径不再成立。").arg(Fmt(T), Fmt(D));
    return false;
  }

  // 未导入 SD-04 时给一套等分示范舱（仍然"由舱室生成浮力体"，只是舱室是程序生成的）
  if (!sd04Imported_) {
    comps_.clear();
    for (int i = 0; i < kDefaultTanks; ++i) {
      keship::Compartment c;
      c.name = QString("D%1").arg(i + 1).toStdString();
      c.x1 = L * i / kDefaultTanks;
      c.x2 = L * (i + 1) / kDefaultTanks;
      c.y1 = -0.5 * B;
      c.y2 = 0.5 * B;
      c.z1 = 0.0;
      c.z2 = D;
      c.permeability = 0.95;
      comps_.push_back(c);
    }
  }

  // 上游舱室必须能放进本页主尺度，否则浮力算出来是"另一条船"的
  for (const auto &c : comps_) {
    if (c.x1 < -1e-6 || c.x2 > L + 1e-6 || c.z1 < -1e-6 || c.z2 > D + 1e-6) {
      errMsg_ = QString("舱室 %1 超出本页主尺度（x∈[0,L]、z∈[0,D]）："
                        "请把 L/B/D 调成与 SD-04 一致后再计算。")
                    .arg(QString::fromStdString(c.name));
      return false;
    }
    if (std::max(std::abs(c.y1), std::abs(c.y2)) > 0.5 * B + 1e-6) {
      errMsg_ = QString("舱室 %1 的横向范围超出 B/2，浮力体无法落在船宽内。")
                    .arg(QString::fromStdString(c.name));
      return false;
    }
  }

  buoys_.clear();
  totalBuoyancyN_ = 0.0;
  totalVolume_ = 0.0;
  double sumVz = 0.0;      // 浸水体积 × 垂心 → KB
  double sumFullV = 0.0;   // 全舱体积
  double sumFullVz = 0.0;  // 全舱体积 × 垂心 → KG 估算
  double sumV = 0.0;

  for (const auto &c : comps_) {
    Buoy b;
    b.name = QString::fromStdString(c.name);
    b.x1 = c.x1; b.x2 = c.x2;
    b.y1 = c.y1; b.y2 = c.y2;
    b.z1 = c.z1; b.z2 = c.z2;
    b.immersedTop = std::min(c.z2, T);
    b.immersedH = std::max(0.0, b.immersedTop - c.z1);

    const double area = (c.x2 - c.x1) * (c.y2 - c.y1);
    b.volume = area * b.immersedH;                 // 浮力体：几何浸水体积
    b.forceN = rho * kGravity * b.volume;
    b.cx = 0.5 * (c.x1 + c.x2);
    b.cy = 0.5 * (c.y1 + c.y2);
    b.cz = c.z1 + 0.5 * b.immersedH;
    b.fullVolume = (c.x2 - c.x1) * (c.y2 - c.y1) * (c.z2 - c.z1);
    b.fullCz = 0.5 * (c.z1 + c.z2);

    totalBuoyancyN_ += b.forceN;
    totalVolume_ += b.fullVolume;
    sumV += b.volume;
    sumVz += b.volume * b.cz;
    sumFullV += b.fullVolume;
    sumFullVz += b.fullVolume * b.fullCz;
    buoys_.push_back(b);
  }

  kgEstimatedM_ = (sumFullV > 1e-12) ? sumFullVz / sumFullV : 0.0;
  kbM_ = (sumV > 1e-12) ? sumVz / sumV : 0.0;

  if (kgAutoCheck_->isChecked()) {
    QSignalBlocker blocker(kgSpin_);
    kgSpin_->setValue(kgEstimatedM_);
  }
  kgM_ = kgSpin_->value();

  fillTable();
  updateView();
  updateCache();
  return true;
}

//-----------------------------------------------------------------------------
void BuoyancyLayoutDialog::fillTable()
{
  buoyTable_->setRowCount(static_cast<int>(buoys_.size()));
  for (int r = 0; r < static_cast<int>(buoys_.size()); ++r) {
    const auto &b = buoys_[static_cast<std::size_t>(r)];
    const QString texts[10] = {
        b.name, Fmt(b.x1), Fmt(b.x2), Fmt(b.y1), Fmt(b.y2),
        Fmt(b.z1), Fmt(b.z2), Fmt(b.immersedH), Fmt(b.volume, 4), Fmt(b.forceN, 2)};
    for (int k = 0; k < 10; ++k) {
      auto *it = new QTableWidgetItem(texts[k]);
      it->setTextAlignment((k == 0) ? (Qt::AlignLeft | Qt::AlignVCenter)
                                    : (Qt::AlignRight | Qt::AlignVCenter));
      buoyTable_->setItem(r, k, it);
    }
  }
}

//-----------------------------------------------------------------------------
void BuoyancyLayoutDialog::updateView()
{
  const double L = lengthSpin_->value();
  const double D = depthSpin_->value();
  const double T = draftSpin_->value();
  const double rho = densitySpin_->value();

  std::vector<BuoySideViewWidget::Box> boxes;
  boxes.reserve(buoys_.size());
  for (const auto &b : buoys_) {
    boxes.push_back({b.x1, b.x2, b.z1, b.z2, b.immersedTop});
  }
  sideView_->setLayoutData(L, D, T, boxes);

  // 需要的浮力：按矩形驳船整船浸水体算（= ρ·g·L·B·T）
  const double requiredN = rho * kGravity * L * breadthSpin_->value() * T;
  const double cover = (requiredN > 1e-9) ? 100.0 * totalBuoyancyN_ / requiredN : 0.0;

  double immersedV = 0.0;
  for (const auto &b : buoys_) immersedV += b.volume;

  const QString warn = (cover < 99.9)
      ? QString("<br><span style='color:#b00'>⚠ 覆盖率不足 100%%："
                "要么有舱室没铺满船长/船宽，要么吃水超过舱顶 z2 —— "
                "此时布放的浮力体托不住该吃水下的排水量。</span>")
      : QString();

  summaryLabel_->setText(
      QString("浮力体 <b>%1</b> 个　｜　浸水总体积 <b>%2</b> m³　｜　总浮力 <b>%3</b> N"
              "（%4 kg 力）<br>"
              "整船浸水体所需浮力（ρ·g·L·B·T）= %5 N　→　布放覆盖率 <b>%6%</b>")
          .arg(buoys_.size())
          .arg(Fmt(immersedV, 4))
          .arg(Fmt(totalBuoyancyN_, 2))
          .arg(Fmt(totalBuoyancyN_ / kGravity, 2))
          .arg(Fmt(requiredN, 2))
          .arg(Fmt(cover, 2))
      + warn);

  kgLabel_->setText(
      QString("浮心高 KB = <b>%1</b> m（浸水体积垂心）　｜　"
              "估算重心高 KG = <b>%2</b> m（全舱体积垂直分布）　｜　"
              "本页采用 KG = <b>%3</b> m<br>"
              "<span style='color:#555'>KG 与 KB 的差是初稳性的第一道坎："
              "GM ≈ KB + BM − KG，KG 抬高就意味着复原力臂被吃掉一块。"
              "本页 KG 只由<b>舱容分布</b>决定，不含设备/货物/压载 —— "
              "HS-08 可直接引用该 KG，但要知道它只是「空船均匀装载」假设下的值。</span>")
          .arg(Fmt(kbM_, 4))
          .arg(Fmt(kgEstimatedM_, 4))
          .arg(Fmt(kgM_, 4)));
}

//-----------------------------------------------------------------------------
void BuoyancyLayoutDialog::updateCache()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double T = draftSpin_->value();
  const double rho = densitySpin_->value();

  lastObject_ = QJsonObject();
  lastObject_["type"] = "buoy_layout";
  lastObject_["name"] = "HS-07 浮体布局";
  lastObject_["lengthM"] = L;
  lastObject_["breadthM"] = B;
  lastObject_["depthM"] = D;
  lastObject_["source"] = sd04Imported_ ? "SD-04" : "builtin_demo";

  lastCondition_ = QJsonObject();
  lastCondition_["draftM"] = T;
  lastCondition_["waterDensity"] = rho;
  lastCondition_["gravity"] = kGravity;

  lastParams_ = QJsonObject();
  lastParams_["kgMode"] = kgAutoCheck_->isChecked() ? "estimated_uniform_volume" : "manual";
  lastParams_["buoyDefinition"] = "immersed_geometric_volume";
  lastParams_["permeabilityApplied"] = false;
  lastParams_["nCompartments"] = static_cast<int>(buoys_.size());

  lastMetrics_ = QJsonObject();
  lastMetrics_["totalBuoyancyN"] = totalBuoyancyN_;
  lastMetrics_["kgM"] = kgM_;
  lastMetrics_["nBuoys"] = static_cast<int>(buoys_.size());

  QJsonArray arr;
  for (const auto &b : buoys_) {
    QJsonObject o;
    o["name"] = b.name;
    o["x1"] = b.x1; o["x2"] = b.x2;
    o["y1"] = b.y1; o["y2"] = b.y2;
    o["z1"] = b.z1; o["z2"] = b.z2;
    o["immersedHeightM"] = b.immersedH;
    o["volumeM3"] = b.volume;
    o["buoyancyN"] = b.forceN;
    o["centroidX"] = b.cx;
    o["centroidY"] = b.cy;
    o["centroidZ"] = b.cz;
    arr.append(o);
  }

  lastExtra_ = QJsonObject();
  lastExtra_["buoys"] = arr;
  lastExtra_["kbM"] = kbM_;
  lastExtra_["kgEstimatedM"] = kgEstimatedM_;
  const double requiredN = rho * kGravity * L * B * T;
  lastExtra_["requiredBuoyancyN"] = requiredN;
  lastExtra_["coverageRatio"] = (requiredN > 1e-9) ? totalBuoyancyN_ / requiredN : 0.0;

  QJsonObject up;
  up["expId"] = "SD-04";
  up["nCompartments"] = upstreamNCompartments_;
  up["totalVolumeM3"] = upstreamTotalVolume_;
  up["imported"] = sd04Imported_;
  lastExtra_["upstream"] = up;
}

//-----------------------------------------------------------------------------
void BuoyancyLayoutDialog::onImportSd04()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 SD-04 舱室表", "sd04_compartments.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!importSd04(path)) {
    QMessageBox::warning(this, "导入 SD-04 舱室表",
        QString::fromStdString("无法使用该文件：\n" + errMsg_.toStdString()));
  }
}

//-----------------------------------------------------------------------------
bool BuoyancyLayoutDialog::importSd04(const QString &_path)
{
  keship::ExpDeliverable d;
  std::string err;
  // 真读 + 校验 expId：拿 HS-08 的文件来当舱室表必须被拒绝（串号防御）
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "SD-04", &err)) {
    errMsg_ = QString::fromStdString(err);
    return false;
  }

  const QJsonArray arr = d.extra["compartments"].toArray();
  if (arr.isEmpty()) {
    errMsg_ = "文件里没有 extra.compartments —— 该文件可能不是 SD-04 导出的舱室表。";
    return false;
  }

  std::vector<keship::Compartment> parsed;
  for (const auto &v : arr) {
    const QJsonObject o = v.toObject();
    keship::Compartment c;
    c.name = o["name"].toString().toStdString();
    c.x1 = o["x1"].toDouble();
    c.x2 = o["x2"].toDouble();
    c.y1 = o["y1"].toDouble();
    c.y2 = o["y2"].toDouble();
    c.z1 = o["z1"].toDouble();
    c.z2 = o["z2"].toDouble();
    c.permeability = o["permeability"].toDouble(0.95);
    parsed.push_back(c);
  }
  if (parsed.empty()) {
    errMsg_ = "解析后没有任何有效舱室。";
    return false;
  }

  comps_ = parsed;
  sd04Imported_ = true;
  upstreamTotalVolume_ = d.metrics["totalVolumeM3"].toDouble();
  upstreamNCompartments_ = d.metrics["nCompartments"].toInt(
      static_cast<int>(parsed.size()));
  upstreamSource_ = _path;

  // 主尺度随舱室表一起搬过来：舱室坐标是上游坐标系里的绝对量，
  // 若这里仍留着本页旧的 L/B/D，浮力体就会落在船体外或凭空缺一块。
  {
    QSignalBlocker b1(lengthSpin_);
    QSignalBlocker b2(breadthSpin_);
    QSignalBlocker b3(depthSpin_);
    lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
    breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));
    depthSpin_->setValue(d.object["depthM"].toDouble(depthSpin_->value()));
  }

  if (!refresh()) return false;

  // 上游值 vs 本页复现值 —— 判据 B 要求"读进来"必须可核对
  double mine = 0.0;
  for (const auto &b : buoys_) mine += b.fullVolume;
  const double rel = (upstreamTotalVolume_ > 1e-9)
                         ? std::abs(mine - upstreamTotalVolume_) / upstreamTotalVolume_
                         : 0.0;
  upstreamLabel_->setText(
      QString("已导入 SD-04：<code>%1</code><br>"
              "上游交付：<b>%2</b> 舱 / 总舱容 <b>%3</b> m³　→　本页复现：<b>%4</b> 舱 / "
              "Σ舱室体积 <b>%5</b> m³（相对偏差 <b>%6%</b>）")
          .arg(_path)
          .arg(upstreamNCompartments_)
          .arg(Fmt(upstreamTotalVolume_, 4))
          .arg(buoys_.size())
          .arg(Fmt(mine, 4))
          .arg(Fmt(rel * 100.0, 4)));
  upstreamLabel_->setStyleSheet(rel > 0.01 ? "color:#b00;" : "color:#338833;");

  std::cout << "HS-07: 已导入 SD-04 舱室 " << buoys_.size()
            << " 个（上游总舱容 " << upstreamTotalVolume_ << " m3）" << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void BuoyancyLayoutDialog::onRecompute()
{
  if (!refresh()) {
    QMessageBox::warning(this, "浮体布局", errMsg_);
    return;
  }
  std::cout << "HS-07: 已布置 " << buoys_.size() << " 个浮力体，总浮力 "
            << totalBuoyancyN_ << " N，KG=" << kgM_ << " m" << std::endl;
}

//-----------------------------------------------------------------------------
bool BuoyancyLayoutDialog::exportTo(const QString &_path)
{
  if (_path.isEmpty()) return false;
  if (!refresh()) return false;

  keship::ExpDeliverable d;
  d.expId = "HS-07";
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cerr << "HS-07: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << "HS-07: 已导出交付物 " << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void BuoyancyLayoutDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 HS-07 浮体布局", "hs07_buoyancy.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出浮体布局", QString("导出失败：%1").arg(path));
    return;
  }
  QMessageBox::information(this, "导出浮体布局",
      QString("已导出：%1\n\n浮力体 %2 个　总浮力 %3 N　KG = %4 m。\n"
              "该文件是 HS-08 的输入（HS-08 读 metrics.kgM 当重心高）。")
          .arg(path).arg(buoys_.size()).arg(Fmt(totalBuoyancyN_, 2)).arg(Fmt(kgM_, 4)));
}
