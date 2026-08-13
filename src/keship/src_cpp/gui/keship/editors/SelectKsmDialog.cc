/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 选择模型对话框实现 */

#include "SelectKsmDialog.hh"

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>

SelectKsmDialog::SelectKsmDialog(const QString &_dir,
                                 const QList<QUrl> &_browseUrls,
                                 QWidget *parent)
    : QDialog(parent), browseUrls_(_browseUrls), parentDir_(_dir)
{
  setWindowTitle("选择模型");
  setMinimumWidth(420);
  resize(420, 480);

  auto *mainLayout = new QVBoxLayout(this);

  // 父目录（只读显示 + 浏览）
  auto *dirRow = new QHBoxLayout;
  dirEdit_ = new QLineEdit(this);
  dirEdit_->setReadOnly(true);
  auto *browseBtn = new QPushButton("浏览...", this);
  connect(browseBtn, &QPushButton::clicked, this, &SelectKsmDialog::browseDir);
  dirRow->addWidget(dirEdit_, 1);
  dirRow->addWidget(browseBtn);
  mainLayout->addLayout(dirRow);

  // 模型列表（仅 *.ksm 文件）
  list_ = new QListWidget(this);
  connect(list_, &QListWidget::itemDoubleClicked, this,
      [this](QListWidgetItem *item) {
    if (item) accept();
  });
  connect(list_, &QListWidget::itemSelectionChanged, this, [this]() {
    if (okBtn_) okBtn_->setEnabled(!selectedFile().isEmpty());
  });
  mainLayout->addWidget(list_, 1);

  // 按钮
  auto *btnRow = new QHBoxLayout;
  auto *rawBtn = new QPushButton("从其他目录选择原始模型...", this);
  connect(rawBtn, &QPushButton::clicked, this, &SelectKsmDialog::browseRawFile);
  okBtn_ = new QPushButton("确定", this);
  okBtn_->setEnabled(false);
  connect(okBtn_, &QPushButton::clicked, this, &QDialog::accept);
  auto *cancelBtn = new QPushButton("取消", this);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  btnRow->addWidget(rawBtn);
  btnRow->addStretch();
  btnRow->addWidget(okBtn_);
  btnRow->addWidget(cancelBtn);
  mainLayout->addLayout(btnRow);

  dirEdit_->setText(parentDir_);
  refreshList();
}

void SelectKsmDialog::refreshList()
{
  list_->clear();
  QDir dir(parentDir_);
  if (!dir.exists()) return;
  const QFileInfoList entries = dir.entryInfoList(
      QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo &fi : entries)
    if (fi.fileName().endsWith(".ksm"))
      list_->addItem(fi.fileName());
  if (okBtn_) okBtn_->setEnabled(false);
}

void SelectKsmDialog::browseDir()
{
  QFileDialog dlg(this, "选择模型目录");
  dlg.setFileMode(QFileDialog::Directory);
  dlg.setOption(QFileDialog::ShowDirsOnly, true);
  dlg.setSidebarUrls(browseUrls_);
  if (!parentDir_.isEmpty())
    dlg.setDirectory(parentDir_);
  if (dlg.exec() != QDialog::Accepted) return;
  const QString dir = dlg.selectedFiles().value(0);
  if (!dir.isEmpty())
  {
    parentDir_ = dir;
    dirEdit_->setText(parentDir_);
    refreshList();
  }
}

void SelectKsmDialog::browseRawFile()
{
  QFileDialog dlg(this, "选择原始模型文件",
                  QString(), "3D 模型 (*.dae *.sdf *.stl *.obj *.gltf *.glb);;"
                              "DAE (*.dae);;SDF (*.sdf);;STL (*.stl);;OBJ (*.obj);;"
                              "glTF (*.gltf *.glb);;All Files (*)");
  dlg.setFileMode(QFileDialog::ExistingFile);
  dlg.setAcceptMode(QFileDialog::AcceptOpen);
  dlg.setSidebarUrls(browseUrls_);
  if (dlg.exec() != QDialog::Accepted) return;
  const QString path = dlg.selectedFiles().value(0);
  if (!path.isEmpty())
  {
    rawFilePath_ = path;
    accept();
  }
}

QString SelectKsmDialog::selectedFile() const
{
  const auto *item = list_->currentItem();
  if (!item) return QString();
  return parentDir_ + "/" + item->text();
}

QString SelectKsmDialog::selectedRawFile() const
{
  return rawFilePath_;
}