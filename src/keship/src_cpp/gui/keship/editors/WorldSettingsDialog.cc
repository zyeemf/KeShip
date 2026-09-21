/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 世界原点设置对话框（经纬高/航向，仅运行时生效，不持久化） */

#include "WorldSettingsDialog.hh"

#include "../shared/UiTheme.hh"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>

WorldSettingsDialog::WorldSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle("世界原点设置");
  resize(320, 200);

  auto *mainLayout = new QVBoxLayout(this);

  auto *group = new QGroupBox("地理坐标 (WGS84 / ENU)", this);
  auto *form = new QFormLayout(group);

  latSpin_ = new QDoubleSpinBox(this);
  latSpin_->setRange(-90, 90);
  latSpin_->setDecimals(5);
  latSpin_->setSuffix(" °");
  latSpin_->setValue(36.00558);
  form->addRow("纬度:", latSpin_);

  lonSpin_ = new QDoubleSpinBox(this);
  lonSpin_->setRange(-180, 180);
  lonSpin_->setDecimals(5);
  lonSpin_->setSuffix(" °");
  lonSpin_->setValue(120.12091);
  form->addRow("经度:", lonSpin_);

  elevSpin_ = new QDoubleSpinBox(this);
  elevSpin_->setRange(-9999, 9999);
  elevSpin_->setDecimals(1);
  elevSpin_->setSuffix(" m");
  form->addRow("海拔:", elevSpin_);

  headingSpin_ = new QDoubleSpinBox(this);
  headingSpin_->setRange(-180, 180);
  headingSpin_->setDecimals(3);
  headingSpin_->setSuffix(" °");
  form->addRow("航向:", headingSpin_);

  mainLayout->addWidget(group);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  UiTheme::setButtonIcon(buttons->button(QDialogButtonBox::Ok),
                         UiTheme::Icon::kOk);
  UiTheme::setButtonIcon(buttons->button(QDialogButtonBox::Cancel),
                         UiTheme::Icon::kCancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);
}

double WorldSettingsDialog::latitudeDeg() const
{
  return latSpin_->value();
}

double WorldSettingsDialog::longitudeDeg() const
{
  return lonSpin_->value();
}

double WorldSettingsDialog::elevation() const
{
  return elevSpin_->value();
}

double WorldSettingsDialog::headingDeg() const
{
  return headingSpin_->value();
}
