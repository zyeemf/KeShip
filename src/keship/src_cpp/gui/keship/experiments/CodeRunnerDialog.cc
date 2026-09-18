/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase D: 学生编程模块对话框实现 */

#include "CodeRunnerDialog.hh"
#include "../shared/UiTheme.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QProcess>
#include <QFont>
#include <QCoreApplication>
#include <QDir>

#include <ament_index_cpp/get_package_share_directory.hpp>

#include <sstream>
#include <cmath>
#include <iostream>

using keship::ShipHullSpec;

static ShipHullSpec mapType(int _idx, double L, double B, double D, double rho) {
  switch (_idx) {
    case 0: return ShipHullSpec::WigleyClassic(L, B, D, rho);
    case 1: return ShipHullSpec::WigleyFlat(L, B, D, rho);
    case 2: return ShipHullSpec::WigleySharp(L, B, D, rho);
    case 3: return ShipHullSpec::WigleyFine(L, B, D, rho);
    case 4: return ShipHullSpec::LewisU(L, B, D, rho);
    case 5: return ShipHullSpec::LewisV(L, B, D, rho);
    default: return ShipHullSpec::WigleyClassic(L, B, D, rho);
  }
}

CodeRunnerDialog::CodeRunnerDialog(QWidget *_parent)
    : QDialog(_parent), process_(nullptr) {
  setWindowTitle("编程练习：梯形积分法计算排水体积");
  setModal(false); resize(900, 700);
  setAttribute(Qt::WA_DeleteOnClose);
  setupUI();
  onTypeChanged();
  updateTemplate();  // 初始显示代码模板
}

void CodeRunnerDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  auto *desc = new QLabel(
    "<b>编程练习</b> — 用梯形积分法编写程序计算排水体积，与标准答案对比", this);
  desc->setWordWrap(true); mainLayout->addWidget(desc);

  // 参数行
  auto *paramRow = new QHBoxLayout();
  typeCombo_ = new QComboBox(this);
  typeCombo_->addItems({"Wigley 经典", "Wigley 平底", "Wigley 尖舭",
                         "Wigley 瘦削", "Lewis U", "Lewis V"});
  paramRow->addWidget(new QLabel("船型:", this));
  paramRow->addWidget(typeCombo_);

  lengthSpin_ = new QDoubleSpinBox(this);
  lengthSpin_->setRange(2,20); lengthSpin_->setValue(6); lengthSpin_->setSuffix("m"); lengthSpin_->setDecimals(1);
  paramRow->addWidget(new QLabel("L:", this)); paramRow->addWidget(lengthSpin_);
  breadthSpin_ = new QDoubleSpinBox(this);
  breadthSpin_->setRange(0.5,5); breadthSpin_->setValue(1.5); breadthSpin_->setSuffix("m"); breadthSpin_->setDecimals(2);
  paramRow->addWidget(new QLabel("B:", this)); paramRow->addWidget(breadthSpin_);
  depthSpin_ = new QDoubleSpinBox(this);
  depthSpin_->setRange(0.3,3); depthSpin_->setValue(1.0); depthSpin_->setSuffix("m"); depthSpin_->setDecimals(2);
  paramRow->addWidget(new QLabel("D:", this)); paramRow->addWidget(depthSpin_);
  densitySpin_ = new QDoubleSpinBox(this);
  densitySpin_->setRange(100,950); densitySpin_->setValue(500); densitySpin_->setSuffix("kg/m³"); densitySpin_->setDecimals(0);
  paramRow->addWidget(new QLabel("ρ:", this)); paramRow->addWidget(densitySpin_);
  draftSpin_ = new QDoubleSpinBox(this);
  draftSpin_->setRange(0.1,3); draftSpin_->setValue(0.5); draftSpin_->setSuffix(" m"); draftSpin_->setDecimals(3);
  paramRow->addWidget(new QLabel("吃水:", this)); paramRow->addWidget(draftSpin_);
  paramRow->addStretch();
  mainLayout->addLayout(paramRow);

  // 语言 + 答案
  auto *langRow = new QHBoxLayout();
  langRow->addWidget(new QLabel("语言:", this));
  cRadio_ = new QRadioButton("C", this); cRadio_->setChecked(true);
  pythonRadio_ = new QRadioButton("Python", this);
  langRow->addWidget(cRadio_); langRow->addWidget(pythonRadio_);
  langRow->addSpacing(20);
  answerLabel_ = new QLabel(this);
  answerLabel_->setStyleSheet("font-weight:bold;");
  langRow->addWidget(answerLabel_);
  langRow->addStretch();
  mainLayout->addLayout(langRow);

  // 代码编辑区
  codeEdit_ = new QPlainTextEdit(this);
  QFont monoFont("monospace", 11);
  codeEdit_->setFont(monoFont);
  codeEdit_->setStyleSheet("background-color: #f5f5dc;");  // 淡黄色背景
  codeEdit_->setTabStopDistance(20);
  codeEdit_->setMinimumHeight(250);
  mainLayout->addWidget(codeEdit_, 2);

  // 按钮行
  auto *btnRow = new QHBoxLayout();
  runBtn_ = new QPushButton("运行代码", this); runBtn_->setMinimumHeight(30);
  hintBtn_ = new QPushButton("查看提示", this); hintBtn_->setMinimumHeight(30);
  resetBtn_ = new QPushButton("重置模板", this); resetBtn_->setMinimumHeight(30);
  btnRow->addWidget(runBtn_); btnRow->addWidget(hintBtn_);
  btnRow->addWidget(resetBtn_); btnRow->addStretch();
  mainLayout->addLayout(btnRow);

  // 输出区
  outputEdit_ = new QPlainTextEdit(this);
  outputEdit_->setReadOnly(true);
  outputEdit_->setMaximumHeight(100);
  outputEdit_->setFont(monoFont);
  mainLayout->addWidget(outputEdit_);

  // 结果行
  auto *resRow = new QHBoxLayout();
  resultLabel_ = new QLabel("结果: —", this);
  resultLabel_->setStyleSheet("font-size:14px; font-weight:bold;");
  verdictLabel_ = new QLabel("", this);
  verdictLabel_->setStyleSheet("font-size:14px; font-weight:bold;");
  resRow->addWidget(resultLabel_); resRow->addWidget(verdictLabel_);
  resRow->addStretch();
  mainLayout->addLayout(resRow);

  // 连接
  connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &CodeRunnerDialog::onTypeChanged);
  connect(lengthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &CodeRunnerDialog::onTypeChanged);
  connect(breadthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &CodeRunnerDialog::onTypeChanged);
  connect(depthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &CodeRunnerDialog::onTypeChanged);
  connect(densitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &CodeRunnerDialog::onTypeChanged);
  connect(draftSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &CodeRunnerDialog::onTypeChanged);
  connect(pythonRadio_, &QRadioButton::toggled, this, &CodeRunnerDialog::onLanguageChanged);
  connect(runBtn_, &QPushButton::clicked, this, &CodeRunnerDialog::onRunCode);
  connect(hintBtn_, &QPushButton::clicked, this, &CodeRunnerDialog::onShowHint);
  connect(resetBtn_, &QPushButton::clicked, this, &CodeRunnerDialog::onResetTemplate);
}

// ═══════════════════════════════════════════════════════════
// 模板生成
// ═══════════════════════════════════════════════════════════

QString CodeRunnerDialog::pythonPreamble() const {
  keship::ShipHullGenerator hull(spec_);
  auto &lines = hull.lines();
  int Nx = (int)lines.stations.size();
  double L = spec_.L, D = spec_.D;
  double dx = L / (Nx - 1);

  // 数据直接嵌入代码（学生写完整程序）
  double draft = draftSpin_->value();
  QString areasStr = "[";
  for (int i = 0; i < Nx; ++i) {
    double area = 0.0;
    for (size_t j = 0; j + 1 < lines.waterlines.size(); ++j) {
      double z1 = lines.waterlines[j], z2 = lines.waterlines[j+1];
      if (z2 < -draft) break;
      double y1 = lines.halfBreadths[i][j], y2 = lines.halfBreadths[i][j+1];
      if (z1 < -draft) {
        double t = (-draft - z1) / (z2 - z1);
        y1 = lines.halfBreadths[i][j] * (1-t) + y2 * t;
        z1 = -draft;
      }
      area += (y1 + y2) * 0.5 * std::abs(z2 - z1);
    }
    area *= 2.0;
    if (i > 0) areasStr += ", ";
    areasStr += QString::number(area, 'f', 6);
  }
  areasStr += "]";

  QString stStr = "[";
  for (int i = 0; i < Nx; ++i) {
    if (i > 0) stStr += ", ";
    stStr += QString::number(lines.stations[i], 'f', 6);
  }
  stStr += "]";

  return QString(
    "# stations (m): 站位 x 坐标\n"
    "stations = %1\n"
    "# areas (m^2):   各站位横剖面面积\n"
    "areas    = %2\n"
    "N = %3\n"
    "dx = %4\n"
    "\n"
    "def calculate_displacement(stations, areas, N, dx):\n"
    "    V = 0.0\n"
    "    # TODO: 用梯形积分 V = sum((areas[i]+areas[i+1])/2*dx)\n"
    "\n"
    "    return V\n"
    "\n"
    "result = calculate_displacement(stations, areas, N, dx)\n"
    "print(f\"{result:.6f}\")\n"
  ).arg(stStr, areasStr).arg(Nx).arg(dx, 0, 'f', 6);
}

QString CodeRunnerDialog::cPreamble() const {
  keship::ShipHullGenerator hull(spec_);
  auto &lines = hull.lines();
  int Nx = (int)lines.stations.size();
  double L = spec_.L, D = spec_.D;
  double dx = L / (Nx - 1);
  double draft = draftSpin_->value();

  QString stStr, arStr;
  for (int i = 0; i < Nx; ++i) {
    if (i > 0) { stStr += ", "; arStr += ", "; }
    stStr += QString::number(lines.stations[i], 'f', 6);
    double area = 0.0;
    for (size_t j = 0; j + 1 < lines.waterlines.size(); ++j) {
      double z1 = lines.waterlines[j], z2 = lines.waterlines[j+1];
      if (z2 < -draft) break;
      double y1 = lines.halfBreadths[i][j], y2 = lines.halfBreadths[i][j+1];
      if (z1 < -draft) {
        double t = (-draft - z1) / (z2 - z1);
        y1 = lines.halfBreadths[i][j] * (1-t) + y2 * t;
        z1 = -draft;
      }
      area += (y1 + y2) * 0.5 * std::abs(z2 - z1);
    }
    area *= 2.0;
    arStr += QString::number(area, 'f', 6);
  }

  return QString(
    "#include <stdio.h>\n"
    "#define N %1\n"
    "\n"
    "/* stations (m): 站位 x 坐标 */\n"
    "double stations[N] = {%2};\n"
    "/* areas (m^2):   各站位横剖面面积 */\n"
    "double areas[N]    = {%3};\n"
    "double dx = %4;\n"
    "\n"
    "int main() {\n"
    "    double V = 0.0;\n"
    "    /* TODO: V = sum((areas[i] + areas[i+1]) / 2.0 * dx); */\n"
    "\n"
    "    printf(\"%.6f\\n\", V);\n"
    "    return 0;\n"
    "}\n"
  ).arg(Nx).arg(stStr, arStr).arg(dx, 0, 'f', 6);
}

void CodeRunnerDialog::updateTemplate() {
  if (cRadio_->isChecked())
    codeEdit_->setPlainText(cPreamble());
  else
    codeEdit_->setPlainText(pythonPreamble());
}

// ═══════════════════════════════════════════════════════════
// 事件处理
// ═══════════════════════════════════════════════════════════

void CodeRunnerDialog::onTypeChanged() {
  double L = lengthSpin_->value(), B = breadthSpin_->value();
  double D = depthSpin_->value(), rho = densitySpin_->value();
  spec_ = mapType(typeCombo_->currentIndex(), L, B, D, rho);

  keship::ShipHullGenerator hull(spec_);
  double draft = draftSpin_->value();
  standardVolume_ = hull.volumeAtDraft(draft);
  answerLabel_->setText(QString("标准答案 ∇ = %1 m³")
      .arg(standardVolume_, 0, 'f', 3));
  // 不覆盖学生已写的代码——仅更新答案显示
}

void CodeRunnerDialog::onLanguageChanged() {
  updateTemplate();  // 切换语言时更新模板（合理覆盖）
}

void CodeRunnerDialog::onResetTemplate() { updateTemplate(); }

void CodeRunnerDialog::onShowHint() {
  QMessageBox::information(this, "梯形积分法提示",
    "<b>梯形积分公式：</b><br>"
    "V ≈ Σ (A[i] + A[i+1]) / 2.0 × dx<br><br>"
    "<b>伪代码：</b><br>"
    "<pre>"
    "V = 0.0\n"
    "for i = 0 to N-2:\n"
    "    V += (areas[i] + areas[i+1]) / 2.0 * dx\n"
    "</pre>"
    "<b>说明：</b><br>"
    "N 是数据点个数 (Python) 或预定义宏 (C)<br>"
    "dx 是相邻站位的间距<br>"
    "循环从第 0 个到第 N-2 个（共 N-1 个小区间）");
}

void CodeRunnerDialog::onRunCode() {
  QString code = codeEdit_->toPlainText();
  if (code.trimmed().isEmpty()) return;

  QString lang = pythonRadio_->isChecked() ? "python" : "c";

  // 通过 ament_index 找 share/keship/scripts/keship_code_runner.py
  QString runnerPath;
  try {
    std::string shareDir = ament_index_cpp::get_package_share_directory("keship");
    runnerPath = QString::fromStdString(shareDir) + "/scripts/keship_code_runner.py";
  } catch (...) {
    runnerPath = QCoreApplication::applicationDirPath()
        + "/../../src/keship/scripts/keship_code_runner.py";
  }

  if (process_) { process_->kill(); delete process_; }
  process_ = new QProcess(this);
  connect(process_, QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),
          this, &CodeRunnerDialog::onProcessFinished);

  outputEdit_->clear();
  outputEdit_->appendPlainText("运行中...");
  runBtn_->setEnabled(false);

  process_->start("python3", {runnerPath, lang, code});
}

void CodeRunnerDialog::onProcessFinished(int _exitCode, QProcess::ExitStatus _status) {
  runBtn_->setEnabled(true);
  QString stdoutStr = process_->readAllStandardOutput();
  QString stderrStr = process_->readAllStandardError();
  outputEdit_->clear();
  if (!stderrStr.isEmpty()) outputEdit_->appendPlainText(stderrStr);
  if (!stdoutStr.isEmpty()) outputEdit_->appendPlainText(stdoutStr);

  // 解析 JSON 结果
  QJsonDocument doc = QJsonDocument::fromJson(stdoutStr.toUtf8());
  if (doc.isObject()) {
    QJsonObject obj = doc.object();
    if (obj["ok"].toBool()) {
      // 解析学生输出的浮点数
      QString valStr = obj["stdout"].toString();
      bool ok = false;
      double studentVal = valStr.toDouble(&ok);
      if (ok && standardVolume_ > 1e-6) {
        double err = std::abs(studentVal - standardVolume_) / standardVolume_ * 100;
        resultLabel_->setText(QString("结果: %1 m³  误差: %2%")
            .arg(studentVal, 0, 'f', 4).arg(err, 0, 'f', 2));
        if (err < 5.0) {
          verdictLabel_->setText("通过");
          verdictLabel_->setStyleSheet(QString("font-size:14px; font-weight:bold; color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
        } else if (err < 10.0) {
          verdictLabel_->setText("接近");
          verdictLabel_->setStyleSheet(QString("font-size:14px; font-weight:bold; color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));
        } else {
          verdictLabel_->setText("不通过");
          verdictLabel_->setStyleSheet(QString("font-size:14px; font-weight:bold; color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kFail)));
        }
      } else {
        resultLabel_->setText("无法解析输出数值");
        verdictLabel_->setText("");
      }
    } else {
      resultLabel_->setText("运行出错");
      verdictLabel_->setText("");
    }
  } else {
    resultLabel_->setText("无法解析运行结果");
    verdictLabel_->setText("");
  }
}

void CodeRunnerDialog::updateStatus() {
  if (!resultLabel_->text().isEmpty())
    resultLabel_->setText("结果: —");
  if (!verdictLabel_->text().isEmpty())
    verdictLabel_->setText("");
}
