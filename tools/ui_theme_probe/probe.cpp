/*
 * ui_theme_probe.cpp — 对话框主题工具验证探针
 *
 * 用途：验证 UiTheme::applyToDialog() 是否**只改颜色、不动布局**。
 * 做法：构造含各类控件的样板对话框，在应用前后记录全部控件尺寸指纹并截图；
 *       尺寸指纹必须逐项一致，截图应大面积变化（说明颜色确已改变）。
 *
 * 来历：本探针为"全局统一配色"方案（已废弃，见 doc/plans/ui_improvement_plan.md）而写，
 *       但结论仍必要且有效 —— 调色板**不参与**布局盒模型，而 QSS **会**
 *       （控件一旦被 QSS 规则匹配即切换 QStyleSheetStyle，尺寸必然变化）。
 *
 * 口径说明：QSS 一旦作用于某类控件，Qt 会为其启用 QSS 代理并**懒创建内部私有控件**
 * （如 QComboBoxListView / QComboBoxPrivateContainer），控件总数会变。
 * 这属实现细节、非尺寸问题，因此按「控件类」分组比较**尺寸多重集**：
 * 对两边都存在的类，其 sizeHint/geometry 集合必须逐一相同。
 */
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QColor>
#include <QMap>
#include <QPalette>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QString>
#include <QStringList>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

#include <QDebug>

#include "UiTheme.hh"

class ProbeDialog : public QDialog
{
public:
  explicit ProbeDialog(QWidget *parent = nullptr) : QDialog(parent)
  {
    setWindowTitle(QString::fromUtf8("UI 主题验证样板"));
    resize(460, 560);

    auto *root = new QVBoxLayout(this);

    auto *form = new QGroupBox(QString::fromUtf8("数值与文本输入"), this);
    auto *fl = new QFormLayout(form);
    for (int i = 0; i < 3; ++i)
    {
      auto *sb = new QDoubleSpinBox(form);
      sb->setRange(-100, 100);
      sb->setValue(i * 1.5);
      fl->addRow(QString::fromUtf8("参数 %1").arg(i + 1), sb);
    }
    auto *ed = new QLineEdit(QString::fromUtf8("文本输入"), form);
    fl->addRow(QString::fromUtf8("名称"), ed);
    auto *sp = new QSpinBox(form);
    sp->setValue(7);
    fl->addRow(QString::fromUtf8("整数"), sp);
    auto *cb = new QComboBox(form);
    cb->addItems({QString::fromUtf8("选项甲"), QString::fromUtf8("选项乙"),
                  QString::fromUtf8("选项丙")});
    fl->addRow(QString::fromUtf8("下拉"), cb);
    root->addWidget(form);

    auto *tbl = new QTableWidget(3, 3, this);
    tbl->setHorizontalHeaderLabels({QString::fromUtf8("列一"), QString::fromUtf8("列二"),
                                    QString::fromUtf8("列三")});
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        tbl->setItem(r, c, new QTableWidgetItem(
            QString::fromUtf8("R%1C%2").arg(r + 1).arg(c + 1)));
    root->addWidget(tbl);

    auto *chk = new QCheckBox(QString::fromUtf8("启用该选项"), this);
    root->addWidget(chk);

    auto *sld = new QSlider(Qt::Horizontal, this);
    sld->setValue(40);
    root->addWidget(sld);

    auto *btnRow = new QWidget(this);
    auto *bl = new QHBoxLayout(btnRow);
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(new QPushButton(QString::fromUtf8("确定"), btnRow));
    bl->addWidget(new QPushButton(QString::fromUtf8("取消"), btnRow));
    bl->addStretch();
    root->addWidget(btnRow);
    root->addStretch();
  }
};

/// 类名 → 排序后的尺寸串列表（多重集）
using SizeIndex = QMap<QString, QStringList>;

static void collect(QWidget *w, SizeIndex &idx)
{
  const auto kids = w->children();
  for (QObject *o : kids)
  {
    QWidget *c = qobject_cast<QWidget *>(o);
    if (nullptr == c)
      continue;
    const QString cls = QString::fromLatin1(c->metaObject()->className());
    // 判定只用 sizeHint：geometry 会被懒布局干扰——实测应用前后两次采样间
    // QScrollBar 的 geo 出现 100x30 → 14x163 跳变（首次真实布局），与主题无关
    idx[cls].append(QString("hint=%1x%2")
                        .arg(c->sizeHint().width())
                        .arg(c->sizeHint().height()));
    collect(c, idx);
  }
}

static SizeIndex indexOf(QWidget *w)
{
  SizeIndex idx;
  collect(w, idx);
  for (auto it = idx.begin(); it != idx.end(); ++it)
    it.value().sort();
  return idx;
}

static int totalWidgets(const SizeIndex &idx)
{
  int n = 0;
  for (auto it = idx.constBegin(); it != idx.constEnd(); ++it)
    n += it.value().size();
  return n;
}

int main(int argc, char **argv)
{
  QApplication app(argc, argv);

  ProbeDialog dlg;
  dlg.show();
  app.processEvents();
  app.processEvents();

  // 基线采样稳定性自检（防止把懒布局噪声误判为"尺寸变化"）
  const SizeIndex probeA = indexOf(&dlg);
  app.processEvents();
  const SizeIndex probeB = indexOf(&dlg);
  if (probeA != probeB)
    qInfo().noquote() << "[0] ⚠ 基线采样自身不稳定（懒布局）——结论需谨慎解读";
  else
    qInfo().noquote() << "[0] 基线采样稳定（连续两次取样一致）";

  const SizeIndex before = indexOf(&dlg);
  dlg.grab().save(QString::fromLatin1("/tmp/theme_probe_before.png"));

  qInfo().noquote() << QString("[1] 应用前：%1 个控件 / %2 个类")
                           .arg(totalWidgets(before)).arg(before.size());

  UiTheme::applyToDialog(&dlg);  // 单个对话框（逐个修改策略）
  app.processEvents();
  app.processEvents();

  const SizeIndex after = indexOf(&dlg);
  dlg.grab().save(QString::fromLatin1("/tmp/theme_probe_after.png"));

  qInfo().noquote() << QString("[2] 应用后：%1 个控件 / %2 个类")
                           .arg(totalWidgets(after)).arg(after.size());

  qInfo().noquote() << "[3] 机制核查：styleSheet"
                    << (app.styleSheet().isEmpty() ? QString::fromUtf8("为空（未使用 QSS ✓）")
                                                   : QString::fromUtf8("非空（意外！）"))
                    << " / 对话框 Highlight ="
                    << dlg.palette().color(QPalette::Highlight).name()
                    << "（期望 #7384e4）";

  QStringList onlyAfter, onlyBefore;
  for (auto it = after.constBegin(); it != after.constEnd(); ++it)
    if (!before.contains(it.key()))
      onlyAfter << QString("%1(%2)").arg(it.key()).arg(it.value().size());
  for (auto it = before.constBegin(); it != before.constEnd(); ++it)
    if (!after.contains(it.key()))
      onlyBefore << QString("%1(%2)").arg(it.key()).arg(it.value().size());

  qInfo().noquote() << "[4] 仅应用后出现的类（QSS 懒创建，属实现细节）:"
                    << (onlyAfter.isEmpty() ? QString::fromUtf8("无")
                                            : onlyAfter.join(QString::fromUtf8(", ")));
  if (!onlyBefore.isEmpty())
    qInfo().noquote() << "    注意：仅应用前存在的类:" << onlyBefore.join(QString::fromUtf8(", "));

  // 共有类逐一比较
  int diffClasses = 0, checkedClasses = 0, checkedWidgets = 0;
  for (auto it = before.constBegin(); it != before.constEnd(); ++it)
  {
    if (!after.contains(it.key()))
      continue;
    ++checkedClasses;
    const QStringList &a = it.value();
    const QStringList &b = after.value(it.key());
    if (a.size() != b.size())
    {
      qInfo().noquote() << QString("    [数量差异] %1：前 %2 个 / 后 %3 个")
                               .arg(it.key()).arg(a.size()).arg(b.size());
      ++diffClasses;
      continue;
    }
    checkedWidgets += a.size();
    for (int i = 0; i < a.size(); ++i)
    {
      if (a[i] != b[i])
      {
        qInfo().noquote() << QString("    [尺寸差异] %1  前:%2  后:%3")
                                 .arg(it.key(), a[i], b[i]);
        ++diffClasses;
        break;
      }
    }
  }

  qInfo().noquote() << QString("[5] 共有类比较：%1 个类 / %2 个控件实例")
                           .arg(checkedClasses).arg(checkedWidgets);
  if (0 == diffClasses)
    qInfo().noquote() << QString::fromUtf8(
        "[6] 结论：共有类尺寸**逐项一致** —— applyToDialog() 未触及布局盒模型 ✓");
  else
    qInfo().noquote() << QString("[6] 结论：%1 个类存在尺寸差异 ✗").arg(diffClasses);

  qInfo().noquote() << QString::fromUtf8(
      "[7] 截图对照：/tmp/theme_probe_before.png 与 after.png —— 应形状一致、颜色不同");

  return 0;
}
