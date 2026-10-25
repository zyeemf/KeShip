/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 数学船型参数输入对话框 — Phase B */

#ifndef SHIP_HULL_DIALOG_HH_
#define SHIP_HULL_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStringList>

#include "ShipHullGenerator.hh"

/// 数学船型参数输入对话框（非模态）
/// 选择船型 + 输入 L/B/D/ρ → 预览静水力 →「生成模型」发信号 → 外部 spawn
/// ★ 2026-10-25：补「实例名称 + 位置 + 姿态」——此前名称取自船型名、位姿由调用方硬编码，
///   生成后既改不了名也放不到位（SL-02 张老师反馈；HS-01 与「生成数学船型」是同一入口）
class ShipHullDialog : public QDialog {
  Q_OBJECT
 public:
  /// @param _instanceNames 现有实例名列表（重名校验用；可空）
  explicit ShipHullDialog(const QStringList &_instanceNames = QStringList(),
                          QWidget *_parent = nullptr);
  ~ShipHullDialog() override = default;

  /// 生成位姿（x/y/z 米，roll/pitch/yaw 弧度）—— 与 SpawnMathHull 的 _pos 同口径
  void getPose(double _pos[6]) const;
  /// 刷新现有实例名（生成成功后由调用方回填，保证连续生成时重名校验不用旧列表）
  void setInstanceNames(const QStringList &_instanceNames);

 signals:
  /// 用户点击「生成模型」时发出，由外部负责 spawn
  void spawnRequested(keship::ShipHullSpec spec, std::string name);

 private slots:
  void onTypeChanged();
  void onParamChanged();
  void onShowLinesPlan();
  void onSpawn();

 private:
  void setupUI();
  void updateHydroDisplay();
  /// 名称框未被打字改过时，按船型名重置默认名（与已有实例重名则加 _2/_3…）
  void updateDefaultName();
  std::string modelName() const;

  QStringList instanceNames_;
  QComboBox *typeCombo_;
  QDoubleSpinBox *lengthSpin_, *breadthSpin_, *depthSpin_;
  QDoubleSpinBox *densitySpin_;

  /// 「生成设置」——名称与位姿（2026-10-25 新增）
  QLineEdit *nameEdit_;
  QDoubleSpinBox *posXSpin_, *posYSpin_, *posZSpin_;
  QDoubleSpinBox *rollSpin_, *pitchSpin_, *yawSpin_;
  bool nameEdited_ = false;  ///< 用户手动改过名称后，不再随船型联动
  bool zAuto_ = true;        ///< Z 未被手动改过时跟随型深（D/2+0.1）

  QLabel *awLabel_, *cwpLabel_, *ciLabel_;
  QLabel *volLabel_, *kbLabel_, *bmLabel_, *gmLabel_;
  QLabel *draftLabel_;

  QPushButton *spawnBtn_, *closeBtn_;

  keship::ShipHullSpec spec_;
};

#endif  // SHIP_HULL_DIALOG_HH_
