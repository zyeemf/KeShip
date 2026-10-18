/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-06 RAO 编程计算实践实现：内置样例 + 学生代码 + 内置算法对比 */

#include "MS06CodePracticeDialog.hh"

#include "../shared/StudentCodeRunner.hh"
#include "../shared/UiTheme.hh"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

#include <cmath>
#include <iostream>

namespace {

/// 内置样例：2026-10-18 干净实测（ke0812_1，T=5 s 横浪，波幅 0.5 m，软系泊）
/// 来源 data/test/rao_sweep_v2_20261018/rao_T5_r1.csv，60 s 时程等间隔抽 120 点。
/// 内置参考值由平台算法（WaveResponseCore::computeMetrics）在该样例上现算，
/// 不硬编码——保证「同数据、同口径、只差实现」。
constexpr int kSampleN = 120;
const double kSampleT[kSampleN] = {
  0.2, 0.616, 1.028, 1.436, 1.848, 2.252, 2.664, 3.076, 3.484, 3.892,
  4.3, 4.712, 5.124, 5.532, 5.944, 6.352, 6.764, 7.176, 7.588, 8,
  8.408, 8.816, 9.22, 9.632, 10.044, 10.452, 10.856, 11.264, 11.676, 12.084,
  12.5, 12.912, 13.32, 13.724, 14.132, 14.54, 14.948, 15.356, 15.768, 16.172,
  16.58, 16.984, 17.392, 17.804, 18.216, 18.624, 19.032, 19.436, 19.848, 20.252,
  20.664, 21.068, 21.48, 21.888, 22.296, 22.7, 23.104, 23.512, 23.916, 24.328,
  24.736, 25.148, 25.56, 25.972, 26.376, 26.784, 27.196, 27.604, 28.016, 28.428,
  28.84, 29.256, 29.668, 30.076, 30.488, 30.9, 31.312, 31.724, 32.128, 32.532,
  32.94, 33.352, 33.76, 34.172, 34.584, 34.996, 35.404, 35.808, 36.22, 36.628,
  37.032, 37.444, 37.856, 38.268, 38.676, 39.08, 39.492, 39.908, 40.324, 40.732,
  41.14, 41.552, 41.956, 42.364, 42.772, 43.184, 43.596, 44.004, 44.412, 44.828,
  45.24, 45.652, 46.064, 46.476, 46.88, 47.288, 47.696, 48.108, 48.52, 48.936,
};
const double kSampleRollDeg[kSampleN] = {
  -0.0017, 6.9139, 3.9992, -0.6153, -4.53, -5.4641, -5.831, -3.6615, -1.7043, 0.5075,
  3.0048, 4.7928, 5.6345, 4.7375, 2.4558, -0.5628, -3.254, -4.8585, -5.249, -4.3741,
  -2.4887, 0.0281, 2.7434, 4.769, 5.6218, 4.8139, 2.938, 0.0089, -2.7665, -4.5721,
  -5.2719, -4.5759, -2.8994, -0.2773, 2.3821, 4.6868, 5.5752, 5.147, 3.3164, 0.4611,
  -2.3881, -4.3363, -5.23, -4.8484, -3.1956, -0.9406, 1.8721, 4.2413, 5.4555, 5.3681,
  3.7088, 0.9529, -1.9604, -3.9125, -5.1386, -5.0634, -3.6934, -1.5708, 1.2281, 3.7598,
  5.2454, 5.5317, 4.1203, 1.5084, -1.0442, -3.5486, -5.0031, -5.1726, -4.0562, -1.9643,
  0.6684, 3.2163, 5.1927, 5.5713, 4.423, 2.0893, -0.879, -3.3273, -4.8587, -5.2324,
  -4.278, -2.2099, 0.3661, 3.0492, 4.9993, 5.6104, 4.8539, 2.6564, -0.2814, -2.8679,
  -4.6209, -5.2635, -4.6613, -2.8644, -0.2417, 2.5064, 4.6774, 5.6011, 4.9465, 3.1992,
  0.3357, -2.3622, -4.3995, -5.2481, -4.8239, -3.2626, -0.7438, 1.9283, 4.292, 5.5016,
  5.2758, 3.387, 0.8486, -2.1672, -4.1276, -5.194, -4.9012, -3.5213, -1.217, 1.7182,
};

QString fmt(double _v, int _dec) { return QString::number(_v, 'f', _dec); }

}  // namespace

MS06CodePracticeDialog::MS06CodePracticeDialog(QWidget *_parent) : QDialog(_parent) {
  setWindowTitle("MS-06 RAO 编程计算实践");
  setModal(false);
  resize(920, 740);
  setAttribute(Qt::WA_DeleteOnClose);
  setupUI();
  loadBuiltinSample();
  updateBuiltinResult();
  updateTemplate();
}

void MS06CodePracticeDialog::setupUI() {
  auto *root = new QVBoxLayout(this);

  auto *desc = new QLabel(
      "<b>MS-06 · RAO 编程计算实践</b>：用平台给出的实测数据编写程序计算横摇 RAO，"
      "与<b>内置算法</b>（与 MS-03 同一实现，末半段口径）的结果对比。", this);
  desc->setWordWrap(true);
  root->addWidget(desc);

  auto *row1 = new QHBoxLayout();
  row1->addWidget(new QLabel("数据源:", this));
  dataCombo_ = new QComboBox(this);
  dataCombo_->addItem("内置样例（T = 5 s 横浪，波幅 0.5 m）");
  dataCombo_->addItem("用我在 MS-03 测的数据（P2 接入）");
  dataCombo_->setEnabled(true);
  // P2 前只允许内置样例：把第二项置灰
  {
    auto *model = dataCombo_->model();
    model->setData(model->index(1, 0), 0, Qt::UserRole - 1);
  }
  row1->addWidget(dataCombo_);

  row1->addSpacing(16);
  row1->addWidget(new QLabel("任务档:", this));
  taskCombo_ = new QComboBox(this);
  taskCombo_->addItem("A 档：给横摇幅值，算 RAO");
  taskCombo_->addItem("B 档：给时程数据，自己求幅值再算 RAO");
  row1->addWidget(taskCombo_);

  row1->addSpacing(16);
  row1->addWidget(new QLabel("语言:", this));
  cRadio_ = new QRadioButton("C", this);
  pythonRadio_ = new QRadioButton("Python", this);
  pythonRadio_->setChecked(true);
  row1->addWidget(cRadio_);
  row1->addWidget(pythonRadio_);
  row1->addStretch();
  root->addLayout(row1);

  builtinLabel_ = new QLabel(this);
  builtinLabel_->setWordWrap(true);
  builtinLabel_->setStyleSheet("color:#333; background:#f6f6fa; padding:4px;");
  root->addWidget(builtinLabel_);

  codeEdit_ = new QPlainTextEdit(this);
  QFont mono("monospace", 11);
  codeEdit_->setFont(mono);
  codeEdit_->setStyleSheet("background-color: #f5f5dc;");
  codeEdit_->setTabStopDistance(20);
  codeEdit_->setMinimumHeight(260);
  root->addWidget(codeEdit_, 2);

  auto *btnRow = new QHBoxLayout();
  runBtn_ = new QPushButton("运行代码", this);
  hintBtn_ = new QPushButton("查看提示", this);
  resetBtn_ = new QPushButton("重置模板", this);
  for (auto *b : {runBtn_, hintBtn_, resetBtn_}) b->setMinimumHeight(30);
  btnRow->addWidget(runBtn_);
  btnRow->addWidget(hintBtn_);
  btnRow->addWidget(resetBtn_);
  btnRow->addStretch();
  root->addLayout(btnRow);

  outputEdit_ = new QPlainTextEdit(this);
  outputEdit_->setReadOnly(true);
  outputEdit_->setMaximumHeight(110);
  outputEdit_->setFont(mono);
  root->addWidget(outputEdit_);

  auto *resRow = new QHBoxLayout();
  resultLabel_ = new QLabel("结果: —", this);
  resultLabel_->setStyleSheet("font-size:14px; font-weight:bold;");
  verdictLabel_ = new QLabel("", this);
  verdictLabel_->setStyleSheet("font-size:14px; font-weight:bold;");
  resRow->addWidget(resultLabel_);
  resRow->addWidget(verdictLabel_);
  resRow->addStretch();
  root->addLayout(resRow);

  connect(taskCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &MS06CodePracticeDialog::onTaskChanged);
  connect(pythonRadio_, &QRadioButton::toggled,
          this, &MS06CodePracticeDialog::onLanguageChanged);
  connect(runBtn_, &QPushButton::clicked, this, &MS06CodePracticeDialog::onRunCode);
  connect(hintBtn_, &QPushButton::clicked, this, &MS06CodePracticeDialog::onShowHint);
  connect(resetBtn_, &QPushButton::clicked, this, &MS06CodePracticeDialog::onResetTemplate);
}

void MS06CodePracticeDialog::loadBuiltinSample() {
  sample_.clear();
  for (int i = 0; i < kSampleN; ++i) {
    keship::WaveSample s;
    s.t = kSampleT[i];
    s.rollDeg = kSampleRollDeg[i];
    sample_.push_back(s);
  }
}

void MS06CodePracticeDialog::updateBuiltinResult() {
  // 内置算法 = 平台实现（与 MS-03 表内同一函数、同一口径）
  const auto m = keship::WaveResponseCore::computeMetrics(sample_, waveAmp_, wavePeriod_);
  builtinRao_ = m.rollRAO;
  builtinRollAmp_ = m.rollAmp;
  builtinLabel_->setText(
      QString("内置算法（与 MS-03 同一实现）：横摇稳态幅值 %1 °，"
              "RAO = %2 °/m（末半段口径，波幅 %3 m）")
          .arg(fmt(builtinRollAmp_, 4), fmt(builtinRao_, 4), fmt(waveAmp_, 2)));
}

QString MS06CodePracticeDialog::buildTemplate() const {
  const bool py = pythonRadio_->isChecked();
  const bool advanced = (taskCombo_->currentIndex() == 1);
  const QString head = QString(
      "# ── MS-06 内置样例：T = %1 s 横浪，波幅 %2 m（平台实测）──\n")
      .arg(fmt(wavePeriod_, 1), fmt(waveAmp_, 2));

  if (!advanced) {
    if (py) {
      return head +
          "# 平台已从实测时程求出横摇稳态幅值（末半段口径）\n" +
          QString("roll_amp_deg = %1   # 横摇稳态幅值 (°)\n").arg(fmt(builtinRollAmp_, 4)) +
          QString("wave_amp_m   = %1   # 波浪波幅 (m)\n").arg(fmt(waveAmp_, 2)) +
          "\n"
          "def calc_rao(roll_amp_deg, wave_amp_m):\n"
          "    # TODO: RAO = 横摇幅值(deg) / 波浪波幅(m)\n"
          "    return 0.0\n"
          "\n"
          "print(f\"{calc_rao(roll_amp_deg, wave_amp_m):.4f}\")\n";
    }
    return QString(
        "#include <stdio.h>\n"
        "\n"
        "int main() {\n"
        "  /* MS-06 内置样例：T = %1 s 横浪，波幅 %2 m */\n"
        "  double roll_amp_deg = %3;  /* 横摇稳态幅值（末半段，平台已求出） */\n"
        "  double wave_amp_m   = %4;  /* 波浪波幅 */\n"
        "  double rao = 0.0;          /* TODO: RAO = 横摇幅值 / 波幅 */\n"
        "  printf(\"%%.4f\\n\", rao);\n"
        "  return 0;\n"
        "}\n")
        .arg(fmt(wavePeriod_, 1), fmt(waveAmp_, 2), fmt(builtinRollAmp_, 4),
             fmt(waveAmp_, 2));
  }

  // B 档：给 60 s 时程（120 点），学生自己取稳态段、求幅值、算 RAO
  QString ts, rs;
  for (int i = 0; i < kSampleN; ++i) {
    if (i) { ts += ", "; rs += ", "; }
    ts += fmt(kSampleT[i], 3);
    rs += fmt(kSampleRollDeg[i], 4);
  }
  if (py) {
    return head +
        "# 横摇时程（120 点，t 为仿真秒，roll 为横摇角 °）\n" +
        QString("t_s      = [%1]\n").arg(ts) +
        QString("roll_deg = [%1]\n").arg(rs) +
        QString("wave_amp_m = %1\n").arg(fmt(waveAmp_, 2)) +
        "\n"
        "def calc_rao(t_s, roll_deg, wave_amp_m):\n"
        "    # TODO:\n"
        "    #   1) 稳态段 = 仿真时间后半段（t >= (t首 + t尾)/2），与平台口径一致\n"
        "    #   2) 横摇幅值 = 该段 (max - min) / 2\n"
        "    #   3) RAO = 幅值 / 波幅\n"
        "    return 0.0\n"
        "\n"
        "print(f\"{calc_rao(t_s, roll_deg, wave_amp_m):.4f}\")\n";
  }
  return QString(
      "#include <stdio.h>\n"
      "\n"
      "#define N %1\n"
      "double t_s[N]      = {%2};\n"
      "double roll_deg[N] = {%3};\n"
      "double wave_amp_m  = %4;\n"
      "\n"
      "int main() {\n"
      "  /* TODO:\n"
      "     1) 稳态段 = 仿真时间后半段（t >= (t首 + t尾)/2）\n"
      "     2) 幅值 = 该段 (max - min) / 2\n"
      "     3) RAO = 幅值 / 波幅 */\n"
      "  double rao = 0.0;\n"
      "  printf(\"%%.4f\\n\", rao);\n"
      "  return 0;\n"
      "}\n")
      .arg(QString::number(kSampleN), ts, rs, fmt(waveAmp_, 2));
}

void MS06CodePracticeDialog::updateTemplate() {
  codeEdit_->setPlainText(buildTemplate());
}

void MS06CodePracticeDialog::onTaskChanged() { updateTemplate(); }

void MS06CodePracticeDialog::onLanguageChanged() { updateTemplate(); }

void MS06CodePracticeDialog::onResetTemplate() { updateTemplate(); }

void MS06CodePracticeDialog::onShowHint() {
  const bool advanced = (taskCombo_->currentIndex() == 1);
  if (!advanced) {
    QMessageBox::information(this, "提示（A 档）",
        "<b>RAO 的定义：</b>单位波幅引起的响应幅值。<br><br>"
        "<b>本题：</b>RAO = 横摇稳态幅值(°) ÷ 波浪波幅(m)，单位 °/m。<br><br>"
        "幅值已由平台从实测时程求出（末半段口径），你只需完成除法。");
    return;
  }
  QMessageBox::information(this, "提示（B 档）",
      "<b>与平台算法一致的三步：</b><br>"
      "1. <b>取稳态段</b>：仿真时间后半段（t ≥ (t首+t尾)/2），排除起振瞬态；<br>"
      "2. <b>求幅值</b>：该段 (max − min) / 2；<br>"
      "3. <b>算 RAO</b>：幅值 ÷ 波幅。<br><br>"
      "注意：若用整段数据，会把首段瞬态算进去，幅值偏大、误差超 10%。");
}

void MS06CodePracticeDialog::onRunCode() {
  const QString code = codeEdit_->toPlainText();
  if (code.trimmed().isEmpty()) return;
  const QString lang = pythonRadio_->isChecked() ? "python" : "c";

  outputEdit_->clear();
  outputEdit_->appendPlainText("运行中...");
  runBtn_->setEnabled(false);
  resultLabel_->setText("结果: —");
  verdictLabel_->setText("");

  keship::StudentCodeRunner::RunAsync(
      this, lang, code, [this](const keship::StudentRunResult &_r) {
        runBtn_->setEnabled(true);
        outputEdit_->clear();
        if (!_r.stderrText.isEmpty()) outputEdit_->appendPlainText(_r.stderrText);
        if (!_r.stdoutText.isEmpty()) outputEdit_->appendPlainText(_r.stdoutText);
        if (!_r.ok || !_r.valueOk) {
          resultLabel_->setText(_r.ok ? "无法解析输出数值（约定 print 一个数）"
                                      : "运行出错");
          return;
        }
        const double err = keship::StudentCodeRunner::ErrorPct(_r.value, builtinRao_);
        resultLabel_->setText(
            QString("学生 RAO = %1 °/m ｜ 内置 RAO = %2 °/m ｜ 误差 %3%")
                .arg(fmt(_r.value, 4), fmt(builtinRao_, 4), fmt(err, 2)));
        const int v = keship::StudentCodeRunner::Verdict(_r.value, builtinRao_);
        if (v == 0) {
          verdictLabel_->setText("通过（<5%）");
          verdictLabel_->setStyleSheet(
              "font-size:14px; font-weight:bold; color:"
              + UiTheme::statusColor(UiTheme::Status::kOk));
        } else if (v == 1) {
          verdictLabel_->setText("接近（<10%）");
          verdictLabel_->setStyleSheet(
              "font-size:14px; font-weight:bold; color:"
              + UiTheme::statusColor(UiTheme::Status::kWarn));
        } else {
          verdictLabel_->setText("不通过（≥10%）");
          verdictLabel_->setStyleSheet(
              "font-size:14px; font-weight:bold; color:"
              + UiTheme::statusColor(UiTheme::Status::kFail));
        }
      });
}
