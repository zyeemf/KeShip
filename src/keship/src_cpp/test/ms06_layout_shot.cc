/*
 * Copyright (C) 2024–2026 KeShip Team
 *
 * MS-06 布局离屏截图（终版：两列 + 区块标题淡底 + 底部通栏页签）。
 */
// MS-06 布局离屏截图（终版：两列 + 区块标题淡底 + 底部通栏页签）。
#include <QApplication>
#include <QPixmap>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <iostream>

#include "experiments/WaveResponseDialog.hh"
#include "HiddenCaseGenerator.hh"

static void fillRaoTable(QWidget *_dlg) {
  for (QTableWidget *tb : _dlg->findChildren<QTableWidget *>()) {
    if (tb->columnCount() != 5) continue;
    if (tb->objectName() == "validationTable") continue;
    tb->insertRow(0);
    tb->setItem(0, 0, new QTableWidgetItem("5.000"));
    tb->setItem(0, 1, new QTableWidgetItem("9.162"));
    tb->setItem(0, 2, new QTableWidgetItem("10.862"));
    tb->setItem(0, 3, new QTableWidgetItem("5.000"));
    tb->setItem(0, 4, new QTableWidgetItem(QString::fromUtf8("已系泊")));
    tb->insertRow(1);
    tb->setItem(1, 0, new QTableWidgetItem("4.000"));
    tb->setItem(1, 1, new QTableWidgetItem("8.436"));
    tb->setItem(1, 2, new QTableWidgetItem("18.323"));
    tb->setItem(1, 3, new QTableWidgetItem("4.000"));
    tb->setItem(1, 4, new QTableWidgetItem(QString::fromUtf8("已系泊")));
    return;
  }
}

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  gz::transport::Node node;

  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{"ke0812_1"}, "ms06");
    const auto cases = keship::GenerateHiddenCases(2, 20261022u);
    dlg.AddRoundForTest(cases[0].trail, cases[0].period, cases[0].waveAmp);
    dlg.AddRoundForTest(cases[1].trail, cases[1].period, cases[1].waveAmp);
    fillRaoTable(&dlg);
    dlg.show();
    app.processEvents();
    QThread::msleep(400);
    app.processEvents();

    QPixmap px = dlg.grab();
    px.save("/tmp/ms06_layout.png");
    std::cout << "MS06 tab1 " << px.width() << "x" << px.height() << std::endl;

    if (auto *tabs = dlg.findChild<QTabWidget *>("curveTabs")) {
      tabs->setCurrentIndex(1);
      app.processEvents();
      QThread::msleep(250);
      app.processEvents();
      px = dlg.grab();
      px.save("/tmp/ms06_layout_tab2.png");
      std::cout << "MS06 tab2 " << px.width() << "x" << px.height() << std::endl;
    }
  }

  {
    WaveResponseDialog dlg(node, "keship_world", QStringList{"ke0812_1"}, "ms03");
    fillRaoTable(&dlg);
    dlg.show();
    app.processEvents();
    QThread::msleep(300);
    app.processEvents();
    const QPixmap px = dlg.grab();
    px.save("/tmp/ms03_layout.png");
    std::cout << "MS03 " << px.width() << "x" << px.height() << std::endl;
  }
  return 0;
}
