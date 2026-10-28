/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-03 横剖面结构 —— 实验对话框
 *
 * 链位置（L3 结构设计链）：
 *   SD-01 船体结构认知 → **SD-03 横剖面结构** → SD-09 综合结构设计
 *
 * 本实验**必须真读** SD-01 的产出（判据 B）：
 *   `sd01_structure.json` 的 `extra.members[]`（名称/类别/板厚/骨材间距/骨材尺寸）
 *   经 `ValidExpDeliverable(d,"SD-01")` 校验后，由 `MidshipFromMembers()` 展开成
 *   `MidshipInput`（板 + 竖直腹板），再用 `ComputeMidship()` 算组合截面特性。
 *   界面上同时显示「上游交付值 vs 本页复现值」，偏差 >2% 会标红并告警
 *   —— 与 HS-05 读 SD-08 的做法一致，避免"看起来导进去了其实算的是另一条船"。
 *
 * 本实验负责的**交付物**：
 *   `sd03_midship.json`：metrics 给出 sectionAreaM2 / neutralAxisZM / ixM4 /
 *   sectionModulusDeckM3 / sectionModulusKeelM3；extra 里另存展开后的
 *   plates[]/stiffeners[]，SD-09 可用它**独立复算** W 并与 metrics 对账。
 */

#ifndef MIDSHIP_SECTION_DIALOG_HH_
#define MIDSHIP_SECTION_DIALOG_HH_

#include <string>
#include <vector>

#include <QDialog>
#include <QString>
#include <QWidget>

#include "MidshipSectionCore.hh"

class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QPaintEvent;

/// 中横剖面 + 中和轴示意
class MidshipSectionWidget : public QWidget
{
public:
  explicit MidshipSectionWidget(QWidget *_parent = nullptr);
  void setSection(const keship::MidshipInput &_in, const keship::MidshipResult &_res);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  keship::MidshipInput in_;
  keship::MidshipResult res_;
};

/// SD-03 对话框
class MidshipSectionDialog : public QDialog
{
  Q_OBJECT

public:
  explicit MidshipSectionDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用）。成功返回 true。
  bool exportTo(const QString &_path, std::string *_err);

  /// 无交互导入 SD-01 交付物（离屏测试用，不弹 QFileDialog）。成功返回 true。
  /// 与 onImportSd01() 走同一条代码路径，测试因此能覆盖"真实的导入分支"，
  /// 而不是另写一段"看起来一样"的读文件逻辑。
  bool importFrom(const QString &_path, std::string *_err);

  /// 当前剖面模数（供测试/下游读取，未导入亦可取默认表结果）
  double sectionModulusDeck() const { return res_.sectionModulusDeckM3; }

private slots:
  void onImportSd01();
  void onRecalc();
  void onExport();
  void onEditCell();

private:
  void buildUi();
  void fillMemberTable(const std::vector<keship::StructureMember> &_members);
  std::vector<keship::StructureMember> collectMembers() const;
  void fillComponentTable();
  void refresh();

  // === 主尺度（导入 SD-01 后自动填）===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *hdbSpin_ = nullptr;

  QTableWidget *memberTable_ = nullptr;  ///< 结构要素（可微调板厚/骨材）
  QTableWidget *compTable_ = nullptr;    ///< 构件表（面积/形心/自身惯性矩）
  MidshipSectionWidget *view_ = nullptr;

  QLabel *importLabel_ = nullptr;   ///< 上游值 vs 本页复现值
  QLabel *resultLabel_ = nullptr;   ///< A / z_NA / I / W
  QLabel *statusLabel_ = nullptr;

  // === 计算状态 ===
  keship::MidshipInput in_;
  keship::MidshipResult res_;

  bool imported_ = false;
  bool clamped_ = false;                           ///< 上游主尺度超出本页量程被截断
  std::vector<keship::StructureMember> upstream_;  ///< 上游原样快照（用于"上游 vs 本页"）
  double upstreamPlateAreaM2_ = 0.0;               ///< SD-01 交付的 totalPlateAreaM2
  int upstreamScore_ = 0;                          ///< SD-01 交付的小测得分（教学闭环）
  int upstreamScoredQuestions_ = 0;
};

#endif  // MIDSHIP_SECTION_DIALOG_HH_
