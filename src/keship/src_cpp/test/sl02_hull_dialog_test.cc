/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SL-02 数学船型对话框：离屏回归（2026-10-25）
 *
 * 目的：把"人工看截图"变成可重复断言 —— 默认档位、系数估算、完整档二次项、
 *       恢复默认、名称/位姿换算、以及**排布审计**（横向滚动条、最小尺寸、125% 字号）。
 * 运行：ctest -R sl02_hull_dialog --output-on-failure
 *       （CMake 已设 QT_QPA_PLATFORM=offscreen）
 * 产物：/tmp/sl02_hull_dialog_*.png（人工复核用，不做像素比对——字体/GPU 差异会误报）
 */

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>

#include <cmath>
#include <cstdio>

#include "ShipHullDialog.hh"

static int g_fail = 0;

static void check(bool _ok, const char *_what)
{
  std::printf("[%s] %s\n", _ok ? "PASS" : "FAIL", _what);
  if (!_ok)
    ++g_fail;
}

int main(int argc, char **argv)
{
  const double kPi = 3.14159265358979323846;
  QApplication app(argc, argv);

  ShipHullDialog dlg(QStringList() << "Wigley");
  dlg.show();
  QApplication::processEvents();

  // ---------------- 默认状态 ----------------
  check(dlg.isHydroEnabled(), "默认勾选「附带默认水动力系数」");
  check(!dlg.isHydroFull(), "默认轻量档");

  const keship::HydroConfig light = dlg.hydroConfig();
  check(light.damping[0] > 0.0 && light.damping[1] > 0.0, "轻量档 Xu/Yv 非零");
  check(light.damping[2] > 1.0, "轻量档 Zw 非零");
  check(light.damping[3] > 1.0, "轻量档 Kp 非零");
  check(light.damping[4] > 1.0, "轻量档 Mq 非零");
  check(light.damping[5] > 0.0, "轻量档 Nr 非零");
  check(light.dampingTerms.empty(), "轻量档无二次项");
  {
    bool amZero = true;
    for (double v : light.addedMass)
      if (v != 0.0) { amZero = false; break; }
    check(amZero, "轻量档不写附加质量");
  }

  // ---------------- 完整档 ----------------
  auto *mode = dlg.findChild<QComboBox *>("hullHydroMode");
  check(mode != nullptr, "按对象名找到档位下拉");
  if (mode)
    mode->setCurrentIndex(1);
  QApplication::processEvents();
  check(dlg.isHydroFull(), "切到完整档");
  const keship::HydroConfig full = dlg.hydroConfig();
  check(full.dampingTerms.size() == 6, "完整档 6 条二次项");
  check(full.addedMass[0] > 0.0 && full.addedMass[7] > 0.0, "完整档附加质量非零");

  // ---------------- 手改 → 恢复默认 ----------------
  auto *zw = dlg.findChild<QDoubleSpinBox *>("hullLin2");
  auto *restore = dlg.findChild<QPushButton *>("hullHydroRestore");
  check(zw != nullptr && restore != nullptr, "按对象名找到 Zw 与恢复按钮");
  if (zw && restore)
  {
    const double before = zw->value();
    zw->setValue(before * 0.5 + 1.0);
    check(std::fabs(zw->value() - before) > 1e-9, "手改系数生效");
    restore->click();
    QApplication::processEvents();
    check(std::fabs(zw->value() - before) < 1e-6, "「恢复默认」把系数改回");
  }

  // ---------------- 名称 / 位姿 ----------------
  auto *name = dlg.findChild<QLineEdit *>("hullName");
  check(name != nullptr && name->text().startsWith("Wigley"), "默认名称取自船型名");
  check(name != nullptr && name->text() != "Wigley", "已有同名实例时默认名加后缀");

  auto *yaw = dlg.findChild<QDoubleSpinBox *>("hullPoseYaw");
  check(yaw != nullptr, "按对象名找到 Yaw");
  if (yaw)
  {
    yaw->setValue(90.0);
    double pos[6];
    dlg.getPose(pos);
    check(std::fabs(pos[5] - kPi / 2.0) < 1e-6, "Yaw 90° → π/2 弧度");
    check(std::fabs(pos[2] - 0.6) < 1e-6, "Z 默认 D/2+0.1（D=1.0）");
  }

  // ---------------- 排布审计 ----------------
  auto *sa = dlg.findChild<QScrollArea *>();
  check(sa != nullptr, "存在滚动区");
  if (sa)
  {
    check(!sa->horizontalScrollBar()->isVisible(), "默认尺寸无横向滚动条");
    dlg.grab().save("/tmp/sl02_hull_dialog_full.png");

    QFont big = QApplication::font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    dlg.setFont(big);
    dlg.resize(720, 970);
    QApplication::processEvents();
    check(!sa->horizontalScrollBar()->isVisible(), "字号 125% 仍无横向滚动条");
    dlg.grab().save("/tmp/sl02_hull_dialog_font125.png");

    dlg.setFont(QApplication::font());
    dlg.resize(dlg.minimumSizeHint());
    QApplication::processEvents();
    check(dlg.minimumWidth() >= 560 && dlg.minimumHeight() >= 640,
          "最小尺寸不小于 560x640（防止被拖坏）");
  }

  std::printf("RESULT fail=%d\n", g_fail);
  std::printf("截图：/tmp/sl02_hull_dialog_full.png, /tmp/sl02_hull_dialog_font125.png\n");
  return g_fail == 0 ? 0 : 1;
}
