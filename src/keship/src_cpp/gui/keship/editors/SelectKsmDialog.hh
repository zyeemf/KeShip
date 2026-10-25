/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 选择模型对话框：列出 *.ksm 模型文件，双击即加载；或从其他目录选择原始模型 */

#ifndef SELECT_KSM_DIALOG_HH_
#define SELECT_KSM_DIALOG_HH_

#include <QDialog>
#include <QUrl>

class QListWidget;
class QLineEdit;
class QPushButton;

/// \brief 选择 Keship 模型（.ksm）对话框
///
/// 结构：
///  - 当前父目录（只读显示）+ "浏览..." 按钮（更换父目录，含侧边栏快捷路径）
///  - 模型列表（仅显示 *.ksm 结尾的文件，排序）；双击条目即选中并 accept；
///    "确定" 按钮仅在选中条目时可用
///  - "从其他目录选择原始模型..." 按钮：QFileDialog 选择 dae/sdf/stl/gltf/glb，
///    选中后 accept（selectedRawFile 返回该路径）
class SelectKsmDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _dir 初始父目录（默认 ks_model）
  /// @param _browseUrls 浏览对话框侧栏快捷路径
  /// @param _ksmOnly true = 只列 .ksm（隐藏"从其他目录选择原始模型"通道）
  explicit SelectKsmDialog(const QString &_dir,
                           const QList<QUrl> &_browseUrls,
                           QWidget *parent = nullptr,
                           bool _ksmOnly = false);

  /// 选中的 .ksm 完整路径（列表通道；未选中返回空）
  QString selectedFile() const;

  /// 选中的原始模型完整路径（"从其他目录选择原始模型"通道；未使用返回空）
  QString selectedRawFile() const;

private slots:
  void browseDir();
  void browseRawFile();

private:
  void refreshList();

  QLineEdit *dirEdit_;
  QListWidget *list_;
  QPushButton *okBtn_;
  QList<QUrl> browseUrls_;
  QString parentDir_;
  QString rawFilePath_;
  /// true = 仅 .ksm 入口（2026-10-25 菜单拆分）
  bool ksmOnly_ = false;
};

#endif