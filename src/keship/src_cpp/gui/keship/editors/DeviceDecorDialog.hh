/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 设备外观装饰编辑器对话框 — 电池 / 电机（纯视觉，无传感器数据） */

#ifndef DEVICE_DECOR_DIALOG_HH_
#define DEVICE_DECOR_DIALOG_HH_

#include <QDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QComboBox>

#include <vector>

#include "ModelManager.hh"

/// @brief 设备外观装饰编辑器：列表 + 类型 + 位姿六轴。
///
/// 与 SensorEditorDialog 的差别：
///   · **不产生任何传感器**（无 topic / 无数据），只挂一个 <visual>；
///   · 没有 3D 预览与相机规格（装饰件没有测量语义）；
///   · 推进器**不在**这里 —— 它的 pose 归推进器编辑器，外观自动跟随。
class DeviceDecorDialog : public QDialog
{
  Q_OBJECT

public:
  explicit DeviceDecorDialog(const QString &_modelName,
                             const QString &_linkName,
                             QWidget *_parent = nullptr);

  keship::DeviceDecorConfig getConfig() const;
  void setConfig(const keship::DeviceDecorConfig &_config);

private slots:
  void onAddItem();
  void onDeleteItem();
  void onSelectionChanged();
  bool onSaveInstance();
  void onApplyAndRebuild();

private:
  void updateList();
  void updateParamPanel();
  QString autoName(keship::DeviceType _type) const;

  struct InternalItem {
    keship::DeviceType type = keship::DeviceType::Battery;
    QString name;
    double pose[6] = {0, 0, 0, 0, 0, 0};
  };

  std::vector<InternalItem> config_;
  QString modelName_;
  QString linkName_;

  QListWidget *listWidget_ = nullptr;
  QGroupBox *panel_ = nullptr;
  QComboBox *typeCombo_ = nullptr;
  QLineEdit *nameEdit_ = nullptr;
  QDoubleSpinBox *poseSpin_[6] = {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr};
  QPushButton *saveBtn_ = nullptr;

  bool isLoading_ = false;
  int selectedIdx_ = -1;
};

#endif
