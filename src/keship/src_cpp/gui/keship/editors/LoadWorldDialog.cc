/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 载入世界对话框实现 */

#include "LoadWorldDialog.hh"
#include "../shared/UiTheme.hh"

#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QHeaderView>
#include <QRegularExpressionValidator>
#include <functional>
#include <vector>

namespace {
QDoubleSpinBox *MakeSpin(double _min, double _max, double _step, int _dec)
{
  auto *s = new QDoubleSpinBox;
  s->setRange(_min, _max);
  s->setSingleStep(_step);
  s->setDecimals(_dec);
  s->setValue(0.0);
  return s;
}

/// 路径 '/a/b/c' → 返回每段
QStringList SplitPath(const QString &_path)
{
  return _path.split('/', Qt::SkipEmptyParts);
}

/// 收集勾选的叶子（_wantWater true 仅收带水域标记的叶子，false 仅收模型叶子）
/// 返回叶子存储的完整路径文本（FullTextRole，与勾选行第三段一致）
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
        const QString full = it->data(0, Qt::UserRole + 1).toString();
        out << (full.isEmpty() ? it->text(0) : full);
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

LoadWorldDialog::LoadWorldDialog(const QString &_suggestedNamespace,
                                 const QStringList &_modelTree,
                                 const QStringList &_waterNames,
                                 QWidget *parent)
    : QDialog(parent), rootText_(_suggestedNamespace)
{
  setWindowTitle("载入世界");
  setMinimumWidth(560);
  resize(560, 680);

  auto *mainLayout = new QVBoxLayout(this);

  // 命名空间（默认留空：中文世界名/输入会产生非法 ROS 2 主题，仅允许字母数字下划线）
  auto *nsGroup = new QGroupBox("命名空间（实例名前缀，仅限字母/数字/下划线）", this);
  auto *nsLayout = new QVBoxLayout(nsGroup);
  nsEdit_ = new QLineEdit(nsGroup);
  nsEdit_->setPlaceholderText("留空 = 无前缀；或如 base_1（仅限字母/数字/下划线）");
  nsEdit_->setValidator(new QRegularExpressionValidator(
      QRegularExpression("[A-Za-z0-9_]*"), nsEdit_));
  // 预填世界名（仅当全部字符合法：字母/数字/下划线；中文世界名保持留空）
  {
    bool legal = true;
    for (const QChar &c : _suggestedNamespace)
      if (!c.isLetterOrNumber() && c != '_') { legal = false; break; }
    if (legal && !_suggestedNamespace.isEmpty())
      nsEdit_->setText(_suggestedNamespace);
  }
  nsLayout->addWidget(nsEdit_);
  mainLayout->addWidget(nsGroup);

  // 六向整体偏移
  auto *offGroup = new QGroupBox("整体位姿偏移（叠加在世界整体位姿之上）", this);
  auto *offForm = new QFormLayout(offGroup);
  xSpin_ = MakeSpin(-100000, 100000, 1.0, 3);
  ySpin_ = MakeSpin(-100000, 100000, 1.0, 3);
  zSpin_ = MakeSpin(-100000, 100000, 0.5, 3);
  rollSpin_ = MakeSpin(-180, 180, 1.0, 2);
  pitchSpin_ = MakeSpin(-180, 180, 1.0, 2);
  yawSpin_ = MakeSpin(-180, 180, 1.0, 2);
  offForm->addRow("X (m):", xSpin_);
  offForm->addRow("Y (m):", ySpin_);
  offForm->addRow("Z (m):", zSpin_);
  offForm->addRow("Roll (deg):", rollSpin_);
  offForm->addRow("Pitch (deg):", pitchSpin_);
  offForm->addRow("Yaw (deg):", yawSpin_);
  mainLayout->addWidget(offGroup);

  // 模型/水域树（勾选）
  auto *treeGroup = new QGroupBox("选择要载入的实体", this);
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
  okBtn->setText("载入");
  okBtn->setMinimumHeight(30);
  buttons->button(QDialogButtonBox::Cancel)->setMinimumHeight(30);
  UiTheme::setButtonIcon(okBtn, UiTheme::Icon::kOk);
  UiTheme::setButtonIcon(buttons->button(QDialogButtonBox::Cancel), UiTheme::Icon::kCancel);
  connect(buttons, &QDialogButtonBox::accepted, this, &LoadWorldDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);

  connect(selAll, &QPushButton::clicked, this, [this](){ setAllChecked(true); });
  connect(clrAll, &QPushButton::clicked, this, [this](){ setAllChecked(false); });

  buildTree(_modelTree, _waterNames);
}

QString LoadWorldDialog::namespaceText() const { return nsEdit_->text().trimmed(); }
double LoadWorldDialog::offsetX() const { return xSpin_->value(); }
double LoadWorldDialog::offsetY() const { return ySpin_->value(); }
double LoadWorldDialog::offsetZ() const { return zSpin_->value(); }
double LoadWorldDialog::offsetRoll() const { return rollSpin_->value(); }
double LoadWorldDialog::offsetPitch() const { return pitchSpin_->value(); }
double LoadWorldDialog::offsetYaw() const { return yawSpin_->value(); }

QStringList LoadWorldDialog::selectedModelNames() const
{
  return CollectCheckedLeaves(tree_, false);
}

QStringList LoadWorldDialog::selectedWaterNames() const
{
  return CollectCheckedLeaves(tree_, true);
}

void LoadWorldDialog::onAccept()
{
  if (selectedModelNames().isEmpty() && selectedWaterNames().isEmpty())
  {
    QMessageBox::warning(this, "警告", "请至少勾选一个模型或水域");
    return;
  }
  accept();
}

void LoadWorldDialog::buildTree(const QStringList &_modelTree,
                                const QStringList &_waterNames)
{
  /// 行解析：子World链 \t 勾选 \t 全链文本（原名在末段）
  struct Row { QString chain; bool checked; QString fullText; };
  auto parseRows = [](const QStringList &_lines, std::vector<Row> &_out)
  {
    for (const QString &line : _lines)
    {
      const QStringList parts = line.split('\t');
      if (parts.size() < 3) continue;
      _out.push_back({ parts[0], parts[1] == "1", parts[2] });
    }
  };
  std::vector<Row> models, waters;
  parseRows(_modelTree, models);
  parseRows(_waterNames, waters);

  /// 世界根组：最高层级（世界名，固定文本，不随输入变化）
  auto *root = new QTreeWidgetItem();
  root->setText(0, rootText_);
  root->setFlags(root->flags() | Qt::ItemIsUserCheckable);
  root->setCheckState(0, Qt::PartiallyChecked);
  tree_->addTopLevelItem(root);

  /// 在 _parent 下按文本查找/创建分组节点
  auto findOrCreateGroup = [&](QTreeWidgetItem *_parent,
                               const QString &_text) -> QTreeWidgetItem *
  {
    const int n = _parent->childCount();
    for (int i = 0; i < n; ++i)
    {
      QTreeWidgetItem *it = _parent->child(i);
      if (it->text(0) == _text) return it;
    }
    auto *g = new QTreeWidgetItem(_parent);
    g->setText(0, _text);
    g->setFlags(g->flags() | Qt::ItemIsUserCheckable);
    g->setCheckState(0, Qt::PartiallyChecked);
    return g;
  };

  /// 分类组落点：空链（顶层实体）→ 世界名根下的“模型/水域”分类组；
  /// 非空 → 根下按子 ksw 链分段建组，再落在组内“模型/水域”分类组
  auto catGroupOf = [&](const QString &_chain, const QString &_type) -> QTreeWidgetItem *
  {
    if (_chain.isEmpty())
      return findOrCreateGroup(root, _type);
    QTreeWidgetItem *cur = root;
    for (const QString &seg : SplitPath(_chain))
      cur = findOrCreateGroup(cur, seg);
    return findOrCreateGroup(cur, _type);
  };

  // 模型：分类组“模型”→ 叶子（显示原名，过滤匹配全链文本）
  for (const Row &row : models)
  {
    auto *cat = catGroupOf(row.chain, "模型");
    auto *leaf = new QTreeWidgetItem(cat);
    leaf->setText(0, row.fullText.section('/', -1));
    leaf->setData(0, Qt::UserRole + 1, row.fullText);
    leaf->setFlags(leaf->flags() | Qt::ItemIsUserCheckable);
    leaf->setCheckState(0, row.checked ? Qt::Checked : Qt::Unchecked);
  }

  // 水域：分类组“水域”→ 叶子（带水域标记，全链文本供过滤匹配）
  for (const Row &row : waters)
  {
    auto *cat = catGroupOf(row.chain, "水域");
    auto *leaf = new QTreeWidgetItem(cat);
    leaf->setText(0, row.fullText.section('/', -1));
    leaf->setData(0, Qt::UserRole, true);
    leaf->setData(0, Qt::UserRole + 1, row.fullText);
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

void LoadWorldDialog::setAllChecked(bool _checked)
{
  for (int i = 0; i < tree_->topLevelItemCount(); ++i)
    tree_->topLevelItem(i)->setCheckState(0, _checked ? Qt::Checked : Qt::Unchecked);
}