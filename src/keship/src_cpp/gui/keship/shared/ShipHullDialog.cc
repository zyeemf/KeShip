/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型参数输入对话框实现 */

#include "ShipHullDialog.hh"
#include "UiTheme.hh"
#include "LinesPlanDialog.hh"

#include <QCheckBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QScrollArea>
#include <QVBoxLayout>
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
  resize(720, 970);
  /// 2026-10-25：防止被拖成 274×129 那种"压坏"的尺寸（原 minimumSizeHint 太小）
  setMinimumSize(560, 640);
  setupUI();
  onTypeChanged();
}

void ShipHullDialog::setupUI() {
  auto *outer = new QVBoxLayout(this);

  /// 内容较高（船型参数 + 静水力 + 生成设置 + 水动力），放进滚动区，
  /// 小屏幕也不会挤掉底部按钮
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  auto *content = new QWidget(scroll);
  auto *mainLayout = new QVBoxLayout(content);
  mainLayout->setContentsMargins(0, 0, 0, 0);
  mainLayout->setSpacing(4);

  auto *paramGroup = new QGroupBox("船型参数", content);
  auto *paramForm = new QFormLayout(paramGroup);

  typeCombo_ = new QComboBox(content);
  typeCombo_->addItems({"Wigley 经典", "Wigley 平底", "Wigley 尖舭",
                         "Wigley 瘦削", "Lewis U", "Lewis V"});
  paramForm->addRow("船型:", typeCombo_);

  lengthSpin_ = new QDoubleSpinBox(content);
  lengthSpin_->setRange(2, 20); lengthSpin_->setValue(6);
  lengthSpin_->setSuffix(" m"); lengthSpin_->setDecimals(1);
  paramForm->addRow("L (船长):", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(content);
  breadthSpin_->setRange(0.5, 5); breadthSpin_->setValue(1.5);
  breadthSpin_->setSuffix(" m"); breadthSpin_->setDecimals(2);
  paramForm->addRow("B (型宽):", breadthSpin_);

  depthSpin_ = new QDoubleSpinBox(content);
  depthSpin_->setRange(0.3, 3); depthSpin_->setValue(1.0);
  depthSpin_->setSuffix(" m"); depthSpin_->setDecimals(2);
  paramForm->addRow("D (型深):", depthSpin_);

  densitySpin_ = new QDoubleSpinBox(content);
  densitySpin_->setRange(100, 950); densitySpin_->setValue(500);
  densitySpin_->setSuffix(" kg/m³"); densitySpin_->setDecimals(0);
  paramForm->addRow("ρ (密度):", densitySpin_);

  // 静水力预览（两列紧凑排布，2026-10-25：给下面的水动力组留出高度）
  auto *hydroFrame = new QFrame(content);
  hydroFrame->setFrameStyle(QFrame::StyledPanel);
  auto *hydroGrid = new QGridLayout(hydroFrame);
  hydroGrid->setContentsMargins(8, 6, 8, 6);
  draftLabel_ = new QLabel; awLabel_ = new QLabel;
  cwpLabel_ = new QLabel; ciLabel_ = new QLabel;
  volLabel_ = new QLabel; kbLabel_ = new QLabel;
  bmLabel_ = new QLabel; gmLabel_ = new QLabel;
  auto addHydroCell = [&](int _row, int _col, const QString &_name, QLabel *_val) {
    auto *cell = new QHBoxLayout();
    cell->addWidget(new QLabel(_name, hydroFrame));
    cell->addWidget(_val);
    cell->addStretch();
    hydroGrid->addLayout(cell, _row, _col);
  };
  addHydroCell(0, 0, "吃水:", draftLabel_);
  addHydroCell(0, 1, "Aw:", awLabel_);
  addHydroCell(1, 0, "Cwp:", cwpLabel_);
  addHydroCell(1, 1, "Ci:", ciLabel_);
  addHydroCell(2, 0, "Vol:", volLabel_);
  addHydroCell(2, 1, "KB:", kbLabel_);
  addHydroCell(3, 0, "BM:", bmLabel_);
  addHydroCell(3, 1, "GM:", gmLabel_);
  paramForm->addRow(hydroFrame);
  mainLayout->addWidget(paramGroup);

  // ★ 2026-10-25 新增：生成设置（名称 + 位置 + 姿态）
  //   此前名称由船型名固定、位姿由 MenuPluginKeship 硬编码 {0, 0, D/2+0.1, 0, 0, 0}，
  //   用户无法指定 —— 与「载入模型」的 ModelSettingsDialog 口径对齐。
  auto *genGroup = new QGroupBox("生成设置", content);
  auto *genForm = new QGridLayout(genGroup);

  nameEdit_ = new QLineEdit(content);
  nameEdit_->setPlaceholderText("实例名称（建议字母/数字/下划线）");
  genForm->addWidget(new QLabel("实例名称:", content), 0, 0);
  genForm->addWidget(nameEdit_, 0, 1, 1, 3);

  auto makePoseSpin = [content](double _min, double _max, double _value,
                                const QString &_suffix, int _decimals) {
    auto *spin = new QDoubleSpinBox(content);
    spin->setRange(_min, _max);
    spin->setValue(_value);
    spin->setSuffix(_suffix);
    spin->setDecimals(_decimals);
    /// 上限 150px：字号放大时不让三个 spin 把整行顶宽（滚动条来源）
    spin->setMaximumWidth(150);
    return spin;
  };

  posXSpin_ = makePoseSpin(-1000.0, 1000.0, 0.0, " m", 3);
  posYSpin_ = makePoseSpin(-1000.0, 1000.0, 0.0, " m", 3);
  posZSpin_ = makePoseSpin(-1000.0, 1000.0, 0.6, " m", 3);
  rollSpin_ = makePoseSpin(-180.0, 180.0, 0.0, " °", 2);
  pitchSpin_ = makePoseSpin(-180.0, 180.0, 0.0, " °", 2);
  yawSpin_ = makePoseSpin(-180.0, 180.0, 0.0, " °", 2);
  /// 位置/姿态两列两列排（2026-10-25 第二次调整：三列在 125% 字号下会横向溢出，
  /// 排布审计实测"生成设置"组最小宽 751px > 视口 684px）
  const char *poseNames[6] = {"X:", "Y:", "Z:", "Roll:", "Pitch:", "Yaw:"};
  QDoubleSpinBox *poseSpins[6] = {posXSpin_, posYSpin_, posZSpin_,
                                  rollSpin_, pitchSpin_, yawSpin_};
  for (int i = 0; i < 6; ++i)
  {
    genForm->addWidget(new QLabel(poseNames[i], content), 1 + i / 2, (i % 2) * 2);
    genForm->addWidget(poseSpins[i], 1 + i / 2, (i % 2) * 2 + 1);
  }
  mainLayout->addWidget(genGroup);

  // ★ 2026-10-25 新增：水动力系数（按船型与尺寸估算，可改、可恢复）
  auto *hydroGroup = new QGroupBox("水动力系数（生成时写入插件）", content);
  auto *hydroLayout = new QVBoxLayout(hydroGroup);

  hydroCheck_ = new QCheckBox("生成时附带默认水动力系数（推荐）", content);
  hydroCheck_->setChecked(true);
  hydroLayout->addWidget(hydroCheck_);

  auto *modeRow = new QHBoxLayout();
  modeRow->addWidget(new QLabel("档位:", content));
  hydroMode_ = new QComboBox(content);
  hydroMode_->addItems({"轻量（线性 8 项）", "完整（+二次阻力 + 附加质量）"});
  /// 下拉框的最小宽度默认取最长条目 —— 会顶宽整行（125% 字号下尤其明显），
  /// 这里改成按 10 个字符计最小宽度，长条目在弹出列表里完整显示即可
  hydroMode_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  hydroMode_->setMinimumContentsLength(10);
  modeRow->addWidget(hydroMode_, 1);
  auto *restoreBtn = new QPushButton("恢复默认", content);
  restoreBtn->setMinimumHeight(28);
  modeRow->addWidget(restoreBtn);
  hydroLayout->addLayout(modeRow);

  const char *linNames[8] = {"Xu", "Yv", "Zw", "Kp", "Mq", "Nr", "Yr", "Nv"};
  auto *linGrid = new QGridLayout();
  for (int i = 0; i < 8; ++i) {
    auto *cell = new QHBoxLayout();
    cell->addWidget(new QLabel(QString(linNames[i]) + ":", content));
    auto *spin = new QDoubleSpinBox(content);
    spin->setRange(0.0, 1e7);
    spin->setDecimals(3);
    spin->setMinimumWidth(96);
    linSpins_[i] = spin;
    cell->addWidget(spin);
    linGrid->addLayout(cell, i / 3, i % 3);
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { hydroEdited_ = true; });
  }
  hydroLayout->addLayout(linGrid);

  /// 完整档才显示的二次阻力项（Morison 型）
  quadWidget_ = new QWidget(content);
  auto *quadGrid = new QGridLayout(quadWidget_);
  quadGrid->setContentsMargins(0, 0, 0, 0);
  const char *quadNames[6] = {"Xu|u|", "Yv|v|", "Zw|w|", "Kp|p|", "Mq|q|", "Nr|r|"};
  for (int i = 0; i < 6; ++i) {
    auto *cell = new QHBoxLayout();
    cell->addWidget(new QLabel(QString(quadNames[i]) + ":", content));
    auto *spin = new QDoubleSpinBox(content);
    spin->setRange(0.0, 1e7);
    spin->setDecimals(2);
    spin->setMinimumWidth(96);
    quadSpins_[i] = spin;
    cell->addWidget(spin);
    quadGrid->addLayout(cell, i / 3, i % 3);
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double) { hydroEdited_ = true; });
  }
  hydroLayout->addWidget(quadWidget_);

  auto *hydroNote = new QLabel(
      "按船型/尺寸估算（ζ 垂荡 0.15、转动 0.10）；完整档另加二次阻力+附加质量；不勾选=零阻尼。",
      content);
  hydroNote->setWordWrap(true);
  hydroNote->setStyleSheet("color:#666;");
  mainLayout->addWidget(hydroGroup);

  scroll->setWidget(content);
  outer->addWidget(scroll, 1);
  /// 说明放在滚动区外：任何字号下都看得见，不会被内容高度挤掉
  outer->addWidget(hydroNote);

  // 按钮（固定在底部，不随滚动）
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
  outer->addLayout(btnRow);

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

  // 水动力：勾选/档位/恢复默认
  connect(hydroCheck_, &QCheckBox::toggled, this, [this](bool _on) {
    hydroMode_->setEnabled(_on);
    for (auto *s : linSpins_) s->setEnabled(_on);
    for (auto *s : quadSpins_) s->setEnabled(_on);
    quadWidget_->setVisible(_on && isHydroFull());
  });
  connect(hydroMode_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) {
    quadWidget_->setVisible(isHydroEnabled() && isHydroFull());
    /// 切档时按当前船型重算（用户显式动作，覆盖手改值；界面提示可再用「恢复默认」）
    refreshHydroDefaults(true);
  });
  connect(restoreBtn, &QPushButton::clicked, this,
          [this]() { refreshHydroDefaults(true); });

  quadWidget_->setVisible(false);   // 默认轻量档：二次项行隐藏
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
  if (!hydroEdited_)
    refreshHydroDefaults(false);
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

void ShipHullDialog::refreshHydroDefaults(bool _force) {
  if (!hydroCheck_ || !linSpins_[0])
    return;
  if (!_force && hydroEdited_)
    return;

  const keship::HydroConfig cfg =
      keship::ModelManager::EstimateMathHullHydro(spec_, isHydroFull());
  for (int i = 0; i < 8; ++i)
  {
    const bool blocked = linSpins_[i]->blockSignals(true);
    linSpins_[i]->setValue(cfg.damping[i]);
    linSpins_[i]->blockSignals(blocked);
  }
  if (isHydroFull())
  {
    for (int i = 0; i < 6 && i < static_cast<int>(cfg.dampingTerms.size()); ++i)
    {
      const bool blocked = quadSpins_[i]->blockSignals(true);
      quadSpins_[i]->setValue(cfg.dampingTerms[i].coefficient);
      quadSpins_[i]->blockSignals(blocked);
    }
  }
  hydroEdited_ = false;
}

bool ShipHullDialog::isHydroEnabled() const {
  return hydroCheck_ && hydroCheck_->isChecked();
}

bool ShipHullDialog::isHydroFull() const {
  return hydroMode_ && hydroMode_->currentIndex() == 1;
}

keship::HydroConfig ShipHullDialog::hydroConfig() const {
  const bool full = isHydroFull();
  keship::HydroConfig cfg =
      keship::ModelManager::EstimateMathHullHydro(spec_, full);
  for (int i = 0; i < 8; ++i)
    cfg.damping[i] = linSpins_[i]->value();
  if (!full)
  {
    cfg.dampingTerms.clear();
    cfg.addedMass.fill(0.0);
  }
  else
  {
    for (int i = 0; i < 6 && i < static_cast<int>(cfg.dampingTerms.size()); ++i)
      cfg.dampingTerms[i].coefficient = quadSpins_[i]->value();
  }
  return cfg;
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
