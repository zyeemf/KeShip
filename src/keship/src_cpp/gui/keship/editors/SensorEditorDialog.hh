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
#include <QComboBox>
#include <QFormLayout>

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
  /// ★ 2026-09-26「Camera 规格用户自选」：仅 Camera 时显示规格三行
  void updateCameraRows();
  /// 规格控件变化 → 重算并显示"预计成本"（含夹紧后的实际生效值回显）
  void onCameraSpecChanged();
  QString autoName(keship::SensorType _type) const;
  Sensor3DInfo currentSensor3DInfo() const;

  struct InternalSensor {
    keship::SensorType type = keship::SensorType::Camera;
    QString name;
    double pose[6] = {0, 0, 0, 0, 0, 0};
    /// ★ 2026-09-26：Camera 规格（仅 Camera 有效）。0 = 未指定 ⇒ 默认档。
    int    camWidth  = 0;
    int    camHeight = 0;
    double camRate   = 0.0;
  };
  std::vector<InternalSensor> config_;
  QString modelName_;
  QString linkName_;

  QListWidget *listWidget_;
  QGroupBox *rightPanel_;
  QFormLayout *formLayout_ = nullptr;
  QLineEdit *nameEdit_;
  QDoubleSpinBox *poseSpin_[6];
  QLabel *typeLabel_;
  QPushButton *saveBtn_;
  Buoyancy3DView *view3d_ = nullptr;
  /// ★ 2026-09-26「Camera 规格用户自选」三行（仅 Camera 类型时可见）
  QComboBox *camResCombo_  = nullptr;   ///< 分辨率档（表同 ModelManager.hh）
  QComboBox *camRateCombo_ = nullptr;   ///< 帧率档（表同 ModelManager.hh）
  QLabel    *camCostLabel_ = nullptr;   ///< 预计成本 ×N（含夹紧回显）

  bool isLoading_ = false;
  int selectedIdx_ = -1;
};

#endif
