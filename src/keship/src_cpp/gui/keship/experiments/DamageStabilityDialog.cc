/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-07 破舱稳性仿真 —— 实现 */

#include "DamageStabilityDialog.hh"

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
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

constexpr double kGravity = 9.81;
constexpr double kGmThreshold = 0.15;   ///< IMO 完整稳性 GM 下限，破舱后作参照线
constexpr int kDefaultTanks = 3;

QString Fmt(double _v, int _n = 3) { return QString::number(_v, 'f', _n); }

}  // namespace

// ═══════════════════════════════════════════════════════════════════════════
// DamageGmBarWidget
// ═══════════════════════════════════════════════════════════════════════════

DamageGmBarWidget::DamageGmBarWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumHeight(200);
}

void DamageGmBarWidget::setData(const QStringList &_names, const std::vector<double> &_gm,
                               const std::vector<bool> &_canFloat)
{
  names_ = _names;
  gm_ = _gm;
  canFloat_ = _canFloat;
  update();
}

void DamageGmBarWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  // 上边距 38 px：标题独立占一行，避免被分组框上边框裁掉
  const QRectF plot(72.0, 38.0, std::max(60.0, width() - 110.0),
                    std::max(60.0, height() - 94.0));
  p.setPen(QPen(QColor("#d0d0d0"), 1.0));
  p.drawRect(plot);
  p.setPen(QColor("#444"));
  p.drawText(QRectF(plot.left(), 8, plot.width(), 18), Qt::AlignLeft,
             "逐舱残存 GM / m（橙虚线 = IMO 0.15 m；红柱 = 不满足或不能漂浮）");

  if (names_.isEmpty()) {
    p.setPen(QColor("#888"));
    p.drawText(plot, Qt::AlignCenter, "尚无破舱结果（请点「开始破舱计算」）");
    return;
  }

  double vMax = kGmThreshold * 1.6;
  for (double v : gm_) vMax = std::max(vMax, v);
  if (vMax <= 1e-9) vMax = 1.0;

  auto yOf = [&](double _v) {
    return plot.bottom() - (_v / vMax) * plot.height();
  };

  // 纵轴刻度（0 / 中值 / 上限），否则柱子高低没有标尺
  p.setPen(QColor("#444"));
  for (int i = 0; i <= 2; ++i) {
    const double v = vMax * i / 2.0;
    p.drawText(QRectF(plot.left() - 60, yOf(v) - 8, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, Fmt(v, 3));
  }

  // 0.15 m 参考线先画线，标注放到柱子之后再画（并铺浅底），
  // 否则柱子会把标注盖掉 —— 这张图的柱子几乎顶满高度，任何"画在柱子上"的文字都会被吃
  p.setPen(QPen(QColor("#e67e22"), 1.4, Qt::DashLine));
  p.drawLine(QPointF(plot.left(), yOf(kGmThreshold)),
             QPointF(plot.right(), yOf(kGmThreshold)));

  const int n = static_cast<int>(gm_.size());
  const double slot = plot.width() / std::max(1, n);
  const double barW = std::min(46.0, slot * 0.6);
  for (int i = 0; i < n; ++i) {
    const double x = plot.left() + slot * (i + 0.5);
    const bool ok = (i < static_cast<int>(canFloat_.size())) ? canFloat_[i] : true;
    const bool pass = ok && gm_[static_cast<std::size_t>(i)] >= kGmThreshold;
    QRectF bar(x - barW * 0.5, yOf(std::max(0.0, gm_[static_cast<std::size_t>(i)])),
               barW, plot.bottom() - yOf(std::max(0.0, gm_[static_cast<std::size_t>(i)])));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(pass ? "#4a90c2" : "#c0504d"));
    p.drawRect(bar);

    p.setPen(QColor("#333"));
    p.drawText(QRectF(x - slot * 0.5, plot.bottom() + 2, slot, 14),
               Qt::AlignHCenter | Qt::AlignTop,
               (i < names_.size()) ? names_[i] : QString("?"));
    if (!ok) {
      p.setPen(QColor("#c0504d"));
      p.drawText(QRectF(x - slot * 0.5, plot.bottom() + 18, slot, 14),
                 Qt::AlignHCenter | Qt::AlignTop, "不能漂浮");
    }
  }

  // 参考线的含义写进标题，而不是压在线旁：柱子几乎顶满高度，
  // 线旁任何文字都会与柱身/纵轴刻度打架
}

// ═══════════════════════════════════════════════════════════════════════════
// DamageStabilityDialog
// ═══════════════════════════════════════════════════════════════════════════

DamageStabilityDialog::DamageStabilityDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("SD-07 破舱稳性仿真 —— 逐舱进水 / 残存 GM / 一舱不沉");
  resize(1240, 860);
  buildUi();
  refresh();
}

void DamageStabilityDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>让一个舱室破损进水，用<b>损失浮力法</b>求破舱后的平衡吃水、横倾、"
      "纵倾与残存初稳性高，并判定是否满足<b>一舱不沉</b>。<br>"
      "<b>上游依赖：</b>「导入 SD-04」读 <code>sd04_compartments.json</code> 的逐舱几何与渗透率"
      "（破舱计算真正用的就是这些舱），「导入 HS-08」读 <code>hs08_gz.json</code> 的 "
      "<code>metrics.gm0M</code> 作完整船基准对照。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  // === 输入 ===
  auto *paramBox = new QGroupBox("完整船输入（与 SD-04 的舱室表必须同一条船）", this);
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

  auto *formM = new QFormLayout();
  draftSpin_ = new QDoubleSpinBox(paramBox);
  draftSpin_->setRange(0.02, 12.0); draftSpin_->setValue(0.3);
  draftSpin_->setSingleStep(0.05); draftSpin_->setDecimals(3); draftSpin_->setSuffix(" m");
  formM->addRow("完整船吃水 T：", draftSpin_);

  kgSpin_ = new QDoubleSpinBox(paramBox);
  kgSpin_->setRange(0.0, 20.0); kgSpin_->setValue(0.5);
  kgSpin_->setSingleStep(0.05); kgSpin_->setDecimals(3); kgSpin_->setSuffix(" m");
  formM->addRow("重心高 KG：", kgSpin_);

  densitySpin_ = new QDoubleSpinBox(paramBox);
  densitySpin_->setRange(900.0, 1100.0); densitySpin_->setValue(1000.0);
  densitySpin_->setSingleStep(10.0); densitySpin_->setDecimals(1);
  densitySpin_->setSuffix(" kg/m³");
  formM->addRow("水密度 ρ：", densitySpin_);

  auto *formR = new QFormLayout();
  floodCombo_ = new QComboBox(paramBox);
  floodCombo_->addItem("全部单舱逐一试破");
  formR->addRow("破舱选择：", floodCombo_);

  paramLayout->addLayout(formL);
  paramLayout->addSpacing(18);
  paramLayout->addLayout(formM);
  paramLayout->addSpacing(18);
  paramLayout->addLayout(formR);

  auto *btnColumn = new QVBoxLayout();
  auto *importSd04Btn = new QPushButton("导入 SD-04 舱室表", paramBox);
  auto *importHs08Btn = new QPushButton("导入 HS-08（完整船 GM）", paramBox);
  auto *computeBtn = new QPushButton("开始破舱计算", paramBox);
  auto *exportBtn = new QPushButton("导出破舱结果（SD-07 交付物）", paramBox);
  btnColumn->addWidget(importSd04Btn);
  btnColumn->addWidget(importHs08Btn);
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

  // === 结果表 ===
  auto *tableBox = new QGroupBox("逐舱破舱结果（损失浮力法，最终平衡态）", this);
  auto *tableLayout = new QVBoxLayout(tableBox);
  resultTable_ = new QTableWidget(tableBox);
  resultTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  resultTable_->setColumnCount(9);
  resultTable_->setHorizontalHeaderLabels(
      QStringList() << "破舱" << "进水量 m³" << "新吃水 m" << "横倾 °" << "纵倾 °"
                    << "残存 GM m" << "能否漂浮" << "一舱不沉" << "说明");
  resultTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
  resultTable_->horizontalHeader()->setStretchLastSection(true);
  resultTable_->verticalHeader()->setVisible(false);
  resultTable_->setMinimumHeight(210);   // 至少能看到 5 行，避免"逐舱"结论只剩两三行
  tableLayout->addWidget(resultTable_);
  mainLayout->addWidget(tableBox, 3);

  // === 柱状图 ===
  auto *barBox = new QGroupBox("逐舱残存 GM 对照", this);
  auto *barLayout = new QVBoxLayout(barBox);
  barWidget_ = new DamageGmBarWidget(barBox);
  barLayout->addWidget(barWidget_);
  mainLayout->addWidget(barBox, 2);

  summaryLabel_ = new QLabel(this);
  summaryLabel_->setWordWrap(true);
  summaryLabel_->setTextFormat(Qt::RichText);
  mainLayout->addWidget(summaryLabel_);

  connect(importSd04Btn, &QPushButton::clicked, this, &DamageStabilityDialog::onImportSd04);
  connect(importHs08Btn, &QPushButton::clicked, this, &DamageStabilityDialog::onImportHs08);
  connect(computeBtn, &QPushButton::clicked, this, &DamageStabilityDialog::onCompute);
  connect(exportBtn, &QPushButton::clicked, this, &DamageStabilityDialog::onExport);
}

//-----------------------------------------------------------------------------
bool DamageStabilityDialog::refresh()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double T = draftSpin_->value();
  const double KG = kgSpin_->value();
  const double rho = densitySpin_->value();

  if (!(L > 1e-6) || !(B > 1e-6) || !(D > 1e-6) || !(T > 1e-6) || !(rho > 1.0)) {
    errMsg_ = "主尺度、吃水与水密度必须为正。";
    return false;
  }
  if (T > D) {
    errMsg_ = QString("完整船吃水 T = %1 m 大于型深 D = %2 m。").arg(Fmt(T), Fmt(D));
    return false;
  }

  // 未导入 SD-04 时给一套等分示范舱（仍走同一条损失浮力计算路径）
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

  if (comps_.empty()) {
    errMsg_ = "没有可用的舱室：请先导入 SD-04 舱室表。";
    return false;
  }

  keship::GzInput in;
  in.L = L;
  in.B = B;
  in.D = D;
  in.draft = T;
  in.KG = KG;
  in.rhoWater = rho;
  // 舱室来自 SD-04 的矩形分舱 ⇒ 船体也必须按矩形驳船算，否则舱室会越出半宽包络
  in.boxSection = true;

  intactGm0M_ = keship::Summarize(in).gm0M;

  // 破舱选择：0 = 全部逐舱；k = 第 k-1 个舱
  const int sel = floodCombo_->currentIndex();
  std::vector<int> targets;
  if (sel <= 0) {
    for (int i = 0; i < static_cast<int>(comps_.size()); ++i) targets.push_back(i);
  } else if (sel - 1 < static_cast<int>(comps_.size())) {
    targets.push_back(sel - 1);
  } else {
    errMsg_ = "破舱选择的舱室索引越界（舱室表已变更？）。";
    return false;
  }

  results_.clear();
  caseNames_.clear();
  for (int idx : targets) {
    const keship::DamageResult r = keship::FloodOne(in, comps_, idx);
    results_.push_back(r);
    caseNames_ << QString::fromStdString(comps_[static_cast<std::size_t>(idx)].name);
  }
  if (results_.empty()) {
    errMsg_ = "破舱计算没有得到任何结果。";
    return false;
  }

  // 汇总：最危险舱只在"还能浮起来"的工况里挑 —— 不能漂浮的工况残存 GM 被内核置 0，
  // 若把它们一起比较，"沉得最彻底"的舱会被误当成"稳性最差"的舱。
  nCanFloat_ = 0;
  oneCompartmentStandard_ = true;
  bool hasFloatCase = false;
  double worst = 1e18;
  QString worstName;
  for (std::size_t i = 0; i < results_.size(); ++i) {
    const auto &r = results_[i];
    if (r.canFloat) {
      ++nCanFloat_;
      hasFloatCase = true;
      if (r.gmResidual < worst) {
        worst = r.gmResidual;
        worstName = caseNames_[static_cast<int>(i)];
      }
    }
    if (!r.canFloat || !r.oneCompartmentStandard) oneCompartmentStandard_ = false;
  }
  if (hasFloatCase) {
    worstGmResidual_ = worst;
    worstCompartment_ = worstName;
  } else {
    worstGmResidual_ = 0.0;
    worstCompartment_ = caseNames_.first();
  }

  fillTable();
  updateSummary();
  updateCache();
  return true;
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::fillTable()
{
  resultTable_->setRowCount(static_cast<int>(results_.size()));
  for (int r = 0; r < static_cast<int>(results_.size()); ++r) {
    const auto &d = results_[static_cast<std::size_t>(r)];
    const QString texts[6] = {
        caseNames_[r],
        Fmt(d.floodedVolume, 4),
        Fmt(d.newDraft, 3),
        Fmt(d.heelDeg, 2),
        Fmt(d.trimDeg, 2),
        Fmt(d.gmResidual, 4)};
    for (int c = 0; c < 6; ++c) {
      auto *it = new QTableWidgetItem(texts[c]);
      it->setTextAlignment((c == 0) ? (Qt::AlignLeft | Qt::AlignVCenter)
                                    : (Qt::AlignRight | Qt::AlignVCenter));
      resultTable_->setItem(r, c, it);
    }
    auto *floatItem = new QTableWidgetItem(d.canFloat ? "是" : "否");
    floatItem->setTextAlignment(Qt::AlignCenter);
    floatItem->setForeground(QBrush(QColor(d.canFloat ? "#1a7f37" : "#c0392b")));
    resultTable_->setItem(r, 6, floatItem);

    auto *stdItem = new QTableWidgetItem(d.oneCompartmentStandard ? "满足" : "不满足");
    stdItem->setTextAlignment(Qt::AlignCenter);
    stdItem->setForeground(QBrush(QColor(d.oneCompartmentStandard ? "#1a7f37" : "#c0392b")));
    resultTable_->setItem(r, 7, stdItem);

    auto *noteItem = new QTableWidgetItem(QString::fromStdString(d.note));
    noteItem->setToolTip(QString::fromStdString(d.note));
    resultTable_->setItem(r, 8, noteItem);
  }
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::updateSummary()
{
  std::vector<double> gms;
  std::vector<bool> canFloat;
  for (const auto &r : results_) {
    gms.push_back(r.gmResidual);
    canFloat.push_back(r.canFloat);
  }
  barWidget_->setData(caseNames_, gms, canFloat);

  QString up;
  if (sd04Imported_ || hs08Imported_) {
    QStringList parts;
    if (sd04Imported_) {
      double mine = 0.0;
      for (const auto &c : comps_) {
        mine += (c.x2 - c.x1) * (c.y2 - c.y1) * (c.z2 - c.z1);
      }
      const double rel = (sd04TotalVolume_ > 1e-9)
                             ? std::abs(mine - sd04TotalVolume_) / sd04TotalVolume_ : 0.0;
      parts << QString("SD-04 交付：<b>%1</b> 舱 / 总舱容 <b>%2</b> m³　→　本页复现："
                       "<b>%3</b> 舱 / Σ舱室体积 <b>%4</b> m³（相对偏差 <b>%5%</b>）")
                   .arg(sd04NCompartments_)
                   .arg(Fmt(sd04TotalVolume_, 4))
                   .arg(comps_.size())
                   .arg(Fmt(mine, 4))
                   .arg(Fmt(rel * 100.0, 4));
    }
    if (hs08Imported_) {
      const double diff = intactGm0M_ - hs08Gm0_;
      parts << QString("HS-08 交付 GM0 = <b>%1</b> m　→　本页完整船 GM0 = <b>%2</b> m"
                       "（差 <b>%3</b> m；口径分别为「GZ 曲线 dGZ/dφ」与「本页同一内核复算」，"
                       "差异只应来自 KG/吃水/船型是否被改动）")
                   .arg(Fmt(hs08Gm0_, 4)).arg(Fmt(intactGm0M_, 4)).arg(Fmt(diff, 4));
    }
    up = parts.join("<br>");
    upstreamLabel_->setStyleSheet("color:#338833;");
  } else {
    up = "未导入上游交付物：舱室使用本页内置的等分示范舱，完整船 GM 无从对照。"
         "（正式实验请先导入 SD-04，再导入 HS-08）";
    upstreamLabel_->setStyleSheet("color:#555;");
  }
  upstreamLabel_->setText(up);

  summaryLabel_->setText(
      QString("完整船 GM0 = <b>%1</b> m　｜　试破工况 <b>%2</b> 个　｜　"
              "能继续漂浮 <b>%3</b> 个　｜　最危险舱 <b>%4</b>（残存 GM = <b>%5</b> m）<br>"
              "<b style='color:%6'>一舱不沉：%7</b>"
              "<span style='color:#555'>　（判据：单舱破损后仍能漂浮且残存 GM &gt; 0；"
              "本内核把「最终水线淹没甲板边缘」直接判为不能漂浮 —— 甲板一旦入水，"
              "储备浮力耗尽，线性 GZ 模型不再成立）</span>")
          .arg(Fmt(intactGm0M_, 4))
          .arg(results_.size())
          .arg(nCanFloat_)
          .arg(worstCompartment_)
          .arg(Fmt(worstGmResidual_, 4))
          .arg(oneCompartmentStandard_ ? "#1a7f37" : "#b00")
          .arg(oneCompartmentStandard_ ? "满足" : "不满足"));
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::updateCache()
{
  lastObject_ = QJsonObject();
  lastObject_["type"] = "damage_stability";
  lastObject_["name"] = "SD-07 破舱稳性";
  lastObject_["lengthM"] = lengthSpin_->value();
  lastObject_["breadthM"] = breadthSpin_->value();
  lastObject_["depthM"] = depthSpin_->value();
  lastObject_["hullSectionModel"] = "box_rectangular";

  lastCondition_ = QJsonObject();
  lastCondition_["draftM"] = draftSpin_->value();
  lastCondition_["kgM"] = kgSpin_->value();
  lastCondition_["waterDensity"] = densitySpin_->value();
  lastCondition_["gravity"] = kGravity;

  lastParams_ = QJsonObject();
  lastParams_["method"] = "lost_buoyancy";
  lastParams_["nCompartments"] = static_cast<int>(comps_.size());
  lastParams_["nCases"] = static_cast<int>(results_.size());
  lastParams_["caseMode"] = (floodCombo_->currentIndex() <= 0) ? "all_single" : "specified";
  lastParams_["gmThresholdM"] = kGmThreshold;

  lastMetrics_ = QJsonObject();
  lastMetrics_["worstCompartment"] = worstCompartment_;
  lastMetrics_["worstGmResidual"] = worstGmResidual_;
  lastMetrics_["nCanFloat"] = nCanFloat_;
  lastMetrics_["oneCompartmentStandard"] = oneCompartmentStandard_;

  QJsonArray cases;
  for (std::size_t i = 0; i < results_.size(); ++i) {
    const auto &r = results_[i];
    QJsonObject o;
    o["name"] = caseNames_[static_cast<int>(i)];
    o["floodedVolumeM3"] = r.floodedVolume;
    o["newDraftM"] = r.newDraft;
    o["heelDeg"] = r.heelDeg;
    o["trimDeg"] = r.trimDeg;
    o["gmResidualM"] = r.gmResidual;
    o["gzMaxResidualM"] = r.gzMaxResidual;
    o["canFloat"] = r.canFloat;
    o["oneCompartmentStandard"] = r.oneCompartmentStandard;
    o["note"] = QString::fromStdString(r.note);
    cases.append(o);
  }

  lastExtra_ = QJsonObject();
  lastExtra_["cases"] = cases;
  lastExtra_["intactGm0M"] = intactGm0M_;
  lastExtra_["compartmentCount"] = static_cast<int>(comps_.size());
  lastExtra_["compartmentSource"] = sd04Imported_ ? "SD-04" : "builtin_demo";

  QJsonObject up;
  up["sd04Imported"] = sd04Imported_;
  up["sd04TotalVolumeM3"] = sd04TotalVolume_;
  up["sd04NCompartments"] = sd04NCompartments_;
  up["hs08Imported"] = hs08Imported_;
  up["hs08Gm0M"] = hs08Gm0_;
  lastExtra_["upstream"] = up;
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::onImportSd04()
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
bool DamageStabilityDialog::importSd04(const QString &_path)
{
  keship::ExpDeliverable d;
  std::string err;
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "SD-04", &err)) {
    errMsg_ = QString::fromStdString(err);
    return false;
  }

  const QJsonArray arr = d.extra["compartments"].toArray();
  if (arr.isEmpty()) {
    errMsg_ = "文件里没有 extra.compartments —— 不是 SD-04 导出的舱室表。";
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
    c.permeability = o["permeability"].toDouble(0.95);   // 渗透率直接进损失浮力计算
    parsed.push_back(c);
  }
  if (parsed.empty()) {
    errMsg_ = "解析后没有任何有效舱室。";
    return false;
  }

  comps_ = parsed;
  sd04Imported_ = true;
  sd04TotalVolume_ = d.metrics["totalVolumeM3"].toDouble();
  sd04NCompartments_ = d.metrics["nCompartments"].toInt(static_cast<int>(parsed.size()));

  {
    QSignalBlocker b1(lengthSpin_);
    QSignalBlocker b2(breadthSpin_);
    QSignalBlocker b3(depthSpin_);
    lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
    breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));
    depthSpin_->setValue(d.object["depthM"].toDouble(depthSpin_->value()));
  }

  // 破舱下拉按新舱室表重建（否则索引还指向旧表，会破错舱）
  {
    QSignalBlocker blocker(floodCombo_);
    floodCombo_->clear();
    floodCombo_->addItem("全部单舱逐一试破");
    for (const auto &c : comps_) {
      floodCombo_->addItem(QString("仅破：%1").arg(QString::fromStdString(c.name)));
    }
  }

  if (!refresh()) return false;
  std::cout << "SD-07: 已导入 SD-04，" << comps_.size() << " 个舱室" << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::onImportHs08()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 HS-08 GZ 结果（取完整船 GM）", "hs08_gz.json", "JSON (*.json)");
  if (path.isEmpty()) return;
  if (!importHs08(path)) {
    QMessageBox::warning(this, "导入 HS-08",
        QString::fromStdString("无法使用该文件：\n" + errMsg_.toStdString()));
  }
}

//-----------------------------------------------------------------------------
bool DamageStabilityDialog::importHs08(const QString &_path)
{
  keship::ExpDeliverable d;
  std::string err;
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "HS-08", &err)) {
    errMsg_ = QString::fromStdString(err);
    return false;
  }
  if (!d.metrics.contains("gm0M")) {
    errMsg_ = "该文件的 metrics 里没有 gm0M。";
    return false;
  }

  hs08Gm0_ = d.metrics["gm0M"].toDouble();
  hs08Imported_ = true;

  {
    QSignalBlocker b1(kgSpin_);
    QSignalBlocker b2(lengthSpin_);
    QSignalBlocker b3(breadthSpin_);
    QSignalBlocker b4(depthSpin_);
    QSignalBlocker b5(draftSpin_);
    kgSpin_->setValue(d.condition["kgM"].toDouble(kgSpin_->value()));
    lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
    breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));
    depthSpin_->setValue(d.object["depthM"].toDouble(depthSpin_->value()));
    draftSpin_->setValue(d.condition["draftM"].toDouble(draftSpin_->value()));
  }

  if (!refresh()) return false;
  std::cout << "SD-07: 已导入 HS-08，完整船 GM0=" << hs08Gm0_ << " m" << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::onCompute()
{
  if (!refresh()) {
    QMessageBox::warning(this, "破舱稳性", errMsg_);
    return;
  }
  std::cout << "SD-07: " << results_.size() << " 个破舱工况，最危险舱 "
            << worstCompartment_.toStdString() << "，残存 GM " << worstGmResidual_
            << " m，一舱不沉=" << (oneCompartmentStandard_ ? "true" : "false") << std::endl;
}

//-----------------------------------------------------------------------------
bool DamageStabilityDialog::exportTo(const QString &_path)
{
  if (_path.isEmpty()) return false;
  if (!refresh()) return false;

  keship::ExpDeliverable d;
  d.expId = "SD-07";
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.condition = lastCondition_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cerr << "SD-07: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << "SD-07: 已导出交付物 " << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void DamageStabilityDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-07 破舱结果", "sd07_damage.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出破舱结果",
        QString("导出失败：%1\n请检查输入（%2）").arg(path).arg(errMsg_));
    return;
  }
  QMessageBox::information(this, "导出破舱结果",
      QString("已导出：%1\n\n试破 %2 个舱室，最危险舱 %3（残存 GM = %4 m）。\n"
              "一舱不沉：%5")
          .arg(path).arg(results_.size()).arg(worstCompartment_)
          .arg(Fmt(worstGmResidual_, 4))
          .arg(oneCompartmentStandard_ ? "满足" : "不满足"));
}
