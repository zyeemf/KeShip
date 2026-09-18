/*
 * Copyright (C) 2024–2026 KeShip Team
 */
#include "UiTheme.hh"

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>

namespace
{
/// 阶段① 是否已应用（QPalette 无"标记"可查，用静态标志保证幂等）
bool gStage1Applied = false;

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

/// 阶段①：对话框统一配色
///
/// ⚠️ 为何用 QPalette 而不是 QSS（2026-09-18 探针实测结论）：
///   QSS **无法做到"只改颜色、不改尺寸"** —— 一旦某类控件被 QSS 规则匹配，
///   Qt 便改用 QStyleSheetStyle 接管它，其默认 padding / border 推导与原生
///   style 不同，控件尺寸必然变化。实测（仅写颜色属性、不含任何尺寸/字体）：
///   QLineEdit 高 27→23、QDoubleSpinBox 30→29、QGroupBox 256→252 …… 10 个类出现差异。
///   QPalette 只参与**绘制取色**、不参与布局盒模型，因此可稳定做到零尺寸变化。
///
/// 作用域：Qt 支持按类名设置调色板，故只作用于 `QDialog` 及其后代，
///         不影响 gz-gui 主界面与面板。
///
/// 精细样式（如单个控件类型的外观差异）以及**字体与尺寸统一**留待阶段②，
/// 届时改用 QSS 并逐个对话框截图核对（阶段② 必然带来尺寸变化，属预期）。
void UiTheme::applyGlobalDialogStyle()
{
  QApplication *app = qApp;
  if (nullptr == app)
    return;
  if (gStage1Applied)
    return;  // 幂等

  QPalette pal = app->palette();

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

  QApplication::setPalette(pal, "QDialog");  // 仅作用于 QDialog 及其后代
  gStage1Applied = true;
}
