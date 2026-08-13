/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 选择世界对话框：列出父目录下 *.ksw 世界文件夹，双击即加载 */

#ifndef SELECT_WORLD_DIALOG_HH_
#define SELECT_WORLD_DIALOG_HH_

#include <QDialog>
#include <QUrl>

class QListWidget;
class QLineEdit;
class QPushButton;

/// \brief 选择 KSW 世界文件夹对话框
///
/// 结构：
///  - 当前父目录（只读显示）+ "浏览..." 按钮（更换父目录，含侧边栏快捷路径）
///  - 世界列表（仅显示 *.ksw 结尾的子目录，排序）；双击条目即选中并 accept；
///    "确定" 按钮仅在选中条目时可用
class SelectWorldDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _dir 初始父目录（默认 models_ksw）
  /// @param _browseUrls 浏览对话框侧栏快捷路径
  explicit SelectWorldDialog(const QString &_dir,
                             const QList<QUrl> &_browseUrls,
                             QWidget *parent = nullptr);

  /// 选中的世界文件夹完整路径（父目录/名字.ksw；未选中返回空）
  QString selectedDir() const;

private slots:
  void browseDir();

private:
  void refreshList();

  QLineEdit *dirEdit_;
  QListWidget *list_;
  QPushButton *okBtn_;
  QList<QUrl> browseUrls_;
  QString parentDir_;
};

#endif
