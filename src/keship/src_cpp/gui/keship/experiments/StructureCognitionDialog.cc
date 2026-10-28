/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-01 船体结构认知 —— 实现 */

#include "StructureCognitionDialog.hh"

#include <algorithm>
#include <cmath>
#include <iostream>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

/// 结构要素类别（key 进交付物 extra.members[].category，label 只给人看）
struct CategoryDef {
  const char *key;
  const char *label;
};

const CategoryDef kCategories[] = {
    {"bottom", "船底板（外板）"},
    {"deck", "甲板"},
    {"side", "舷侧板"},
    {"bulkhead", "横舱壁"},
    {"girder", "纵桁（中桁材）"},
    {"stiffener", "骨材（附着内底）"},
    {"doublebottom", "双层底（内底板）"},
};
constexpr int kCategoryCount = static_cast<int>(sizeof(kCategories) / sizeof(kCategories[0]));

/// 默认结构要素表：覆盖课程要求的七类部位。
/// **必须与 test/structure_chain_test.cc 的 DefaultMembers() 同值** ——
/// 链级测试会断言"从 sd01_structure.json 读回的 members 与这张表逐字段相等"，
/// 两边一改就得一起改，否则测试会红（这是刻意的护栏）。
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

int CategoryIndex(const std::string &_key)
{
  for (int i = 0; i < kCategoryCount; ++i)
    if (_key == kCategories[i].key) return i;
  return 0;
}

/// 认知小测：3 题"名称→位置" + 2 题"位置→名称"
struct QuizItem {
  const char *question;
  const char *options[4];
  int correct;
};

const QuizItem kQuiz[] = {
    {"【名称 → 位置】船底板（外板）在横剖面上位于哪里？",
     {"剖面最下端的水平板（基线处）", "剖面最上端的水平板", "两舷的竖直板",
      "双层底顶面的水平板"},
     0},
    {"【名称 → 位置】甲板在横剖面上位于哪里？",
     {"剖面最下端的水平板（基线处）", "剖面最上端的水平板（型深 z = D 处）",
      "两舷的竖直板", "舯部中线的竖直构件"},
     1},
    {"【名称 → 位置】舷侧板在横剖面上位于哪里？",
     {"剖面最下端的水平板", "剖面最上端的水平板", "两舷的竖直板", "双层底内部"},
     2},
    {"【位置 → 名称】把双层底与上方货舱隔开的那层水平板叫？",
     {"甲板", "内底板", "舷侧纵骨", "肋板"},
     1},
    {"【位置 → 名称】沿船长方向布置、在剖面中线从船底竖直向上支撑的构件叫？",
     {"横舱壁", "舷侧纵骨", "中桁材（纵桁）", "内底板"},
     2},
};
constexpr int kQuizCount = static_cast<int>(sizeof(kQuiz) / sizeof(kQuiz[0]));

}  // namespace

//=============================================================================
// StructureSectionWidget
//=============================================================================
StructureSectionWidget::StructureSectionWidget(QWidget *_parent) : QWidget(_parent)
{
  setMinimumSize(360, 300);
}

void StructureSectionWidget::setSection(const keship::MidshipInput &_in,
                                        double _doubleBottomHeightM)
{
  in_ = _in;
  hdb_ = _doubleBottomHeightM;
  update();
}

void StructureSectionWidget::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  p.setPen(QColor("#666"));
  p.drawText(QRectF(8, 4, width() - 16, 18), Qt::AlignLeft,
             "板 + 骨材（骨材为红色短划，腹板为灰蓝色）");

  const double B = in_.breadthM;
  const double D = in_.depthM;
  if (B <= 1e-6 || D <= 1e-6) {
    p.setPen(QColor("#888"));
    p.drawText(rect(), Qt::AlignCenter, "请先给出有效的型宽 / 型深");
    return;
  }

  const double mLeft = 44.0, mRight = 96.0, mTop = 30.0, mBottom = 56.0;
  const double sx = (width() - mLeft - mRight) / B;
  const double sy = (height() - mTop - mBottom) / D;
  const double s = std::min(sx, sy);
  if (s <= 0.0) return;

  const double cx = mLeft + (width() - mLeft - mRight) / 2.0;
  const double baseY = height() - mBottom;
  auto px = [&](double _x) { return cx + _x * s; };        ///< x ∈ [−B/2, +B/2] → 像素
  auto py = [&](double _z) { return baseY - _z * s; };     ///< z ∈ [0, D] → 像素

  // 基线
  p.setPen(QPen(QColor("#c8c8c8"), 1.0, Qt::DashLine));
  p.drawLine(QPointF(mLeft - 24, baseY), QPointF(width() - 12, baseY));
  p.setPen(QColor("#888"));
  p.drawText(QPointF(mLeft - 22, baseY - 4), "基线");

  const QColor plateColor("#4a6c8f");
  const QColor webColor("#7a94ad");
  const QColor stiffColor("#b5603a");

  // 水平板（甲板 / 船底 / 内底）
  for (const auto &pl : in_.plates) {
    const double z0 = pl.zCenterM - 0.5 * pl.thicknessM;
    const double h = std::max(2.0, pl.thicknessM * s);
    QRectF r(px(-B / 2.0), py(z0 + pl.thicknessM), B * s, h);
    p.fillRect(r, plateColor);
    p.setPen(QPen(plateColor.darker(130), 1.0));
    p.drawRect(r);
  }

  // 竖直腹板（舷侧板 / 横舱壁 / 中桁材）与骨材
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
        const double x = -B / 2.0 + (static_cast<double>(k) + 0.5) * B / static_cast<double>(n);
        p.fillRect(QRectF(px(x) - w / 2.0, py(z1), w, (z1 - z0) * s), stiffColor);
      }
    }
  }

  // 双层底高度参考线
  if (hdb_ > 0.0 && hdb_ < D) {
    p.setPen(QPen(QColor("#2f7d4f"), 1.0, Qt::DashLine));
    p.drawLine(QPointF(px(-B / 2.0), py(hdb_)), QPointF(px(B / 2.0), py(hdb_)));
  }

  // 部位标注（右侧一列，留够 mRight = 96 px 不会被裁；基线以上/以下错开避免互相压字）
  QFont f = p.font();
  f.setPointSizeF(std::max(7.5, f.pointSizeF() - 1.0));
  p.setFont(f);
  p.setPen(QColor("#33506b"));
  p.drawText(QPointF(px(B / 2.0) + 8, py(D) + 12), "甲板");
  p.drawText(QPointF(px(B / 2.0) + 8, py(0.0) + 14), "船底板");
  p.drawText(QPointF(px(-B / 2.0) - 40, py(D / 2.0)), "舷侧板");
  if (hdb_ > 0.0 && hdb_ < D)
    p.drawText(QPointF(px(B / 2.0) + 8, py(hdb_) - 5), "内底板");
  p.drawText(QPointF(px(0.0) + 6, py(D / 2.0) + 12), "中桁材");

  // 型宽/型深尺寸文字（示意，非等比例）
  p.setPen(QColor("#999"));
  p.drawText(QRectF(8.0, height() - 22.0, width() - 16.0, 18.0), Qt::AlignLeft,
             QString("B = %1 m　D = %2 m　双层底 %3 m（示意）")
                 .arg(B, 0, 'f', 3)
                 .arg(D, 0, 'f', 3)
                 .arg(hdb_, 0, 'f', 3));
}

//=============================================================================
// StructureCognitionDialog
//=============================================================================
StructureCognitionDialog::StructureCognitionDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("SD-01 船体结构认知 —— 结构要素 / 部位认知 / 交付物");
  resize(1180, 800);
  buildUi();
  fillTable(DefaultMembers());
  refreshSection();
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::buildUi()
{
  auto *main = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>认识中横剖面的七类结构要素（外板 / 甲板 / 舷侧板 / 横舱壁 / 纵桁 / "
      "骨材 / 双层底），并理解「板厚」与「骨材尺寸」如何决定剖面的抗弯能力。<br>"
      "<b>交付物：</b>「导出结构要素」生成的 <code>sd01_structure.json</code> 是 "
      "<b>SD-03 横剖面结构</b>的输入——SD-03 会直接用下表的板厚与骨材尺寸构造中横剖面；"
      "改一行板厚，下游的剖面模数就会变。",
      this);
  intro->setWordWrap(true);
  main->addWidget(intro);

  // === 主尺度 ===
  auto *dimBox = new QGroupBox("主尺度", this);
  auto *dimLayout = new QHBoxLayout(dimBox);
  auto *form = new QFormLayout();

  lengthSpin_ = new QDoubleSpinBox(dimBox);
  lengthSpin_->setRange(2.0, 300.0);
  lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5);
  lengthSpin_->setDecimals(3);
  lengthSpin_->setSuffix(" m");
  form->addRow("船长 L：", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(dimBox);
  breadthSpin_->setRange(0.5, 60.0);
  breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1);
  breadthSpin_->setDecimals(3);
  breadthSpin_->setSuffix(" m");
  form->addRow("型宽 B：", breadthSpin_);

  depthSpin_ = new QDoubleSpinBox(dimBox);
  depthSpin_->setRange(0.3, 30.0);
  depthSpin_->setValue(1.0);
  depthSpin_->setSingleStep(0.05);
  depthSpin_->setDecimals(3);
  depthSpin_->setSuffix(" m");
  form->addRow("型深 D：", depthSpin_);

  hdbSpin_ = new QDoubleSpinBox(dimBox);
  hdbSpin_->setRange(0.0, 5.0);
  hdbSpin_->setValue(0.10);
  hdbSpin_->setSingleStep(0.01);
  hdbSpin_->setDecimals(3);
  hdbSpin_->setSuffix(" m");
  form->addRow("双层底高：", hdbSpin_);

  dimLayout->addLayout(form);
  auto *dimHint = new QLabel(
      "默认值即平台教学船（6 m × 1.5 m × 1.0 m）。<br>"
      "船长 L 会随交付物一路传到 SD-09（波浪弯矩按 L² 增长）。", dimBox);
  dimHint->setWordWrap(true);
  dimLayout->addWidget(dimHint, 1);
  main->addWidget(dimBox);

  // === 结构要素表 + 示意剖面 ===
  auto *body = new QHBoxLayout();

  auto *tableBox = new QGroupBox("结构要素表（板厚/骨材尺寸单位为 mm，可直接改）", this);
  auto *tableLayout = new QVBoxLayout(tableBox);
  table_ = new QTableWidget(tableBox);
  table_->setColumnCount(6);
  table_->setHorizontalHeaderLabels({"名称", "类别", "板厚 (mm)", "骨材间距 (mm)",
                                     "腹板高 (mm)", "腹板厚 (mm)"});
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  table_->verticalHeader()->setVisible(false);
  tableLayout->addWidget(table_, 1);

  auto *btnRow = new QHBoxLayout();
  auto *addBtn = new QPushButton("添加行", tableBox);
  auto *delBtn = new QPushButton("删除选中行", tableBox);
  auto *resetBtn = new QPushButton("恢复默认表", tableBox);
  btnRow->addWidget(addBtn);
  btnRow->addWidget(delBtn);
  btnRow->addWidget(resetBtn);
  btnRow->addStretch();
  tableLayout->addLayout(btnRow);
  body->addWidget(tableBox, 3);

  auto *viewBox = new QGroupBox("中横剖面示意", this);
  auto *viewLayout = new QVBoxLayout(viewBox);
  sectionView_ = new StructureSectionWidget(viewBox);
  viewLayout->addWidget(sectionView_);
  body->addWidget(viewBox, 2);

  main->addLayout(body, 3);

  // === 认知小测 ===
  main->addWidget(buildQuizBox());

  // === 状态 + 导出 ===
  auto *footRow = new QHBoxLayout();
  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  statusLabel_->setWordWrap(true);
  footRow->addWidget(statusLabel_, 1);
  auto *exportBtn = new QPushButton("导出结构要素（SD-01 交付物）", this);
  footRow->addWidget(exportBtn);
  main->addLayout(footRow);

  connect(addBtn, &QPushButton::clicked, this, &StructureCognitionDialog::onAddRow);
  connect(delBtn, &QPushButton::clicked, this, &StructureCognitionDialog::onRemoveRow);
  connect(resetBtn, &QPushButton::clicked, this, &StructureCognitionDialog::onResetTable);
  connect(table_, &QTableWidget::itemChanged, this, &StructureCognitionDialog::onTableEdited);
  connect(exportBtn, &QPushButton::clicked, this, &StructureCognitionDialog::onExport);
  for (QDoubleSpinBox *s : {lengthSpin_, breadthSpin_, depthSpin_, hdbSpin_})
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double) { refreshSection(); });
}

//-----------------------------------------------------------------------------
QWidget *StructureCognitionDialog::buildQuizBox()
{
  auto *box = new QGroupBox("认知小测（5 题：3 题给名称选位置，2 题给位置选名称）", this);
  auto *layout = new QVBoxLayout(box);

  for (int i = 0; i < kQuizCount; ++i) {
    auto *q = new QLabel(QString("%1. %2").arg(i + 1).arg(kQuiz[i].question), box);
    q->setWordWrap(true);
    layout->addWidget(q);

    auto *combo = new QComboBox(box);
    combo->addItem("— 请选择 —", -1);
    for (int k = 0; k < 4; ++k) combo->addItem(kQuiz[i].options[k], k);
    quizCombos_.push_back(combo);
    layout->addWidget(combo);
  }

  auto *row = new QHBoxLayout();
  auto *submitBtn = new QPushButton("提交答案", box);
  quizResultLabel_ = new QLabel("尚未提交", box);
  quizResultLabel_->setStyleSheet("color:#555;");
  row->addWidget(submitBtn);
  row->addWidget(quizResultLabel_, 1);
  layout->addLayout(row);

  connect(submitBtn, &QPushButton::clicked, this, &StructureCognitionDialog::onSubmitQuiz);
  quizTotal_ = kQuizCount;
  return box;
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::fillTable(const std::vector<keship::StructureMember> &_members)
{
  const QSignalBlocker blocker(table_);  ///< 逐格填充时不要触发 itemChanged → 反复重算
  table_->setRowCount(static_cast<int>(_members.size()));
  for (int i = 0; i < static_cast<int>(_members.size()); ++i) {
    const auto &m = _members[i];

    table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(m.name)));

    auto *combo = new QComboBox(table_);
    for (int k = 0; k < kCategoryCount; ++k)
      combo->addItem(kCategories[k].label, kCategories[k].key);
    combo->setCurrentIndex(CategoryIndex(m.category));
    table_->setCellWidget(i, 1, combo);

    auto put = [&](int _col, double _mm) {
      auto *it = new QTableWidgetItem(QString::number(_mm, 'f', 1));
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      table_->setItem(i, _col, it);
    };
    put(2, m.thicknessM * 1000.0);
    put(3, m.stiffenerSpacingM * 1000.0);
    put(4, m.stiffenerWebHeightM * 1000.0);
    put(5, m.stiffenerWebThicknessM * 1000.0);
  }
}

//-----------------------------------------------------------------------------
std::vector<keship::StructureMember> StructureCognitionDialog::collectMembers() const
{
  std::vector<keship::StructureMember> out;
  if (!table_) return out;

  for (int i = 0; i < table_->rowCount(); ++i) {
    auto text = [&](int _col) {
      const QTableWidgetItem *it = table_->item(i, _col);
      return it ? it->text().trimmed() : QString();
    };

    keship::StructureMember m;
    m.name = text(0).toStdString();
    if (m.name.empty()) m.name = QString("构件 %1").arg(i + 1).toStdString();

    if (auto *cb = qobject_cast<QComboBox *>(table_->cellWidget(i, 1)))
      m.category = cb->currentData().toString().toStdString();
    else
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
void StructureCognitionDialog::refreshSection()
{
  const auto members = collectMembers();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double hdb = hdbSpin_->value();

  const auto in = keship::MidshipFromMembers(members, B, D, hdb);
  sectionView_->setSection(in, hdb);

  const auto r = keship::ComputeMidship(in);
  if (!r.valid) {
    statusLabel_->setText(QString("结构要素 %1 行；本表展开的截面不可用：%2")
                              .arg(members.size())
                              .arg(QString::fromStdString(r.note)));
    statusLabel_->setStyleSheet("color:#b00;");
    return;
  }
  statusLabel_->setText(
      QString("结构要素 %1 行，板面积 %2 m²；本表展开的截面：A = %3 m²，中和轴 z = %4 m "
              "（预览值，剖面模数由 SD-03 正式计算）")
          .arg(members.size())
          .arg(keship::TotalPlateAreaM2(members, B, D, hdb), 0, 'f', 5)
          .arg(r.areaM2, 0, 'f', 5)
          .arg(r.neutralAxisZM, 0, 'f', 5));
  statusLabel_->setStyleSheet("color:#555;");
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::onAddRow()
{
  const int row = table_->rowCount();
  table_->blockSignals(true);
  table_->insertRow(row);
  table_->setItem(row, 0, new QTableWidgetItem(QString("新构件 %1").arg(row + 1)));
  auto *combo = new QComboBox(table_);
  for (int k = 0; k < kCategoryCount; ++k)
    combo->addItem(kCategories[k].label, kCategories[k].key);
  table_->setCellWidget(row, 1, combo);
  for (int c = 2; c <= 5; ++c) {
    auto *it = new QTableWidgetItem("0.0");
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setItem(row, c, it);
  }
  table_->blockSignals(false);
  refreshSection();
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::onRemoveRow()
{
  const int row = table_->currentRow();
  if (row < 0) {
    QMessageBox::information(this, "删除选中行", "请先在表格里选中一行。");
    return;
  }
  table_->blockSignals(true);
  table_->removeRow(row);
  table_->blockSignals(false);
  refreshSection();
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::onResetTable()
{
  fillTable(DefaultMembers());
  refreshSection();
  std::cout << "SD-01: 结构要素表已恢复默认（7 类部位）" << std::endl;
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::onTableEdited() { refreshSection(); }

//-----------------------------------------------------------------------------
void StructureCognitionDialog::onSubmitQuiz()
{
  int score = 0;
  int answered = 0;
  for (int i = 0; i < static_cast<int>(quizCombos_.size()) && i < kQuizCount; ++i) {
    const int v = quizCombos_[i]->currentData().toInt();
    if (v < 0) continue;  ///< 未作答的题不计入已判分题数
    ++answered;
    if (v == kQuiz[i].correct) ++score;
  }

  score_ = score;
  scoredQuestions_ = answered;

  if (answered == 0) {
    quizResultLabel_->setText("请至少作答一题后再提交。");
    quizResultLabel_->setStyleSheet("color:#b06000;");
    return;
  }

  quizResultLabel_->setText(
      QString("得分 %1 / %2（已判分 %3 题）%4")
          .arg(score)
          .arg(quizTotal_)
          .arg(answered)
          .arg(score == quizTotal_ ? " —— 全部正确" : " —— 错题请对照右侧剖面再想一次"));
  quizResultLabel_->setStyleSheet(score == quizTotal_ ? "color:#338833;font-weight:bold;"
                                                      : "color:#b06000;font-weight:bold;");
  std::cout << "SD-01: 小测得分 " << score << "/" << quizTotal_ << "（已判分 " << answered
            << " 题）" << std::endl;
}

//-----------------------------------------------------------------------------
bool StructureCognitionDialog::exportTo(const QString &_path, std::string *_err)
{
  const auto members = collectMembers();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double hdb = hdbSpin_->value();

  keship::ExpDeliverable d;
  d.expId = "SD-01";
  d.createdAt = keship::NowIso8601();

  d.object["type"] = "structure_cognition";
  d.object["lengthM"] = lengthSpin_->value();
  d.object["breadthM"] = B;
  d.object["depthM"] = D;
  d.object["doubleBottomHeightM"] = hdb;

  d.params["quizTotal"] = quizTotal_;
  d.params["categoryScheme"] = "bottom/deck/side/bulkhead/girder/stiffener/doublebottom";
  /// 单位口径必须写进交付物：下游若把 mm 当 m 读，截面会差 1000 倍而界面看不出来
  d.params["memberFieldUnits"] = "thicknessM/stiffenerSpacingM/stiffenerWebHeightM/"
                                 "stiffenerWebThicknessM 均为 m（界面表格里显示的是 mm）";

  d.metrics["memberCount"] = static_cast<int>(members.size());
  d.metrics["totalPlateAreaM2"] = keship::TotalPlateAreaM2(members, B, D, hdb);
  d.metrics["scoredQuestions"] = scoredQuestions_;
  d.metrics["score"] = score_;

  QJsonArray arr;
  for (const auto &m : members) {
    QJsonObject o;
    o["name"] = QString::fromStdString(m.name);
    o["category"] = QString::fromStdString(m.category);
    o["thicknessM"] = m.thicknessM;
    o["stiffenerSpacingM"] = m.stiffenerSpacingM;
    o["stiffenerWebHeightM"] = m.stiffenerWebHeightM;
    o["stiffenerWebThicknessM"] = m.stiffenerWebThicknessM;
    arr.append(o);
  }
  d.extra["members"] = arr;

  return keship::WriteExpDeliverable(_path.toStdString(), d, _err);
}

//-----------------------------------------------------------------------------
void StructureCognitionDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-01 结构要素交付物", "sd01_structure.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!exportTo(path, &err)) {
    QMessageBox::warning(this, "导出结构要素",
                         QString::fromStdString("导出失败：" + err));
    return;
  }
  QMessageBox::information(
      this, "导出结构要素",
      QString("已导出：%1\n\n该文件是 SD-03 横剖面结构的输入（判据 B：机器交付物贯通）。")
          .arg(path));
  std::cout << "SD-01: 已导出交付物 " << path.toStdString() << std::endl;
}
