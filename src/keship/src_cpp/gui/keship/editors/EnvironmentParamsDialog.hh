/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 风浪流环境参数编辑对话框 */

#ifndef ENVIRONMENT_PARAMS_DIALOG_HH_
#define ENVIRONMENT_PARAMS_DIALOG_HH_

#include <QDialog>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>

#include "keship/utils/WaterArea.hh"

class EnvironmentParamsDialog : public QDialog
{
  Q_OBJECT

public:
  EnvironmentParamsDialog(const keship::WaterArea &_area,
                          QWidget *parent = nullptr);

  keship::WaterArea getWaterArea() const;

private slots:
  void onWindChanged();
  void onWaveDirAutoToggled(bool _checked);
  void onParamEdited();
  void onSeaStateSelected(int _index);
  void onSeaConfirmYes();
  void onSeaConfirmNo();

private:
  void setupUi();
  void loadFrom(const keship::WaterArea &_area);
  void updateSeaStateLabel();
  void showSeaConfirm(int _level);
  void applySeaStateLevel(int _level);

  keship::WaterArea area_;

  // 海况
  QLabel      *seaStateLabel_;
  QComboBox   *seaStateCombo_;
  int          pendingSeaLevel_{-1};
  QWidget     *seaConfirmBar_{nullptr};
  QLabel      *seaConfirmLabel_{nullptr};
  QPushButton *seaConfirmYes_{nullptr};
  QPushButton *seaConfirmNo_{nullptr};

  // 风
  QDoubleSpinBox *windSpeedSpin_;
  QDoubleSpinBox *windDirSpin_;

  // 浪
  QDoubleSpinBox *wavePeriodSpin_;
  QDoubleSpinBox *waveDirSpin_;
  QSpinBox      *waveNumSpin_;
  QDoubleSpinBox *waveAngleSpin_;
  QDoubleSpinBox *waveScaleSpin_;
  QDoubleSpinBox *waveGainSpin_;
  QDoubleSpinBox *waveSteepSpin_;
  QDoubleSpinBox *waveTauSpin_;

  // 浪向跟随风向
  QCheckBox *waveDirAutoCheck_;

  // 流
  QDoubleSpinBox *currentSpeedSpin_;
  QDoubleSpinBox *currentDirSpin_;
};

#endif
