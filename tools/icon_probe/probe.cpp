/*
 * icon_probe.cpp — KeShip 自绘 SVG 图标渲染验证
 *
 * 独立编译的程序不含 qrc 资源，故由 run.sh 先用 rcc 把 icons.qrc 编成
 * /tmp/icons.rcc，探针在运行时注册它，从而加载真实图标。
 *
 * 判据：
 *   1. 资源注册必须成功
 *   2. 每个按钮的 icon().isNull() 必须为 false —— 若为 true 说明路径写错或 SVG 未生效
 *   3. 截图（含 2 倍放大）供肉眼核对
 */
#include <QApplication>
#include <QDebug>
#include <QHBoxLayout>
#include <QImage>
#include <QPushButton>
#include <QResource>
#include <QString>
#include <QVBoxLayout>
#include <QDialog>

#include "UiTheme.hh"

struct Item
{
  const char *text;
  UiTheme::Icon icon;
};

int main(int argc, char **argv)
{
  QApplication app(argc, argv);

  if (!QResource::registerResource(QString::fromLatin1("/tmp/icons.rcc")))
  {
    qWarning().noquote() << "[FAIL] 无法注册 /tmp/icons.rcc";
    return 1;
  }
  qInfo().noquote() << "[OK] 已注册 icons.rcc";

  QDialog dlg;
  dlg.setWindowTitle(QString::fromUtf8("图标渲染验证"));
  auto *lay = new QVBoxLayout(&dlg);
  auto *row = new QHBoxLayout();

  const Item items[] = {
    {"完成", UiTheme::Icon::kOk},
    {"取消", UiTheme::Icon::kCancel},
    {"关闭", UiTheme::Icon::kClose},
    {"添加", UiTheme::Icon::kAdd},
    {"删除", UiTheme::Icon::kDelete},
    {"保存", UiTheme::Icon::kSave},
  };

  int nullCount = 0;
  for (const Item &it : items)
  {
    auto *b = new QPushButton(QString::fromUtf8(it.text));
    UiTheme::setButtonIcon(b, it.icon);
    b->setMinimumHeight(30);
    row->addWidget(b);

    const bool isNull = b->icon().isNull();
    if (isNull)
      ++nullCount;
    qInfo().noquote() << "  " << QString::fromUtf8(it.text)
                      << "  图标为空 = " << (isNull ? "是" : "否");
  }
  lay->addLayout(row);
  lay->addStretch();

  dlg.resize(520, 90);
  dlg.show();
  QApplication::processEvents();

  const QImage img = dlg.grab().toImage();
  img.save(QString::fromLatin1("/tmp/icon_probe.png"));
  img.scaled(img.width() * 2, img.height() * 2, Qt::IgnoreAspectRatio,
             Qt::FastTransformation)
     .save(QString::fromLatin1("/tmp/icon_probe_2x.png"));

  qInfo().noquote() << QString::fromUtf8("[结论] 图标为空的按钮数 = %1 （须为 0）")
                           .arg(nullCount);
  return (0 == nullCount) ? 0 : 1;
}
