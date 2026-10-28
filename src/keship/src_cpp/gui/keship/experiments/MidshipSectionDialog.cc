/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-03 横剖面结构 —— 实现 */

#include "MidshipSectionDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

/// 默认结构要素表（未导入 SD-01 时的演示截面）
/// 与 SD-01 的默认表**同值**：这样"没导入也能用"，且导入后数值不变，学生一眼能看懂
/// "导入"这一步到底改变了什么（只改变来源，不改变几何）。
std::vector<keship::StructureMember> DefaultMembers()
{
  return {
      {"船底板（外板）", "bottom", 0.012, 0.25, 0.080, 0.006},
      {"甲板", "deck", 0.010, 0.25, 0.075, 0.006},
      {"舷侧板", "side", 0.010, 0.25, 0.075, 0.006},
      {"横舱壁", "bulkhead", 0.008, 0.30, 0.070, 0.006},
      {"纵桁", "girder", 0.009, 0.000, 0.000, 0.000},
      {"内底纵骨（骨材）", "stiffener", 0.000, 0.25, 0.075, 0.006},
      {"双层底（内底板）", "doublebottom", 0.010, 0.25, 0.000, 0.000},
  };
}

}  // namespace

//=============================================================================
// MidshipSectionWidget
//=============================================================================
MidshipSectionWidget::MidshipSectionWidget(QWidget *_parent) : QWidget(_parent)
{
  /// 最小尺寸刻意压小：本控件与「结果标签」同处一个竖排布局，
  /// 若最小高度要得太大（曾经 340），窗口一小就会被 Qt 允许"重叠摆放"，
  /// 结果标签会压到图的底注上 —— 那种重叠在截图前几乎发现不了。
  setMinimumSize(320, 240);
}

void MidshipSectionWidget::setSection(const keship::MidshipInput &_in,
                                      const keship::MidshipResult &_res)
{
  in_ = _in;
  res_ = _res;
  update();
}

void MidshipSectionWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  const double B = in_.breadthM;
  const double D = in_.depthM;
  if (B <= 1e-6 || D <= 1e-6) {
    p.setPen(QColor("#888"));
    p.drawText(rect(), Qt::AlignCenter, "截面无效（型宽/型深为 0）");
    return;
  }

  const double mLeft = 40.0, mRight = 24.0, mTop = 18.0, mBottom = 56.0;
  const double s = std::min((width() - mLeft - mRight) / B, (height() - mTop - mBottom) / D);
  if (s <= 0.0) return;
  const double cx = mLeft + (width() - mLeft - mRight) / 2.0;
  const double baseY = height() - mBottom;
  auto px = [&](double _x) { return cx + _x * s; };
  auto py = [&](double _z) { return baseY - _z * s; };

  // 基线
  p.setPen(QPen(QColor("#c8c8c8"), 1.0, Qt::DashLine));
  p.drawLine(QPointF(mLeft - 26, baseY), QPointF(width() - 14, baseY));

  const QColor plateColor("#4a6c8f");
  const QColor webColor("#7a94ad");
  const QColor stiffColor("#b5603a");

  for (const auto &pl : in_.plates) {
    const double z0 = pl.zCenterM - 0.5 * pl.thicknessM;
    const double h = std::max(2.0, pl.thicknessM * s);
    QRectF r(px(-B / 2.0), py(z0 + pl.thicknessM), B * s, h);
    p.fillRect(r, plateColor);
    p.setPen(QPen(plateColor.darker(130), 1.0));
    p.drawRect(r);
  }

  for (const auto &st : in_.stiffeners) {
    const bool isWeb = (std::abs(st.webHeightM - D) < 1e-9) && st.spacingM <= 0.0;
    const double z0 = st.zBaseM;
    const double z1 = st.zBaseM + st.webHeightM;
    if (isWeb) {
      const double w = std::max(2.0, st.webThicknessM * s);
      if (st.count >= 2) {
        p.fillRect(QRectF(px(-B / 2.0) - w, py(z1), w, (z1 - z0) * s), webColor);
        p.fillRect(QRectF(px(B / 2.0), py(z1), w, (z1 - z0) * s), webColor);
      } else {
        p.fillRect(QRectF(px(0.0) - w / 2.0, py(z1), w, (z1 - z0) * s), webColor);
      }
    } else {
      const int n = std::max(1, st.count);
      const double w = std::max(1.5, std::min(4.0, st.webThicknessM * s * 1.6));
      for (int k = 0; k < n; ++k) {
        const double x =
            -B / 2.0 + (static_cast<double>(k) + 0.5) * B / static_cast<double>(n);
        p.fillRect(QRectF(px(x) - w / 2.0, py(z1), w, (z1 - z0) * s), stiffColor);
      }
    }
  }

  // 中和轴（本实验的核心标注）
  if (res_.valid) {
    const double zNa = res_.neutralAxisZM;
    p.setPen(QPen(QColor("#c0392b"), 1.6, Qt::DashDotLine));
    p.drawLine(QPointF(mLeft - 18, py(zNa)), QPointF(width() - 12, py(zNa)));

    QFont f = p.font();
    f.setPointSizeF(std::max(7.5, f.pointSizeF() - 1.0));
    f.setBold(true);
    p.setFont(f);

    const QString naText = QString("中和轴 z_NA = %1 m").arg(zNa, 0, 'f', 4);
    const QRectF naRect(px(-B / 2.0) + 6, py(zNa) - 20, 200, 18);
    p.fillRect(naRect.adjusted(-2, 0, 2, 0), QColor(255, 255, 255, 235));  ///< 压住骨材短划，保证可读
    p.setPen(QColor("#c0392b"));
    p.drawText(naRect, Qt::AlignLeft | Qt::AlignVCenter, naText);

    // 两个力臂（剖面模数的分母）排在图外下方，不与中和轴标注抢位置
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor("#7a4b3a"));
    p.drawText(QRectF(8.0, height() - 38.0, width() - 16.0, 18.0), Qt::AlignLeft,
               QString("z_NA = %1 m　｜　D − z_NA = %2 m").arg(zNa, 0, 'f', 4).arg(D - zNa, 0,
                                                                                   'f', 4));
  }

  p.setPen(QColor("#999"));
  p.drawText(QRectF(8.0, height() - 20.0, width() - 16.0, 18.0), Qt::AlignLeft,
             QString("B = %1 m　D = %2 m（示意，非等比例）")
                 .arg(B, 0, 'f', 3)
                 .arg(D, 0, 'f', 3));
}

//=============================================================================
// MidshipSectionDialog
//=============================================================================
MidshipSectionDialog::MidshipSectionDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("SD-03 横剖面结构 —— 组合截面特性 / 中和轴 / 剖面模数");
  resize(1280, 860);
  buildUi();
  fillMemberTable(DefaultMembers());
  refresh();
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::buildUi()
{
  auto *main = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>把结构要素展开成「矩形板 + 竖直腹板」的组合截面，求中和轴位置、"
      "惯性矩与剖面模数 <i>W</i>——<i>W</i> 就是 SD-09 总纵强度校核的分母。<br>"
      "<b>链：</b>请先点「导入 SD-01 结构要素」；导入后可微调板厚/骨材尺寸，"
      "界面会实时给出「上游交付值 vs 本页复现值」。",
      this);
  intro->setWordWrap(true);
  main->addWidget(intro);

  // === 顶部：导入 + 主尺度 ===
  auto *topBox = new QGroupBox("上游交付物 / 主尺度", this);
  auto *topLayout = new QHBoxLayout(topBox);

  auto *leftCol = new QVBoxLayout();
  auto *importBtn = new QPushButton("导入 SD-01 结构要素（sd01_structure.json）", topBox);
  leftCol->addWidget(importBtn);
  importLabel_ = new QLabel("尚未导入 SD-01：下面用默认结构要素表作演示。", topBox);
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

  depthSpin_ = new QDoubleSpinBox(topBox);
  depthSpin_->setRange(0.3, 30.0);
  depthSpin_->setValue(1.0);
  depthSpin_->setDecimals(3);
  depthSpin_->setSuffix(" m");
  form->addRow("型深 D：", depthSpin_);

  hdbSpin_ = new QDoubleSpinBox(topBox);
  hdbSpin_->setRange(0.0, 5.0);
  hdbSpin_->setValue(0.10);
  hdbSpin_->setDecimals(3);
  hdbSpin_->setSuffix(" m");
  form->addRow("双层底高：", hdbSpin_);
  topLayout->addLayout(form, 2);

  main->addWidget(topBox);

  // === 中段：结构要素表 + 剖面图 ===
  auto *mid = new QHBoxLayout();

  auto *memBox = new QGroupBox("结构要素（来自 SD-01，可微调：板厚/间距/腹板尺寸，单位 mm）", this);
  auto *memLayout = new QVBoxLayout(memBox);
  memberTable_ = new QTableWidget(memBox);
  memberTable_->setColumnCount(6);
  memberTable_->setHorizontalHeaderLabels({"名称", "类别", "板厚 (mm)", "骨材间距 (mm)",
                                           "腹板高 (mm)", "腹板厚 (mm)"});
  memberTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  memberTable_->verticalHeader()->setVisible(false);
  memLayout->addWidget(memberTable_, 1);
  auto *recalcBtn = new QPushButton("重算截面", memBox);
  memLayout->addWidget(recalcBtn);
  mid->addWidget(memBox, 3);

  auto *viewBox = new QGroupBox("中横剖面（含中和轴标注）", this);
  auto *viewLayout = new QVBoxLayout(viewBox);
  view_ = new MidshipSectionWidget(viewBox);
  viewLayout->addWidget(view_, 1);
  resultLabel_ = new QLabel(viewBox);
  resultLabel_->setWordWrap(true);
  resultLabel_->setTextFormat(Qt::RichText);
  viewLayout->addWidget(resultLabel_);
  mid->addWidget(viewBox, 2);

  main->addLayout(mid, 4);

  // === 构件表 ===
  auto *compBox = new QGroupBox("构件表（面积 / 形心 / 对自身形心轴的惯性矩）", this);
  auto *compLayout = new QVBoxLayout(compBox);
  compTable_ = new QTableWidget(compBox);
  compTable_->setColumnCount(4);
  compTable_->setHorizontalHeaderLabels(
      {"构件", "面积 A (m²)", "形心 z (m)", "自身惯性矩 I₀ (m⁴)"});
  compTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  compTable_->setMinimumHeight(140);
  compTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  compLayout->addWidget(compTable_);
  main->addWidget(compBox, 2);

  // === 底部 ===
  auto *foot = new QHBoxLayout();
  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  statusLabel_->setWordWrap(true);
  foot->addWidget(statusLabel_, 1);
  auto *exportBtn = new QPushButton("导出横剖面（SD-03 交付物）", this);
  foot->addWidget(exportBtn);
  main->addLayout(foot);

  connect(importBtn, &QPushButton::clicked, this, &MidshipSectionDialog::onImportSd01);
  connect(recalcBtn, &QPushButton::clicked, this, &MidshipSectionDialog::onRecalc);
  connect(exportBtn, &QPushButton::clicked, this, &MidshipSectionDialog::onExport);
  connect(memberTable_, &QTableWidget::itemChanged, this, &MidshipSectionDialog::onEditCell);
  for (QDoubleSpinBox *s : {lengthSpin_, breadthSpin_, depthSpin_, hdbSpin_})
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { refresh(); });
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::fillMemberTable(
    const std::vector<keship::StructureMember> &_members)
{
  const QSignalBlocker blocker(memberTable_);
  memberTable_->setRowCount(static_cast<int>(_members.size()));
  for (int i = 0; i < static_cast<int>(_members.size()); ++i) {
    const auto &m = _members[i];

    auto *nameItem = new QTableWidgetItem(QString::fromStdString(m.name));
    nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);  ///< 名称/类别只读
    memberTable_->setItem(i, 0, nameItem);

    auto *catItem = new QTableWidgetItem(QString::fromStdString(m.category));
    catItem->setFlags(catItem->flags() & ~Qt::ItemIsEditable);
    memberTable_->setItem(i, 1, catItem);

    auto put = [&](int _col, double _mm) {
      auto *it = new QTableWidgetItem(QString::number(_mm, 'f', 1));
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      memberTable_->setItem(i, _col, it);
    };
    put(2, m.thicknessM * 1000.0);
    put(3, m.stiffenerSpacingM * 1000.0);
    put(4, m.stiffenerWebHeightM * 1000.0);
    put(5, m.stiffenerWebThicknessM * 1000.0);
  }
}

//-----------------------------------------------------------------------------
std::vector<keship::StructureMember> MidshipSectionDialog::collectMembers() const
{
  std::vector<keship::StructureMember> out;
  if (!memberTable_) return out;

  for (int i = 0; i < memberTable_->rowCount(); ++i) {
    auto text = [&](int _col) {
      const QTableWidgetItem *it = memberTable_->item(i, _col);
      return it ? it->text().trimmed() : QString();
    };
    keship::StructureMember m;
    m.name = text(0).toStdString();
    m.category = text(1).toStdString();
    m.thicknessM = text(2).toDouble() / 1000.0;
    m.stiffenerSpacingM = text(3).toDouble() / 1000.0;
    m.stiffenerWebHeightM = text(4).toDouble() / 1000.0;
    m.stiffenerWebThicknessM = text(5).toDouble() / 1000.0;
    out.push_back(m);
  }
  return out;
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::onImportSd01()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 SD-01 结构要素交付物", "sd01_structure.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!importFrom(path, &err)) {
    QMessageBox::warning(this, "导入 SD-01 结构要素",
                         QString::fromStdString("无法使用该文件：\n" + err));
    return;
  }
  if (clamped_) {
    QMessageBox::warning(this, "导入 SD-01 结构要素",
                         "SD-01 的主尺度超出本页量程，已被截断——复现结果不可比。");
  }
  std::cout << "SD-03: 已导入 SD-01 交付物 " << path.toStdString() << "（"
            << upstream_.size() << " 行结构要素），W_deck=" << res_.sectionModulusDeckM3
            << std::endl;
}

//-----------------------------------------------------------------------------
bool MidshipSectionDialog::importFrom(const QString &_path, std::string *_err)
{
  keship::ExpDeliverable d;
  std::string err;
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "SD-01", &err)) {
    if (_err) *_err = err;
    return false;
  }

  const QJsonArray arr = d.extra["members"].toArray();
  if (arr.isEmpty()) {
    if (_err)
      *_err = "该 SD-01 交付物没有 extra.members[]（结构要素表为空），无法构造中横剖面";
    return false;
  }

  std::vector<keship::StructureMember> members;
  members.reserve(static_cast<size_t>(arr.size()));
  for (const auto &v : arr) {
    const QJsonObject o = v.toObject();
    keship::StructureMember m;
    m.name = o["name"].toString().toStdString();
    m.category = o["category"].toString().toStdString();
    m.thicknessM = o["thicknessM"].toDouble();
    m.stiffenerSpacingM = o["stiffenerSpacingM"].toDouble();
    m.stiffenerWebHeightM = o["stiffenerWebHeightM"].toDouble();
    m.stiffenerWebThicknessM = o["stiffenerWebThicknessM"].toDouble();
    members.push_back(m);
  }

  // 主尺度：本页量程窄于 SD-01 时会 clamp ⇒ 必须提示，否则复现值与上游不可比
  const double wantL = d.object["lengthM"].toDouble(lengthSpin_->value());
  const double wantB = d.object["breadthM"].toDouble(breadthSpin_->value());
  const double wantD = d.object["depthM"].toDouble(depthSpin_->value());
  const double wantH = d.object["doubleBottomHeightM"].toDouble(hdbSpin_->value());
  clamped_ = false;
  if (wantB < breadthSpin_->minimum() || wantB > breadthSpin_->maximum()) clamped_ = true;
  if (wantD < depthSpin_->minimum() || wantD > depthSpin_->maximum()) clamped_ = true;
  if (wantH < hdbSpin_->minimum() || wantH > hdbSpin_->maximum()) clamped_ = true;

  breadthSpin_->setValue(wantB);
  depthSpin_->setValue(wantD);
  hdbSpin_->setValue(wantH);
  lengthSpin_->setValue(wantL);

  fillMemberTable(members);

  upstream_ = members;
  upstreamPlateAreaM2_ = d.metrics["totalPlateAreaM2"].toDouble();
  upstreamScore_ = d.metrics["score"].toInt();
  upstreamScoredQuestions_ = d.metrics["scoredQuestions"].toInt();
  imported_ = true;

  refresh();
  return true;
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::fillComponentTable()
{
  const QSignalBlocker blocker(compTable_);
  const int n = static_cast<int>(res_.members.size());
  compTable_->setRowCount(n);
  for (int i = 0; i < n; ++i) {
    const auto &m = res_.members[i];
    auto put = [&](int _col, const QString &_txt) {
      auto *it = new QTableWidgetItem(_txt);
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      compTable_->setItem(i, _col, it);
    };
    put(0, QString::fromStdString(m.name));
    put(1, QString::number(m.areaM2, 'f', 6));
    put(2, QString::number(m.zCenterM, 'f', 6));
    put(3, QString::number(m.iOwnM4, 'g', 6));
  }
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::refresh()
{
  const auto members = collectMembers();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double hdb = hdbSpin_->value();

  in_ = keship::MidshipFromMembers(members, B, D, hdb);
  res_ = keship::ComputeMidship(in_);

  view_->setSection(in_, res_);
  fillComponentTable();

  if (!res_.valid) {
    resultLabel_->setText(QString("<span style='color:#b00;'>截面不可用：%1</span>")
                              .arg(QString::fromStdString(res_.note)));
    statusLabel_->setText("请检查板厚/骨材尺寸是否大于 0。");
    statusLabel_->setStyleSheet("color:#b00;");
    return;
  }

  resultLabel_->setText(
      QString(
          "总面积 A = <b>%1</b> m²　｜　中和轴 z<sub>NA</sub> = <b>%2</b> m"
          "（距龙骨）<br>"
          "惯性矩 I = <b>%3</b> m⁴<br>"
          "<span style='color:#1f5fa8;'>W_deck = I/(D−z<sub>NA</sub>) = "
          "<b>%4</b> m³</span>　｜　"
          "<span style='color:#7a4b3a;'>W_keel = I/z<sub>NA</sub> = <b>%5</b> m³</span>")
          .arg(res_.areaM2, 0, 'f', 6)
          .arg(res_.neutralAxisZM, 0, 'f', 6)
          .arg(res_.ixM4, 0, 'f', 6)
          .arg(res_.sectionModulusDeckM3, 0, 'f', 6)
          .arg(res_.sectionModulusKeelM3, 0, 'f', 6));

  // 「上游值 vs 本页复现值」：导入后每次刷新都重算，微调板厚会立刻反映在偏差里
  const double localArea = keship::TotalPlateAreaM2(members, B, D, hdb);
  if (imported_) {
    const double rel = (std::abs(upstreamPlateAreaM2_) > 1e-12)
                           ? std::abs(localArea - upstreamPlateAreaM2_) / upstreamPlateAreaM2_
                           : 0.0;
    importLabel_->setText(
        QString("已导入 SD-01：%1 行结构要素（上游小测 %2/%3）\n"
                "上游 totalPlateAreaM2 = %4 m²　→　本页由同一 members 复算 = %5 m²"
                "（相对偏差 %6）%7")
            .arg(static_cast<int>(members.size()))
            .arg(upstreamScore_)
            .arg(upstreamScoredQuestions_)
            .arg(upstreamPlateAreaM2_, 0, 'f', 6)
            .arg(localArea, 0, 'f', 6)
            .arg(rel * 100.0, 0, 'f', 3)
            .arg(rel > 0.02 ? "%　⚠ 已偏离上游交付值 >2%（改动来自本页微调）" : "%"));
    importLabel_->setStyleSheet(rel > 0.02 ? "color:#b00;" : "color:#338833;");
  } else {
    importLabel_->setText(
        QString("尚未导入 SD-01：下面用默认结构要素表作演示（板面积 %1 m²）。\n"
                "正式做实验请先点「导入 SD-01 结构要素」——上游的板厚与骨材尺寸必须由"
                "交付物读入，而不是手抄。")
            .arg(localArea, 0, 'f', 6));
    importLabel_->setStyleSheet("color:#555;");
  }

  statusLabel_->setText(
      QString("板 %1 块 + 竖直腹板/骨材 %2 道；构件表 %3 行。导出后 SD-09 将用 "
              "W_deck = %4 m³ 做总纵强度校核。")
          .arg(in_.plates.size())
          .arg(in_.stiffeners.size())
          .arg(res_.members.size())
          .arg(res_.sectionModulusDeckM3, 0, 'f', 6));
  statusLabel_->setStyleSheet("color:#555;");
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::onEditCell() { refresh(); }

//-----------------------------------------------------------------------------
void MidshipSectionDialog::onRecalc()
{
  refresh();
  std::cout << "SD-03: 已重算截面（W_deck=" << res_.sectionModulusDeckM3 << "）" << std::endl;
}

//-----------------------------------------------------------------------------
bool MidshipSectionDialog::exportTo(const QString &_path, std::string *_err)
{
  if (!res_.valid) {
    if (_err) *_err = "当前截面无效，无法导出：" + res_.note;
    return false;
  }

  const auto members = collectMembers();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();

  keship::ExpDeliverable d;
  d.expId = "SD-03";
  d.createdAt = keship::NowIso8601();

  d.object["type"] = "midship_section";
  d.object["lengthM"] = lengthSpin_->value();
  d.object["breadthM"] = B;
  d.object["depthM"] = D;
  d.object["doubleBottomHeightM"] = hdbSpin_->value();
  d.object["sourceExpId"] = imported_ ? "SD-01" : "default_table";

  d.params["formula"] =
      "A=sum(Ai); zNA=sum(Ai*zi)/A; I=sum(Ii+Ai*(zi-zNA)^2); "
      "W_deck=I/(D-zNA); W_keel=I/zNA";
  d.params["stiffenerModel"] =
      "stiffener = vertical web b=h x t attached at zBase, grows +z, A=h*t*count, "
      "I=count*t*h^3/12; side shell = 1 web of height D, thickness t, count=2";
  d.params["unitsNote"] = "lengths in m, areas m^2, inertia m^4, section modulus m^3";

  d.metrics["sectionAreaM2"] = res_.areaM2;
  d.metrics["neutralAxisZM"] = res_.neutralAxisZM;
  d.metrics["ixM4"] = res_.ixM4;
  d.metrics["sectionModulusDeckM3"] = res_.sectionModulusDeckM3;
  d.metrics["sectionModulusKeelM3"] = res_.sectionModulusKeelM3;

  // 结构要素原始表（追溯用：能看出这一版截面是哪些板厚算出来的）
  QJsonArray rawArr;
  for (const auto &m : members) {
    QJsonObject o;
    o["name"] = QString::fromStdString(m.name);
    o["category"] = QString::fromStdString(m.category);
    o["thicknessM"] = m.thicknessM;
    o["stiffenerSpacingM"] = m.stiffenerSpacingM;
    o["stiffenerWebHeightM"] = m.stiffenerWebHeightM;
    o["stiffenerWebThicknessM"] = m.stiffenerWebThicknessM;
    rawArr.append(o);
  }
  d.extra["members"] = rawArr;

  // 展开后的几何（SD-09 用它独立复算 W，与 metrics 对账）
  QJsonArray plates, stiffs;
  for (const auto &p : in_.plates) {
    QJsonObject o;
    o["name"] = QString::fromStdString(p.name);
    o["widthM"] = p.widthM;
    o["thicknessM"] = p.thicknessM;
    o["zCenterM"] = p.zCenterM;
    plates.append(o);
  }
  for (const auto &s : in_.stiffeners) {
    QJsonObject o;
    o["name"] = QString::fromStdString(s.name);
    o["webHeightM"] = s.webHeightM;
    o["webThicknessM"] = s.webThicknessM;
    o["spacingM"] = s.spacingM;
    o["zBaseM"] = s.zBaseM;
    o["count"] = s.count;
    stiffs.append(o);
  }
  d.extra["plates"] = plates;
  d.extra["stiffeners"] = stiffs;

  return keship::WriteExpDeliverable(_path.toStdString(), d, _err);
}

//-----------------------------------------------------------------------------
void MidshipSectionDialog::onExport()
{
  if (!res_.valid) {
    QMessageBox::warning(this, "导出横剖面", "当前截面无效，请先修正板厚/骨材尺寸。");
    return;
  }

  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-03 横剖面交付物", "sd03_midship.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!exportTo(path, &err)) {
    QMessageBox::warning(this, "导出横剖面", QString::fromStdString("导出失败：" + err));
    return;
  }
  QMessageBox::information(
      this, "导出横剖面",
      QString("已导出：%1\n\nW_deck = %2 m³ 是 SD-09 总纵强度校核的输入。")
          .arg(path)
          .arg(res_.sectionModulusDeckM3, 0, 'f', 6));
  std::cout << "SD-03: 已导出交付物 " << path.toStdString() << std::endl;
}
