/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-01 船体结构认知 —— 实验对话框
 *
 * 链位置（L3 结构设计链）：
 *   **SD-01 船体结构认知** → SD-03 横剖面结构 → SD-09 综合结构设计
 *
 * 本实验负责的**交付物**（判据 B 要求）：
 *   导出 `sd01_structure.json`（schema keship.exp.v1），`extra.members[]` 逐构件给出
 *   name / category / thicknessM / stiffenerSpacingM / stiffenerWebHeightM /
 *   stiffenerWebThicknessM —— SD-03 读入后**必须**能用这几列直接构造中横剖面。
 *   故本文件里"板厚/骨材"不是装饰性输入：改一行板厚，下游的剖面模数就会变。
 *
 * 为什么示意剖面用 MidshipFromMembers() 而不是另画一张"漂亮但无关"的图：
 *   学生看到的图与 SD-03 实际计算的截面必须是**同一个几何**。若示意自己写一套
 *   画法，图上"甲板在这儿、内底在那儿"与下游算的 z 值可能对不上，
 *   而界面完全看不出来（两边都"像条船"）。故图由内核展开的 MidshipInput 驱动。
 */

#ifndef STRUCTURE_COGNITION_DIALOG_HH_
#define STRUCTURE_COGNITION_DIALOG_HH_

#include <string>
#include <vector>

#include <QDialog>
#include <QString>
#include <QWidget>

#include "MidshipSectionCore.hh"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QPaintEvent;

/// 中横剖面示意（板 + 骨材，不要求真实型线）
///
/// 不声明 Q_OBJECT：本控件只需要 paintEvent，没有信号/槽，
/// 少一次 moc 就少一个"忘了把 .hh 加进 CMake 就链接失败"的坑。
class StructureSectionWidget : public QWidget
{
public:
  explicit StructureSectionWidget(QWidget *_parent = nullptr);

  /// 用内核展开后的截面刷新（与 SD-03 计算的是同一份几何）
  void setSection(const keship::MidshipInput &_in, double _doubleBottomHeightM);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  keship::MidshipInput in_;
  double hdb_ = 0.0;
};

/// SD-01 对话框
class StructureCognitionDialog : public QDialog
{
  Q_OBJECT

public:
  explicit StructureCognitionDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试 / 脚本用，不弹 QFileDialog）。成功返回 true。
  bool exportTo(const QString &_path, std::string *_err);

private slots:
  void onAddRow();
  void onRemoveRow();
  void onResetTable();
  void onTableEdited();
  void onSubmitQuiz();
  void onExport();

private:
  void buildUi();
  void fillTable(const std::vector<keship::StructureMember> &_members);
  std::vector<keship::StructureMember> collectMembers() const;
  void refreshSection();
  QWidget *buildQuizBox();

  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *hdbSpin_ = nullptr;

  QTableWidget *table_ = nullptr;
  StructureSectionWidget *sectionView_ = nullptr;

  std::vector<QComboBox *> quizCombos_;
  QLabel *quizResultLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  int quizTotal_ = 5;
  int score_ = 0;             ///< 已判分的得分（未提交为 0）
  int scoredQuestions_ = 0;   ///< 已判分的题数（未提交为 0）
};

#endif  // STRUCTURE_COGNITION_DIALOG_HH_
