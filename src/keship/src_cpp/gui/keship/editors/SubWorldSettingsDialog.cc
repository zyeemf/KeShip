/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 子世界属性对话框（链名改名 + 整体位姿偏移 + 链内实体清单） */

#include "SubWorldSettingsDialog.hh"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QRegExp>
#include <QVBoxLayout>

SubWorldSettingsDialog::SubWorldSettingsDialog(const QString &_chainName,
                                               const QStringList &_models,
                                               const QStringList &_waters,
                                               QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle("子世界属性");
  setModal(true);
  resize(360, 480);

  auto *mainLayout = new QVBoxLayout(this);

  /// 链名（末段）
  auto *nameGroup = new QGroupBox("名称", this);
  auto *nameLayout = new QHBoxLayout(nameGroup);
  nameEdit_ = new QLineEdit(_chainName, nameGroup);
  nameEdit_->setPlaceholderText("字母/数字/下划线");
  nameLayout->addWidget(new QLabel("链名:", nameGroup));
  nameLayout->addWidget(nameEdit_, 1);
  mainLayout->addWidget(nameGroup);

  /// 整体位姿偏移（对应载入世界时的偏移参数）
  auto *offGroup = new QGroupBox("整体位姿偏移（应用到链内全部模型）", this);
  auto *offForm = new QFormLayout(offGroup);

  offXSpin_ = new QDoubleSpinBox(offGroup);
  offXSpin_->setRange(-9999, 9999);
  offXSpin_->setDecimals(2);
  offXSpin_->setSuffix(" m");
  offForm->addRow("Δx:", offXSpin_);

  offYSpin_ = new QDoubleSpinBox(offGroup);
  offYSpin_->setRange(-9999, 9999);
  offYSpin_->setDecimals(2);
  offYSpin_->setSuffix(" m");
  offForm->addRow("Δy:", offYSpin_);

  offZSpin_ = new QDoubleSpinBox(offGroup);
  offZSpin_->setRange(-9999, 9999);
  offZSpin_->setDecimals(2);
  offZSpin_->setSuffix(" m");
  offForm->addRow("Δz:", offZSpin_);

  offRollSpin_ = new QDoubleSpinBox(offGroup);
  offRollSpin_->setRange(-180, 180);
  offRollSpin_->setDecimals(2);
  offRollSpin_->setSuffix(" °");
  offForm->addRow("Δroll:", offRollSpin_);

  offPitchSpin_ = new QDoubleSpinBox(offGroup);
  offPitchSpin_->setRange(-180, 180);
  offPitchSpin_->setDecimals(2);
  offPitchSpin_->setSuffix(" °");
  offForm->addRow("Δpitch:", offPitchSpin_);

  offYawSpin_ = new QDoubleSpinBox(offGroup);
  offYawSpin_->setRange(-180, 180);
  offYawSpin_->setDecimals(2);
  offYawSpin_->setSuffix(" °");
  offForm->addRow("Δyaw:", offYawSpin_);

  mainLayout->addWidget(offGroup);

  /// 链内实体只读清单
  auto *entityGroup = new QGroupBox("链内实体", this);
  auto *entityLayout = new QVBoxLayout(entityGroup);
  entityList_ = new QListWidget(entityGroup);
  entityList_->setSelectionMode(QAbstractItemView::NoSelection);
  entityList_->setFocusPolicy(Qt::NoFocus);
  for (const QString &m : _models)
    entityList_->addItem("模型: " + m);
  for (const QString &w : _waters)
    entityList_->addItem("水域: " + w);
  entityLayout->addWidget(entityList_);
  mainLayout->addWidget(entityGroup, 1);

  /// 按钮
  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted,
          this, &SubWorldSettingsDialog::validateAndAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);
}

QString SubWorldSettingsDialog::chainName() const
{
  return nameEdit_->text().trimmed();
}

double SubWorldSettingsDialog::offsetX() const { return offXSpin_->value(); }
double SubWorldSettingsDialog::offsetY() const { return offYSpin_->value(); }
double SubWorldSettingsDialog::offsetZ() const { return offZSpin_->value(); }
double SubWorldSettingsDialog::offsetRollDeg() const { return offRollSpin_->value(); }
double SubWorldSettingsDialog::offsetPitchDeg() const { return offPitchSpin_->value(); }
double SubWorldSettingsDialog::offsetYawDeg() const { return offYawSpin_->value(); }

void SubWorldSettingsDialog::validateAndAccept()
{
  const QString name = chainName();
  if (name.isEmpty())
  {
    QMessageBox::warning(this, "警告", "链名不能为空");
    nameEdit_->setFocus();
    return;
  }
  QRegExp re("[a-zA-Z0-9_]+$");
  if (!re.exactMatch(name))
  {
    QMessageBox::warning(this, "警告", "链名只能包含字母、数字、下划线");
    nameEdit_->setFocus();
    return;
  }
  accept();
}