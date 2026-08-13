/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 浮力体编辑器对话框 — 含 marker 3D 预览 */
/// ZhangYu 创建 2026.5.6, marker 2026.5.7

#ifndef BUOYANCY_EDITOR_DIALOG_HH_
#define BUOYANCY_EDITOR_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QTimer>

#include <string>
#include <vector>

#include <gz/transport/Node.hh>
#include <gz/math/Pose3.hh>

#include "ModelManager.hh"
#include "../shared/Buoyancy3DView.hh"

class BuoyancyEditorDialog : public QDialog
{
  Q_OBJECT

public:
  BuoyancyEditorDialog(const std::string &_modelName,
                       const std::string &_linkName,
                       gz::transport::Node &_transportNode,
                       const Buoyancy3DModelInfo &_modelInfo,
                       const std::string &_worldName = "keship_world",
                       QWidget *parent = nullptr);
  ~BuoyancyEditorDialog() override;

  keship::BuoyancyConfig getConfig() const { return config_; }
  void setConfig(const keship::BuoyancyConfig &_config);
  void setCreateMode();

private slots:
  void onBodySelected(int _index);
  void onTypeChanged(int _typeIdx);
  void onParamChanged();
  void onAddBody();
  void onDeleteBody();
  void onSaveBody();
  void onApplyAndRebuild();

private:
  void updateList();
  void updateParamPanel();
  void update3DView();

  std::string modelName_;
  std::string linkName_;
  std::string worldName_;
  keship::BuoyancyConfig config_;

  QComboBox *linkCombo_;
  QListWidget *bodyList_;
  QComboBox *typeCombo_;
  QDoubleSpinBox *dxSpin_, *dySpin_, *dzSpin_;
  QDoubleSpinBox *rSpin_, *lSpin_;
  QDoubleSpinBox *posX_, *posY_, *posZ_;
  QDoubleSpinBox *roll_, *pitch_, *yaw_;
  QWidget *boxParams_, *cylParams_;
  QGroupBox *paramGroup_ = nullptr;

  int currentBodyIdx_ = -1;
  keship::PrimitiveType pendingType_ = keship::PrimitiveType::Box;

  bool isLoadingParams_ = false;
  bool isCreateMode_ = false;  /// 首次创建模式（vs 编辑已有模型）

  Buoyancy3DView *view3d_ = nullptr;

  gz::transport::Node &transportNode_;
};

#endif // BUOYANCY_EDITOR_DIALOG_HH_
