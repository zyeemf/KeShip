/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 浮力体编辑器对话框实现 — 含 marker 3D 预览 */
/// ZhangYu 创建 2026.5.6, marker 2026.5.7

#include "BuoyancyEditorDialog.hh"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>

#include <cmath>
#include <iostream>

BuoyancyEditorDialog::BuoyancyEditorDialog(
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
  setWindowTitle(QString("浮力体编辑器 — %1").arg(QString::fromStdString(modelName_)));
  setModal(false);
  resize(700, 500);

  config_.linkName = linkName_;

  auto *mainLayout = new QVBoxLayout(this);
  const double maxVal = std::numeric_limits<double>::max();
  const int decimals = 6;

  /// Link 选择器
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

  auto *listGroup = new QGroupBox("浮力体列表", this);
  auto *listVLayout = new QVBoxLayout(listGroup);
  bodyList_ = new QListWidget(this);
  listVLayout->addWidget(bodyList_);

  auto *btnLayout = new QHBoxLayout();
  auto *addBtn = new QPushButton("+ 添加", this);
  auto *delBtn = new QPushButton("删除", this);
  btnLayout->addWidget(addBtn);
  btnLayout->addWidget(delBtn);
  listVLayout->addLayout(btnLayout);
  leftColumn->addWidget(listGroup);

  /// 参数面板
  paramGroup_ = new QGroupBox("参数", this);
  auto *paramForm = new QFormLayout(paramGroup_);

  typeCombo_ = new QComboBox(this);
  typeCombo_->addItem("Box");
  typeCombo_->addItem("Cylinder");
  paramForm->addRow("类型:", typeCombo_);

  boxParams_ = new QWidget(this);
  auto *boxForm = new QFormLayout(boxParams_);
  dxSpin_ = new QDoubleSpinBox(this);
  dxSpin_->setRange(0.001, maxVal); dxSpin_->setValue(1.0); dxSpin_->setDecimals(decimals); dxSpin_->setSuffix(" m");
  boxForm->addRow("dx:", dxSpin_);
  dySpin_ = new QDoubleSpinBox(this);
  dySpin_->setRange(0.001, maxVal); dySpin_->setValue(1.0); dySpin_->setDecimals(decimals); dySpin_->setSuffix(" m");
  boxForm->addRow("dy:", dySpin_);
  dzSpin_ = new QDoubleSpinBox(this);
  dzSpin_->setRange(0.001, maxVal); dzSpin_->setValue(1.0); dzSpin_->setDecimals(decimals); dzSpin_->setSuffix(" m");
  boxForm->addRow("dz:", dzSpin_);
  paramForm->addRow(boxParams_);

  cylParams_ = new QWidget(this);
  auto *cylForm = new QFormLayout(cylParams_);
  rSpin_ = new QDoubleSpinBox(this);
  rSpin_->setRange(0.001, maxVal); rSpin_->setValue(0.5); rSpin_->setDecimals(decimals); rSpin_->setSuffix(" m");
  cylForm->addRow("radius:", rSpin_);
  lSpin_ = new QDoubleSpinBox(this);
  lSpin_->setRange(0.001, maxVal); lSpin_->setValue(2.0); lSpin_->setDecimals(decimals); lSpin_->setSuffix(" m");
  cylForm->addRow("length:", lSpin_);
  cylParams_->hide();
  paramForm->addRow(cylParams_);

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

  auto *saveBtn = new QPushButton("保存", paramGroup_);
  paramForm->addRow(saveBtn);

  leftColumn->addWidget(paramGroup_);

  /// 3D 预览视图（右侧，随窗口缩放）
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
  auto *cancelBtn = new QPushButton("取消", this);
  bottomLayout->addStretch();
  bottomLayout->addWidget(applyBtn);
  bottomLayout->addWidget(cancelBtn);
  mainLayout->addLayout(bottomLayout);

  connect(bodyList_, &QListWidget::currentRowChanged, this, &BuoyancyEditorDialog::onBodySelected);
  connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BuoyancyEditorDialog::onTypeChanged);
  connect(addBtn, &QPushButton::clicked, this, &BuoyancyEditorDialog::onAddBody);
  connect(delBtn, &QPushButton::clicked, this, &BuoyancyEditorDialog::onDeleteBody);
  connect(saveBtn, &QPushButton::clicked, this, &BuoyancyEditorDialog::onSaveBody);
  connect(applyBtn, &QPushButton::clicked, this, &BuoyancyEditorDialog::onApplyAndRebuild);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  /// 参数修改 → 更新配置 + 刷新 marker
  auto params = {dxSpin_, dySpin_, dzSpin_, rSpin_, lSpin_, posX_, posY_, posZ_, roll_, pitch_, yaw_};
  for (auto *sp : params)
    connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &BuoyancyEditorDialog::onParamChanged);
}

BuoyancyEditorDialog::~BuoyancyEditorDialog()
{
}

void BuoyancyEditorDialog::setConfig(const keship::BuoyancyConfig &_config)
{
  config_ = _config;
  updateList();
  /// 自动选中第一个
  if (!config_.bodies.empty())
    bodyList_->setCurrentRow(0);
  else if (paramGroup_)
    paramGroup_->setVisible(false);
  update3DView();
}

void BuoyancyEditorDialog::setCreateMode()
{
  isCreateMode_ = true;
  setWindowTitle(windowTitle() + " [新建]");
}

void BuoyancyEditorDialog::onBodySelected(int _index)
{
  if (_index < 0 || _index >= (int)config_.bodies.size())
  {
    currentBodyIdx_ = -1;
    if (paramGroup_) paramGroup_->setVisible(false);
    if (view3d_) view3d_->setSelectedIndex(-1);
    return;
  }

  currentBodyIdx_ = _index;
  if (paramGroup_) paramGroup_->setVisible(true);
  updateParamPanel();
  if (view3d_) view3d_->setSelectedIndex(_index);
}

void BuoyancyEditorDialog::onTypeChanged(int _typeIdx)
{
  boxParams_->setVisible(_typeIdx == 0);
  cylParams_->setVisible(_typeIdx == 1);
  pendingType_ = (_typeIdx == 0) ? keship::PrimitiveType::Box
                                 : keship::PrimitiveType::Cylinder;

  /// 切换类型时立即更新 3D 预览
  if (view3d_ && currentBodyIdx_ >= 0)
  {
    Buoyancy3DBodyInfo bi;
    bi.isBox = (pendingType_ == keship::PrimitiveType::Box);
    bi.boxDx = dxSpin_->value(); bi.boxDy = dySpin_->value(); bi.boxDz = dzSpin_->value();
    bi.cylRadius = rSpin_->value(); bi.cylLength = lSpin_->value();
    bi.poseX = posX_->value(); bi.poseY = posY_->value(); bi.poseZ = posZ_->value();
    bi.poseRoll = roll_->value() * M_PI / 180.0;
    bi.posePitch = pitch_->value() * M_PI / 180.0;
    bi.poseYaw = yaw_->value() * M_PI / 180.0;
    view3d_->updateBody(currentBodyIdx_, bi);
  }
}

void BuoyancyEditorDialog::onParamChanged()
{
  if (isLoadingParams_)
    return;

  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size())
    return;

  /// spinbox 变化 → 增量更新选中浮力体的 3D 预览
  if (view3d_)
  {
    Buoyancy3DBodyInfo bi;
    bi.isBox = (pendingType_ == keship::PrimitiveType::Box);
    bi.boxDx = dxSpin_->value(); bi.boxDy = dySpin_->value(); bi.boxDz = dzSpin_->value();
    bi.cylRadius = rSpin_->value(); bi.cylLength = lSpin_->value();
    bi.poseX = posX_->value(); bi.poseY = posY_->value(); bi.poseZ = posZ_->value();
    bi.poseRoll = roll_->value() * M_PI / 180.0;
    bi.posePitch = pitch_->value() * M_PI / 180.0;
    bi.poseYaw = yaw_->value() * M_PI / 180.0;
    view3d_->updateBody(currentBodyIdx_, bi);
  }
}

void BuoyancyEditorDialog::onAddBody()
{
  /// 用默认值创建新浮力体，不读取 spinbox（避免带入其他浮力体的值）
  keship::BuoyancyBodyConfig body;
  body.name = "body_" + std::to_string(config_.bodies.size());
  body.type = pendingType_;
  config_.bodies.push_back(body);
  updateList();
  bodyList_->setCurrentRow(config_.bodies.size() - 1);
  update3DView();
}

void BuoyancyEditorDialog::onDeleteBody()
{
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size())
    return;

  config_.bodies.erase(config_.bodies.begin() + currentBodyIdx_);
  currentBodyIdx_ = -1;
  updateList();
  /// 删除后自动选中剩余浮力体，或隐藏面板
  if (!config_.bodies.empty())
    bodyList_->setCurrentRow(0);
  else
  {
    if (paramGroup_) paramGroup_->setVisible(false);
    if (view3d_) view3d_->setSelectedIndex(-1);
  }
  update3DView();
}

void BuoyancyEditorDialog::onSaveBody()
{
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size())
    return;

  auto &body = config_.bodies[currentBodyIdx_];
  body.type = pendingType_;
  body.box.dx = dxSpin_->value();
  body.box.dy = dySpin_->value();
  body.box.dz = dzSpin_->value();
  body.cyl.radius = rSpin_->value();
  body.cyl.length = lSpin_->value();
  body.pose[0] = posX_->value();
  body.pose[1] = posY_->value();
  body.pose[2] = posZ_->value();
  body.pose[3] = roll_->value() * M_PI / 180.0;
  body.pose[4] = pitch_->value() * M_PI / 180.0;
  body.pose[5] = yaw_->value() * M_PI / 180.0;

  updateList();
  bodyList_->setCurrentRow(currentBodyIdx_);
  update3DView();

  std::cout << "BuoyancyEditorDialog: 已保存浮力体 [" << currentBodyIdx_
            << "] " << body.name << std::endl;
}

void BuoyancyEditorDialog::onApplyAndRebuild()
{
  if (config_.bodies.empty())
  {
    QMessageBox::warning(this, "警告", "至少需要一个浮力体");
    return;
  }

  /// 先保存当前选中浮力体的 spinbox 修改
  if (currentBodyIdx_ >= 0)
    onSaveBody();

  accept();
}

void BuoyancyEditorDialog::updateList()
{
  bodyList_->blockSignals(true);
  bodyList_->clear();

  for (const auto &body : config_.bodies)
  {
    QString typeStr = (body.type == keship::PrimitiveType::Box) ? "Box" : "Cyl";
    bodyList_->addItem(QString("%1 (%2)").arg(QString::fromStdString(body.name), typeStr));
  }

  bodyList_->blockSignals(false);
}

void BuoyancyEditorDialog::updateParamPanel()
{
  if (currentBodyIdx_ < 0 || currentBodyIdx_ >= (int)config_.bodies.size())
    return;

  const auto &body = config_.bodies[currentBodyIdx_];

  typeCombo_->blockSignals(true);
  typeCombo_->setCurrentIndex(body.type == keship::PrimitiveType::Box ? 0 : 1);
  typeCombo_->blockSignals(false);

  /// 同步 pendingType_：UI 显示类型与"+"按钮一致
  pendingType_ = body.type;

  boxParams_->setVisible(body.type == keship::PrimitiveType::Box);
  cylParams_->setVisible(body.type == keship::PrimitiveType::Cylinder);

  isLoadingParams_ = true;

  dxSpin_->setValue(body.box.dx);
  dySpin_->setValue(body.box.dy);
  dzSpin_->setValue(body.box.dz);
  rSpin_->setValue(body.cyl.radius);
  lSpin_->setValue(body.cyl.length);

  posX_->setValue(body.pose[0]);
  posY_->setValue(body.pose[1]);
  posZ_->setValue(body.pose[2]);
  roll_->setValue(body.pose[3] * 180.0 / M_PI);
  pitch_->setValue(body.pose[4] * 180.0 / M_PI);
  yaw_->setValue(body.pose[5] * 180.0 / M_PI);

  isLoadingParams_ = false;
}

void BuoyancyEditorDialog::update3DView()
{
  if (!view3d_) return;

  std::vector<Buoyancy3DBodyInfo> infos;
  for (size_t i = 0; i < config_.bodies.size(); i++)
  {
    Buoyancy3DBodyInfo bi;
    if (static_cast<int>(i) == currentBodyIdx_)
    {
      /// 选中浮力体用 spinbox 预览值
      bi.isBox = (pendingType_ == keship::PrimitiveType::Box);
      bi.boxDx = dxSpin_->value(); bi.boxDy = dySpin_->value(); bi.boxDz = dzSpin_->value();
      bi.cylRadius = rSpin_->value(); bi.cylLength = lSpin_->value();
      bi.poseX = posX_->value(); bi.poseY = posY_->value(); bi.poseZ = posZ_->value();
      bi.poseRoll = roll_->value() * M_PI / 180.0;
      bi.posePitch = pitch_->value() * M_PI / 180.0;
      bi.poseYaw = yaw_->value() * M_PI / 180.0;
    }
    else
    {
      const auto &b = config_.bodies[i];
      bi.isBox = (b.type == keship::PrimitiveType::Box);
      bi.boxDx = b.box.dx; bi.boxDy = b.box.dy; bi.boxDz = b.box.dz;
      bi.cylRadius = b.cyl.radius; bi.cylLength = b.cyl.length;
      bi.poseX = b.pose[0]; bi.poseY = b.pose[1]; bi.poseZ = b.pose[2];
      bi.poseRoll = b.pose[3]; bi.posePitch = b.pose[4]; bi.poseYaw = b.pose[5];
    }
    infos.push_back(bi);
  }
  view3d_->syncBodies(infos);
  view3d_->setSelectedIndex(currentBodyIdx_);
}
