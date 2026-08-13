/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 世界原点设置对话框（经纬高/航向，仅运行时生效，不持久化） */

#ifndef WORLD_SETTINGS_DIALOG_HH_
#define WORLD_SETTINGS_DIALOG_HH_

#include <QDialog>
#include <QDoubleSpinBox>

class WorldSettingsDialog : public QDialog
{
  Q_OBJECT

public:
  explicit WorldSettingsDialog(QWidget *parent = nullptr);

  double latitudeDeg() const;
  double longitudeDeg() const;
  double elevation() const;
  double headingDeg() const;

private:
  QDoubleSpinBox *latSpin_;
  QDoubleSpinBox *lonSpin_;
  QDoubleSpinBox *elevSpin_;
  QDoubleSpinBox *headingSpin_;
};

#endif
