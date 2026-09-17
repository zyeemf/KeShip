/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 水域复制对话框（命名 + 位置微调，几何与风浪流参数整体继承） */

#include "DuplicateWaterDialog.hh"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QVBoxLayout>

DuplicateWaterDialog::DuplicateWaterDialog(const QString &_defaultName,
                                           QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle("复制水域");
  setModal(true);
  resize(320, 200);

  auto *mainLayout = new QVBoxLayout(this);

  auto *info = new QLabel("几何形状与风浪流参数将整体继承原水域，"
                          "此处可重命名并做位置微调。", this);
  info->setWordWrap(true);
  mainLayout->addWidget(info);

  auto *form = new QFormLayout();

  nameEdit_ = new QLineEdit(_defaultName, this);
  form->addRow("名称:", nameEdit_);

  dxSpin_ = new QDoubleSpinBox(this);
  dxSpin_->setRange(-9999, 9999);
  dxSpin_->setDecimals(2);
  dxSpin_->setSuffix(" m");
  form->addRow("Δx:", dxSpin_);

  dySpin_ = new QDoubleSpinBox(this);
  dySpin_->setRange(-9999, 9999);
  dySpin_->setDecimals(2);
  dySpin_->setSuffix(" m");
  form->addRow("Δy:", dySpin_);

  baseHeightSpin_ = new QDoubleSpinBox(this);
  baseHeightSpin_->setRange(-9999, 9999);
  baseHeightSpin_->setDecimals(2);
  baseHeightSpin_->setSuffix(" m");
  form->addRow("静水面Δz:", baseHeightSpin_);

  mainLayout->addLayout(form);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted,
          this, &DuplicateWaterDialog::validateAndAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);
}

QString DuplicateWaterDialog::waterName() const
{
  return nameEdit_->text().trimmed();
}

double DuplicateWaterDialog::dx() const { return dxSpin_->value(); }
double DuplicateWaterDialog::dy() const { return dySpin_->value(); }
double DuplicateWaterDialog::baseHeight() const { return baseHeightSpin_->value(); }

void DuplicateWaterDialog::validateAndAccept()
{
  if (waterName().isEmpty())
  {
    QMessageBox::warning(this, "警告", "水域名称不能为空");
    nameEdit_->setFocus();
    return;
  }
  accept();
}