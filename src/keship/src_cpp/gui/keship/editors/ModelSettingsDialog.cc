/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 模型设置对话框实现 */
/// Zhangyu 创建 2026.1.16
#include "ModelSettingsDialog.hh"
#include "../shared/Buoyancy3DView.hh"

#include <iostream>

#include <QMessageBox>

#include "../utils/FossenModel.hh"
#include <math.h>

/**
 * @brief 模型设置对话框构造函数
 * @param parent 父窗口指针
 */
ModelSettingsDialog::ModelSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
  //
  this->instanceNameList = QStringList();

  // 窗口设置
  setWindowTitle("模型设置");
  setModal(true);
  resize(650, 600);

  /// 外层垂直布局：上方表单 | 下方按钮
  QVBoxLayout *mainLayout = new QVBoxLayout(this);

  /// --- 上方表单 ---
  QWidget *formWidget = new QWidget(this);
  QFormLayout *formLayout = new QFormLayout(formWidget);
  formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

  /// 模型名称
  instanceNameEdit = new QLineEdit(formWidget);
  instanceNameEdit->setPlaceholderText("请输入模型名称");
  formLayout->addRow("模型名称", instanceNameEdit);


  { /// 位置设置
    double posMin = -std::numeric_limits<double>::infinity();
    double posMax = std::numeric_limits<double>::infinity();

    auto *posRow = new QWidget(formWidget);
    QHBoxLayout *posLayout = new QHBoxLayout(posRow);
    posLayout->setContentsMargins(0, 0, 0, 0);
    posLayout->setSpacing(5);

    posXSpinBox = new QDoubleSpinBox(posRow);
    posXSpinBox->setRange(posMin, posMax);
    posXSpinBox->setValue(0.0);
    posXSpinBox->setPrefix("x  ");
    posXSpinBox->setDecimals(3);
    posXSpinBox->setSuffix(" m");
    posXSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    posLayout->addWidget(posXSpinBox);

    posYSpinBox = new QDoubleSpinBox(posRow);
    posYSpinBox->setRange(posMin, posMax);
    posYSpinBox->setValue(0.0);
    posYSpinBox->setPrefix("y  ");
    posYSpinBox->setDecimals(3);
    posYSpinBox->setSuffix(" m");
    posYSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    posLayout->addWidget(posYSpinBox);
 
    posZSpinBox = new QDoubleSpinBox(posRow);
    posZSpinBox->setRange(posMin, posMax);
    posZSpinBox->setValue(0.0);
    posZSpinBox->setPrefix("z  ");
    posZSpinBox->setDecimals(3);
    posZSpinBox->setSuffix(" m");
    posZSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    posLayout->addWidget(posZSpinBox);
 
    formLayout->addRow("位置", posRow);
  }


  { /// 姿态设置
    double angleMin = -180.0;
    double angleMax = 180.0;

    auto *rotRow = new QWidget(formWidget);
    QHBoxLayout *rotLayout = new QHBoxLayout(rotRow);
    rotLayout->setContentsMargins(0, 0, 0, 0);
    rotLayout->setSpacing(5);

    rollSpinBox = new QDoubleSpinBox(rotRow);
    rollSpinBox->setRange(angleMin, angleMax);
    rollSpinBox->setValue(0.0);
    rollSpinBox->setPrefix("Roll(绕X) ");
    rollSpinBox->setDecimals(2);
    rollSpinBox->setSuffix("°");
    rollSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rotLayout->addWidget(rollSpinBox);

    pitchSpinBox = new QDoubleSpinBox(rotRow);
    pitchSpinBox->setRange(angleMin, angleMax);
    pitchSpinBox->setValue(0.0);
    pitchSpinBox->setPrefix("Pitch(绕Y) ");
    pitchSpinBox->setDecimals(2);
    pitchSpinBox->setSuffix("°");
    pitchSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rotLayout->addWidget(pitchSpinBox);
 
    yawSpinBox = new QDoubleSpinBox(rotRow);
    yawSpinBox->setRange(angleMin, angleMax);
    yawSpinBox->setValue(0.0);
    yawSpinBox->setPrefix("Yaw(绕Z) ");
    yawSpinBox->setDecimals(2);
    yawSpinBox->setSuffix("°");
    yawSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rotLayout->addWidget(yawSpinBox);
 
    formLayout->addRow("姿态", rotRow);
  }


  
  { /// 设置
    staticCheckBox = new QCheckBox("静态模型",formWidget);
    staticCheckBox->setChecked(false);
    staticCheckBox->setToolTip("如果勾选，模型将设置为静态（不可移动）");

    addBuoyancyCheckBox = new QCheckBox("添加浮力项",formWidget);
    addBuoyancyCheckBox->setChecked(false);
    addBuoyancyCheckBox->setToolTip("如果勾选，模型将添加浮力体");

    setInertiaCheckBox = new QCheckBox("惯性参数",formWidget);
    setInertiaCheckBox->setChecked(false);
    setInertiaCheckBox->setToolTip("如果勾选，模型将设置惯性量");
    
    auto *settingsRow = new QWidget(formWidget);
    QHBoxLayout *rowLayout = new QHBoxLayout(settingsRow);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(10);
    rowLayout->addWidget(staticCheckBox);
    rowLayout->addWidget(addBuoyancyCheckBox);
    rowLayout->addWidget(setInertiaCheckBox);
    rowLayout->addStretch();

    formLayout->addRow("设置", settingsRow);

    connect(addBuoyancyCheckBox, &QCheckBox::stateChanged, this, [=](int state)
            {
              if (!addBuoyancyCheckBox->isEnabled()) return;
              if (state == Qt::Checked)
              {
                setInertiaCheckBox->setChecked(true);
                setInertiaCheckBox->setEnabled(false);
              }
              else{
                setInertiaCheckBox->setEnabled(true);
              }
            }
          );
  }

  { /// 惯性质量设置
    inertiaMassSpinBox = new QDoubleSpinBox(formWidget);
    inertiaMassSpinBox->setRange(0, std::numeric_limits<double>::infinity());
    inertiaMassSpinBox->setValue(0.0);
    inertiaMassSpinBox->setDecimals(3); 
    inertiaMassSpinBox->setSuffix(" kg");
    inertiaMassSpinBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    inertiaMassSpinBox->setEnabled(false);

    formLayout->addRow("惯性质量", inertiaMassSpinBox);
    connect(setInertiaCheckBox, &QCheckBox::stateChanged, this, [=](int state)
            { inertiaMassSpinBox->setEnabled(
                state == Qt::Checked && setInertiaCheckBox->isEnabled()); });
  }


  { /// 惯性张量 3x3 矩阵
    double min = -std::numeric_limits<double>::infinity();
    double max = std::numeric_limits<double>::infinity();

    QStringList tensorLabels = {"Ixx", "Ixy", "Ixz", "Iyx", "Iyy", "Iyz", "Izx", "Izy", "Izz"};

    auto *tensorWidget = new QWidget(formWidget);
    QGridLayout *grid = new QGridLayout(tensorWidget);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(5);

    for (int r = 0; r < 3; ++r)
    {
      for (int c = 0; c < 3; ++c)
      {
        int i = r * 3 + c;
        inertiaTensorSpinBoxes[i] = new QDoubleSpinBox(tensorWidget);
        inertiaTensorSpinBoxes[i]->setRange(0, max);
        inertiaTensorSpinBoxes[i]->setValue(0.0);
        inertiaTensorSpinBoxes[i]->setPrefix(tensorLabels[i] + ": ");
        inertiaTensorSpinBoxes[i]->setDecimals(4);
        inertiaTensorSpinBoxes[i]->setSuffix(" kg·m²");
        inertiaTensorSpinBoxes[i]->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        inertiaTensorSpinBoxes[i]->setEnabled(false);
        
        grid->addWidget(inertiaTensorSpinBoxes[i], r, c);
      }
    }
    formLayout->addRow("惯性张量", tensorWidget);
    //
    connect(setInertiaCheckBox, &QCheckBox::stateChanged, this, [=](int state)
          {
            bool enabled = (state == Qt::Checked) && setInertiaCheckBox->isEnabled();
            for (int i = 0; i < 9; ++i)
              inertiaTensorSpinBoxes[i]->setEnabled(enabled);
          });
  }

  
  { /// 重心偏移设置 (CoG)
    double cogMin = -std::numeric_limits<double>::infinity();
    double cogMax = std::numeric_limits<double>::infinity();
    int cogDec = 3;
    auto *cogRow = new QWidget(formWidget);
    QHBoxLayout *cogLayout = new QHBoxLayout(cogRow);
    cogLayout->setContentsMargins(0, 0, 0, 0);
    cogLayout->setSpacing(5);

    cogXSpinBox_ = new QDoubleSpinBox(cogRow);
    cogXSpinBox_->setRange(cogMin, cogMax);
    cogXSpinBox_->setValue(0.0);
    cogXSpinBox_->setPrefix("x  ");
    cogXSpinBox_->setDecimals(cogDec);
    cogXSpinBox_->setSuffix(" m");
    cogXSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cogXSpinBox_->setEnabled(false);
    cogLayout->addWidget(cogXSpinBox_);

    cogYSpinBox_ = new QDoubleSpinBox(cogRow);
    cogYSpinBox_->setRange(cogMin, cogMax);
    cogYSpinBox_->setValue(0.0);
    cogYSpinBox_->setPrefix("y  ");
    cogYSpinBox_->setDecimals(cogDec);
    cogYSpinBox_->setSuffix(" m");
    cogYSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cogYSpinBox_->setEnabled(false);
    cogLayout->addWidget(cogYSpinBox_);
    
    cogZSpinBox_ = new QDoubleSpinBox(cogRow);
    cogZSpinBox_->setRange(cogMin, cogMax);
    cogZSpinBox_->setValue(0.0);
    cogZSpinBox_->setPrefix("z  ");
    cogZSpinBox_->setDecimals(cogDec);
    cogZSpinBox_->setSuffix(" m");
    cogZSpinBox_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    cogZSpinBox_->setEnabled(false);
    cogLayout->addWidget(cogZSpinBox_);

    //
    formLayout->addRow("重心偏移", cogRow);
    //
    connect(setInertiaCheckBox, &QCheckBox::stateChanged, this, [=](int state)
          {
            bool enabled = (state == Qt::Checked) && setInertiaCheckBox->isEnabled();
            cogXSpinBox_->setEnabled(enabled);
            cogYSpinBox_->setEnabled(enabled);
            cogZSpinBox_->setEnabled(enabled);
          });
  }
  
  /// 主布局
  mainLayout->addWidget(formWidget);


  { /// --- 中间 3D 预览 ---
    cogPreview_ = new Buoyancy3DView(this);
    cogPreview_->setMinimumSize(220, 220);
    cogPreview_->hide();
    ///
    mainLayout->addWidget(cogPreview_);
    /// CoG 实时预览
    connect(cogXSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ModelSettingsDialog::updateCoGPreview);
    connect(cogYSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ModelSettingsDialog::updateCoGPreview);
    connect(cogZSpinBox_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ModelSettingsDialog::updateCoGPreview);
  }

  { /// --- 下方按钮 ---
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->setContentsMargins(0, 0, 0, 0);
    btnLayout->addStretch();
    btnLayout->addWidget(buttonBox);
    btnLayout->addStretch();
    ///
    mainLayout->addLayout(btnLayout);
    /// 连接按钮信号槽
    connect(buttonBox, &QDialogButtonBox::accepted, this, &ModelSettingsDialog::validateAndAccept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  }

}

/**
 * @brief 模型设置对话框构造函数，用于设置已存在模型的参数
 * @param _instanceNameList 已存在模型名称列表
 * @param parent 父窗口指针
 */
ModelSettingsDialog::ModelSettingsDialog(const QStringList &_instanceNameList,
                                           Mode _mode, QWidget *parent)
    : ModelSettingsDialog(parent)
{
  this->instanceNameList = _instanceNameList;

  if (_mode == Ksm)
  {
    staticCheckBox->setEnabled(false);
    staticCheckBox->setToolTip("由 KSM 文件配置");
    addBuoyancyCheckBox->setEnabled(false);
    addBuoyancyCheckBox->setToolTip("浮力/推力由 KSM 文件配置");
    setInertiaCheckBox->setEnabled(false);
    setInertiaCheckBox->setToolTip("惯性参数由 KSM 文件配置");
  }
}

ModelSettingsDialog::ModelSettingsDialog(const QString &_modelName, double pos[6],
                                           Mode _mode, QWidget *parent)
    : ModelSettingsDialog(parent)
{
  this->instanceNameEdit->setText(_modelName);
  this->instanceNameEdit->setEnabled(false);

  posXSpinBox->setValue(pos[0]);
  posYSpinBox->setValue(pos[1]);
  posZSpinBox->setValue(pos[2]);
  rollSpinBox->setValue(pos[3] * 180.0 / M_PI);
  pitchSpinBox->setValue(pos[4] * 180.0 / M_PI);
  yawSpinBox->setValue(pos[5] * 180.0 / M_PI);

  if (_mode == OnlyPose)
  {
    /// 已 spawn 模型：显示位姿+静态+惯性（浮力走独立编辑器）
    addBuoyancyCheckBox->hide();
    staticCheckBox->setEnabled(false);
    cogPreview_->setShowCoG(true);
    cogPreview_->show();
    updateCoGPreview();
  }
  else if (_mode == Ksm)
  {
    staticCheckBox->setEnabled(false);
    staticCheckBox->setToolTip("由 KSM 文件配置");
    addBuoyancyCheckBox->setEnabled(false);
    addBuoyancyCheckBox->setToolTip("浮力/推力由 KSM 文件配置");
    setInertiaCheckBox->setEnabled(false);
    setInertiaCheckBox->setToolTip("惯性参数由 KSM 文件配置");
  }
}

void ModelSettingsDialog::validateAndAccept()
{
  QString instanceName = instanceNameEdit->text().trimmed();
  if (instanceName.isEmpty())
  {
    QMessageBox::warning(this, "警告", "模型名称不能为空");
    instanceNameEdit->setFocus();
    return;
  }

  // 实例名只能包含字母、数字、下划线（SDF name 替换与保存文件名安全）
  for (const QChar &ch : instanceName)
    if (!ch.isLetterOrNumber() && ch != '_')
    {
      QMessageBox::warning(this, "警告", "模型名称只能包含字母、数字、下划线");
      instanceNameEdit->setFocus();
      return;
    }

  // std::cout<<"***实例名称列表："<<std::endl;
  // foreach(QString v , this->instanceNameList)
  // {
  //   std::cout<<v.toStdString()<<std::endl;
  // }
  if (this->instanceNameList.contains(instanceName))
  {
    QMessageBox::warning(nullptr, "警告", "模型名称已存在");
    return;
  }
  if (setInertiaCheckBox->isEnabled() && setInertiaCheckBox->isChecked())
  {
    auto v = keship::ValidateInertia(
        inertiaMassSpinBox->value(),
        inertiaTensorSpinBoxes[0]->value(),
        inertiaTensorSpinBoxes[4]->value(),
        inertiaTensorSpinBoxes[8]->value(),
        inertiaTensorSpinBoxes[1]->value(),
        inertiaTensorSpinBoxes[2]->value(),
        inertiaTensorSpinBoxes[5]->value());
    if (!v.valid)
    {
      QMessageBox::warning(this, "惯性参数无效",
          QString::fromStdString(v.message));
      return;
    }
  }
  accept();
}

QString ModelSettingsDialog::getModelName() const
{
  return instanceNameEdit->text();
}

double ModelSettingsDialog::getPosX() const { return posXSpinBox->value(); }
double ModelSettingsDialog::getPosY() const { return posYSpinBox->value(); }
double ModelSettingsDialog::getPosZ() const { return posZSpinBox->value(); }
double ModelSettingsDialog::getRoll() const { return rollSpinBox->value(); }
double ModelSettingsDialog::getPitch() const { return pitchSpinBox->value(); }
double ModelSettingsDialog::getYaw() const { return yawSpinBox->value(); }
bool ModelSettingsDialog::isStatic() const { return staticCheckBox->isChecked(); }
bool ModelSettingsDialog::isAddBuoyancy() const { return addBuoyancyCheckBox->isChecked(); }
bool ModelSettingsDialog::isAddInertia() const { return setInertiaCheckBox->isChecked(); }
double ModelSettingsDialog::getInertiaMass() const { return inertiaMassSpinBox->value(); }
std::vector<double> ModelSettingsDialog::getInertiaTensor() const
{
  std::vector<double> tensor;
  for (int i = 0; i < 9; ++i)
    tensor.push_back(inertiaTensorSpinBoxes[i]->value());
  return tensor;
}

void ModelSettingsDialog::setInertiaValues(double _mass,
    double _ixx, double _ixy, double _ixz,
    double _iyy, double _iyz, double _izz)
{
  setInertiaCheckBox->setChecked(true);
  inertiaMassSpinBox->setValue(_mass);
  inertiaTensorSpinBoxes[0]->setValue(_ixx);
  inertiaTensorSpinBoxes[1]->setValue(_ixy);
  inertiaTensorSpinBoxes[2]->setValue(_ixz);
  inertiaTensorSpinBoxes[3]->setValue(_ixy);  // Iyx = Ixy
  inertiaTensorSpinBoxes[4]->setValue(_iyy);
  inertiaTensorSpinBoxes[5]->setValue(_iyz);
  inertiaTensorSpinBoxes[6]->setValue(_ixz);  // Izx = Ixz
  inertiaTensorSpinBoxes[7]->setValue(_iyz);  // Izy = Iyz
  inertiaTensorSpinBoxes[8]->setValue(_izz);
}

double ModelSettingsDialog::getCogX() const { return cogXSpinBox_->value(); }
double ModelSettingsDialog::getCogY() const { return cogYSpinBox_->value(); }
double ModelSettingsDialog::getCogZ() const { return cogZSpinBox_->value(); }

void ModelSettingsDialog::setCogValues(double _cogX, double _cogY, double _cogZ)
{
  cogXSpinBox_->setValue(_cogX);
  cogYSpinBox_->setValue(_cogY);
  cogZSpinBox_->setValue(_cogZ);
}

void ModelSettingsDialog::setModelInfo(const Buoyancy3DModelInfo &_info)
{
  if (cogPreview_)
    cogPreview_->setModel(_info);
}

void ModelSettingsDialog::setShowCoGPreview(bool _show)
{
  if (cogPreview_)
  {
    cogPreview_->setShowCoG(_show);
    cogPreview_->setVisible(_show);
  }
}

void ModelSettingsDialog::setStatic(bool _isStatic)
{
  staticCheckBox->setChecked(_isStatic);
}

void ModelSettingsDialog::setBuoyancy(bool _hasBuoyancy)
{
  addBuoyancyCheckBox->setChecked(_hasBuoyancy);
}

void ModelSettingsDialog::updateCoGPreview()
{
  if (cogPreview_)
    cogPreview_->setCoGPosition(
      (float)cogXSpinBox_->value(),
      (float)cogYSpinBox_->value(),
      (float)cogZSpinBox_->value());
}
