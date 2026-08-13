/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 圆柱体浮力测试对话框 */
/// ZhangYu 创建 2026.5.5

#ifndef CYLINDER_TEST_DIALOG_HH_
#define CYLINDER_TEST_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>

class CylinderTestDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CylinderTestDialog(const QStringList &_instanceNameList, QWidget *parent = nullptr);

  QString getInstanceName() const;
  double getDiameter() const;
  double getLength() const;
  double getDensity() const;
  double getPosX() const;
  double getPosY() const;
  double getPosZ() const;
  double getRoll() const;
  double getPitch() const;
  double getYaw() const;
  QString getDampingMode() const;

private slots:
  void onDampingModeChanged(int _idx);
  void validateAndAccept();

private:
  QStringList instanceNameList;

  QLineEdit *instanceNameEdit;
  QDoubleSpinBox *diameterSpinBox;
  QDoubleSpinBox *lengthSpinBox;
  QDoubleSpinBox *densitySpinBox;
  QDoubleSpinBox *posXSpinBox;
  QDoubleSpinBox *posYSpinBox;
  QDoubleSpinBox *posZSpinBox;
  QDoubleSpinBox *rollSpinBox;
  QDoubleSpinBox *pitchSpinBox;
  QDoubleSpinBox *yawSpinBox;

  QComboBox *dampingModeCombo_;
};

#endif // CYLINDER_TEST_DIALOG_HH_
