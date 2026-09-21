/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 推进器编辑器对话框实现 — 含 3D 箭头预览 */
/// ZhangYu 创建 2026.5.11

#include "ThrusterEditorDialog.hh"
#include "../shared/UiTheme.hh"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>

#include <cmath>
#include <iostream>

ThrusterEditorDialog::ThrusterEditorDialog(
    const std::string &_modelName,
    const std::string &_linkName,
    gz::transport::Node &_transportNode,
    const Buoyancy3DModelInfo &_modelInfo,
    const std::string &_worldName,
    QWidget *parent)
    : QDialog(parent),
      modelName_(_modelName),
      linkName_(_linkName),
      transportNode_(_transportNode),
      worldName_(_worldName)
{
  setWindowTitle(QString("推进器编辑器 — %1").arg(QString::fromStdString(modelName_)));
  setModal(false);
  resize(700, 500);

  config_.linkName = linkName_;

  auto *mainLayout = new QVBoxLayout(this);
  const double maxVal = std::numeric_limits<double>::max();
  const int decimals = 6;

  /// Link 选择器（不可编辑）
  auto *linkLayout = new QHBoxLayout();
  linkLayout->addWidget(new QLabel("Link:"));
  linkCombo_ = new QComboBox(this);
  linkCombo_->addItem(QString::fromStdString(linkName_));
  linkCombo_->setEnabled(false);
  linkLayout->addWidget(linkCombo_);
  linkLayout->addStretch();
  mainLayout->addLayout(linkLayout);

  /// 主体：左列（列表 + 参数）+ 右列（3D 预览）
  auto *bodyLayout = new QHBoxLayout();
  auto *leftColumn = new QVBoxLayout();

  auto *listGroup = new QGroupBox("推进器列表", this);
  auto *listVLayout = new QVBoxLayout(listGroup);
  bodyList_ = new QListWidget(this);
  listVLayout->addWidget(bodyList_);

  auto *btnLayout = new QHBoxLayout();
  auto *addBtn = new QPushButton("+ 添加", this);
  UiTheme::setButtonIcon(addBtn, UiTheme::Icon::kAdd);
  auto *delBtn = new QPushButton("删除", this);
  UiTheme::setButtonIcon(delBtn, UiTheme::Icon::kDelete);
  btnLayout->addWidget(addBtn);
  btnLayout->addWidget(delBtn);
  listVLayout->addLayout(btnLayout);
  leftColumn->addWidget(listGroup);

  /// 参数面板
  paramGroup_ = new QGroupBox("参数", this);
  auto *paramForm = new QFormLayout(paramGroup_);

  /// 类型标签（只读，当前只有 Fixed）
  auto *typeLabel = new QLabel("Fixed", paramGroup_);
  paramForm->addRow("类型:", typeLabel);

  nameEdit_ = new QLineEdit(this);
  nameEdit_->setPlaceholderText("推进器名称");
  paramForm->addRow("名称:", nameEdit_);

  posX_ = new QDoubleSpinBox(this);
  posX_->setRange(-maxVal, maxVal); posX_->setDecimals(decimals); posX_->setSuffix(" m");
  paramForm->addRow("X:", posX_);
  posY_ = new QDoubleSpinBox(this);
  posY_->setRange(-maxVal, maxVal); posY_->setDecimals(decimals); posY_->setSuffix(" m");
  paramForm->addRow("Y:", posY_);
  posZ_ = new QDoubleSpinBox(this);
  posZ_->setRange(-maxVal, maxVal); posZ_->setDecimals(decimals); posZ_->setSuffix(" m");
  paramForm->addRow("Z:", posZ_);

  roll_ = new QDoubleSpinBox(this);
  roll_->setRange(-180, 180); roll_->setDecimals(2); roll_->setSuffix("°");
  paramForm->addRow("Roll:", roll_);
  pitch_ = new QDoubleSpinBox(this);
  pitch_->setRange(-180, 180); pitch_->setDecimals(2); pitch_->setSuffix("°");
  paramForm->addRow("Pitch:", pitch_);
  yaw_ = new QDoubleSpinBox(this);
  yaw_->setRange(-180, 180); yaw_->setDecimals(2); yaw_->setSuffix("°");
  paramForm->addRow("Yaw:", yaw_);

  maxForce_ = new QDoubleSpinBox(this);
  maxForce_->setRange(0.1, maxVal); maxForce_->setDecimals(1);
  maxForce_->setSuffix(" N"); maxForce_->setValue(1000.0);
  paramForm->addRow("最大推力:", maxForce_);

  auto *saveBtn = new QPushButton("保存", paramGroup_);
  UiTheme::setButtonIcon(saveBtn, UiTheme::Icon::kSave);
  paramForm->addRow(saveBtn);

  leftColumn->addWidget(paramGroup_);

  /// 3D 预览（右侧）
  auto *viewGroup = new QGroupBox("3D 预览", this);
  auto *viewLayout = new QVBoxLayout(viewGroup);
  view3d_ = new Buoyancy3DView(viewGroup);
  view3d_->setModel(_modelInfo);
  viewLayout->addWidget(view3d_);

  bodyLayout->addLayout(leftColumn, 1);
  bodyLayout->addWidget(viewGroup, 2);
  mainLayout->addLayout(bodyLayout);

  /// 底部按钮
  auto *bottomLayout = new QHBoxLayout();
  auto *applyBtn = new QPushButton("应用并重建模型", this);
  UiTheme::setButtonIcon(applyBtn, UiTheme::Icon::kOk);
  auto *cancelBtn = new QPushButton("取消", this);
  UiTheme::setButtonIcon(cancelBtn, UiTheme::Icon::kCancel);
  bottomLayout->addStretch();
  bottomLayout->addWidget(applyBtn);
  bottomLayout->addWidget(cancelBtn);
  mainLayout->addLayout(bottomLayout);

  connect(bodyList_, &QListWidget::currentRowChanged, this, &ThrusterEditorDialog::onBodySelected);
  connect(addBtn, &QPushButton::clicked, this, &ThrusterEditorDialog::onAddBody);
  connect(delBtn, &QPushButton::clicked, this, &ThrusterEditorDialog::onDeleteBody);
  connect(saveBtn, &QPushButton::clicked, this, &ThrusterEditorDialog::onSaveBody);
  connect(applyBtn, &QPushButton::clicked, this, &ThrusterEditorDialog::onApplyAndRebuild);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  auto params = {posX_, posY_, posZ_, roll_, pitch_, yaw_, maxForce_};
  for (auto *sp : params)
    connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ThrusterEditorDialog::onParamChanged);
}

ThrusterEditorDialog::~ThrusterEditorDialog()
{
}

void ThrusterEditorDialog::setConfig(const keship::ThrusterConfig &_config)
{
  config_ = _config;
  updateList();
  if (!config_.bodies.empty())
    bodyList_->setCurrentRow(0);
  else if (paramGroup_)
    paramGroup_->setVisible(false);
  update3DView();
}

void ThrusterEditorDialog::onBodySelected(int _index)
{
  if (_index < 0 || _index >= (int)config_.bodies.size())
  {
    currentBodyIdx_ = -1;
    if (paramGroup_) paramGroup_->setVisible(false);
    if (view3d_) view3d_->setSelectedThrusterIdx(-1);
    return;
  }
  currentBodyIdx_ = _index;
  if (paramGroup_) paramGroup_->setVisible(true);
  updateParamPanel();
  if (view3d_) view3d_->setSelectedThrusterIdx(_index);
}

void ThrusterEditorDialog::onParamChanged()
{
  if (isLoadingParams_) return;
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size()) return;

  if (view3d_)
  {
    Thruster3DInfo ti;
    ti.poseX = posX_->value(); ti.poseY = posY_->value(); ti.poseZ = posZ_->value();
    ti.poseRoll = roll_->value() * M_PI / 180.0;
    ti.posePitch = pitch_->value() * M_PI / 180.0;
    ti.poseYaw = yaw_->value() * M_PI / 180.0;
    ti.maxForce = maxForce_->value();
    view3d_->updateThruster(currentBodyIdx_, ti);
  }
}

void ThrusterEditorDialog::onAddBody()
{
  // 自动命名取第一个未占用名（防止删除后重加产生重名 → 推力 topic 冲突）
  keship::ThrusterBodyConfig body;
  int n = 0;
  while (true)
  {
    bool used = false;
    body.name = "thrust_" + std::to_string(n);
    for (const auto &b : config_.bodies)
      if (b.name == body.name) { used = true; break; }
    if (!used) break;
    n++;
  }
  config_.bodies.push_back(body);
  updateList();
  bodyList_->setCurrentRow(config_.bodies.size() - 1);
  update3DView();
}

void ThrusterEditorDialog::onDeleteBody()
{
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size()) return;
  config_.bodies.erase(config_.bodies.begin() + currentBodyIdx_);
  currentBodyIdx_ = -1;
  updateList();
  if (!config_.bodies.empty())
    bodyList_->setCurrentRow(0);
  else
  {
    if (paramGroup_) paramGroup_->setVisible(false);
    if (view3d_) view3d_->setSelectedThrusterIdx(-1);
  }
  update3DView();
}

bool ThrusterEditorDialog::onSaveBody()
{
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size())
    return false;

  QString newName = nameEdit_->text().trimmed();
  if (newName.isEmpty())
  {
    QMessageBox::warning(this, "名称无效", "推进器名称不能为空");
    return false;
  }

  /// 检查是否与其他推进器重名（忽略自身）——重名会导致推力 topic 冲突
  for (int i = 0; i < (int)config_.bodies.size(); i++)
  {
    if (i == currentBodyIdx_) continue;
    if (config_.bodies[i].name == newName.toStdString())
    {
      QMessageBox::warning(this, "名称冲突",
          QString("推进器名称 \"%1\" 已被使用，请使用其他名称").arg(newName));
      return false;
    }
  }

  auto &body = config_.bodies[currentBodyIdx_];
  body.name = newName.toStdString();
  body.pose[0] = posX_->value();
  body.pose[1] = posY_->value();
  body.pose[2] = posZ_->value();
  body.pose[3] = roll_->value() * M_PI / 180.0;
  body.pose[4] = pitch_->value() * M_PI / 180.0;
  body.pose[5] = yaw_->value() * M_PI / 180.0;
  body.maxForce = maxForce_->value();

  updateList();
  update3DView();

  std::cout << "ThrusterEditorDialog: 已保存推进器 [" << currentBodyIdx_
            << "] " << body.name << std::endl;
  return true;
}

void ThrusterEditorDialog::onApplyAndRebuild()
{
  /// 先保存当前选中推进器的 spinbox 修改
  if (currentBodyIdx_ >= 0)
    if (!onSaveBody())
      return;

  accept();
}

void ThrusterEditorDialog::updateList()
{
  int oldRow = bodyList_->currentRow();
  bodyList_->blockSignals(true);
  bodyList_->clear();

  for (const auto &body : config_.bodies)
    bodyList_->addItem(QString("%1 (Fixed, %2 N)").arg(QString::fromStdString(body.name)).arg(body.maxForce));

  if (oldRow >= 0 && oldRow < bodyList_->count())
    bodyList_->setCurrentRow(oldRow);
  bodyList_->blockSignals(false);
}

void ThrusterEditorDialog::updateParamPanel()
{
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size()) return;

  const auto &body = config_.bodies[currentBodyIdx_];
  isLoadingParams_ = true;

  nameEdit_->setText(QString::fromStdString(body.name));
  posX_->setValue(body.pose[0]);
  posY_->setValue(body.pose[1]);
  posZ_->setValue(body.pose[2]);
  roll_->setValue(body.pose[3] * 180.0 / M_PI);
  pitch_->setValue(body.pose[4] * 180.0 / M_PI);
  yaw_->setValue(body.pose[5] * 180.0 / M_PI);
  maxForce_->setValue(body.maxForce);

  isLoadingParams_ = false;
}

void ThrusterEditorDialog::update3DView()
{
  if (!view3d_) return;

  std::vector<Thruster3DInfo> infos;
  for (size_t i = 0; i < config_.bodies.size(); i++)
  {
    Thruster3DInfo ti;
    if (static_cast<int>(i) == currentBodyIdx_)
    {
      ti.poseX = posX_->value(); ti.poseY = posY_->value(); ti.poseZ = posZ_->value();
      ti.poseRoll = roll_->value() * M_PI / 180.0;
      ti.posePitch = pitch_->value() * M_PI / 180.0;
      ti.poseYaw = yaw_->value() * M_PI / 180.0;
      ti.maxForce = maxForce_->value();
    }
    else
    {
      const auto &b = config_.bodies[i];
      ti.poseX = b.pose[0]; ti.poseY = b.pose[1]; ti.poseZ = b.pose[2];
      ti.poseRoll = b.pose[3]; ti.posePitch = b.pose[4]; ti.poseYaw = b.pose[5];
      ti.maxForce = b.maxForce;
    }
    infos.push_back(ti);
  }
  view3d_->syncThrusters(infos);
  view3d_->setSelectedThrusterIdx(currentBodyIdx_);
}
