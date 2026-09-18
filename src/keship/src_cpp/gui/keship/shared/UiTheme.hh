/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* UiTheme — KeShip 界面设计令牌（C++ / QWidget 侧）
 *
 * 与 QML 侧 MenuPluginKeship.qml 顶部的 QtObject{ id: theme } 使用**同一套色值**，
 * 两者共同构成"界面颜色的唯一来源"。改色时**两处都要改**——
 * 完整令牌表见 doc/plans/ui_improvement_plan.md §3。
 *
 * 2026-09-18 批次 2 · 阶段①：对话框统一风格，**仅颜色 + 字体，不含任何尺寸属性**。
 */

#ifndef KESHIP_UI_THEME_HH_
#define KESHIP_UI_THEME_HH_

#include <QString>

class UiTheme
{
public:
  /// 状态语义：统一各对话框原先混用的 green / orange / red / gray
  enum class Status
  {
    kOk,       ///< 通过 / 正常
    kWarn,     ///< 警告 / 临界
    kFail,     ///< 失败 / 超限
    kDisabled  ///< 未启用 / 待开发
  };

  /// 返回状态语义对应的色值
  static QString statusColor(Status s);

  /// 把 KeShip 对话框统一样式应用到整个 Qt 应用。
  /// - **幂等**：重复调用只生效一次（按样式表标记判断）
  /// - **追加式**：不覆盖应用既有样式表
  /// - **作用域**：限定 `QDialog` 及其后代，不影响 gz-gui 主界面
  static void applyGlobalDialogStyle();
};

#endif  // KESHIP_UI_THEME_HH_
