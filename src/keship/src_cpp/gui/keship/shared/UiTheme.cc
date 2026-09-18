/*
 * Copyright (C) 2024–2026 KeShip Team
 */
#include "UiTheme.hh"

#include <QColor>
#include <QDialog>
#include <QIcon>
#include <QPalette>
#include <QPushButton>
#include <QSize>
#include <QString>

namespace
{
/// 设计令牌（与 QML 侧 MenuPluginKeship.qml 的 QtObject{ id: theme } 保持一致）
const char *kSurface      = "#ffffff";
const char *kSurfaceAlt   = "#f4f6fa";
const char *kButtonBg     = "#e8e8ee";
const char *kText         = "#20202a";
const char *kTextDim      = "#999999";
const char *kPrimary      = "#7384e4";
const char *kOnPrimary    = "#ffffff";
}  // namespace

QString UiTheme::statusColor(Status s)
{
  switch (s)
  {
    case Status::kOk:       return QStringLiteral("#338833");
    case Status::kWarn:     return QStringLiteral("#d9a62e");
    case Status::kFail:     return QStringLiteral("#d9534f");
    case Status::kDisabled: return QStringLiteral("#999999");
  }
  return QStringLiteral("#20202a");
}

/// 把设计令牌应用到**单个**对话框。
///
/// 背景（2026-09-18 实测）：曾用 QApplication::setPalette(pal, "QDialog") 做**全局**统一，
/// 但那会把原生按钮 / 边框的细节绘制拉平 —— 一套配色套到全部对话框上观感并不合适，
/// 已经撤销（张老师拍板："不适合统一风格，逐个修改"）。本函数保留为**逐个修改**时
/// 可选的手段，仅作用于传入的那一个对话框。
///
/// 两条实测结论值得留存：
///   1. **QSS 无法"只改颜色、不改尺寸"** —— 某类控件一旦被 QSS 规则匹配，Qt 即改用
///      QStyleSheetStyle 接管，其默认 padding / border 推导与原生 style 不同，尺寸必然变化
///      （样板实测 10 个类变化，如 QLineEdit 高 27→23，即便只写颜色属性、不含尺寸与字体）。
///   2. **QPalette 不参与布局盒模型** —— 尺寸零变化（探针实测指纹逐项一致），
///      但代价是覆盖 style 的细节绘制、观感偏平。是否采用取决于具体对话框。
void UiTheme::applyToDialog(QDialog *dialog)
{
  if (nullptr == dialog)
    return;

  QPalette pal = dialog->palette();

  // 容器 / 背景
  pal.setColor(QPalette::Window,          QColor(kSurfaceAlt));   // 对话框底
  pal.setColor(QPalette::Base,            QColor(kSurface));      // 输入框 / 列表底
  pal.setColor(QPalette::AlternateBase,   QColor(kSurfaceAlt));   // 交替行

  // 文字
  pal.setColor(QPalette::WindowText,      QColor(kText));
  pal.setColor(QPalette::Text,            QColor(kText));
  pal.setColor(QPalette::PlaceholderText, QColor(kTextDim));

  // 按钮
  pal.setColor(QPalette::Button,          QColor(kButtonBg));
  pal.setColor(QPalette::ButtonText,      QColor(kText));

  // 选中 / 高亮（主色）
  pal.setColor(QPalette::Highlight,       QColor(kPrimary));
  pal.setColor(QPalette::HighlightedText, QColor(kOnPrimary));

  // 提示气泡
  pal.setColor(QPalette::ToolTipBase,     QColor(kSurface));
  pal.setColor(QPalette::ToolTipText,     QColor(kText));

  // 禁用态
  pal.setColor(QPalette::Disabled, QPalette::WindowText, QColor(kTextDim));
  pal.setColor(QPalette::Disabled, QPalette::Text,       QColor(kTextDim));
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(kTextDim));

  dialog->setPalette(pal);  // 仅作用于该对话框（调色板会向子控件传播）
}

void UiTheme::setButtonIcon(QPushButton *btn, Icon which)
{
  if (nullptr == btn)
    return;

  /// 自绘 SVG 图标（2026-09-18，张老师定"图标来自自绘 SVG"）
  /// 实体文件：`gui/keship/icons/*.svg`，由 `gui/keship/icons.qrc` 以
  /// prefix `KeShipIcons/` 注册。**路径只在此处出现一次**。
  static const char *kPath[] = {
    ":/KeShipIcons/ok.svg",
    ":/KeShipIcons/cancel.svg",
    ":/KeShipIcons/close.svg",
    ":/KeShipIcons/add.svg",
    ":/KeShipIcons/delete.svg",
    ":/KeShipIcons/save.svg",
  };
  const int idx = static_cast<int>(which);
  if (idx < 0 || idx >= static_cast<int>(sizeof(kPath) / sizeof(kPath[0])))
    return;

  btn->setIcon(QIcon(QString::fromLatin1(kPath[idx])));
  btn->setIconSize(QSize(16, 16));
}
