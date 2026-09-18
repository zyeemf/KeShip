/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* UiTheme — KeShip 界面设计令牌（C++ / QWidget 侧）
 *
 * 与 QML 侧 MenuPluginKeship.qml 顶部的 QtObject{ id: theme } 使用**同一套色值**，
 * 两者共同构成"界面颜色的唯一来源"。改色时**两处都要改**——
 * 完整令牌表见 doc/plans/ui_improvement_plan.md §3。
 *
 * 策略（2026-09-18 张老师拍板）：
 *   **不采用"一套样式套用全部对话框"的全局统一方案** —— 实测会把原生按钮 / 边框的
 *   细节绘制拉平，观感不合适。改为**逐个对话框**按需调整：
 *     · statusColor()   —— 统一状态语义色（原先混用 green/orange/red/gray）
 *     · applyToDialog() —— 可选的「单对话框」调色板应用，仅在确有必要时使用
 */

#ifndef KESHIP_UI_THEME_HH_
#define KESHIP_UI_THEME_HH_

#include <QString>

class QDialog;      // 前置声明：仅在接口中出现指针，无需完整定义
class QPushButton;

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

  /// 按钮图标语义（2026-09-18 起为**自绘 SVG**，资源 prefix `KeShipIcons/`）
  enum class Icon
  {
    kOk,      ///< 确定 / 完成 / 载入（绿勾）
    kCancel,  ///< 取消（红叉）
    kClose,   ///< 关闭（中性叉，区别于"取消"）
    kAdd,     ///< 添加（绿加号）
    kDelete,  ///< 删除（红垃圾桶）
    kSave     ///< 保存（蓝软盘）
  };

  /// 返回状态语义对应的色值
  static QString statusColor(Status s);

  /// 把设计令牌作为调色板应用到**单个**对话框（调色板会向子控件传播），
  /// 不影响其他窗口 —— 「逐个修改」策略可选的基础设施。
  ///
  /// ⚠️ 注意：调色板会覆盖原生 style 对按钮 / 边框的细节绘制，可能让控件观感变平。
  ///    仅在确有必要时使用，且必须逐个截图核对。
  static void applyToDialog(QDialog *dialog);

  /// 给按钮设置 KeShip 自绘图标，并固定图标尺寸 16×16。
  /// **图标路径集中在 UiTheme.cc** —— 日后换图标或改尺寸只改一处，
  /// 避免 `":/KeShipIcons/xxx.svg"` 散落到几十个对话框里。
  static void setButtonIcon(QPushButton *btn, Icon which);
};

#endif  // KESHIP_UI_THEME_HH_
