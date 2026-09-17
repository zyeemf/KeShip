/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 子世界属性对话框（链名改名 + 整体位姿偏移 + 链内实体清单） */

#ifndef SUB_WORLD_SETTINGS_DIALOG_HH_
#define SUB_WORLD_SETTINGS_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QListWidget>

class SubWorldSettingsDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _chainName 当前链名（末段）
  /// @param _models 链内模型显示名清单（含嵌套子链前缀）
  /// @param _waters 链内水域显示名清单（含嵌套子链前缀）
  SubWorldSettingsDialog(const QString &_chainName,
                         const QStringList &_models,
                         const QStringList &_waters,
                         QWidget *parent = nullptr);

  /// 新链名（末段，字母/数字/下划线）
  QString chainName() const;
  /// 整体位置偏移（m）
  double offsetX() const;
  double offsetY() const;
  double offsetZ() const;
  /// 整体旋转偏移（度）
  double offsetRollDeg() const;
  double offsetPitchDeg() const;
  double offsetYawDeg() const;

private slots:
  void validateAndAccept();

private:
  QLineEdit *nameEdit_;
  QDoubleSpinBox *offXSpin_;
  QDoubleSpinBox *offYSpin_;
  QDoubleSpinBox *offZSpin_;
  QDoubleSpinBox *offRollSpin_;
  QDoubleSpinBox *offPitchSpin_;
  QDoubleSpinBox *offYawSpin_;
  QListWidget *entityList_;
};

#endif