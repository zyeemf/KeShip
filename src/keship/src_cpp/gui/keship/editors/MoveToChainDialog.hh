/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 移动实体到目标子世界链对话框（下拉已有链 + 可编辑新建链 + 顶层） */

#ifndef MOVE_TO_CHAIN_DIALOG_HH_
#define MOVE_TO_CHAIN_DIALOG_HH_

#include <QDialog>
#include <QComboBox>

class MoveToChainDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _chains 现有子世界链清单（不含顶层）
  /// @param _entityDesc 实体类型说明（如"模型"、"水域"、"子世界"）
  MoveToChainDialog(const QStringList &_chains,
                    const QString &_entityDesc,
                    QWidget *parent = nullptr);

  /// 目标链（"顶层" → 空字符串；可输入新建链）
  QString targetChain() const;

private slots:
  void validateAndAccept();

private:
  QComboBox *chainCombo_;
};

#endif