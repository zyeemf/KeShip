/*
 * Copyright (C) 2024-2026 KeShip Team
 */
#include "OffsetsHullDialog.hh"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <QButtonGroup>
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
  formR->addRow("船体密度 rho：", densitySpin_);

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
  pythonRadio_->setChecked(true);
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

  lines_ = d;
  report_ = keship::ValidateOffsets(d, L, B, D, keship::WigleyExactVolume(L, B, D));

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
