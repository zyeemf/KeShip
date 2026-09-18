/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 保存世界对话框实现 */

#include "SaveWorldDialog.hh"

#include <QGroupBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QMessageBox>
#include <QFileDialog>
#include <QUrl>
#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <functional>
#include <vector>

namespace {
/// 路径 '/a/b/c' → 返回每段
QStringList SplitPath(const QString &_path)
{
  return _path.split('/', Qt::SkipEmptyParts);
}

/// 收集勾选的叶子（_wantWater true 仅收带水域标记的叶子，false 仅收模型叶子）
/// 返回叶子存储的提交键（FullTextRole，模型=实例名、水域=名字；缺省回退叶子文本）
QStringList CollectCheckedLeaves(QTreeWidget *_tree, bool _wantWater)
{
  QStringList out;
  std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *it)
  {
    if (it->childCount() == 0 && it->checkState(0) == Qt::Checked)
    {
      const bool marked = it->data(0, Qt::UserRole).toBool();
      if (marked == _wantWater)
      {
        const QString key = it->data(0, Qt::UserRole + 1).toString();
        out << (key.isEmpty() ? it->text(0) : key);
      }
    }
    for (int i = 0; i < it->childCount(); ++i)
      walk(it->child(i));
  };
  for (int i = 0; i < _tree->topLevelItemCount(); ++i)
    walk(_tree->topLevelItem(i));
  return out;
}
} // namespace

SaveWorldDialog::SaveWorldDialog(const QString &_worldName,
                                 const QStringList &_modelTree,
                                 const QStringList &_waterNames,
                                 const QString &_parentDir,
                                 const QList<QUrl> &_browseUrls,
                                 QWidget *parent)
    : QDialog(parent), browseUrls_(_browseUrls), parentDir_(_parentDir)
{
  setWindowTitle("保存世界");
  setMinimumWidth(560);
  resize(560, 680);

  auto *mainLayout = new QVBoxLayout(this);

  // 世界名
  auto *nameGroup = new QGroupBox("世界名（字母数字下划线，作为层级树根）", this);
  auto *nameForm = new QFormLayout(nameGroup);
  nameEdit_ = new QLineEdit(nameGroup);
  nameEdit_->setText(_worldName);
  nameForm->addRow("世界名:", nameEdit_);
  mainLayout->addWidget(nameGroup);

  // 保存目录（只读自动拼接；浏览仅更换父目录；世界名变化自动跟随）
  auto *dirGroup = new QGroupBox(
      "保存到父目录（自动新建 <世界名>.ksw 文件夹，内含 keship_world.kswf 清单与 *.ksm）", this);
  auto *dirRow = new QHBoxLayout(dirGroup);
  dirEdit_ = new QLineEdit(dirGroup);
  dirEdit_->setReadOnly(true);
  auto *browseBtn = new QPushButton("浏览...", dirGroup);
  connect(browseBtn, &QPushButton::clicked, this, &SaveWorldDialog::browseDir);
  dirRow->addWidget(dirEdit_, 1);
  dirRow->addWidget(browseBtn);
  mainLayout->addWidget(dirGroup);

  // 世界名改变 → 同步更新自动拼接的目录（若用户未手动改过目录）
  connect(nameEdit_, &QLineEdit::textChanged, this,
      [this](const QString &_text){
        const QString oldName = nameEdit_->property("_lastName").toString();
        updateDirFromName(oldName);
        nameEdit_->setProperty("_lastName", _text);
      });
  nameEdit_->setProperty("_lastName", _worldName);

  refreshDirEdit();

  // 模型/水域树（勾选）
  auto *treeGroup = new QGroupBox("选择要保存的实体", this);
  auto *treeLayout = new QVBoxLayout(treeGroup);
  tree_ = new QTreeWidget(treeGroup);
  tree_->setHeaderLabels({ "模型 / 水区" });
  tree_->header()->setStretchLastSection(true);
  tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
  tree_->setAlternatingRowColors(true);
  treeLayout->addWidget(tree_);
  auto *btnRow = new QHBoxLayout;
  auto *selAll = new QPushButton("全选", treeGroup);
  auto *clrAll = new QPushButton("清空", treeGroup);
  btnRow->addWidget(selAll);
  btnRow->addWidget(clrAll);
  btnRow->addStretch();
  treeLayout->addLayout(btnRow);
  mainLayout->addWidget(treeGroup, 1);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  buttons->setCenterButtons(true);
  auto *okBtn = buttons->button(QDialogButtonBox::Ok);
  okBtn->setText("保存");
  okBtn->setMinimumHeight(30);
  buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(30);
  connect(buttons, &QDialogButtonBox::accepted, this, &SaveWorldDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);

  // 全选/清空
  connect(selAll, &QPushButton::clicked, this, [this](){ setAllChecked(true); });
  connect(clrAll, &QPushButton::clicked, this, [this](){ setAllChecked(false); });

  buildTree(_modelTree, _waterNames);
}

void SaveWorldDialog::refreshDirEdit()
{
  QString base = parentDir_.trimmed();
  while (base.endsWith('/'))
    base.chop(1);
  dirEdit_->setText(base + "/" + worldName() + ".ksw");
}

void SaveWorldDialog::updateDirFromName(const QString &_oldName)
{
  // 仅当目录仍为“父目录/旧世界名.ksw”的自动拼接结果时跟随更新；
  // 用户手动改过目录则保持不变
  QString base = parentDir_.trimmed();
  while (base.endsWith('/'))
    base.chop(1);
  if (dirEdit_->text() == base + "/" + _oldName + ".ksw")
    dirEdit_->setText(base + "/" + worldName() + ".ksw");
}

void SaveWorldDialog::browseDir()
{
  QFileDialog dlg(this, "选择保存父目录");
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
    refreshDirEdit();
  }
}

void SaveWorldDialog::onAccept()
{
  const QString n = worldName();
  for (const QChar &ch : n)
    if (!ch.isLetterOrNumber() && ch != '_')
    {
      QMessageBox::warning(this, "警告", "世界名只能包含字母、数字、下划线");
      return;
    }
  if (n.isEmpty())
  {
    QMessageBox::warning(this, "警告", "世界名不能为空");
    return;
  }

  const QString dir = saveDir();
  if (dir.isEmpty())
  {
    QMessageBox::warning(this, "警告", "请选择保存目录");
    return;
  }

  // 目录不存在 → 自动创建（父目录必须存在）；已存在（同名覆盖）→ 询问确认
  const QString parent = QFileInfo(dir).dir().absolutePath();
  if (!QDir(parent).exists())
  {
    QMessageBox::warning(this, "警告", "父目录不存在：\n" + parent);
    return;
  }
  if (!QDir(dir).exists())
  {
    if (!QDir().mkpath(dir))
    {
      QMessageBox::warning(this, "警告", "目录创建失败：\n" + dir);
      return;
    }
  }
  else if (QMessageBox::question(this, "保存世界",
          "同名世界已存在，是否覆盖？\n" + dir,
          QMessageBox::Yes | QMessageBox::No,
          QMessageBox::No) != QMessageBox::Yes)
  {
    return;
  }

  if (selectedModels().isEmpty() && selectedWaterNames().isEmpty())
  {
    QMessageBox::warning(this, "警告", "请至少勾选一个模型或水区");
    return;
  }

  accept();
}

void SaveWorldDialog::buildTree(const QStringList &_modelTree,
                                const QStringList &_waterNames)
{
  /// 行解析（模型行 4 段：子World链 \t 勾选 \t 原名 \t 实例名；兼容 3 段）
  struct ModelRow { QString chain; bool checked; QString displayName; QString instName; };
  auto parseModels = [](const QStringList &_lines, std::vector<ModelRow> &_out)
  {
    for (const QString &line : _lines)
    {
      const QStringList parts = line.split('\t');
      if (parts.size() < 3) continue;
      const QString inst = (parts.size() >= 4) ? parts[3] : parts[2];
      _out.push_back({ parts[0], parts[1] == "1", parts[2], inst });
    }
  };
  /// 水域行：链 \t 勾选 \t 水域名（兼容仅名字列表：默认全勾选）
  struct WaterRow { QString chain; bool checked; QString name; };
  auto parseWaters = [](const QStringList &_lines, std::vector<WaterRow> &_out)
  {
    for (const QString &w : _lines)
    {
      const QStringList parts = w.split('\t');
      if (parts.size() < 3)
        _out.push_back({ QString(), true, w });
      else
        _out.push_back({ parts[0], parts[1] == "1", parts[2] });
    }
  };
  std::vector<ModelRow> models;
  std::vector<WaterRow> waters;
  parseModels(_modelTree, models);
  parseWaters(_waterNames, waters);

  /// 在 _parent（nullptr = 树顶层）下按文本查找/创建分组节点
  auto findOrCreateGroup = [&](QTreeWidgetItem *_parent,
                               const QString &_text) -> QTreeWidgetItem *
  {
    const int n = _parent ? _parent->childCount() : tree_->topLevelItemCount();
    for (int i = 0; i < n; ++i)
    {
      QTreeWidgetItem *it = _parent ? _parent->child(i) : tree_->topLevelItem(i);
      if (it->text(0) == _text) return it;
    }
    auto *g = new QTreeWidgetItem();
    g->setText(0, _text);
    g->setFlags(g->flags() | Qt::ItemIsUserCheckable);
    g->setCheckState(0, Qt::PartiallyChecked);
    if (_parent)
      _parent->addChild(g);
    else
      tree_->addTopLevelItem(g);
    return g;
  };

  /// 分类组落点：空链（顶层实体）→ 树顶层的“模型/水域”分类组；
  /// 非空 → 顶部按子 ksw 链分段建组，再落在组内“模型/水域”分类组
  /// （保存对话框不设根节点：世界名可编辑，根节点无意义）
  auto catGroupOf = [&](const QString &_chain, const QString &_type) -> QTreeWidgetItem *
  {
    if (_chain.isEmpty())
      return findOrCreateGroup(nullptr, _type);
    QTreeWidgetItem *cur = nullptr;
    for (const QString &seg : SplitPath(_chain))
      cur = findOrCreateGroup(cur, seg);
    return findOrCreateGroup(cur, _type);
  };

  // 模型：分类组“模型”→ 叶子（显示原名，实例名存入 FullTextRole 供收集）
  for (const ModelRow &row : models)
  {
    auto *cat = catGroupOf(row.chain, "模型");
    auto *leaf = new QTreeWidgetItem(cat);
    leaf->setText(0, row.displayName.section('/', -1));
    leaf->setData(0, Qt::UserRole, false);            // 区分水域标记（模型 false）
    leaf->setData(0, Qt::UserRole + 1, row.instName); // 实例名（缓存键/保存依据）
    leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
    leaf->setCheckState(0, row.checked ? Qt::Checked : Qt::Unchecked);
  }

  // 水域：分类组“水域”→ 叶子（带水域标记，名字存入 FullKey 供收集）
  for (const WaterRow &row : waters)
  {
    auto *cat = catGroupOf(row.chain, "水域");
    auto *leaf = new QTreeWidgetItem(cat);
    leaf->setText(0, row.name.section('/', -1));
    leaf->setData(0, Qt::UserRole, true);             // 水域标记
    leaf->setData(0, Qt::UserRole + 1, row.name);
    leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
    leaf->setCheckState(0, row.checked ? Qt::Checked : Qt::Unchecked);
  }

  // 父节点状态联动（勾选/取消时同步子节点）
  connect(tree_, &QTreeWidget::itemChanged, this,
      [](QTreeWidgetItem *item, int col){
    if (col != 0) return;
    const Qt::CheckState st = item->checkState(0);
    for (int i = 0; i < item->childCount(); ++i)
      item->child(i)->setCheckState(0, st);
  });
}

void SaveWorldDialog::setAllChecked(bool _checked)
{
  for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    tree_->topLevelItem(i)->setCheckState(0, _checked ? Qt::Checked : Qt::Unchecked);
}

QString SaveWorldDialog::worldName() const { return nameEdit_->text().trimmed(); }
QString SaveWorldDialog::saveDir() const { return dirEdit_->text().trimmed(); }

QStringList SaveWorldDialog::selectedModels() const
{
  return CollectCheckedLeaves(tree_, false);
}

QStringList SaveWorldDialog::selectedWaterNames() const
{
  return CollectCheckedLeaves(tree_, true);
}