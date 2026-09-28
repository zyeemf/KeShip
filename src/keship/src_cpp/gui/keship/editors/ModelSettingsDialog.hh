/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 模型设置对话框 */
/// Zhangyu 创建 2026.1.20

#ifndef MODEL_SETTINGS_DIALOG_HH_
#define MODEL_SETTINGS_DIALOG_HH_



#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>

#include "../shared/Buoyancy3DView.hh"
/// 碰撞体积计算方式（`keship::CollisionMode`）—— 与 ModelManager.hh **同源**
#include "ModelManager.hh"

class ModelSettingsDialog : public QDialog
{
  Q_OBJECT

public:
  enum Mode { Full, OnlyPose, Ksm };

  explicit ModelSettingsDialog(QWidget *parent = nullptr);
  ModelSettingsDialog(const QStringList &_instanceNameList,
                      Mode _mode = Full, QWidget *parent = nullptr);
  ModelSettingsDialog(const QString &_modelName, double pos[6],
                      Mode _mode = OnlyPose, QWidget *parent = nullptr);

  
public: /// 获取设置的值
  /// 获取模型名称
  QString getModelName() const;
  /// 获取位置坐标
  double getPosX() const;
  double getPosY() const;
  double getPosZ() const;
  /// 获取旋转角度
  double getRoll() const;
  double getPitch() const;
  double getYaw() const;
  /// 预填位姿（弧度制 [x y z r p y]，复制模型等场景复用）
  void setPose(const double _pos[6]);
  /// 预填模型名称（复制模型等场景复用）
  void setDefaultName(const QString &_name);
  /// 获取是否静态模型和是否添加浮力项
  bool isStatic() const;
  bool isAddBuoyancy() const;
  bool isAddInertia() const;
  /// 获取/设置惯性质量和惯性张量
  double getInertiaMass() const;
  std::vector<double> getInertiaTensor() const;
  void setInertiaValues(double _mass,
      double _ixx, double _ixy, double _ixz,
      double _iyy, double _iyz, double _izz);
  /// 重心偏移 (CoG)
  double getCogX() const;
  double getCogY() const;
  double getCogZ() const;
  void setCogValues(double _cogX, double _cogY, double _cogZ);
  void setStatic(bool _isStatic);
  void setBuoyancy(bool _hasBuoyancy);
  void setModelInfo(const Buoyancy3DModelInfo &_info);
  void setShowCoGPreview(bool _show);
  /// 碰撞体积的**计算方式**（结构网格 / 凸包 / 无）—— 2026-09-28 新增
  keship::CollisionMode getCollisionMode() const;
  void setCollisionMode(keship::CollisionMode _mode);

private slots:
  void validateAndAccept();
  void updateCoGPreview();

private:
  QStringList instanceNameList;
private: // 
  // std::string instanceName;
  // double pos[6];
private:
  /// @brief 模型名称输入框
  QLineEdit *instanceNameEdit;
  /// @brief 位置坐标输入框
  QDoubleSpinBox *posXSpinBox;
  QDoubleSpinBox *posYSpinBox;
  QDoubleSpinBox *posZSpinBox;
  /// @brief 旋转角度输入框
  QDoubleSpinBox *rollSpinBox;
  QDoubleSpinBox *pitchSpinBox;
  QDoubleSpinBox *yawSpinBox;
  /// @brief 静态模型复选框
  QCheckBox *staticCheckBox;
  /// @brief 添加浮力项复选框
  QCheckBox *addBuoyancyCheckBox;
  /// @brief 碰撞体积计算方式下拉框（结构网格 / 凸包 / 无）
  QComboBox *collisionModeCombo = nullptr;

  /// @brief 是否设置惯性
  QCheckBox *setInertiaCheckBox;
  /// @brief 惯性质量输入框
  QDoubleSpinBox *inertiaMassSpinBox;
  /// @brief 惯性张量输入框
  QDoubleSpinBox *inertiaTensorSpinBoxes[9];
  /// @brief 重心偏移输入框
  QDoubleSpinBox *cogXSpinBox_;
  QDoubleSpinBox *cogYSpinBox_;
  QDoubleSpinBox *cogZSpinBox_;

  /// @brief CoG 3D 预览
  Buoyancy3DView *cogPreview_ = nullptr;

};

#endif  // MODEL_SETTINGS_DIALOG_HH_