/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型参数输入对话框实现 */

#include "ShipHullDialog.hh"
#include "UiTheme.hh"
#include "LinesPlanDialog.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QFrame>
#include <QMessageBox>
#include <QtMath>

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

ShipHullDialog::ShipHullDialog(const QStringList &_instanceNames, QWidget *_parent)
    : QDialog(_parent), instanceNames_(_instanceNames) {
  setWindowTitle("新建数学船型");
  setAttribute(Qt::WA_DeleteOnClose);
  resize(440, 720);
  setupUI();
  onTypeChanged();
}

void ShipHullDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);

  auto *paramGroup = new QGroupBox("船型参数", this);
  auto *paramForm = new QFormLayout(paramGroup);

  typeCombo_ = new QComboBox(this);
  typeCombo_->addItems({"Wigley 经典", "Wigley 平底", "Wigley 尖舭",
                         "Wigley 瘦削", "Lewis U", "Lewis V"});
  paramForm->addRow("船型:", typeCombo_);

  lengthSpin_ = new QDoubleSpinBox(this);
  lengthSpin_->setRange(2, 20); lengthSpin_->setValue(6);
  lengthSpin_->setSuffix(" m"); lengthSpin_->setDecimals(1);
  paramForm->addRow("L (船长):", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(this);
  breadthSpin_->setRange(0.5, 5); breadthSpin_->setValue(1.5);
  breadthSpin_->setSuffix(" m"); breadthSpin_->setDecimals(2);
  paramForm->addRow("B (型宽):", breadthSpin_);

  depthSpin_ = new QDoubleSpinBox(this);
  depthSpin_->setRange(0.3, 3); depthSpin_->setValue(1.0);
  depthSpin_->setSuffix(" m"); depthSpin_->setDecimals(2);
  paramForm->addRow("D (型深):", depthSpin_);

  densitySpin_ = new QDoubleSpinBox(this);
  densitySpin_->setRange(100, 950); densitySpin_->setValue(500);
  densitySpin_->setSuffix(" kg/m³"); densitySpin_->setDecimals(0);
  paramForm->addRow("ρ (密度):", densitySpin_);

  // 静水力预览
  auto *hydroFrame = new QFrame(this);
  hydroFrame->setFrameStyle(QFrame::StyledPanel);
  auto *hydroForm = new QFormLayout(hydroFrame);
  draftLabel_ = new QLabel; awLabel_ = new QLabel;
  cwpLabel_ = new QLabel; ciLabel_ = new QLabel;
  volLabel_ = new QLabel; kbLabel_ = new QLabel;
  bmLabel_ = new QLabel; gmLabel_ = new QLabel;
  hydroForm->addRow("吃水:", draftLabel_);
  hydroForm->addRow("Aw:", awLabel_);
  hydroForm->addRow("Cwp:", cwpLabel_);
  hydroForm->addRow("Ci:", ciLabel_);
  hydroForm->addRow("Vol:", volLabel_);
  hydroForm->addRow("KB:", kbLabel_);
  hydroForm->addRow("BM:", bmLabel_);
  hydroForm->addRow("GM:", gmLabel_);
  paramForm->addRow(hydroFrame);
  mainLayout->addWidget(paramGroup);

  // ★ 2026-10-25 新增：生成设置（名称 + 位置 + 姿态）
  //   此前名称由船型名固定、位姿由 MenuPluginKeship 硬编码 {0, 0, D/2+0.1, 0, 0, 0}，
  //   用户无法指定 —— 与「载入模型」的 ModelSettingsDialog 口径对齐。
  auto *genGroup = new QGroupBox("生成设置", this);
  auto *genForm = new QFormLayout(genGroup);

  nameEdit_ = new QLineEdit(this);
  nameEdit_->setPlaceholderText("实例名称（建议字母/数字/下划线）");
  genForm->addRow("实例名称:", nameEdit_);

  auto makePoseSpin = [this](double _min, double _max, double _value,
                             const QString &_suffix, int _decimals) {
    auto *spin = new QDoubleSpinBox(this);
    spin->setRange(_min, _max);
    spin->setValue(_value);
    spin->setSuffix(_suffix);
    spin->setDecimals(_decimals);
    return spin;
  };

  posXSpin_ = makePoseSpin(-1000.0, 1000.0, 0.0, " m", 3);
  posYSpin_ = makePoseSpin(-1000.0, 1000.0, 0.0, " m", 3);
  posZSpin_ = makePoseSpin(-1000.0, 1000.0, 0.6, " m", 3);
  rollSpin_ = makePoseSpin(-180.0, 180.0, 0.0, " °", 2);
  pitchSpin_ = makePoseSpin(-180.0, 180.0, 0.0, " °", 2);
  yawSpin_ = makePoseSpin(-180.0, 180.0, 0.0, " °", 2);
  genForm->addRow("X 位置:", posXSpin_);
  genForm->addRow("Y 位置:", posYSpin_);
  genForm->addRow("Z 位置:", posZSpin_);
  genForm->addRow("Roll (绕X):", rollSpin_);
  genForm->addRow("Pitch (绕Y):", pitchSpin_);
  genForm->addRow("Yaw (绕Z):", yawSpin_);
  mainLayout->addWidget(genGroup);

  // 按钮
  auto *btnRow = new QHBoxLayout();
  auto *linesBtn = new QPushButton("型线图", this);
  linesBtn->setMinimumHeight(32);
  spawnBtn_ = new QPushButton("生成模型", this);
  spawnBtn_->setMinimumHeight(32);
  closeBtn_ = new QPushButton("关闭", this);
  closeBtn_->setMinimumHeight(30);
  UiTheme::setButtonIcon(closeBtn_, UiTheme::Icon::kClose);
  btnRow->addWidget(linesBtn);
  btnRow->addWidget(spawnBtn_);
  btnRow->addWidget(closeBtn_);
  mainLayout->addLayout(btnRow);

  connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ShipHullDialog::onTypeChanged);
  connect(lengthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &ShipHullDialog::onParamChanged);
  connect(breadthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &ShipHullDialog::onParamChanged);
  connect(depthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &ShipHullDialog::onParamChanged);
  connect(densitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &ShipHullDialog::onParamChanged);

  // 名称：用户打字后不再随船型联动
  connect(nameEdit_, &QLineEdit::textEdited, this,
          [this]() { nameEdited_ = true; });
  // Z：只有用户手动改过才停止跟随型深（程序自动改值会 blockSignals，不触发此槽）
  connect(posZSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double) { zAuto_ = false; });

  connect(linesBtn, &QPushButton::clicked, this, &ShipHullDialog::onShowLinesPlan);
  connect(spawnBtn_, &QPushButton::clicked, this, &ShipHullDialog::onSpawn);
  connect(closeBtn_, &QPushButton::clicked, this, &QDialog::close);
}

void ShipHullDialog::onTypeChanged() {
  onParamChanged();
  if (!nameEdited_)
    updateDefaultName();
}

void ShipHullDialog::onParamChanged() {
  double L = lengthSpin_->value(), B = breadthSpin_->value();
  double D = depthSpin_->value(), rho = densitySpin_->value();
  spec_ = mapType(typeCombo_->currentIndex(), L, B, D, rho);
  if (zAuto_ && posZSpin_)
  {
    // 默认浮于水面附近（历史值 D/2 + 0.1 保持不变），D 变了跟着变
    const bool blocked = posZSpin_->blockSignals(true);
    posZSpin_->setValue(spec_.D / 2.0 + 0.1);
    posZSpin_->blockSignals(blocked);
  }
  updateHydroDisplay();
}

void ShipHullDialog::updateDefaultName() {
  const QString base = QString::fromStdString(modelName());
  QString name = base;
  int idx = 2;
  while (instanceNames_.contains(name))
    name = base + "_" + QString::number(idx++);
  nameEdit_->setText(name);
  nameEdited_ = false;
}

void ShipHullDialog::updateHydroDisplay() {
  keship::ShipHullGenerator hull(spec_);
  auto &hp = hull.hydroProps();
  draftLabel_->setText(QString::number(hp.draft, 'f', 3) + " m");
  awLabel_->setText(QString::number(hp.Aw, 'f', 3) + " m²");
  cwpLabel_->setText(QString::number(hp.Cwp, 'f', 3));
  ciLabel_->setText(QString::number(hp.Ci, 'f', 3));
  volLabel_->setText(QString::number(hp.volume, 'f', 3) + " m³");
  kbLabel_->setText(QString::number(hp.KB, 'f', 3) + " m");
  bmLabel_->setText(QString::number(hp.BM, 'f', 4) + " m");
  gmLabel_->setText(QString::number(hp.GM, 'f', 4) + " m");
  gmLabel_->setStyleSheet(
      QString("font-weight:bold;color:%1;")
          .arg(UiTheme::statusColor(hp.GM > 0.01 ? UiTheme::Status::kOk
                                                  : UiTheme::Status::kFail)));
}

void ShipHullDialog::getPose(double _pos[6]) const {
  _pos[0] = posXSpin_->value();
  _pos[1] = posYSpin_->value();
  _pos[2] = posZSpin_->value();
  _pos[3] = qDegreesToRadians(rollSpin_->value());
  _pos[4] = qDegreesToRadians(pitchSpin_->value());
  _pos[5] = qDegreesToRadians(yawSpin_->value());
}

void ShipHullDialog::setInstanceNames(const QStringList &_instanceNames) {
  instanceNames_ = _instanceNames;
  if (!nameEdited_)
    updateDefaultName();
}

void ShipHullDialog::onSpawn() {
  const QString name = nameEdit_->text().trimmed();
  if (name.isEmpty())
  {
    QMessageBox::warning(this, "警告", "实例名称不能为空");
    nameEdit_->setFocus();
    return;
  }
  if (instanceNames_.contains(name))
  {
    QMessageBox::warning(this, "警告", "模型名称已存在，请换一个名称");
    nameEdit_->setFocus();
    return;
  }
  emit spawnRequested(spec_, name.toStdString());
}

void ShipHullDialog::onShowLinesPlan() {
  onParamChanged();
  auto *dlg = new LinesPlanDialog(spec_);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->setWindowFlags(dlg->windowFlags() | Qt::Window);
  dlg->show();
}

std::string ShipHullDialog::modelName() const {
  static const char *names[] = {"Wigley", "WigleyFlat", "WigleySharp",
                                 "WigleyFine", "LewisU", "LewisV"};
  int idx = typeCombo_->currentIndex();
  return (idx >= 0 && idx < 6) ? names[idx] : "MathHull";
}
