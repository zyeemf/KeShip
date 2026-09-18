/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 选择世界对话框实现 */

#include "SelectWorldDialog.hh"

#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>

SelectWorldDialog::SelectWorldDialog(const QString &_dir,
                                     const QList<QUrl> &_browseUrls,
                                     QWidget *parent)
    : QDialog(parent), browseUrls_(_browseUrls), parentDir_(_dir)
{
  setWindowTitle("选择世界");
  setMinimumWidth(420);
  resize(420, 480);

  auto *mainLayout = new QVBoxLayout(this);

  // 父目录（只读显示 + 浏览）
  auto *dirRow = new QHBoxLayout;
  dirEdit_ = new QLineEdit(this);
  dirEdit_->setReadOnly(true);
  auto *browseBtn = new QPushButton("浏览...", this);
  connect(browseBtn, &QPushButton::clicked, this, &SelectWorldDialog::browseDir);
  dirRow->addWidget(dirEdit_, 1);
  dirRow->addWidget(browseBtn);
  mainLayout->addLayout(dirRow);

  // 世界列表（仅 *.ksw 子目录）
  list_ = new QListWidget(this);
  connect(list_, &QListWidget::itemDoubleClicked, this,
      [this](QListWidgetItem *item) {
    if (item) accept();
  });
  connect(list_, &QListWidget::itemSelectionChanged, this, [this]() {
    if (okBtn_) okBtn_->setEnabled(!selectedDir().isEmpty());
  });
  mainLayout->addWidget(list_, 1);

  // 按钮
  auto *btnRow = new QHBoxLayout;
  okBtn_ = new QPushButton("确定", this);
  okBtn_->setEnabled(false);
  connect(okBtn_, &QPushButton::clicked, this, &QDialog::accept);
  auto *cancelBtn = new QPushButton("取消", this);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
  btnRow->addStretch();
  btnRow->addWidget(okBtn_);
  btnRow->addWidget(cancelBtn);
  btnRow->addStretch();
  for (QPushButton *b : {okBtn_, cancelBtn}) b->setMinimumHeight(30);
  mainLayout->addLayout(btnRow);

  dirEdit_->setText(parentDir_);
  refreshList();
}

void SelectWorldDialog::refreshList()
{
  list_->clear();
  QDir dir(parentDir_);
  if (!dir.exists()) return;
  const QFileInfoList entries = dir.entryInfoList(
      QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QFileInfo &fi : entries)
    if (fi.fileName().endsWith(".ksw"))
      list_->addItem(fi.fileName());
  if (okBtn_) okBtn_->setEnabled(false);
}

void SelectWorldDialog::browseDir()
{
  QFileDialog dlg(this, "选择世界父目录");
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

QString SelectWorldDialog::selectedDir() const
{
  const auto *item = list_->currentItem();
  if (!item) return QString();
  return parentDir_ + "/" + item->text();
}
