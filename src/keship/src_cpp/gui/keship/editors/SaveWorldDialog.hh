/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 保存世界对话框：世界名 + 保存目录 + 模型/水域树勾选 */

#ifndef SAVE_WORLD_DIALOG_HH_
#define SAVE_WORLD_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include <QTreeWidget>
#include <QDialogButtonBox>

class QPushButton;

/// \brief 保存 KSW 世界对话框
///
/// 结构：
///  - 世界名输入框（字母数字下划线，无斜杠）
///  - 保存到目录（可编辑 + 浏览按钮）
///  - 模型/水域树勾选（无世界名根节点：世界名可编辑，避免误导）：
///    顶层“模型”“水域”分组（新建实体）与各子 ksw 链分组（组内“模型”“水域”分类），
///    模型叶子显示原名（实例名仅作提交键）
///  - 全选 / 清空按钮
class SaveWorldDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _worldName 初始世界名（建议：目录名）
  /// @param _modelTree 模型树文本行（每行：子World链 \t 勾选标志 \t 原名 \t 实例名；
  ///        兼容 3 段（无第四段时原名=实例名））
  /// @param _waterNames 水域行（每行：子World链 \t 勾选标志 \t 水域名；
  ///        仅名字的兼容列表 = 默认全勾选；空链 → 顶层“水域”分组）
  /// @param _parentDir 初始父目录（自动拼接 <世界名>/ 子目录）
  /// @param _browseUrls 浏览对话框侧栏快捷路径
  explicit SaveWorldDialog(const QString &_worldName,
                           const QStringList &_modelTree,
                           const QStringList &_waterNames,
                           const QString &_parentDir,
                           const QList<QUrl> &_browseUrls,
                           QWidget *parent = nullptr);

  QString worldName() const;
  /// 保存目录（父目录 + 世界名 子目录）
  QString saveDir() const;
  /// 勾选的模型实例名（叶子行，不含水域）
  QStringList selectedModels() const;
  /// 勾选的水域名
  QStringList selectedWaterNames() const;

private slots:
  void browseDir();
  void onAccept();

private:
  enum ItemRole { WaterRole = Qt::UserRole };

  void updateDirFromName(const QString &_oldName);
  void refreshDirEdit();
  void buildTree(const QStringList &_modelTree, const QStringList &_waterNames);
  void setAllChecked(bool _checked);

  QLineEdit *nameEdit_;
  QLineEdit *dirEdit_;
  QTreeWidget *tree_;
  QList<QUrl> browseUrls_;
  QString parentDir_;
};

#endif