/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-09 综合结构设计 —— 实现 */

#include "LongitudinalStrengthDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

//=============================================================================
// StrengthStressWidget
//=============================================================================
StrengthStressWidget::StrengthStressWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumSize(360, 260);
}

void StrengthStressWidget::setData(double _deckMpa, double _keelMpa, double _allowMpa,
                                   bool _valid)
{
  deck_ = _deckMpa;
  keel_ = _keelMpa;
  allow_ = _allowMpa;
  valid_ = _valid;
  update();
}

void StrengthStressWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  if (!valid_) {
    p.setPen(QColor("#888"));
    p.drawText(rect(), Qt::AlignCenter, "尚未导入 SD-03 剖面模数（W），无法计算应力");
    return;
  }

  const QRectF plot(56.0, 30.0, std::max(80.0, width() - 88.0),
                    std::max(80.0, height() - 78.0));
  p.setPen(QPen(QColor("#d0d0d0"), 1.0));
  p.drawRect(plot);

  const double vmax = std::max({deck_, keel_, allow_, 1e-9}) * 1.15;
  auto yOf = [&](double _v) { return plot.bottom() - (_v / vmax) * plot.height(); };

  // 许用应力参考线（数值由第三根柱子标出，这里不再重复写字，避免与柱顶数值压字）
  p.setPen(QPen(QColor("#2f7d4f"), 1.4, Qt::DashLine));
  p.drawLine(QPointF(plot.left(), yOf(allow_)), QPointF(plot.right(), yOf(allow_)));

  struct Bar {
    double v;
    QString label;
    QColor color;
  };
  const Bar bars[3] = {{deck_, "甲板 σ", QColor("#1f5fa8")},
                       {keel_, "船底 σ", QColor("#7a4b3a")},
                       {allow_, "许用 [σ]", QColor("#9bbf9e")}};

  const double slot = plot.width() / 3.0;
  const double bw = slot * 0.42;
  QFont f = p.font();
  f.setPointSizeF(std::max(7.5, f.pointSizeF() - 1.0));
  for (int i = 0; i < 3; ++i) {
    const double cx = plot.left() + slot * (static_cast<double>(i) + 0.5);
    const QRectF r(cx - bw / 2.0, yOf(bars[i].v), bw, plot.bottom() - yOf(bars[i].v));
    p.fillRect(r, bars[i].color);
    p.setPen(QPen(bars[i].color.darker(135), 1.0));
    p.drawRect(r);

    p.setFont(f);
    p.setPen(QColor("#333"));
    p.drawText(QRectF(cx - slot / 2.0, plot.bottom() + 4, slot, 20), Qt::AlignHCenter,
               bars[i].label);
    p.drawText(QRectF(cx - slot / 2.0, std::max(plot.top() - 2.0, yOf(bars[i].v) - 18.0),
                      slot, 18.0),
               Qt::AlignHCenter, QString("%1 MPa").arg(bars[i].v, 0, 'f', 3));
  }

  p.setPen(QColor("#888"));
  p.drawText(QPointF(6.0, plot.top() + 4), QString("σ (MPa)"));
  p.setPen(QColor("#666"));
  p.drawText(QRectF(plot.left() + 6, plot.top() + 6, plot.width() - 12, 18), Qt::AlignRight,
             QString("利用率 = σ_max/[σ] = %1 %")
                 .arg(allow_ > 0.0 ? std::max(deck_, keel_) / allow_ * 100.0 : 0.0, 0, 'f', 2));
}

//=============================================================================
// LongitudinalStrengthDialog
//=============================================================================
LongitudinalStrengthDialog::LongitudinalStrengthDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("SD-09 综合结构设计 —— 总纵强度校核 / 剖面模数驱动");
  resize(1180, 780);
  buildUi();
  refresh();
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::buildUi()
{
  auto *main = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>用 SD-03 算出的剖面模数 <i>W</i> 做总纵强度校核："
      "<i>σ = M/W</i>，把「弯矩」与「剖面」两件事连起来。<br>"
      "<b>链：</b>「导入 SD-03 剖面模数」是必经一步——本页不会自己编一个 W；"
      "导入后还会用 SD-03 交付物里的构件几何独立复算一遍做对账。",
      this);
  intro->setWordWrap(true);
  main->addWidget(intro);

  // === 导入 + 输入 ===
  auto *topBox = new QGroupBox("上游交付物 / 载荷与许用应力", this);
  auto *topLayout = new QHBoxLayout(topBox);

  auto *leftCol = new QVBoxLayout();
  auto *importBtn = new QPushButton("导入 SD-03 剖面模数（sd03_midship.json）", topBox);
  leftCol->addWidget(importBtn);
  importLabel_ = new QLabel("尚未导入 SD-03：没有剖面模数就无法校核。", topBox);
  importLabel_->setWordWrap(true);
  importLabel_->setStyleSheet("color:#555;");
  leftCol->addWidget(importLabel_, 1);
  topLayout->addLayout(leftCol, 3);

  auto *form = new QFormLayout();
  lengthSpin_ = new QDoubleSpinBox(topBox);
  lengthSpin_->setRange(2.0, 300.0);
  lengthSpin_->setValue(6.0);
  lengthSpin_->setDecimals(3);
  lengthSpin_->setSuffix(" m");
  form->addRow("船长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(topBox);
  breadthSpin_->setRange(0.5, 60.0);
  breadthSpin_->setValue(1.5);
  breadthSpin_->setDecimals(3);
  breadthSpin_->setSuffix(" m");
  form->addRow("型宽 B：", breadthSpin_);

  cbSpin_ = new QDoubleSpinBox(topBox);
  cbSpin_->setRange(0.20, 0.95);
  cbSpin_->setValue(0.60);
  cbSpin_->setSingleStep(0.01);
  cbSpin_->setDecimals(3);
  form->addRow("方形系数 Cb：", cbSpin_);
  topLayout->addLayout(form, 2);

  auto *form2 = new QFormLayout();
  swMomentSpin_ = new QDoubleSpinBox(topBox);
  swMomentSpin_->setRange(-1.0e6, 1.0e6);
  swMomentSpin_->setValue(60.0);
  swMomentSpin_->setSingleStep(10.0);
  swMomentSpin_->setDecimals(3);
  swMomentSpin_->setSuffix(" kN·m");
  form2->addRow("静水弯矩 M_sw：", swMomentSpin_);

  waveMomentSpin_ = new QDoubleSpinBox(topBox);
  waveMomentSpin_->setRange(0.0, 1.0e6);
  waveMomentSpin_->setValue(0.0);
  waveMomentSpin_->setSingleStep(1.0);
  waveMomentSpin_->setDecimals(3);
  waveMomentSpin_->setSuffix(" kN·m");
  form2->addRow("波浪弯矩 M_w：", waveMomentSpin_);

  allowableSpin_ = new QDoubleSpinBox(topBox);
  allowableSpin_->setRange(1.0, 1000.0);
  allowableSpin_->setValue(175.0);
  allowableSpin_->setSingleStep(5.0);
  allowableSpin_->setDecimals(1);
  allowableSpin_->setSuffix(" MPa");
  form2->addRow("许用应力 [σ]：", allowableSpin_);
  topLayout->addLayout(form2, 2);

  auto *btnCol = new QVBoxLayout();
  auto *estBtn = new QPushButton("按简化式估算 M_w", topBox);
  auto *demoBtn = new QPushButton("示例载荷（利用率≈60%）", topBox);
  auto *calcBtn = new QPushButton("计算 / 刷新", topBox);
  auto *exportBtn = new QPushButton("导出强度校核（SD-09 交付物）", topBox);
  btnCol->addWidget(estBtn);
  btnCol->addWidget(demoBtn);
  btnCol->addWidget(calcBtn);
  btnCol->addWidget(exportBtn);
  btnCol->addStretch();
  topLayout->addLayout(btnCol);

  main->addWidget(topBox);

  // === 结果 + 图 ===
  auto *bottom = new QHBoxLayout();

  auto *resBox = new QGroupBox("校核结果", this);
  auto *resLayout = new QVBoxLayout(resBox);
  resultLabel_ = new QLabel(resBox);
  resultLabel_->setTextFormat(Qt::RichText);
  resultLabel_->setWordWrap(true);
  resultLabel_->setAlignment(Qt::AlignTop);
  resLayout->addWidget(resultLabel_, 1);
  bottom->addWidget(resBox, 3);

  auto *chartBox = new QGroupBox("应力柱状图（甲板 / 船底 vs 许用）", this);
  auto *chartLayout = new QVBoxLayout(chartBox);
  chart_ = new StrengthStressWidget(chartBox);
  chartLayout->addWidget(chart_);
  bottom->addWidget(chartBox, 2);

  main->addLayout(bottom, 1);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  statusLabel_->setWordWrap(true);
  main->addWidget(statusLabel_);

  connect(importBtn, &QPushButton::clicked, this,
          &LongitudinalStrengthDialog::onImportSd03);
  connect(estBtn, &QPushButton::clicked, this,
          &LongitudinalStrengthDialog::onEstimateWave);
  connect(demoBtn, &QPushButton::clicked, this, &LongitudinalStrengthDialog::onDemoLoad);
  connect(calcBtn, &QPushButton::clicked, this, &LongitudinalStrengthDialog::onCompute);
  connect(exportBtn, &QPushButton::clicked, this, &LongitudinalStrengthDialog::onExport);
  for (QDoubleSpinBox *s : {lengthSpin_, breadthSpin_, cbSpin_, swMomentSpin_,
                            waveMomentSpin_, allowableSpin_})
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { refresh(); });
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::onImportSd03()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 SD-03 剖面模数交付物", "sd03_midship.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!importFrom(path, &err)) {
    QMessageBox::warning(this, "导入 SD-03 剖面模数",
                         QString::fromStdString("无法使用该文件：\n" + err));
    return;
  }
  std::cout << "SD-09: 已导入 SD-03 交付物 " << path.toStdString() << "（W_deck 交付 "
            << wDeckUp_ << "，复算 " << recomputedWDeck_ << "）" << std::endl;
}

//-----------------------------------------------------------------------------
bool LongitudinalStrengthDialog::importFrom(const QString &_path, std::string *_err)
{
  keship::ExpDeliverable d;
  std::string err;
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "SD-03", &err)) {
    if (_err) *_err = err;
    return false;
  }

  const double wDeck = d.metrics["sectionModulusDeckM3"].toDouble();
  const double wKeel = d.metrics["sectionModulusKeelM3"].toDouble();
  if (wDeck <= 0.0 || wKeel <= 0.0) {
    if (_err)
      *_err = "该 SD-03 交付物缺少有效的 sectionModulusDeckM3 / sectionModulusKeelM3";
    return false;
  }

  wDeckUp_ = wDeck;
  wKeelUp_ = wKeel;

  lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
  breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));

  // 用 SD-03 交付的构件几何独立复算 W（不是"信"上游的一个数字，而是核对它）
  keship::MidshipInput in;
  in.breadthM = d.object["breadthM"].toDouble();
  in.depthM = d.object["depthM"].toDouble();
  depthUp_ = in.depthM;
  for (const auto &v : d.extra["plates"].toArray()) {
    const QJsonObject o = v.toObject();
    keship::PlateSegment ps;
    ps.name = o["name"].toString().toStdString();
    ps.widthM = o["widthM"].toDouble();
    ps.thicknessM = o["thicknessM"].toDouble();
    ps.zCenterM = o["zCenterM"].toDouble();
    in.plates.push_back(ps);
  }
  for (const auto &v : d.extra["stiffeners"].toArray()) {
    const QJsonObject o = v.toObject();
    keship::Stiffener st;
    st.name = o["name"].toString().toStdString();
    st.webHeightM = o["webHeightM"].toDouble();
    st.webThicknessM = o["webThicknessM"].toDouble();
    st.spacingM = o["spacingM"].toDouble();
    st.zBaseM = o["zBaseM"].toDouble();
    st.count = o["count"].toInt();
    in.stiffeners.push_back(st);
  }
  recomputedWDeck_ = 0.0;
  recomputedWKeel_ = 0.0;
  if (!in.plates.empty() || !in.stiffeners.empty()) {
    const auto r = keship::ComputeMidship(in);
    if (r.valid) {
      recomputedWDeck_ = r.sectionModulusDeckM3;
      recomputedWKeel_ = r.sectionModulusKeelM3;
    }
  }

  imported_ = true;
  onEstimateWave();  ///< 顺手按简化式填一次 M_w（学生仍可手改）
  refresh();
  return true;
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::onEstimateWave()
{
  const double mw = keship::EstimateWaveMomentKNm(
      lengthSpin_->value(), breadthSpin_->value(), cbSpin_->value(), 1.0);
  waveMomentSpin_->setValue(mw);  ///< 触发 valueChanged → refresh
  refresh();
  std::cout << "SD-09: 波浪弯矩按简化式估算 M_w = " << mw << " kN·m" << std::endl;
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::refresh()
{
  in_.sectionModulusDeckM3 = wDeckUp_;
  in_.sectionModulusKeelM3 = wKeelUp_;
  in_.stillWaterMomentKNm = swMomentSpin_->value();
  in_.waveMomentKNm = waveMomentSpin_->value();
  in_.allowableStressMpa = allowableSpin_->value();

  res_ = keship::CheckLongitudinalStrength(in_);

  const bool hasW = (wDeckUp_ > 0.0 && wKeelUp_ > 0.0);
  chart_->setData(res_.deckStressMpa, res_.keelStressMpa, in_.allowableStressMpa, hasW);

  if (!hasW) {
    resultLabel_->setText(
        "<span style='color:#b00;'>尚未导入 SD-03 剖面模数。</span><br>"
        "总纵弯曲应力 σ = M/W 缺少分母 W —— 请点左上「导入 SD-03 剖面模数」。");
    statusLabel_->setText("提示：SD-01 → SD-03 → SD-09 必须逐级导入交付物，手抄的 W 不算贯通。");
    statusLabel_->setStyleSheet("color:#b00;");
  } else {
    const QString color = res_.passed ? "#338833" : "#b00";
    resultLabel_->setText(
        QString(
            "总弯矩 M = M_sw + M_w = <b>%1</b> kN·m<br>"
            "甲板应力 σ_deck = M/W_deck = <b>%2</b> MPa<br>"
            "船底应力 σ_keel = M/W_keel = <b>%3</b> MPa<br>"
            "利用率 = σ_max / [σ] = <b>%4</b>%%<br>"
            "<span style='color:%5;font-weight:bold;'>%6</span>"
            "<br><small>按许用应力反算的可承受弯矩：M_allow = [σ]·W_deck = %7 kN·m</small>")
            .arg(res_.totalMomentKNm, 0, 'f', 3)
            .arg(res_.deckStressMpa, 0, 'f', 4)
            .arg(res_.keelStressMpa, 0, 'f', 4)
            .arg(res_.utilization * 100.0, 0, 'f', 3)
            .arg(color)
            .arg(res_.passed ? "校核通过（利用率 ≤ 100%）" : "校核不通过（利用率 > 100%）")
            .arg(in_.allowableStressMpa * wDeckUp_ * 1000.0, 0, 'f', 1));

    const double dev = (recomputedWDeck_ > 0.0)
                           ? std::abs(recomputedWDeck_ - wDeckUp_) / wDeckUp_ : -1.0;
    importLabel_->setText(
        QString("已导入 SD-03：W_deck = <b>%1</b> m³，W_keel = <b>%2</b> m³\n"
                "本页由 extra.plates/stiffeners 复算：W_deck = %3 m³（相对偏差 %4）")
            .arg(wDeckUp_, 0, 'f', 6)
            .arg(wKeelUp_, 0, 'f', 6)
            .arg(recomputedWDeck_ > 0.0 ? QString::number(recomputedWDeck_, 'f', 6)
                                        : QString("—"))
            .arg(dev < 0.0 ? QString("（交付物无构件几何，无法复算）")
                           : QString::number(dev * 100.0, 'f', 3) + " %"));
    importLabel_->setStyleSheet(dev >= 0.0 && dev > 0.02 ? "color:#b00;" : "color:#338833;");

    statusLabel_->setText(
        QString("波浪弯矩按 M_w = 0.19·C·L²·B·Cb 估算（C=1.0，教学简化值，非规范值）；"
                "本页 C = %1，L = %2 m，B = %3 m，Cb = %4。")
            .arg(1.0, 0, 'f', 1)
            .arg(lengthSpin_->value(), 0, 'f', 3)
            .arg(breadthSpin_->value(), 0, 'f', 3)
            .arg(cbSpin_->value(), 0, 'f', 3));
    statusLabel_->setStyleSheet("color:#555;");
  }
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::onDemoLoad()
{
  /// 教学演示载荷：把静水弯矩设成「许用弯矩的 60% − 当前波浪弯矩」。
  /// 为什么需要这个按钮：教学船（6 m）板厚是模型尺度、剖面模数相对载荷很大，
  /// 默认载荷下利用率只有百分之一二，柱状图上看不出"校核"这件事。
  /// 本按钮显式地把工况推到 60% 附近，**并在状态栏写明这是教学演示载荷**，
  /// 不伪装成规范算例。
  const double wMin = std::min(wDeckUp_, wKeelUp_);
  if (!(wMin > 0.0)) {
    QMessageBox::information(this, "示例载荷",
                             "请先导入 SD-03 剖面模数，否则算不出许用弯矩。");
    return;
  }
  const double mAllow = in_.allowableStressMpa * wMin * 1000.0;  ///< kN·m
  swMomentSpin_->setValue(std::max(0.0, 0.6 * mAllow - waveMomentSpin_->value()));
  refresh();
  statusLabel_->setText(
      QString("已载入「教学演示载荷」：静水弯矩按许用弯矩 M_allow = %1 kN·m 的 60% 反设"
              "（不是规范算例，仅为把利用率推到可观察的区间）。")
          .arg(mAllow, 0, 'f', 1));
  std::cout << "SD-09: 已载入教学演示载荷（M_allow=" << mAllow << " kN·m）" << std::endl;
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::onCompute()
{
  refresh();
  std::cout << "SD-09: 已刷新校核（利用率 " << res_.utilization << "）" << std::endl;
}

//-----------------------------------------------------------------------------
bool LongitudinalStrengthDialog::exportTo(const QString &_path, std::string *_err)
{
  if (!imported_ || wDeckUp_ <= 0.0) {
    if (_err) *_err = "尚未导入 SD-03 剖面模数，无法导出（判据 B：必须消费上游交付物）";
    return false;
  }
  refresh();

  keship::ExpDeliverable d;
  d.expId = "SD-09";
  d.createdAt = keship::NowIso8601();

  d.object["type"] = "longitudinal_strength";
  d.object["lengthM"] = lengthSpin_->value();
  d.object["breadthM"] = breadthSpin_->value();
  d.object["depthM"] = depthUp_;  ///< 来自 SD-03（不重算，原样传递）
  d.object["sourceExpId"] = "SD-03";
  d.object["sectionModulusDeckM3"] = wDeckUp_;
  d.object["sectionModulusKeelM3"] = wKeelUp_;

  d.params["formula"] = QString::fromStdString(keship::WaveMomentFormulaText());
  d.params["cb"] = cbSpin_->value();
  d.params["waveCoefficientC"] = 1.0;
  d.params["stressFormula"] = "sigma[MPa] = M_total[kN*m]*1000 / W[m^3] / 1e6";
  d.params["allowableNote"] =
      "allowable stress 175 MPa is a TEACHING value (typical mild-steel ship "
      "permissible bending stress order), not a rule-derived limit";
  d.params["sectionModulusSource"] = "sd03_midship.json metrics";

  d.condition["seaState"] = "wave bending per user input / simplified estimate";

  d.metrics["stillWaterMomentKNm"] = in_.stillWaterMomentKNm;
  d.metrics["waveMomentKNm"] = in_.waveMomentKNm;
  d.metrics["totalMomentKNm"] = res_.totalMomentKNm;
  d.metrics["deckStressMpa"] = res_.deckStressMpa;
  d.metrics["keelStressMpa"] = res_.keelStressMpa;
  d.metrics["allowableMpa"] = in_.allowableStressMpa;
  d.metrics["utilization"] = res_.utilization;
  d.metrics["passed"] = res_.passed;

  return keship::WriteExpDeliverable(_path.toStdString(), d, _err);
}

//-----------------------------------------------------------------------------
void LongitudinalStrengthDialog::onExport()
{
  if (!imported_ || wDeckUp_ <= 0.0) {
    QMessageBox::warning(this, "导出强度校核",
                         "请先「导入 SD-03 剖面模数」——本实验的 W 必须来自上游交付物。");
    return;
  }

  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-09 强度校核交付物", "sd09_strength.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!exportTo(path, &err)) {
    QMessageBox::warning(this, "导出强度校核", QString::fromStdString("导出失败：" + err));
    return;
  }
  QMessageBox::information(
      this, "导出强度校核",
      QString("已导出：%1\n\n利用率 %2%%，%3")
          .arg(path)
          .arg(res_.utilization * 100.0, 0, 'f', 3)
          .arg(res_.passed ? "校核通过。" : "校核不通过，需加厚板或加大剖面模数。"));
  std::cout << "SD-09: 已导出交付物 " << path.toStdString() << std::endl;
}
