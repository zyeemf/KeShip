/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 风浪流环境参数编辑对话框实现 */

#include "EnvironmentParamsDialog.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QMessageBox>
#include <cmath>

#include "keship/utils/SeaState.hh"

EnvironmentParamsDialog::EnvironmentParamsDialog(
    const keship::WaterArea &_area, QWidget *parent)
    : QDialog(parent), area_(_area)
{
  setupUi();
  loadFrom(_area);
}

void EnvironmentParamsDialog::setupUi()
{
  setWindowTitle(QString("水域环境参数 — %1")
      .arg(QString::fromStdString(area_.name)));
  setModal(true);
  resize(360, 600);

  auto *mainLayout = new QVBoxLayout(this);
  const double maxVal = 9999.0;
  const int dec = 3;

  // ── 海况速览 ──
  auto *seaGroup = new QGroupBox("海况", this);
  auto *seaLayout = new QVBoxLayout(seaGroup);

  seaStateCombo_ = new QComboBox(this);
  seaStateCombo_->addItem("按海况等级自动设置风浪参数", -1);
  for (const auto &b : keship::kSeaStateTable)
    seaStateCombo_->addItem(QString("%1级 · %2")
        .arg(b.level).arg(QString::fromUtf8(b.nameZh.c_str())), b.level);
  connect(seaStateCombo_, QOverload<int>::of(&QComboBox::activated),
          this, &EnvironmentParamsDialog::onSeaStateSelected);
  seaLayout->addWidget(new QLabel("按海况等级自动设置风浪参数:", this));
  seaLayout->addWidget(seaStateCombo_);

  seaStateLabel_ = new QLabel(this);
  seaStateLabel_->setStyleSheet("background:#f0f0f0; padding:6px;");
  seaStateLabel_->setWordWrap(true);
  seaLayout->addWidget(seaStateLabel_);

  mainLayout->addWidget(seaGroup);

  // ── 风参数 ──
  auto *windGroup = new QGroupBox("风参数", this);
  auto *windForm = new QFormLayout(windGroup);

  windSpeedSpin_ = new QDoubleSpinBox(this);
  windSpeedSpin_->setRange(0, maxVal);
  windSpeedSpin_->setDecimals(1);
  windSpeedSpin_->setSuffix(" m/s");
  windForm->addRow("风速:", windSpeedSpin_);

  windDirSpin_ = new QDoubleSpinBox(this);
  windDirSpin_->setRange(0, 360);
  windDirSpin_->setDecimals(1);
  windDirSpin_->setSuffix(" °");
  windDirSpin_->setWrapping(true);
  windForm->addRow("风向:", windDirSpin_);

  waveDirAutoCheck_ = new QCheckBox("浪向跟随风向", this);
  waveDirAutoCheck_->setChecked(true);
  windForm->addRow(waveDirAutoCheck_);

  mainLayout->addWidget(windGroup);

  // ── 波浪参数 ──
  auto *waveGroup = new QGroupBox("波浪参数 (PMS)", this);
  auto *waveForm = new QFormLayout(waveGroup);

  wavePeriodSpin_ = new QDoubleSpinBox(this);
  wavePeriodSpin_->setRange(0.1, 999);
  wavePeriodSpin_->setDecimals(1);
  wavePeriodSpin_->setSuffix(" s");
  waveForm->addRow("周期:", wavePeriodSpin_);

  waveDirSpin_ = new QDoubleSpinBox(this);
  waveDirSpin_->setRange(0, 360);
  waveDirSpin_->setDecimals(1);
  waveDirSpin_->setSuffix(" °");
  waveDirSpin_->setWrapping(true);
  waveForm->addRow("方向:", waveDirSpin_);

  waveNumSpin_ = new QSpinBox(this);
  waveNumSpin_->setRange(1, 8);  // 与 GerstnerWaves shader MAX 8 对齐
  waveForm->addRow("波数:", waveNumSpin_);

  waveAngleSpin_ = new QDoubleSpinBox(this);
  waveAngleSpin_->setRange(0, M_PI);
  waveAngleSpin_->setDecimals(3);
  waveAngleSpin_->setSuffix(" rad");
  waveAngleSpin_->setSingleStep(0.1);
  waveForm->addRow("扩散角:", waveAngleSpin_);

  waveScaleSpin_ = new QDoubleSpinBox(this);
  waveScaleSpin_->setRange(0.1, 10);
  waveScaleSpin_->setDecimals(2);
  waveScaleSpin_->setSingleStep(0.1);
  waveForm->addRow("尺度:", waveScaleSpin_);

  waveGainSpin_ = new QDoubleSpinBox(this);
  waveGainSpin_->setRange(0, maxVal);
  waveGainSpin_->setDecimals(2);
  waveGainSpin_->setSingleStep(0.1);
  waveForm->addRow("波高增益:", waveGainSpin_);

  waveSteepSpin_ = new QDoubleSpinBox(this);
  waveSteepSpin_->setRange(0, 1);
  waveSteepSpin_->setDecimals(3);
  waveSteepSpin_->setSingleStep(0.05);
  waveForm->addRow("陡度:", waveSteepSpin_);

  waveTauSpin_ = new QDoubleSpinBox(this);
  waveTauSpin_->setRange(0.1, 60);
  waveTauSpin_->setDecimals(1);
  waveTauSpin_->setSuffix(" s");
  waveForm->addRow("缓起时间 τ:", waveTauSpin_);

  mainLayout->addWidget(waveGroup);

  // ── 流参数 ──
  auto *curGroup = new QGroupBox("流参数", this);
  auto *curForm = new QFormLayout(curGroup);

  currentSpeedSpin_ = new QDoubleSpinBox(this);
  currentSpeedSpin_->setRange(0, maxVal);
  currentSpeedSpin_->setDecimals(1);
  currentSpeedSpin_->setSuffix(" m/s");
  curForm->addRow("流速:", currentSpeedSpin_);

  currentDirSpin_ = new QDoubleSpinBox(this);
  currentDirSpin_->setRange(0, 360);
  currentDirSpin_->setDecimals(1);
  currentDirSpin_->setSuffix(" °");
  currentDirSpin_->setWrapping(true);
  curForm->addRow("流向:", currentDirSpin_);

  mainLayout->addWidget(curGroup);

  // ── 按钮 ──
  auto *btnLayout = new QHBoxLayout();
  auto *applyBtn = new QPushButton("应用", this);
  auto *cancelBtn = new QPushButton("取消", this);
  btnLayout->addStretch();
  btnLayout->addWidget(applyBtn);
  btnLayout->addWidget(cancelBtn);
  mainLayout->addLayout(btnLayout);

  connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  connect(windSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &EnvironmentParamsDialog::onWindChanged);
  connect(windDirSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &EnvironmentParamsDialog::onWindChanged);
  connect(waveDirAutoCheck_, &QCheckBox::toggled,
          this, &EnvironmentParamsDialog::onWaveDirAutoToggled);

  connect(windSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &EnvironmentParamsDialog::onParamEdited);
  connect(wavePeriodSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &EnvironmentParamsDialog::onParamEdited);
  connect(waveGainSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &EnvironmentParamsDialog::onParamEdited);
  connect(currentSpeedSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &EnvironmentParamsDialog::onParamEdited);
}

void EnvironmentParamsDialog::loadFrom(const keship::WaterArea &_area)
{
  windSpeedSpin_->setValue(_area.windParams.speed);
  windDirSpin_->setValue(_area.windParams.direction * 180.0 / M_PI);

  wavePeriodSpin_->setValue(_area.waveParams.period);
  waveNumSpin_->setValue(_area.waveParams.number);
  waveAngleSpin_->setValue(_area.waveParams.angle);
  waveScaleSpin_->setValue(_area.waveParams.scale);
  waveGainSpin_->setValue(_area.waveParams.gain);
  waveSteepSpin_->setValue(_area.waveParams.steepness);
  waveTauSpin_->setValue(_area.waveParams.tau);
  waveDirSpin_->setValue(_area.waveParams.direction * 180.0 / M_PI);

  waveDirAutoCheck_->setChecked(_area.waveDirectionAuto);
  onWaveDirAutoToggled(_area.waveDirectionAuto);

  currentSpeedSpin_->setValue(_area.currentParams.speed);
  currentDirSpin_->setValue(_area.currentParams.direction * 180.0 / M_PI);

  updateSeaStateLabel();
}

void EnvironmentParamsDialog::onWindChanged()
{
  if (waveDirAutoCheck_->isChecked())
    waveDirSpin_->setValue(windDirSpin_->value());
}

void EnvironmentParamsDialog::onWaveDirAutoToggled(bool _checked)
{
  waveDirSpin_->setEnabled(!_checked);
  if (_checked)
    waveDirSpin_->setValue(windDirSpin_->value());
}

void EnvironmentParamsDialog::onParamEdited()
{
  updateSeaStateLabel();
}

void EnvironmentParamsDialog::updateSeaStateLabel()
{
  const double gain = waveGainSpin_->value();
  const double period = wavePeriodSpin_->value();
  const double speed = windSpeedSpin_->value();
  const double current = currentSpeedSpin_->value();

  const double hs = keship::SeaState::HsFromParams(gain, period);
  const int seaLv = keship::SeaState::LevelFromHs(hs);
  const auto &seaBand = keship::kSeaStateTable[seaLv];
  const int windLv = keship::SeaState::BeaufortFromSpeed(speed);
  const auto &windBand = keship::kBeaufortTable[windLv];

  QString text = QString("海况：%1级 · %2（Hs≈%3 m）\n"
                         "风级：%4级 · %5（%6 m/s）\n"
                         "流速：%7 m/s")
      .arg(seaBand.level)
      .arg(QString::fromUtf8(seaBand.nameZh.c_str()))
      .arg(hs, 0, 'f', 2)
      .arg(windBand.level)
      .arg(QString::fromUtf8(windBand.nameZh.c_str()))
      .arg(speed, 0, 'f', 1)
      .arg(current, 0, 'f', 1);
  seaStateLabel_->setText(text);
}

void EnvironmentParamsDialog::onSeaStateSelected(int _index)
{
  const int level = seaStateCombo_->itemData(_index).toInt();
  if (level < 0)
    return;

  double tp = 0.0, wind = 0.0, gain = 0.0;
  keship::SeaState::ParamsFromLevel(level, &tp, &wind, &gain);
  const double hs = keship::SeaState::MidHs(level);
  const auto &band = keship::kSeaStateTable[level];

  const QString note =
      (hs <= 0.0)
      ? QString()
      : (std::sqrt(hs / keship::SeaState::kHsPeriodFactor)
             < keship::SeaState::kMinTp)
        ? QString("（注：该等级理论周期低于渲染下限 2.5s，"
                  "周期取 2.5s，波高增益自动补偿，等级对应不变）")
        : QString();

  const QString msg = QString("按“%1级 · %2”设置（Hs≈%3 m）：\n"
                              "  周期 → %4 s\n"
                              "  风速 → %5 m/s\n"
                              "  波高增益 → %6\n"
                              "%7将覆盖当前风浪参数，是否继续？")
      .arg(band.level)
      .arg(QString::fromUtf8(band.nameZh.c_str()))
      .arg(hs, 0, 'f', 2)
      .arg(tp, 0, 'f', 1)
      .arg(wind, 0, 'f', 1)
      .arg(gain, 0, 'f', 2)
      .arg(note.isEmpty() ? QString() : note + "\n");
  if (QMessageBox::question(this, "按海况设置", msg,
                            QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes)
  {
    seaStateCombo_->setCurrentIndex(0);
    return;
  }

  windSpeedSpin_->setValue(wind);
  wavePeriodSpin_->setValue(tp);
  waveGainSpin_->setValue(gain);
  seaStateCombo_->setCurrentIndex(0);
  updateSeaStateLabel();
}

keship::WaterArea EnvironmentParamsDialog::getWaterArea() const
{
  auto a = area_;
  a.windParams.speed = windSpeedSpin_->value();
  a.windParams.direction = windDirSpin_->value() * M_PI / 180.0;

  a.waveParams.period = wavePeriodSpin_->value();
  a.waveParams.number = waveNumSpin_->value();
  a.waveParams.angle = waveAngleSpin_->value();
  a.waveParams.scale = waveScaleSpin_->value();
  a.waveParams.gain = waveGainSpin_->value();
  a.waveParams.steepness = waveSteepSpin_->value();
  a.waveParams.tau = waveTauSpin_->value();
  a.waveParams.direction = waveDirSpin_->value() * M_PI / 180.0;

  a.waveDirectionAuto = waveDirAutoCheck_->isChecked();

  a.currentParams.speed = currentSpeedSpin_->value();
  a.currentParams.direction = currentDirSpin_->value() * M_PI / 180.0;

  return a;
}
