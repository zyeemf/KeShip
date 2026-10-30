/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 设备外观装饰编辑器对话框实现 — 列表 + 类型 + 位姿六轴 */

#include "DeviceDecorDialog.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>

DeviceDecorDialog::DeviceDecorDialog(const QString &_modelName,
                                     const QString &_linkName,
                                     QWidget *_parent)
    : QDialog(_parent), modelName_(_modelName), linkName_(_linkName)
{
  setWindowTitle("设备装饰 — " + modelName_);
  setModal(false);
  resize(520, 460);

  auto *mainLayout = new QVBoxLayout(this);
  auto *contentLayout = new QHBoxLayout();

  /// 左侧：设备列表 + 增删
  auto *leftPanel = new QVBoxLayout();
  listWidget_ = new QListWidget;
  listWidget_->setMinimumWidth(170);
  leftPanel->addWidget(listWidget_);

  auto *addDelLayout = new QHBoxLayout();
  auto *addBtn = new QPushButton("+ 添加");
  auto *delBtn = new QPushButton("删除");
  /**
   * 注：按钮文案统一为「删除」。源码里历史写法有 `x 删除`（字母 x 系笔误）与
   * `× 删除`（乘号）两种，SL-02 已统一，新增代码不再重复那个笔误。
   */
  addDelLayout->addWidget(addBtn);
  addDelLayout->addWidget(delBtn);
  leftPanel->addLayout(addDelLayout);

  /// 右侧：参数面板
  panel_ = new QGroupBox("装饰参数");
  auto *formLayout = new QFormLayout(panel_);

  typeCombo_ = new QComboBox;
  typeCombo_->addItem("电池", (int)keship::DeviceType::Battery);
  typeCombo_->addItem("电机", (int)keship::DeviceType::Motor);
  formLayout->addRow("类型:", typeCombo_);

  nameEdit_ = new QLineEdit;
  formLayout->addRow("名称:", nameEdit_);

  const char *poseLabels[] = {"X:", "Y:", "Z:", "Roll:", "Pitch:", "Yaw:"};
  const char *poseSuffix[] = {" m", " m", " m", "°", "°", "°"};
  for (int i = 0; i < 6; i++)
  {
    poseSpin_[i] = new QDoubleSpinBox;
    poseSpin_[i]->setRange(-1000, 1000);
    poseSpin_[i]->setDecimals(3);
    poseSpin_[i]->setSingleStep(0.05);
    poseSpin_[i]->setSuffix(poseSuffix[i]);
    formLayout->addRow(poseLabels[i], poseSpin_[i]);
  }

  saveBtn_ = new QPushButton("保存修改");
  formLayout->addRow(saveBtn_);

  leftPanel->addWidget(panel_);
  contentLayout->addLayout(leftPanel);
  panel_->hide();

  mainLayout->addLayout(contentLayout);

  /// 底部按钮
  auto *bottomLayout = new QHBoxLayout();
  auto *applyBtn = new QPushButton("应用并重建模型");
  auto *cancelBtn = new QPushButton("取消");
  bottomLayout->addStretch();
  bottomLayout->addWidget(applyBtn);
  bottomLayout->addWidget(cancelBtn);
  mainLayout->addLayout(bottomLayout);

  connect(listWidget_, &QListWidget::currentRowChanged,
          this, [this](int _row) { selectedIdx_ = _row; onSelectionChanged(); });
  connect(addBtn, &QPushButton::clicked, this, &DeviceDecorDialog::onAddItem);
  connect(delBtn, &QPushButton::clicked, this, &DeviceDecorDialog::onDeleteItem);
  connect(saveBtn_, &QPushButton::clicked, this, &DeviceDecorDialog::onSaveInstance);
  connect(applyBtn, &QPushButton::clicked, this, &DeviceDecorDialog::onApplyAndRebuild);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  /// 类型改了 ⇒ 名称自动跟着换（未手动改过的前提下）
  connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) {
            if (isLoading_ || selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size())
              return;
            const auto t = (keship::DeviceType)typeCombo_->currentData().toInt();
            config_[selectedIdx_].type = t;
            if (config_[selectedIdx_].name.startsWith("battery_") ||
                config_[selectedIdx_].name.startsWith("motor_"))
              nameEdit_->setText(autoName(t));
          });
}

//////////////////////////////////////////////////////////////////////////////////
QString DeviceDecorDialog::autoName(keship::DeviceType _type) const
{
  const QString prefix = (_type == keship::DeviceType::Battery) ? "battery_" : "motor_";
  int n = 0;
  for (const auto &it : config_)
    if (it.type == _type) n++;
  return prefix + QString::number(n + 1);
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::onAddItem()
{
  const auto t = (keship::DeviceType)typeCombo_->currentData().toInt();
  InternalItem item;
  item.type = t;
  item.name = autoName(t);
  config_.push_back(item);

  updateList();
  listWidget_->setCurrentRow((int)config_.size() - 1);
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::onDeleteItem()
{
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size()) return;
  config_.erase(config_.begin() + selectedIdx_);
  selectedIdx_ = -1;
  updateList();
  if (!config_.empty())
    listWidget_->setCurrentRow(0);
  else
  {
    updateParamPanel();
  }
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::onSelectionChanged()
{
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size())
  {
    updateParamPanel();
    return;
  }
  isLoading_ = true;
  const auto &it = config_[selectedIdx_];
  typeCombo_->setCurrentIndex(typeCombo_->findData((int)it.type));
  nameEdit_->setText(it.name);
  for (int i = 0; i < 6; i++)
    poseSpin_[i]->setValue(it.pose[i]);
  panel_->show();
  isLoading_ = false;
}

//////////////////////////////////////////////////////////////////////////////////
bool DeviceDecorDialog::onSaveInstance()
{
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size()) return false;

  const QString newName = nameEdit_->text().trimmed();
  if (newName.isEmpty())
  {
    QMessageBox::warning(this, "名称无效", "名称不能为空");
    return false;
  }
  for (int i = 0; i < (int)config_.size(); i++)
  {
    if (i == selectedIdx_) continue;
    if (config_[i].name == newName)
    {
      QMessageBox::warning(this, "名称冲突",
          QString("名称 \"%1\" 已被使用，请换一个").arg(newName));
      return false;
    }
  }

  auto &it = config_[selectedIdx_];
  it.type = (keship::DeviceType)typeCombo_->currentData().toInt();
  it.name = newName;
  for (int i = 0; i < 6; i++)
    it.pose[i] = poseSpin_[i]->value();

  const int saved = selectedIdx_;
  updateList();
  listWidget_->setCurrentRow(saved);
  return true;
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::onApplyAndRebuild()
{
  if (selectedIdx_ >= 0 && selectedIdx_ < (int)config_.size())
  {
    if (!onSaveInstance())
      return;
  }
  accept();
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::updateList()
{
  listWidget_->clear();
  for (const auto &it : config_)
  {
    const QString tag = (it.type == keship::DeviceType::Battery) ? "[电池]" : "[电机]";
    listWidget_->addItem(tag + " " + it.name);
  }
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::updateParamPanel()
{
  const bool valid = (selectedIdx_ >= 0 && selectedIdx_ < (int)config_.size());
  panel_->setVisible(valid);
  if (!valid)
  {
    nameEdit_->setText("");
    for (int i = 0; i < 6; i++) poseSpin_[i]->setValue(0.0);
  }
}

//////////////////////////////////////////////////////////////////////////////////
keship::DeviceDecorConfig DeviceDecorDialog::getConfig() const
{
  keship::DeviceDecorConfig cfg;
  cfg.linkName = linkName_.toStdString();
  for (const auto &it : config_)
  {
    keship::DeviceItem item;
    item.type = it.type;
    item.name = it.name.toStdString();
    for (int i = 0; i < 6; i++) item.pose[i] = it.pose[i];
    cfg.items.push_back(item);
  }
  return cfg;
}

//////////////////////////////////////////////////////////////////////////////////
void DeviceDecorDialog::setConfig(const keship::DeviceDecorConfig &_config)
{
  config_.clear();
  linkName_ = QString::fromStdString(_config.linkName);
  for (const auto &item : _config.items)
  {
    InternalItem it;
    it.type = item.type;
    it.name = QString::fromStdString(item.name);
    for (int i = 0; i < 6; i++) it.pose[i] = item.pose[i];
    config_.push_back(it);
  }
  selectedIdx_ = -1;
  updateList();
  updateParamPanel();
  if (!config_.empty())
    listWidget_->setCurrentRow(0);
}
