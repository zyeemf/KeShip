/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-06 学生算法面板实现（v2：数据从文件读，代码可泛化到隐藏数据） */

#include "StudentAlgorithmPanel.hh"

#include "StudentCodeRunner.hh"
#include "HiddenCaseGenerator.hh"

#include <QFont>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QVBoxLayout>

namespace {

QString fmt(double _v, int _dec) { return QString::number(_v, 'f', _dec); }

}  // namespace

StudentAlgorithmPanel::StudentAlgorithmPanel(QWidget *_parent) : QWidget(_parent) {
  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);

  auto *title = new QLabel(
      "<b>学生算法</b>：从数据文件读本轮时程 → 稳态段 → 幅值 → RAO", this);
  root->addWidget(title);

  auto *langRow = new QHBoxLayout();
  langRow->addWidget(new QLabel("语言:", this));
  cRadio_ = new QRadioButton("C", this);
  pythonRadio_ = new QRadioButton("Python", this);
  pythonRadio_->setChecked(true);
  langRow->addWidget(cRadio_);
  langRow->addWidget(pythonRadio_);
  langRow->addStretch();
  root->addLayout(langRow);

  codeEdit_ = new QPlainTextEdit(this);
  QFont mono("monospace", 10);
  codeEdit_->setFont(mono);
  codeEdit_->setStyleSheet("background-color: #f5f5dc;");
  codeEdit_->setTabStopDistance(20);
  codeEdit_->setMinimumHeight(260);
  root->addWidget(codeEdit_, 1);

  auto *btnRow = new QHBoxLayout();
  runBtn_ = new QPushButton("运行本轮", this);
  hintBtn_ = new QPushButton("提示", this);
  resetBtn_ = new QPushButton("重置模板", this);
  for (auto *b : {runBtn_, hintBtn_, resetBtn_}) b->setMinimumHeight(28);
  btnRow->addWidget(runBtn_);
  btnRow->addWidget(hintBtn_);
  btnRow->addWidget(resetBtn_);
  btnRow->addStretch();
  root->addLayout(btnRow);

  statusLabel_ = new QLabel("等待记录一轮后运行", this);
  statusLabel_->setWordWrap(true);
  statusLabel_->setStyleSheet("color:#444;");
  root->addWidget(statusLabel_);

  connect(runBtn_, &QPushButton::clicked, this, &StudentAlgorithmPanel::onRun);
  connect(hintBtn_, &QPushButton::clicked, this, &StudentAlgorithmPanel::onShowHint);
  connect(resetBtn_, &QPushButton::clicked, this, &StudentAlgorithmPanel::ResetTemplate);
  connect(pythonRadio_, &QRadioButton::toggled, this,
          &StudentAlgorithmPanel::onLanguageChanged);

  codeEdit_->setObjectName("algoCodeEdit");
  runBtn_->setObjectName("algoRunBtn");
  hintBtn_->setObjectName("algoHintBtn");
  resetBtn_->setObjectName("algoResetBtn");
  pythonRadio_->setObjectName("algoPythonRadio");
  cRadio_->setObjectName("algoCRadio");
  statusLabel_->setObjectName("algoStatusLabel");
}

void StudentAlgorithmPanel::SetRound(const std::vector<keship::WaveSample> &_trail,
                                     double _waveAmp, double _period) {
  trail_ = _trail;
  waveAmp_ = _waveAmp;
  period_ = _period;
  hasRound_ = true;
  // 把本轮数据写入固定路径，学生代码从该文件读取（同一份代码可跑隐藏数据）
  keship::WriteRoundDataCsv(trail_, waveAmp_, keship::kRoundDataPath);
  statusLabel_->setText(
      QString("本轮已载入 %1 点（T = %2 s，波幅 %3 m），数据文件 %4")
          .arg(static_cast<int>(trail_.size()))
          .arg(fmt(period_, 2))
          .arg(fmt(waveAmp_, 2))
          .arg(QString::fromUtf8(keship::kRoundDataPath)));
  ResetTemplate();
}

QString StudentAlgorithmPanel::CurrentCode() const { return codeEdit_->toPlainText(); }
bool StudentAlgorithmPanel::IsPython() const { return pythonRadio_->isChecked(); }

QString StudentAlgorithmPanel::buildTemplate() const {
  if (pythonRadio_->isChecked()) {
    return QString(
        "# ── MS-06：本轮横摇时程由平台写入数据文件 ──\n"
        "# 文件格式：首行 \"wave_amp=<波幅 m>\"，其后每行 \"t_s,roll_deg\"\n"
        "data_path = \"%1\"\n"
        "\n"
        "def load_data(path):\n"
        "    wave_amp = None\n"
        "    t_s, roll_deg = [], []\n"
        "    with open(path) as f:\n"
        "        for line in f:\n"
        "            line = line.strip()\n"
        "            if not line:\n"
        "                continue\n"
        "            if line.startswith(\"wave_amp=\"):\n"
        "                wave_amp = float(line.split(\"=\")[1])\n"
        "                continue\n"
        "            a, b = line.split(\",\")\n"
        "            t_s.append(float(a))\n"
        "            roll_deg.append(float(b))\n"
        "    return t_s, roll_deg, wave_amp\n"
        "\n"
        "def calc_rao(t_s, roll_deg, wave_amp_m):\n"
        "    # TODO:\n"
        "    #   1) 稳态段 = 仿真时间后半段（t >= (t首 + t尾)/2）\n"
        "    #   2) 横摇幅值 = 该段 (max - min) / 2\n"
        "    #   3) RAO = 幅值 / 波幅\n"
        "    return 0.0\n"
        "\n"
        "t_s, roll_deg, wave_amp_m = load_data(data_path)\n"
        "print(f\"{calc_rao(t_s, roll_deg, wave_amp_m):.4f}\")\n")
        .arg(QString::fromUtf8(keship::kRoundDataPath));
  }
  return QString(
      "#include <stdio.h>\n"
      "\n"
      "#define N 4096\n"
      "\n"
      "int main() {\n"
      "  /* 数据文件：首行 \"wave_amp=<m>\"，其后每行 \"t_s,roll_deg\" */\n"
      "  const char *data_path = \"%1\";\n"
      "  double wave_amp_m = 0.0;\n"
      "  static double t_s[N], roll_deg[N];\n"
      "  int n = 0;\n"
      "  FILE *f = fopen(data_path, \"r\");\n"
      "  if (!f) { printf(\"0.0000\\n\"); return 0; }\n"
      "  char line[256];\n"
      "  while (fgets(line, sizeof(line), f)) {\n"
      "    double a, b;\n"
      "    if (sscanf(line, \"wave_amp=%lf\", &wave_amp_m) == 1) continue;\n"
      "    if (sscanf(line, \"%lf,%lf\", &a, &b) == 2 && n < N) {\n"
      "      t_s[n] = a; roll_deg[n] = b; ++n;\n"
      "    }\n"
      "  }\n"
      "  fclose(f);\n"
      "  /* TODO:\n"
      "     1) 稳态段 = 仿真时间后半段（t >= (t首 + t尾)/2）\n"
      "     2) 幅值 = 该段 (max - min) / 2\n"
      "     3) RAO = 幅值 / 波幅 */\n"
      "  double rao = 0.0;\n"
      "  printf(\"%.4f\\n\", rao);\n"
      "  return 0;\n"
      "}\n")
      .arg(QString::fromUtf8(keship::kRoundDataPath));
}

void StudentAlgorithmPanel::ResetTemplate() {
  if (!hasRound_) {
    codeEdit_->setPlainText("# 请先记录一轮数据（开始记录 → 停止 → 记录本次结果）\n");
    return;
  }
  codeEdit_->setPlainText(buildTemplate());
}

void StudentAlgorithmPanel::onLanguageChanged() { ResetTemplate(); }

void StudentAlgorithmPanel::onRun() {
  if (!hasRound_) {
    statusLabel_->setText("尚无本轮数据：请先记录一轮。");
    return;
  }
  const QString code = codeEdit_->toPlainText();
  if (code.trimmed().isEmpty()) return;
  const QString lang = pythonRadio_->isChecked() ? "python" : "c";
  statusLabel_->setText("运行中…");
  runBtn_->setEnabled(false);
  keship::StudentCodeRunner::RunAsync(
      this, lang, code, [this](const keship::StudentRunResult &_r) {
        runBtn_->setEnabled(true);
        if (!_r.ok || !_r.valueOk) {
          statusLabel_->setText(
              _r.ok ? "无法解析输出数值（约定 print 一个数）"
                    : ("运行出错：" + _r.stderrText.left(200)));
          emit runFailed(_r.stderrText);
          return;
        }
        statusLabel_->setText(QString("本轮学生 RAO = %1 °/m").arg(fmt(_r.value, 4)));
        emit studentRaoReady(_r.value);
      });
}

void StudentAlgorithmPanel::onShowHint() {
  QMessageBox::information(this, "提示",
      "<b>与平台算法一致的三步：</b><br>"
      "1. <b>取稳态段</b>：仿真时间后半段（t ≥ (t首+t尾)/2），排除起振瞬态；<br>"
      "2. <b>求幅值</b>：该段 (max − min) / 2；<br>"
      "3. <b>算 RAO</b>：幅值 ÷ 波幅。<br><br>"
      "数据从数据文件读取（格式见模板首行注释）——<b>代码里不要写死任何数值</b>，"
      "验收时会用未公开的数据跑你的代码。");
}
