/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 传感器编辑器对话框 — 多实例 + 位姿编辑 + 3D 预览 */
/// ZhangYu 创建 2026.5.16

#ifndef SENSOR_EDITOR_DIALOG_HH_
#define SENSOR_EDITOR_DIALOG_HH_

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>

#include "ModelManager.hh"
#include "../shared/Buoyancy3DView.hh"

class SensorEditorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit SensorEditorDialog(const QString &_modelName,
                              const QString &_linkName,
                              const Buoyancy3DModelInfo &_modelInfo,
                              QWidget *_parent = nullptr);

  keship::SensorConfig getConfig() const;
  void setConfig(const keship::SensorConfig &_config);

private slots:
  void onAddSensor();
  void onDeleteSensor();
  void onSelectionChanged();
  bool onSaveInstance();
  void onApplyAndRebuild();
  void onParamChanged();

private:
  void updateList();
  void updateParamPanel();
  void update3DView();
  QString autoName(keship::SensorType _type) const;
  Sensor3DInfo currentSensor3DInfo() const;

  struct InternalSensor {
    keship::SensorType type = keship::SensorType::Camera;
    QString name;
    double pose[6] = {0, 0, 0, 0, 0, 0};
  };
  std::vector<InternalSensor> config_;
  QString modelName_;
  QString linkName_;

  QListWidget *listWidget_;
  QGroupBox *rightPanel_;
  QLineEdit *nameEdit_;
  QDoubleSpinBox *poseSpin_[6];
  QLabel *typeLabel_;
  QPushButton *saveBtn_;
  Buoyancy3DView *view3d_ = nullptr;

  bool isLoading_ = false;
  int selectedIdx_ = -1;
};

#endif
