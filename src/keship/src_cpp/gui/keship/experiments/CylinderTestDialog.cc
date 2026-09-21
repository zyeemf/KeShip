/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 圆柱体浮力测试对话框实现 */
/// ZhangYu 创建 2026.5.5

#include "CylinderTestDialog.hh"

#include "../shared/UiTheme.hh"

#include <QFormLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QDoubleSpinBox>
#include <QPushButton>

CylinderTestDialog::CylinderTestDialog(const QStringList &_instanceNameList, QWidget *parent)
    : QDialog(parent), instanceNameList(_instanceNameList)
{
  setWindowTitle("圆柱体浮力测试");
  setModal(true);
  resize(400, 400);

  auto *formLayout = new QFormLayout(this);
  const double maxVal = std::numeric_limits<double>::max();
  const int decimals = 6;

  /// 模型名称
  instanceNameEdit = new QLineEdit(this);
  instanceNameEdit->setPlaceholderText("请输入模型名称");
  formLayout->addRow("实例名称:", instanceNameEdit);

  /// 直径
  diameterSpinBox = new QDoubleSpinBox(this);
  diameterSpinBox->setRange(0.001, maxVal);
  diameterSpinBox->setValue(5.0);
  diameterSpinBox->setDecimals(decimals);
  diameterSpinBox->setSuffix(" m");
  formLayout->addRow("直径:", diameterSpinBox);

  /// 长度
  lengthSpinBox = new QDoubleSpinBox(this);
  lengthSpinBox->setRange(0.001, maxVal);
  lengthSpinBox->setValue(20.0);
  lengthSpinBox->setDecimals(decimals);
  lengthSpinBox->setSuffix(" m");
  formLayout->addRow("长度:", lengthSpinBox);

  /// 密度
  densitySpinBox = new QDoubleSpinBox(this);
  densitySpinBox->setRange(1.0, 50000.0);
  densitySpinBox->setValue(700.0);
  densitySpinBox->setDecimals(1);
  densitySpinBox->setSuffix(" kg/m³");
  formLayout->addRow("密度:", densitySpinBox);

  /// 阻尼模式
  dampingModeCombo_ = new QComboBox(this);
  dampingModeCombo_->addItem("Fossen 模型 (推荐)");
  dampingModeCombo_->addItem("基于位姿 (Pose-Based)");
  formLayout->addRow("阻尼模式:", dampingModeCombo_);

  /// 位置
  posXSpinBox = new QDoubleSpinBox(this);
  posXSpinBox->setRange(-maxVal, maxVal);
  posXSpinBox->setValue(0.0);
  posXSpinBox->setDecimals(decimals);
  posXSpinBox->setSuffix(" m");
  formLayout->addRow("X 位置:", posXSpinBox);

  posYSpinBox = new QDoubleSpinBox(this);
  posYSpinBox->setRange(-maxVal, maxVal);
  posYSpinBox->setValue(0.0);
  posYSpinBox->setDecimals(decimals);
  posYSpinBox->setSuffix(" m");
  formLayout->addRow("Y 位置:", posYSpinBox);

  posZSpinBox = new QDoubleSpinBox(this);
  posZSpinBox->setRange(-maxVal, maxVal);
  posZSpinBox->setValue(0.0);
  posZSpinBox->setDecimals(decimals);
  posZSpinBox->setSuffix(" m");
  formLayout->addRow("Z 位置:", posZSpinBox);

  /// 姿态
  rollSpinBox = new QDoubleSpinBox(this);
  rollSpinBox->setRange(-180.0, 180.0);
  rollSpinBox->setValue(0.0);
  rollSpinBox->setDecimals(2);
  rollSpinBox->setSuffix(" °");
  formLayout->addRow("Roll (绕X):", rollSpinBox);

  pitchSpinBox = new QDoubleSpinBox(this);
  pitchSpinBox->setRange(-180.0, 180.0);
  pitchSpinBox->setValue(0.0);
  pitchSpinBox->setDecimals(2);
  pitchSpinBox->setSuffix(" °");
  formLayout->addRow("Pitch (绕Y):", pitchSpinBox);

  yawSpinBox = new QDoubleSpinBox(this);
  yawSpinBox->setRange(-180.0, 180.0);
  yawSpinBox->setValue(0.0);
  yawSpinBox->setDecimals(2);
  yawSpinBox->setSuffix(" °");
  formLayout->addRow("Yaw (绕Z):", yawSpinBox);

  /// 按钮
  auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  UiTheme::setButtonIcon(buttonBox->button(QDialogButtonBox::Ok),
                         UiTheme::Icon::kOk);
  UiTheme::setButtonIcon(buttonBox->button(QDialogButtonBox::Cancel),
                         UiTheme::Icon::kCancel);
  formLayout->addRow(buttonBox);

  connect(buttonBox, &QDialogButtonBox::accepted, this, &CylinderTestDialog::validateAndAccept);
  connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(dampingModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &CylinderTestDialog::onDampingModeChanged);
}

void CylinderTestDialog::onDampingModeChanged(int _idx)
{
  if (_idx == 0)
  {
    QMessageBox::information(this, "Fossen 模型",
        "将弹出水动力参数编辑器，系数已根据圆柱尺寸和密度自动计算。\n"
        "如需调整可直接编辑各 DOF 的阻尼系数。");
  }
  else
  {
    QMessageBox::information(this, "基于位姿阻尼",
        "圆柱体将使用基于实时位姿与水面线的阻尼计算（线性+二次），\n"
        "无需手动设置水动力系数。");
  }
}

void CylinderTestDialog::validateAndAccept()
{
  QString name = instanceNameEdit->text().trimmed();
  if (name.isEmpty())
  {
    QMessageBox::warning(this, "警告", "实例名称不能为空");
    instanceNameEdit->setFocus();
    return;
  }
  if (instanceNameList.contains(name))
  {
    QMessageBox::warning(this, "警告", "模型名称已存在");
    return;
  }
  accept();
}

QString CylinderTestDialog::getInstanceName() const { return instanceNameEdit->text(); }
double CylinderTestDialog::getDiameter() const { return diameterSpinBox->value(); }
double CylinderTestDialog::getLength() const { return lengthSpinBox->value(); }
double CylinderTestDialog::getDensity() const { return densitySpinBox->value(); }
double CylinderTestDialog::getPosX() const { return posXSpinBox->value(); }
double CylinderTestDialog::getPosY() const { return posYSpinBox->value(); }
double CylinderTestDialog::getPosZ() const { return posZSpinBox->value(); }
double CylinderTestDialog::getRoll() const { return rollSpinBox->value(); }
double CylinderTestDialog::getPitch() const { return pitchSpinBox->value(); }
double CylinderTestDialog::getYaw() const { return yawSpinBox->value(); }
QString CylinderTestDialog::getDampingMode() const
{
  return (dampingModeCombo_->currentIndex() == 0) ? "fossen" : "pose_based";
}
