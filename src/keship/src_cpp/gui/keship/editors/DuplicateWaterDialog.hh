/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水域复制对话框（命名 + 位置微调，几何与风浪流参数整体继承） */

#ifndef DUPLICATE_WATER_DIALOG_HH_
#define DUPLICATE_WATER_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>

class DuplicateWaterDialog : public QDialog
{
  Q_OBJECT

public:
  DuplicateWaterDialog(const QString &_defaultName,
                       QWidget *parent = nullptr);

  QString waterName() const;
  /// 整体平移量（m）
  double dx() const;
  double dy() const;
  /// 静水面高度微调（m）
  double baseHeight() const;

private slots:
  void validateAndAccept();

private:
  QLineEdit *nameEdit_;
  QDoubleSpinBox *dxSpin_;
  QDoubleSpinBox *dySpin_;
  QDoubleSpinBox *baseHeightSpin_;
};

#endif