/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 移动实体到目标子世界链对话框（下拉已有链 + 可编辑新建链 + 顶层） */

#include "MoveToChainDialog.hh"

#include "../shared/UiTheme.hh"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegExp>
#include <QVBoxLayout>

MoveToChainDialog::MoveToChainDialog(const QStringList &_chains,
                                     const QString &_entityDesc,
                                     QWidget *parent)
    : QDialog(parent)
{
  setWindowTitle("移动到子世界");
  resize(340, 140);

  auto *mainLayout = new QVBoxLayout(this);

  auto *label = new QLabel(QString("将%1移动到哪个子世界下？\n"
                                   "可选已有链，或输入新链名新建（可多级，如 前海/内湾）。")
                               .arg(_entityDesc),
                           this);
  label->setWordWrap(true);
  mainLayout->addWidget(label);

  auto *form = new QFormLayout();
  chainCombo_ = new QComboBox(this);
  chainCombo_->setEditable(true);
  chainCombo_->setInsertPolicy(QComboBox::NoInsert);
  chainCombo_->addItem("顶层");
  for (const QString &c : _chains)
  {
    if (chainCombo_->findText(c) < 0)
      chainCombo_->addItem(c);
  }
  form->addRow("目标链:", chainCombo_);
  mainLayout->addLayout(form);

  auto *buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  UiTheme::setButtonIcon(buttons->button(QDialogButtonBox::Ok),
                         UiTheme::Icon::kOk);
  UiTheme::setButtonIcon(buttons->button(QDialogButtonBox::Cancel),
                         UiTheme::Icon::kCancel);
  connect(buttons, &QDialogButtonBox::accepted,
          this, &MoveToChainDialog::validateAndAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);
}

QString MoveToChainDialog::targetChain() const
{
  QString t = chainCombo_->currentText().trimmed();
  if (t == "顶层") return QString();
  return t;
}

void MoveToChainDialog::validateAndAccept()
{
  const QString t = chainCombo_->currentText().trimmed();
  if (t == "顶层")
  {
    accept();
    return;
  }
  if (t.isEmpty())
  {
    QMessageBox::warning(this, "警告", "目标链不能为空");
    return;
  }
  // 链段校验：字母/数字/下划线（可 / 分隔多级）
  QRegExp re("[a-zA-Z0-9_/]+$");
  if (!re.exactMatch(t))
  {
    QMessageBox::warning(this, "警告",
        "链名只能包含字母、数字、下划线（多级用 / 分隔）");
    return;
  }
  accept();
}