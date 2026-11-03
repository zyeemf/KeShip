/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-08 参数化型线设计 —— 实现 */

#include "ParametricHullDialog.hh"

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
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"
#include "ShipHullGenerator.hh"

namespace {
/// 海水密度：与 HS-04 / 静水力口径一致（ShipHullGenerator 内部用 1000 淡水）
constexpr double kWaterDensity = 1000.0;
}  // namespace

ParametricHullDialog::ParametricHullDialog(QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle("SD-08 参数化型线设计 —— 主尺度 / 型线系数 / 型值表");
  resize(1080, 760);
  buildUi();
  refresh();
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>改变主尺度 <i>L/B/D</i> 与型线形状指数 <i>p/q</i>，观察方形系数 "
      "<i>C<sub>b</sub></i>、水线面系数 <i>C<sub>wp</sub></i> 与初稳性高 <i>GM</i> 如何随之变化；"
      "也可给定目标 <i>C<sub>b</sub></i>，由程序反解所需的水线面指数。<br>"
      "<b>交付物：</b>「导出型线数据」生成的 <code>sd08_lines.json</code> 是 HS-05 / "
      "HS-09 的输入——下游必须能用它复现同一船型的静水力。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 参数区 ===
  auto *paramBox = new QGroupBox("参数", this);
  auto *paramLayout = new QHBoxLayout(paramBox);

  auto *formLeft = new QFormLayout();
  familyCombo_ = new QComboBox(paramBox);
  familyCombo_->addItem("Wigley 经典（抛物水线 + 抛物横剖）");
  familyCombo_->addItem("Wigley 平底（大水线面）");
  familyCombo_->addItem("Wigley 尖底（V 形）");
  familyCombo_->addItem("Wigley 瘦削（小水线面）");
  familyCombo_->addItem("Lewis U 形横剖");
  familyCombo_->addItem("Lewis V 形横剖");
  formLeft->addRow("船型族：", familyCombo_);

  lengthSpin_ = new QDoubleSpinBox(paramBox);
  lengthSpin_->setRange(2.0, 30.0); lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5); lengthSpin_->setSuffix(" m");
  lengthSpin_->setDecimals(3);
  formLeft->addRow("船长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(paramBox);
  breadthSpin_->setRange(0.5, 10.0); breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1); breadthSpin_->setSuffix(" m");
  breadthSpin_->setDecimals(3);
  formLeft->addRow("型宽 B：", breadthSpin_);

  depthSpin_ = new QDoubleSpinBox(paramBox);
  depthSpin_->setRange(0.3, 5.0); depthSpin_->setValue(1.0);
  depthSpin_->setSingleStep(0.05); depthSpin_->setSuffix(" m");
  depthSpin_->setDecimals(3);
  formLeft->addRow("型深 D：", depthSpin_);

  auto *formRight = new QFormLayout();
  densitySpin_ = new QDoubleSpinBox(paramBox);
  densitySpin_->setRange(100.0, 990.0); densitySpin_->setValue(500.0);
  densitySpin_->setSingleStep(25.0); densitySpin_->setSuffix(" kg/m³");
  densitySpin_->setDecimals(1);
  formRight->addRow("船体密度 ρ：", densitySpin_);

  wlExpSpin_ = new QDoubleSpinBox(paramBox);
  wlExpSpin_->setRange(0.5, 8.0); wlExpSpin_->setValue(2.0);
  wlExpSpin_->setSingleStep(0.1); wlExpSpin_->setDecimals(2);
  formRight->addRow("水线面指数 p：", wlExpSpin_);

  secExpSpin_ = new QDoubleSpinBox(paramBox);
  secExpSpin_->setRange(0.5, 8.0); secExpSpin_->setValue(2.0);
  secExpSpin_->setSingleStep(0.1); secExpSpin_->setDecimals(2);
  formRight->addRow("横剖面指数 q：", secExpSpin_);

  targetCbSpin_ = new QDoubleSpinBox(paramBox);
  targetCbSpin_->setRange(0.0, 0.95); targetCbSpin_->setValue(0.0);
  targetCbSpin_->setSingleStep(0.01); targetCbSpin_->setDecimals(3);
  targetCbSpin_->setSpecialValueText("不反解");
  formRight->addRow("目标 Cb：", targetCbSpin_);

  paramLayout->addLayout(formLeft);
  paramLayout->addSpacing(24);
  paramLayout->addLayout(formRight);

  auto *btnColumn = new QVBoxLayout();
  auto *regenBtn = new QPushButton("重新生成型线", paramBox);
  auto *solveBtn = new QPushButton("按目标 Cb 反解", paramBox);
  auto *exportBtn = new QPushButton("导出型线数据（SD-08 交付物）", paramBox);
  btnColumn->addWidget(regenBtn);
  btnColumn->addWidget(solveBtn);
  btnColumn->addWidget(exportBtn);
  btnColumn->addStretch();
  paramLayout->addLayout(btnColumn);

  mainLayout->addWidget(paramBox);

  // === 静水力 ===
  auto *hydroBox = new QGroupBox("静水力（设计吃水处）", this);
  auto *hydroLayout = new QVBoxLayout(hydroBox);
  hydroLabel_ = new QLabel(this);
  hydroLabel_->setWordWrap(true);
  hydroLabel_->setTextFormat(Qt::RichText);
  hydroLayout->addWidget(hydroLabel_);
  mainLayout->addWidget(hydroBox);

  // === 型值表 ===
  auto *linesBox = new QGroupBox("型值表（半宽 y，单位 m；纵向为站位 x，横向为水线 z）", this);
  auto *linesLayout = new QVBoxLayout(linesBox);
  linesTable_ = new QTableWidget(this);
  linesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  linesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  linesLayout->addWidget(linesTable_);
  mainLayout->addWidget(linesBox, 1);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  mainLayout->addWidget(statusLabel_);

  connect(familyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ParametricHullDialog::onFamilyChanged);
  for (QDoubleSpinBox *s : {lengthSpin_, breadthSpin_, depthSpin_, densitySpin_,
                            wlExpSpin_, secExpSpin_})
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { /* 由「重新生成」显式触发，避免每次键入都重算 */ });
  connect(regenBtn, &QPushButton::clicked, this, &ParametricHullDialog::onRegenerate);
  connect(solveBtn, &QPushButton::clicked, this, &ParametricHullDialog::onSolveCb);
  connect(exportBtn, &QPushButton::clicked, this, &ParametricHullDialog::onExport);
}

//-----------------------------------------------------------------------------
QString ParametricHullDialog::currentFamilyName() const
{
  return familyCombo_ ? familyCombo_->currentText() : QString();
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::onFamilyChanged(int)
{
  // 换船型族时，形状指数回到该族的名义值（再允许用户微调）
  // ★ 2026-10-29（D4-余）：Lewis U/V 两族走 secType=LEWIS_U/V 独立分支，
  //   sectionShape() 不读 secExponent —— 界面此前仍显示 2.5/1.0 的"无效数字"会误导
  //   学生（水线指数 wlExponent 两族仍参与计算，保持可用）。
  //   拍板方案 A：该两族下置灰 sec 指数并提示，切回 Wigley 族恢复可用。
  const int fam = familyCombo_->currentIndex();
  const bool lewis = (fam == 4 || fam == 5);
  switch (fam) {
    case 1: wlExpSpin_->setValue(3.0); secExpSpin_->setValue(2.0); break;
    case 2: wlExpSpin_->setValue(2.0); secExpSpin_->setValue(1.0); break;
    case 3: wlExpSpin_->setValue(1.2); secExpSpin_->setValue(2.0); break;
    case 4:                                              ///< Lewis U：sec 指数不适用
    case 5:                                              ///< Lewis V：sec 指数不适用
    default: wlExpSpin_->setValue(2.0); secExpSpin_->setValue(2.0); break;
  }
  secExpSpin_->setEnabled(!lewis);
  secExpSpin_->setToolTip(lewis
      ? "本族横剖面形状由 Lewis 型线决定，横剖面指数不适用"
      : "");
  refresh();
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::refresh()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double rho = densitySpin_->value();

  keship::ShipHullSpec spec;
  switch (familyCombo_->currentIndex()) {
    case 1: spec = keship::ShipHullSpec::WigleyFlat(L, B, D, rho); break;
    case 2: spec = keship::ShipHullSpec::WigleySharp(L, B, D, rho); break;
    case 3: spec = keship::ShipHullSpec::WigleyFine(L, B, D, rho); break;
    case 4: spec = keship::ShipHullSpec::LewisU(L, B, D, rho); break;
    case 5: spec = keship::ShipHullSpec::LewisV(L, B, D, rho); break;
    default: spec = keship::ShipHullSpec::WigleyClassic(L, B, D, rho); break;
  }
  spec.wlExponent = wlExpSpin_->value();
  spec.secExponent = secExpSpin_->value();

  hull_ = std::make_unique<keship::ShipHullGenerator>(spec);

  const auto &hp = hull_->hydroProps();
  cbAchieved_ = (hp.draft > 1e-9) ? hp.volumeD / (L * B * hp.draft) : 0.0;

  fillHydroLabels();
  fillLinesTable();

  statusLabel_->setText(
      QString("型线网格：%1 站位 × %2 水线；网格排水体积（散度定理）= %3 m³")
          .arg(hull_->lines().stations.size())
          .arg(hull_->lines().waterlines.size())
          .arg(hull_->cattoTotalVolume(), 0, 'f', 5));
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::fillHydroLabels()
{
  if (!hull_) return;
  const auto &hp = hull_->hydroProps();
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();

  hydroLabel_->setText(
      QString("排水体积 ∇<sub>D</sub> = <b>%1</b> m³　｜　排水量 Δ = <b>%2</b> kg　｜　"
              "设计吃水 T = <b>%3</b> m　｜　方形系数 C<sub>b</sub> = <b>%4</b><br>"
              "水线面 Aw = %5 m²　C<sub>wp</sub> = %6　｜　"
              "KB = %7 m　BM = %8 m　<b>GM = %9</b> m　｜　I<sub>x</sub> = %10 m⁴")
          .arg(hp.volumeD, 0, 'f', 5)
          .arg(hp.volumeD * kWaterDensity, 0, 'f', 2)
          .arg(hp.draft, 0, 'f', 5)
          .arg(cbAchieved_, 0, 'f', 5)
          .arg(hp.AwD, 0, 'f', 5)
          .arg(hp.Cwp, 0, 'f', 5)
          .arg(hp.KBup, 0, 'f', 5)
          .arg(hp.BMd, 0, 'f', 5)
          .arg(hp.GMd, 0, 'f', 5)
          .arg(hp.IxD, 0, 'f', 6));

  Q_UNUSED(L);
  Q_UNUSED(B);
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::fillLinesTable()
{
  if (!hull_) return;
  const auto &ld = hull_->lines();
  const int nSt = static_cast<int>(ld.stations.size());
  const int nWl = static_cast<int>(ld.waterlines.size());
  if (nSt == 0 || nWl == 0) return;

  // 抽样显示，保证行列都不超过约 12×8（完整数据在导出文件里）
  lastStationStride_ = std::max(1, nSt / 12);
  lastWaterlineStride_ = std::max(1, nWl / 8);

  const int rows = (nSt + lastStationStride_ - 1) / lastStationStride_;
  const int cols = (nWl + lastWaterlineStride_ - 1) / lastWaterlineStride_;

  linesTable_->clear();
  linesTable_->setRowCount(rows);
  linesTable_->setColumnCount(cols);

  QStringList headers;
  for (int j = 0; j < cols; ++j) {
    const int wlIdx = std::min(j * lastWaterlineStride_, nWl - 1);
    headers << QString("z=%1 m").arg(ld.waterlines[wlIdx], 0, 'f', 3);
  }
  linesTable_->setHorizontalHeaderLabels(headers);

  for (int i = 0; i < rows; ++i) {
    const int stIdx = std::min(i * lastStationStride_, nSt - 1);
    linesTable_->setVerticalHeaderItem(
        i, new QTableWidgetItem(QString("x=%1 m").arg(ld.stations[stIdx], 0, 'f', 3)));
    const auto &row = ld.halfBreadths[stIdx];
    for (int j = 0; j < cols; ++j) {
      const int wlIdx = std::min(j * lastWaterlineStride_, nWl - 1);
      const double v = (wlIdx < static_cast<int>(row.size())) ? row[wlIdx] : 0.0;
      auto *item = new QTableWidgetItem(QString::number(v, 'f', 4));
      item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      linesTable_->setItem(i, j, item);
    }
  }
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::onRegenerate()
{
  refresh();
  std::cout << "SD-08: 已重新生成型线（Cb = " << cbAchieved_ << "）" << std::endl;
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::onSolveCb()
{
  const double target = targetCbSpin_->value();
  if (target <= 0.0) {
    QMessageBox::information(this, "按目标 Cb 反解",
        "请先把「目标 Cb」设为大于 0 的值（例如 0.6）。\n"
        "反解对象是水线面形状指数 p（Cb 随 p 增大而增大）。");
    return;
  }

  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double rho = densitySpin_->value();
  const int family = familyCombo_->currentIndex();
  const double q = secExpSpin_->value();

  auto cbOf = [&](double _p) {
    keship::ShipHullSpec s;
    switch (family) {
      case 1: s = keship::ShipHullSpec::WigleyFlat(L, B, D, rho); break;
      case 2: s = keship::ShipHullSpec::WigleySharp(L, B, D, rho); break;
      case 3: s = keship::ShipHullSpec::WigleyFine(L, B, D, rho); break;
      case 4: s = keship::ShipHullSpec::LewisU(L, B, D, rho); break;
      case 5: s = keship::ShipHullSpec::LewisV(L, B, D, rho); break;
      default: s = keship::ShipHullSpec::WigleyClassic(L, B, D, rho); break;
    }
    s.wlExponent = _p;
    s.secExponent = q;
    keship::ShipHullGenerator g(s);
    const auto &h = g.hydroProps();
    return (h.draft > 1e-9) ? h.volumeD / (L * B * h.draft) : 0.0;
  };

  // ① 先验单调性：Cb 必须随 p 单调递增，否则「反解」得到的答案是假的。
  //    （不做这一步就二分，会在非单调区间给出看似合理其实错的 p —— 见检查器空转教训）
  const int kProbe = 9;
  double prev = cbOf(0.5);
  bool monotone = true;
  for (int i = 1; i < kProbe; ++i) {
    const double p = 0.5 + (8.0 - 0.5) * i / (kProbe - 1);
    const double cb = cbOf(p);
    if (cb <= prev) { monotone = false; break; }
    prev = cb;
  }
  if (!monotone) {
    QMessageBox::warning(this, "按目标 Cb 反解",
        "当前船型族下 Cb 对水线面指数 p 并非单调，反解结果不可信，已中止。\n"
        "请改用「重新生成型线」手动试算，或换一个船型族。");
    return;
  }

  double lo = 0.5, hi = 8.0;
  const double fLo = cbOf(lo) - target;
  const double fHi = cbOf(hi) - target;
  if (fLo * fHi > 0.0) {
    QMessageBox::information(this, "按目标 Cb 反解",
        QString("目标 Cb = %1 不在当前主尺度可达区间 [%2, %3] 内。\n"
                "可调大/调小 L、B 或改变横剖面指数 q 后重试。")
            .arg(target, 0, 'f', 3)
            .arg(cbOf(lo), 0, 'f', 3)
            .arg(cbOf(hi), 0, 'f', 3));
    return;
  }

  double p = wlExpSpin_->value();
  for (int it = 0; it < 60; ++it) {
    p = 0.5 * (lo + hi);
    const double f = cbOf(p) - target;
    if (std::abs(f) < 1e-4) break;
    if (f * fLo > 0.0) lo = p; else hi = p;
  }

  wlExpSpin_->setValue(p);
  refresh();

  QMessageBox::information(this, "按目标 Cb 反解",
      QString("反解完成：p = %1\n目标 Cb = %2　达成 Cb = %3（偏差 %4）")
          .arg(p, 0, 'f', 5)
          .arg(target, 0, 'f', 5)
          .arg(cbAchieved_, 0, 'f', 5)
          .arg(std::abs(cbAchieved_ - target), 0, 'e', 2));
  std::cout << "SD-08: Cb 反解 p=" << p << " 达成 Cb=" << cbAchieved_ << std::endl;
}

//-----------------------------------------------------------------------------
bool ParametricHullDialog::exportTo(const QString &_path, std::string *_err)
{
  if (!hull_) {
    if (_err) *_err = "尚未生成型线，请先点「重新生成型线」";
    return false;
  }
  if (_path.isEmpty()) {
    if (_err) *_err = "导出路径为空";
    return false;
  }

  const auto &hp = hull_->hydroProps();
  const auto &ld = hull_->lines();

  keship::ExpDeliverable d;
  d.expId = "SD-08";
  d.createdAt = keship::NowIso8601();

  d.object["type"] = "math_hull";
  d.object["family"] = currentFamilyName();
  /// 下游（HS-05）靠这个整数还原船型族——用文字匹配会在"尖底/尖舭"这类译名差异上失配
  d.object["familyIndex"] = familyCombo_->currentIndex();
  d.object["lengthM"] = lengthSpin_->value();
  d.object["breadthM"] = breadthSpin_->value();
  d.object["depthM"] = depthSpin_->value();
  d.object["densityKgM3"] = densitySpin_->value();

  d.params["wlExponent"] = wlExpSpin_->value();
  d.params["secExponent"] = secExpSpin_->value();
  d.params["targetCb"] = targetCbSpin_->value();
  d.params["waterDensity"] = kWaterDensity;

  d.condition["draftM"] = hp.draft;

  d.metrics["volumeD"] = hp.volumeD;
  d.metrics["displacementKg"] = hp.volumeD * kWaterDensity;
  d.metrics["draftM"] = hp.draft;
  d.metrics["cb"] = cbAchieved_;
  d.metrics["awD"] = hp.AwD;
  d.metrics["cwp"] = hp.Cwp;
  d.metrics["KBup"] = hp.KBup;
  d.metrics["BMd"] = hp.BMd;
  d.metrics["GMd"] = hp.GMd;
  d.metrics["IxD"] = hp.IxD;
  d.metrics["cattoVolume"] = hull_->cattoTotalVolume();

  QJsonArray stations, waterlines, halfBreadths;
  for (double x : ld.stations) stations.append(x);
  for (double z : ld.waterlines) waterlines.append(z);
  for (const auto &row : ld.halfBreadths) {
    QJsonArray r;
    for (double v : row) r.append(v);
    halfBreadths.append(r);
  }
  d.extra["stations"] = stations;
  d.extra["waterlines"] = waterlines;
  d.extra["halfBreadths"] = halfBreadths;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    if (_err) *_err = err;
    return false;
  }
  std::cout << "SD-08: 已导出交付物 " << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void ParametricHullDialog::onExport()
{
  if (!hull_) {
    QMessageBox::warning(this, "导出型线数据", "尚未生成型线，请先点「重新生成型线」。");
    return;
  }

  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-08 型线数据", "sd08_lines.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!exportTo(path, &err)) {
    QMessageBox::warning(this, "导出型线数据",
        QString::fromStdString("导出失败：" + err));
    return;
  }

  QMessageBox::information(this, "导出型线数据",
      QString("已导出：%1\n\n该文件是 HS-05 / HS-09 的输入（判据 B：机器交付物贯通）。")
          .arg(path));
}
