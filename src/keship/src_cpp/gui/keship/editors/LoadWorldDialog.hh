/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* 载入世界对话框：命名空间 + 六向整体偏移 + 模型/水域勾选树 */

#ifndef LOAD_WORLD_DIALOG_HH_
#define LOAD_WORLD_DIALOG_HH_

#include <QDialog>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QTreeWidget>
#include <QFormLayout>
#include <QDialogButtonBox>

class QPushButton;

/// \brief 载入 KSW 世界对话框
///
/// 结构：
///  - 命名空间输入（默认留空；仅允许字母/数字/下划线/斜杠，中文会产生非法 ROS 2 主题）
///  - 六向整体偏移（X/Y/Z 平移 + R/P/Y 旋转，作用于本次载入全部实体）
///  - 模型/水域树勾选：世界名根组（最高层级，载入来源保留）下
///    “模型”“水域”分组（顶层实体）与子 ksw 链分组，各组内含“模型”“水域”分类，
///    叶子显示原名；默认全勾选，父节点勾选联动子节点
class LoadWorldDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _suggestedNamespace 建议命名空间（世界名；输入端默认留空，仅作树根组文本）
  /// @param _modelTree 模型树行（每行：子 World 链 \t 勾选标志 \t 完整路径文本）
  ///        子 World 链以 '/' 连接，勾选标志 1/0；完整路径文本 = 链/baseName
  /// @param _waterNames 水域行（每行：子 World 链 \t 勾选标志 \t 完整路径文本），
  ///        空链 → 根下“水域”，非空链 → 对应链分组下“水域”，默认全勾选
  explicit LoadWorldDialog(const QString &_suggestedNamespace,
                           const QStringList &_modelTree,
                           const QStringList &_waterNames,
                           QWidget *parent = nullptr);

  /// 命名空间（可含 / 多层标签）
  QString namespaceText() const;
  /// 六向偏移（角度为度制，调用方换算弧度）
  double offsetX() const;
  double offsetY() const;
  double offsetZ() const;
  double offsetRoll() const;
  double offsetPitch() const;
  double offsetYaw() const;
  /// 勾选的模型（完整路径文本列表）
  QStringList selectedModelNames() const;
  /// 勾选的水域（完整路径文本列表）
  QStringList selectedWaterNames() const;

private slots:
  void onAccept();

private:
  enum ItemRole { WaterRole = Qt::UserRole, FullTextRole = Qt::UserRole + 1 };

  void buildTree(const QStringList &_modelTree, const QStringList &_waterNames);
  void setAllChecked(bool _checked);

  QLineEdit *nsEdit_;
  QDoubleSpinBox *xSpin_, *ySpin_, *zSpin_;
  QDoubleSpinBox *rollSpin_, *pitchSpin_, *yawSpin_;
  QTreeWidget *tree_;
  QString rootText_;   ///< 世界根组文本（世界名，固定，不随输入变化）
};

#endif
