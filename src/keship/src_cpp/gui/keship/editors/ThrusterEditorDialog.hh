/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 推进器编辑器对话框 — 含 3D 箭头预览 */
/// ZhangYu 创建 2026.5.11

#ifndef THRUSTER_EDITOR_DIALOG_HH_
#define THRUSTER_EDITOR_DIALOG_HH_

#include <QDialog>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QDoubleSpinBox>
#include <QGroupBox>

#include <string>
#include <vector>

#include <gz/transport/Node.hh>

#include "ModelManager.hh"
#include "../shared/Buoyancy3DView.hh"

class ThrusterEditorDialog : public QDialog
{
  Q_OBJECT

public:
  ThrusterEditorDialog(const std::string &_modelName,
                       const std::string &_linkName,
                       gz::transport::Node &_transportNode,
                       const Buoyancy3DModelInfo &_modelInfo,
                       const std::string &_worldName = "keship_world",
                       QWidget *parent = nullptr);
  ~ThrusterEditorDialog() override;

  keship::ThrusterConfig getConfig() const { return config_; }
  void setConfig(const keship::ThrusterConfig &_config);

private slots:
  void onBodySelected(int _index);
  void onParamChanged();
  void onAddBody();
  void onDeleteBody();
  bool onSaveBody();
  void onApplyAndRebuild();

private:
  void updateList();
  void updateParamPanel();
  void update3DView();

  std::string modelName_;
  std::string linkName_;
  std::string worldName_;
  keship::ThrusterConfig config_;

  QComboBox *linkCombo_;
  QListWidget *bodyList_;
  QLineEdit *nameEdit_;
  QDoubleSpinBox *posX_, *posY_, *posZ_;
  QDoubleSpinBox *roll_, *pitch_, *yaw_;
  QDoubleSpinBox *maxForce_;

  QGroupBox *paramGroup_ = nullptr;
  int currentBodyIdx_ = -1;
  bool isLoadingParams_ = false;

  Buoyancy3DView *view3d_ = nullptr;
  gz::transport::Node &transportNode_;
};

#endif // THRUSTER_EDITOR_DIALOG_HH_
