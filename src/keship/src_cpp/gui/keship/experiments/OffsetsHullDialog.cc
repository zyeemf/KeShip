/*
 * Copyright (C) 2024-2026 KeShip Team
 */
#include "OffsetsHullDialog.hh"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <QButtonGroup>
#include <QCheckBox>
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
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"
#include "../shared/StudentCodeRunner.hh"

namespace {
constexpr double kWaterDensity = 1000.0;

/// 资料坐标 → 平台坐标（张老师 2026-10-31 要求"坐标系统一"）
///   资料：x∈[-1,1]（中点为 0）、z∈[0,1]（水线 0 → 龙骨 1，**向下为正**）、y=Y/(B/2)
///   平台：x∈[0,L]（艉 0 → 艏 L）、z∈[-D,0]（水线 0 → 龙骨 -D，**向上**）、y 为实际半宽
///   换算：x=(x_l+1)·L/2、z=-z_l·D、y=y_l·B/2
///   stations 仍升序、waterlines 仍降序（0 → -D），与 LinesData 约定一致，无需反转。
void ConvertDocCoordsToPlatform(keship::LinesData &_d, double _L, double _B, double _D)
{
  for (auto &x : _d.stations) x = (x + 1.0) * 0.5 * _L;
  for (auto &z : _d.waterlines) z = -z * _D;
  for (auto &row : _d.halfBreadths)
    for (auto &y : row) y *= 0.5 * _B;

  /// ⚠ 顺序必须一并反转：ParseOffsetsCsv 把 waterlines 排成**降序**（按资料 z 从 1 到 0），
  ///   取负后变成 [-D, 0] **升序**，与 LinesData 约定（0 → -D 降序）相反。
  ///   实测症状：V4 报「水线未覆盖 [-D,0]：首 -1.000000，末 -0.000000」。
  ///   halfBreadths 的列与 waterlines 一一对应，必须同步反转，否则半宽会错位。
  std::reverse(_d.waterlines.begin(), _d.waterlines.end());
  for (auto &row : _d.halfBreadths) std::reverse(row.begin(), row.end());
}

const char *kPyTemplate = R"PY(# SD-10 型值表生成 —— 补全公式后点「运行」
# 输出约定：每行一个型值点，格式 x,z,halfBreadth（逗号分隔，单位 m）
#           只打印右舷（halfBreadth >= 0），左舷由平台镜像；行序不限
L, B, D = 6.0, 1.5, 1.0
Nx, Nz = 20, 10          # 平台要求 Nx >= 20, Nz >= 10

for i in range(Nx + 1):
    x = L * i / Nx
    for j in range(Nz + 1):
        z = -D * j / Nz                  # z: 0(水线) -> -D(龙骨)
        # ==== 在此实现 Wigley 半宽公式 ====
        # xi = 2*x/L - 1,  zeta = -z/D
        # halfBreadth = B/2 * (1 - xi*xi) * (1 - zeta*zeta)
        halfBreadth = 0.0
        print("%.6f,%.6f,%.6f" % (x, z, halfBreadth))
)PY";

const char *kCTemplate = R"C(/* SD-10 型值表生成 —— 补全公式后点「运行」
   输出约定：每行一个型值点，格式 x,z,halfBreadth（逗号分隔，单位 m）
             只打印右舷（halfBreadth >= 0），左舷由平台镜像；行序不限 */
#include <stdio.h>
int main(void)
{
    double L = 6.0, B = 1.5, D = 1.0;
    int Nx = 20, Nz = 10;        /* 平台要求 Nx >= 20, Nz >= 10 */
    for (int i = 0; i <= Nx; i++) {
        double x = L * i / Nx;
        for (int j = 0; j <= Nz; j++) {
            double z = -D * j / Nz;          /* z: 0(水线) -> -D(龙骨) */
            /* ==== 在此实现 Wigley 半宽公式 ====
               xi = 2*x/L - 1,  zeta = -z/D
               halfBreadth = B/2 * (1 - xi*xi) * (1 - zeta*zeta) */
            double halfBreadth = 0.0;
            printf("%.6f,%.6f,%.6f\n", x, z, halfBreadth);
        }
    }
    return 0;
}
)C";
}  // namespace

//-----------------------------------------------------------------------------
OffsetsHullDialog::OffsetsHullDialog(QWidget *parent) : QDialog(parent)
{
  buildUi();
  onResetTemplate();
  fillTable();
  showReport();
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::buildUi()
{
  setWindowTitle("SD-10 型值表编程生成可浮船型");
  resize(1080, 720);

  auto *root = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的：</b>自己编程算出船体型值点，由平台据此造出一条<b>真能浮起来</b>的船。<br>"
      "改一行公式，船的形状与吃水就会变 —— 这就是本实验的验收判据。"
      "<b>输出约定：</b>每行 <code>x,z,halfBreadth</code>（逗号分隔，单位 m），"
      "只打右舷、行序不限。平台会做六项校验（缺格/重复/负半宽/超 B/2/缺端点/端部未趋零/体积对拍）。",
      this);
  intro->setWordWrap(true);
  root->addWidget(intro);

  // === 主尺度（模板常量，与代码里的 L/B/D 一致）===
  auto *paramBox = new QGroupBox("主尺度与网格（须与学生代码中的常量一致）", this);
  auto *paramLayout = new QHBoxLayout(paramBox);
  auto *formL = new QFormLayout();
  auto *formR = new QFormLayout();

  auto mkSpin = [paramBox](double lo, double hi, double val, double step,
                           const char *suffix, int dec) {
    auto *s = new QDoubleSpinBox(paramBox);
    s->setRange(lo, hi);
    s->setValue(val);
    s->setSingleStep(step);
    s->setDecimals(dec);
    if (suffix) s->setSuffix(suffix);
    return s;
  };

  lengthSpin_ = mkSpin(2.0, 30.0, 6.0, 0.5, " m", 3);
  breadthSpin_ = mkSpin(0.5, 10.0, 1.5, 0.1, " m", 3);
  depthSpin_ = mkSpin(0.3, 5.0, 1.0, 0.05, " m", 3);
  densitySpin_ = mkSpin(100.0, 990.0, 500.0, 25.0, " kg/m3", 1);
  formL->addRow("船长 L：", lengthSpin_);
  formL->addRow("型宽 B：", breadthSpin_);
  formL->addRow("型深 D：", depthSpin_);
  formL->addRow("船体密度 rho：", densitySpin_);   ///< 与长/宽/深同列（张老师 2026-10-31）

  familyCombo_ = new QComboBox(paramBox);
  familyCombo_->addItem("Wigley 经典（抛物水线 + 抛物横剖）");
  familyCombo_->addItem("Wigley 平底（大水线面）");
  familyCombo_->addItem("Wigley 尖底（V 形）");
  familyCombo_->addItem("Wigley 瘦削（小水线面）");
  familyCombo_->addItem("Lewis U 形横剖");
  familyCombo_->addItem("Lewis V 形横剖");
  familyCombo_->addItem("自定义 / 课程资料模型（不对拍体积）");
  familyCombo_->setToolTip("选择你代码里实现的船型族：\n"
                           "体积校验(V6)按该族的解析体积对拍。\n"
                           "选错会导致体积被误判为\"写错\"。");
  formR->addRow("参考船型族：", familyCombo_);

  docCoordsCheck_ = new QCheckBox("按资料坐标输入（归一化，z 向下）", paramBox);
  docCoordsCheck_->setToolTip("勾选后：把资料的 x∈[-1,1]、z∈[0,1]（向下为正）、y=Y/(B/2)\n"
                              "自动换算为平台的 x∈[0,L]、z∈[-D,0]（向上）、y 实际半宽。");
  formR->addRow(QString(), docCoordsCheck_);

  auto *helpBtn = new QPushButton("船型说明…", paramBox);
  helpBtn->setToolTip("查看 6 个船型族的半宽公式、体积与适用场景");
  formR->addRow(QString(), helpBtn);

  nxSpin_ = new QSpinBox(paramBox);
  nxSpin_->setRange(20, 120);
  nxSpin_->setValue(20);
  formR->addRow("站位数 Nx：", nxSpin_);
  nzSpin_ = new QSpinBox(paramBox);
  nzSpin_->setRange(10, 60);
  nzSpin_->setValue(10);
  formR->addRow("水线数 Nz：", nzSpin_);

  paramLayout->addLayout(formL);
  paramLayout->addSpacing(24);
  paramLayout->addLayout(formR);
  root->addWidget(paramBox);

  // === 左右分栏：代码 / 结果 ===
  auto *splitter = new QSplitter(Qt::Horizontal, this);

  auto *leftBox = new QGroupBox("学生代码", splitter);
  auto *leftLayout = new QVBoxLayout(leftBox);
  auto *langRow = new QHBoxLayout();
  pythonRadio_ = new QRadioButton("Python", leftBox);
  cRadio_ = new QRadioButton("C", leftBox);
  cRadio_->setChecked(true);   ///< 默认 C（课程要求；Python 仍可切换）
  auto *langGroup = new QButtonGroup(leftBox);
  langGroup->addButton(pythonRadio_);
  langGroup->addButton(cRadio_);
  langRow->addWidget(new QLabel("语言：", leftBox));
  langRow->addWidget(pythonRadio_);
  langRow->addWidget(cRadio_);
  langRow->addStretch();
  leftLayout->addLayout(langRow);

  codeEdit_ = new QPlainTextEdit(leftBox);
  codeEdit_->setLineWrapMode(QPlainTextEdit::NoWrap);
  codeEdit_->setStyleSheet("font-family: monospace;");
  leftLayout->addWidget(codeEdit_, 1);

  auto *btnRow = new QHBoxLayout();
  runBtn_ = new QPushButton("运行代码", leftBox);
  auto *resetBtn = new QPushButton("重置模板", leftBox);
  btnRow->addWidget(runBtn_);
  btnRow->addWidget(resetBtn);
  btnRow->addStretch();
  leftLayout->addLayout(btnRow);

  logEdit_ = new QPlainTextEdit(leftBox);
  logEdit_->setReadOnly(true);
  logEdit_->setMaximumHeight(120);
  logEdit_->setStyleSheet("font-family: monospace;");
  leftLayout->addWidget(logEdit_);

  auto *rightBox = new QGroupBox("型值表与校验", splitter);
  auto *rightLayout = new QVBoxLayout(rightBox);
  table_ = new QTableWidget(rightBox);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  rightLayout->addWidget(table_, 1);

  reportLabel_ = new QLabel(rightBox);
  reportLabel_->setWordWrap(true);
  reportLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rightLayout->addWidget(reportLabel_);

  hydroLabel_ = new QLabel(rightBox);
  hydroLabel_->setWordWrap(true);
  hydroLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  rightLayout->addWidget(hydroLabel_);

  splitter->addWidget(leftBox);
  splitter->addWidget(rightBox);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  root->addWidget(splitter, 1);

  // === 底部动作 ===
  auto *actionRow = new QHBoxLayout();
  spawnBtn_ = new QPushButton("生成可浮船型", this);
  spawnBtn_->setEnabled(false);
  exportBtn_ = new QPushButton("导出交付物（SD-10）", this);
  exportBtn_->setEnabled(false);
  actionRow->addStretch();
  actionRow->addWidget(spawnBtn_);
  actionRow->addWidget(exportBtn_);
  root->addLayout(actionRow);

  connect(runBtn_, &QPushButton::clicked, this, &OffsetsHullDialog::onRunCode);
  connect(resetBtn, &QPushButton::clicked, this, &OffsetsHullDialog::onResetTemplate);
  connect(pythonRadio_, &QRadioButton::toggled, this, &OffsetsHullDialog::onLanguageChanged);
  connect(spawnBtn_, &QPushButton::clicked, this, &OffsetsHullDialog::onSpawn);
  connect(exportBtn_, &QPushButton::clicked, this, &OffsetsHullDialog::onExport);
  connect(helpBtn, &QPushButton::clicked, this, &OffsetsHullDialog::onShowFamilyHelp);
  connect(familyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          [this](int) { if (hull_) { rebuildSpec(); } showReport(); });
}

//-----------------------------------------------------------------------------
QString OffsetsHullDialog::buildTemplate() const
{
  return QString::fromUtf8(pythonRadio_->isChecked() ? kPyTemplate : kCTemplate);
}

void OffsetsHullDialog::onResetTemplate()
{
  if (codeEdit_) codeEdit_->setPlainText(buildTemplate());
}

void OffsetsHullDialog::onLanguageChanged()
{
  onResetTemplate();
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::onRunCode()
{
  const QString lang = pythonRadio_->isChecked() ? "python" : "c";
  const QString code = codeEdit_->toPlainText();
  logEdit_->setPlainText("运行中…");
  runBtn_->setEnabled(false);

  keship::StudentCodeRunner::RunAsync(this, lang, code,
      [this](const keship::StudentRunResult &_r) {
        runBtn_->setEnabled(true);
        if (!_r.ok) {
          logEdit_->setPlainText("运行失败：\n" + _r.stderrText);
          spawnBtn_->setEnabled(false);
          exportBtn_->setEnabled(false);
          return;
        }
        std::string err;
        const bool ok = applyCsvText(_r.stdoutText, &err);
        QString msg = ok ? QStringLiteral("运行成功：型值表已通过六项校验。\n")
                         : (QStringLiteral("运行完成，但校验未通过：\n") +
                            QString::fromStdString(err) + "\n");
        if (!_r.stderrText.isEmpty())
          msg += QStringLiteral("\n[程序 stderr]\n") + _r.stderrText;
        logEdit_->setPlainText(msg);
      });
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::rebuildSpec()
{
  const double L = lengthSpin_->value();
  const double B = breadthSpin_->value();
  const double D = depthSpin_->value();
  const double rho = densitySpin_->value();

  spec_ = keship::ShipHullSpec::WigleyClassic(L, B, D, rho);
  spec_.L = L;
  spec_.B = B;
  spec_.D = D;
  spec_.rho = rho;
  spec_.offsets = lines_;
  spec_.useOffsets = true;

  // 网格分辨率跟随学生给出的站/水线数（视觉与浮力同一套，保证所见即所浮）
  const int nx = std::max(1, static_cast<int>(lines_.stations.size()) - 1);
  const int nz = std::max(1, static_cast<int>(lines_.waterlines.size()) - 1);
  spec_.visNx = nx;
  spec_.visNz = nz;
  spec_.buoyNx = nx;
  spec_.buoyNz = nz;
}

//-----------------------------------------------------------------------------
bool OffsetsHullDialog::applyCsvText(const QString &_csv, std::string *_err)
{
  std::vector<std::string> perr;
  keship::LinesData d;
  const bool parsed = keship::ParseOffsetsCsv(_csv.toStdString(), d, perr);

  const double L = lengthSpin_ ? lengthSpin_->value() : 6.0;
  const double B = breadthSpin_ ? breadthSpin_->value() : 1.5;
  const double D = depthSpin_ ? depthSpin_->value() : 1.0;

  if (!parsed) {
    report_ = keship::OffsetsReport{};
    report_.errors = perr;
    lines_ = keship::LinesData{};
    hull_.reset();
    fillTable();
    showReport();
    if (_err) *_err = perr.empty() ? std::string("型值表解析失败") : perr.front();
    return false;
  }

  if (docCoordsCheck_ && docCoordsCheck_->isChecked())
    ConvertDocCoordsToPlatform(d, L, B, D);   ///< 资料坐标 → 平台坐标

  lines_ = d;
  report_ = keship::ValidateOffsets(d, L, B, D, familyVolume());

  if (!report_.ok) {
    hull_.reset();
    fillTable();
    showReport();
    if (_err) *_err = report_.errors.empty() ? std::string("校验未通过") : report_.errors.front();
    return false;
  }

  rebuildSpec();
  hull_ = std::make_unique<keship::ShipHullGenerator>(spec_);
  fillTable();
  showReport();
  return true;
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::fillTable()
{
  if (!table_) return;
  const int nSt = static_cast<int>(lines_.stations.size());
  const int nWl = static_cast<int>(lines_.waterlines.size());
  table_->clear();
  table_->setRowCount(nSt);
  table_->setColumnCount(nWl + 1);

  QStringList headers;
  headers << "x \\ z";
  for (int j = 0; j < nWl; ++j)
    headers << QString::number(lines_.waterlines[j], 'f', 3);
  table_->setHorizontalHeaderLabels(headers);

  for (int i = 0; i < nSt; ++i) {
    table_->setItem(i, 0, new QTableWidgetItem(
        QString::number(lines_.stations[i], 'f', 3)));
    for (int j = 0; j < nWl; ++j) {
      const double y = (i < static_cast<int>(lines_.halfBreadths.size()) &&
                        j < static_cast<int>(lines_.halfBreadths[i].size()))
                           ? lines_.halfBreadths[i][j]
                           : 0.0;
      table_->setItem(i, j + 1, new QTableWidgetItem(QString::number(y, 'f', 4)));
    }
  }
  table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::showReport()
{
  if (!reportLabel_) return;

  QString s;
  if (report_.nStations > 0) {
    s += QString("网格：%1 站 x %2 水线　最大半宽 %3 m　预估三角 %4\n")
             .arg(report_.nStations)
             .arg(report_.nWaterlines)
             .arg(report_.maxHalfBreadth, 0, 'f', 4)
             .arg(report_.estTriangles);
    s += QString("型值表积分体积：%1 m3　解析参考：%2 m3　偏差：%3%\n")
             .arg(report_.volumeFromTable, 0, 'f', 6)
             .arg(report_.exactVolume, 0, 'f', 6)
             .arg(report_.volumeErrPct, 0, 'f', 3);
  }

  if (report_.ok) {
    s += QStringLiteral("<span style='color:#1a7f37;'>校验通过</span>");
  } else if (report_.errors.empty()) {
    // 还没运行代码 / 未提交型值表 —— 不是"未通过"，别用错误色吓人
    s += QStringLiteral("<span style='color:#8a6d3b;'>待运行代码：尚未提交型值表</span>");
  } else {
    s += QStringLiteral("<span style='color:#b42318;'>校验未通过（%1 条）</span><ul>")
             .arg(report_.errors.size());
    for (const auto &e : report_.errors)
      s += "<li>" + QString::fromStdString(e) + "</li>";
    s += "</ul>";
  }
  for (const auto &w : report_.warnings)
    s += QStringLiteral("<div style='color:#8a6d3b;'>提示：%1</div>")
             .arg(QString::fromStdString(w));

  reportLabel_->setText(s);

  if (hydroLabel_) {
    if (hull_) {
      const auto &hp = hull_->hydroProps();
      hydroLabel_->setText(
          QString("静水力：设计吃水 %1 m　排水体积 %2 m3　KBup %3 m　BMd %4 m　GMd %5 m　AwD %6 m2")
              .arg(hp.draft, 0, 'f', 4)
              .arg(hp.volumeD, 0, 'f', 4)
              .arg(hp.KBup, 0, 'f', 4)
              .arg(hp.BMd, 0, 'f', 4)
              .arg(hp.GMd, 0, 'f', 4)
              .arg(hp.AwD, 0, 'f', 4));
    } else {
      hydroLabel_->setText(QStringLiteral("静水力：待型值表通过校验后计算"));
    }
  }

  const bool ready = (hull_ != nullptr);
  if (spawnBtn_) spawnBtn_->setEnabled(ready);
  if (exportBtn_) exportBtn_->setEnabled(ready);
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::onSpawn()
{
  if (!hull_) {
    QMessageBox::warning(this, "生成可浮船型", "请先运行代码并让型值表通过校验。");
    return;
  }
  emit spawnRequested(spec_, std::string("sd10_hull"));
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::setFamilyIndex(int _i)
{
  if (!familyCombo_) return;
  if (_i < 0 || _i >= familyCombo_->count()) return;
  familyCombo_->setCurrentIndex(_i);
}

void OffsetsHullDialog::setDocCoords(bool _on)
{
  if (docCoordsCheck_) docCoordsCheck_->setChecked(_on);
}

bool OffsetsHullDialog::docCoords() const
{
  return docCoordsCheck_ && docCoordsCheck_->isChecked();
}

int OffsetsHullDialog::familyIndex() const
{
  return familyCombo_ ? familyCombo_->currentIndex() : 0;
}

//-----------------------------------------------------------------------------
QString OffsetsHullDialog::familyName() const
{
  static const char *kNames[] = {"Wigley 经典", "Wigley 平底", "Wigley 尖底",
                                 "Wigley 瘦削", "Lewis U 形", "Lewis V 形", "自定义"};
  const int i = familyCombo_ ? familyCombo_->currentIndex() : 0;
  return QString::fromUtf8(kNames[(i >= 0 && i < 7) ? i : 0]);
}

//-----------------------------------------------------------------------------
double OffsetsHullDialog::familyVolume() const
{
  const double L = lengthSpin_ ? lengthSpin_->value() : 6.0;
  const double B = breadthSpin_ ? breadthSpin_->value() : 1.5;
  const double D = depthSpin_ ? depthSpin_->value() : 1.0;
  const double rho = densitySpin_ ? densitySpin_->value() : 500.0;

  /// ★ V6 的对拍基准**必须按族取**：实测六族的全船体积相对 4LBD/9 相差
  ///   flat +12.4% / sharp −25.0% / fine −18.2% / lewis-u +17.6% / lewis-v −25.0%，
  ///   只有 classic 是 4LBD/9。若一律用 4LBD/9，除经典族外全部会被误判成"公式写错"。
  keship::ShipHullSpec s;
  const int i = familyCombo_ ? familyCombo_->currentIndex() : 0;
  switch (i) {
    case 1: s = keship::ShipHullSpec::WigleyFlat(L, B, D, rho); break;
    case 2: s = keship::ShipHullSpec::WigleySharp(L, B, D, rho); break;
    case 3: s = keship::ShipHullSpec::WigleyFine(L, B, D, rho); break;
    case 4: s = keship::ShipHullSpec::LewisU(L, B, D, rho); break;
    case 5: s = keship::ShipHullSpec::LewisV(L, B, D, rho); break;
    case 6: return -1.0;   ///< 自定义：无解析参考 ⇒ ValidateOffsets 跳过 V6，仅报告体积
    default: s = keship::ShipHullSpec::WigleyClassic(L, B, D, rho); break;
  }
  return keship::ShipHullGenerator(s).hydroProps().volume;
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::onShowFamilyHelp()
{
  const double L = lengthSpin_->value(), B = breadthSpin_->value(), D = depthSpin_->value();
  const double vFull = 4.0 * L * B * D / 9.0;

  auto row = [&](const char *_n, const char *_wl, const char *_sec, double _v) {
    const double err = 100.0 * (_v - vFull) / vFull;
    return QString("<tr><td><b>%1</b></td><td>%2</td><td>%3</td>"
                   "<td align='right'>%4</td><td align='right'>%5%</td></tr>")
        .arg(QString::fromUtf8(_n), QString::fromUtf8(_wl), QString::fromUtf8(_sec))
        .arg(_v, 0, 'f', 4)
        .arg(err, 0, 'f', 1);
  };

  auto vol = [&](int _i) {
    keship::ShipHullSpec s;
    switch (_i) {
      case 1: s = keship::ShipHullSpec::WigleyFlat(L, B, D, 500.0); break;
      case 2: s = keship::ShipHullSpec::WigleySharp(L, B, D, 500.0); break;
      case 3: s = keship::ShipHullSpec::WigleyFine(L, B, D, 500.0); break;
      case 4: s = keship::ShipHullSpec::LewisU(L, B, D, 500.0); break;
      case 5: s = keship::ShipHullSpec::LewisV(L, B, D, 500.0); break;
      default: s = keship::ShipHullSpec::WigleyClassic(L, B, D, 500.0); break;
    }
    return keship::ShipHullGenerator(s).hydroProps().volume;
  };

  QString html = QString(
      "<h3>数学船型族说明</h3>"
      "<p>所有族的半宽都写成 <b>y = B/2 · W(ξ) · S(ζ)</b>，"
      "ξ = 2x/L − 1（沿船长，−1 艉 → +1 艏），ζ = −z/D（沿吃水，0 水线 → 1 龙骨）。"
      "换族 = 换 W 和 S 两个函数。</p>"
      "<table border='1' cellspacing='0' cellpadding='4'>"
      "<tr><th>族</th><th>水线面 W(ξ)</th><th>横剖面 S(ζ)</th>"
      "<th>全船体积 m³</th><th>相对经典族</th></tr>");

  html += row("Wigley 经典", "1 − ξ²", "1 − ζ²", vol(0));
  html += row("Wigley 平底", "1 − ξ²", "1 − ζ⁴（底部变方）", vol(1));
  html += row("Wigley 尖底", "1 − ξ²", "1 − ζ（V 形到底）", vol(2));
  html += row("Wigley 瘦削", "1 − ξ⁴（端部更尖）", "1 − ζ²", vol(3));
  html += row("Lewis U 形", "1 − ξ²", "√(1 − ζ²)（U 形，圆舭）", vol(4));
  html += row("Lewis V 形", "1 − ξ²", "1 − ζ（直线斜剖）", vol(5));
  html += "</table>";

  html += QString(
      "<p><b>怎么用：</b>先在「参考船型族」里选中你要实现的族，"
      "再照上表写 W 与 S。平台的体积校验(V6)会按<b>该族</b>的解析体积对拍。</p>"
      "<p><b>为什么必须选对：</b>六族体积相差很大（上表最后一列），"
      "选错会让正确代码被判成「体积偏差过大」。</p>"
      "<p><b>建议：</b>先做经典族（体积恰为 4LBD/9），通过后再换族体会形状对稳性的影响。</p>"
      "<hr><h3>与课程资料（Wigley 四模型基准）的对照</h3>"
      "<p>课程资料来自 <b>Gothenburg 1980</b> 的 Wigley 基准算例，公式是：</p>"
      "<p style=\'margin-left:20px\'><code>y = (1−z²)(1−x²)(1+a₂x²+a₄x⁴) + α·z²(1−z⁸)(1−x²)⁴</code><br>"
      "a₂=0.2, a₄=0.0；α=1.0 → 模型 I/II（Cm=0.909）；α=0.0 → 模型 III/IV（Cm=0.667）</p>"
      "<p style=\'margin-left:20px;color:#555\'>自检：α=1 时舯剖面 y(0,z)=(1−z²)+z²(1−z⁸)=1−z¹⁰，"
      "∫₀¹(1−z¹⁰)dz=10/11=0.9091 ⇒ Cm=0.909 ✓；"
      "α=0 时 y(0,z)=1−z²，∫=2/3 ⇒ Cm=0.667 ✓。"
      "体积也吻合：L3/B0.3/d0.1875 下 Model I 得 0.0946、Model III 得 0.0780，与资料表 1 一致。</p>"
      "<p><b>三处与平台不同，照着写要注意：</b></p>"
      "<ol>"
      "<li><b>坐标系（必须统一到平台）</b>：平台只用一套 —— "
      "<code>x∈[0,L]</code>（艉 0 → 艏 L）、<code>z∈[−D,0]</code>（水线 0 → 龙骨 −D，<b>向上</b>）、"
      "<code>y</code> 直接是半宽（不是归一化值）。"
      "资料的 <code>x∈[−1,1]</code>、<code>z∈[0,1]</code> <b>向下</b>、<code>y=Y/(B/2)</code> 需换算："
      "<code>x_平台=(x_资料+1)·L/2</code>、<code>z_平台=−z_资料·D</code>、<code>y_平台=y_资料·B/2</code>。"
      "<br>懒得换算？勾选下方的<b>「按资料坐标输入」</b>，平台自动转换。</li>"
      "<li><b>公式多一项</b>：资料的 Model III/IV 含 <code>(1+0.2x²)</code>，"
      "平台经典族没有 ⇒ 同主尺度下体积大 <b>约 4%</b>，超过 V6 的 2% 容差。</li>"
      "<li><b>Model I/II 平台没有对应族</b>：它们的横剖面更丰满（Cm=0.909 vs 平台的 0.667）。</li>"
      "</ol>"
      "<p><b>所以照着资料写时，请把「参考船型族」选为"
      "「自定义 / 课程资料模型（不对拍体积）」</b> —— "
      "此时平台只做 V1~V5（网格/半宽/端点）校验，V6 改为<b>只报告体积不判定</b>。"
      "这不是放过错误：资料模型的解析体积与平台族本就不同，硬套一个参考值只会制造假通过或假失败。</p>"
      "<p><b>资料的真正价值</b>：它附带 Model I~IV 的<b>实测静水阻力</b>（Fn=0.20/0.30），"
      "你造出船后可以拿平台的阻力实验（RP-01/02）与之对照 —— 这是本实验往 CFD 方向延伸的入口。</p>");

  QMessageBox box(this);
  box.setWindowTitle("数学船型族说明");
  box.setTextFormat(Qt::RichText);
  box.setText(html);
  box.setStandardButtons(QMessageBox::Ok);
  box.exec();
}

//-----------------------------------------------------------------------------
bool OffsetsHullDialog::exportTo(const QString &_path, std::string *_err)
{
  if (!hull_) {
    if (_err) *_err = "尚未生成船体（请先运行代码并通过校验）";
    return false;
  }
  if (_path.isEmpty()) {
    if (_err) *_err = "导出路径为空";
    return false;
  }

  const auto &hp = hull_->hydroProps();

  keship::ExpDeliverable d;
  d.expId = "SD-10";
  d.createdAt = keship::NowIso8601();

  d.object["type"] = "offsets_hull";
  d.object["lengthM"] = spec_.L;
  d.object["breadthM"] = spec_.B;
  d.object["depthM"] = spec_.D;
  d.object["densityKgM3"] = spec_.rho;

  d.params["waterDensity"] = kWaterDensity;
  d.params["nx"] = spec_.visNx;
  d.params["nz"] = spec_.visNz;

  d.condition["draftM"] = hp.draft;

  d.metrics["draftM"] = hp.draft;
  d.metrics["volumeD"] = hp.volumeD;
  d.metrics["displacementKg"] = hp.volumeD * kWaterDensity;
  d.metrics["tableVolume"] = report_.volumeFromTable;
  d.metrics["volumeErrPct"] = report_.volumeErrPct;
  d.metrics["GMd"] = hp.GMd;
  d.metrics["KBup"] = hp.KBup;
  d.metrics["BMd"] = hp.BMd;
  d.metrics["AwD"] = hp.AwD;
  d.metrics["nStations"] = report_.nStations;
  d.metrics["nWaterlines"] = report_.nWaterlines;
  d.metrics["estTriangles"] = report_.estTriangles;

  QJsonArray st, wl, hb;
  for (double v : lines_.stations) st.append(v);
  for (double v : lines_.waterlines) wl.append(v);
  for (const auto &row : lines_.halfBreadths) {
    QJsonArray r;
    for (double v : row) r.append(v);
    hb.append(r);
  }
  d.extra["stations"] = st;
  d.extra["waterlines"] = wl;
  d.extra["halfBreadths"] = hb;

  return keship::WriteExpDeliverable(_path.toStdString(), d, _err);
}

//-----------------------------------------------------------------------------
void OffsetsHullDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-10 交付物", "sd10_offsets_hull.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  std::string err;
  if (!exportTo(path, &err)) {
    QMessageBox::warning(this, "导出交付物",
        QString::fromStdString("导出失败：" + err));
    return;
  }
  QMessageBox::information(this, "导出交付物",
      QString("已导出：%1\n\n含主尺度、静水力与完整型值表，可供 HS-05 / HS-09 复现同一船型。").arg(path));
}
