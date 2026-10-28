/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-07 破舱稳性仿真 —— 实验对话框
 *
 * 链位置（L2 结构建模→稳性链）：
 *   SD-04 舱室划分 → **SD-07 破舱稳性仿真**
 *   HS-08 完整稳性综合校核 → **SD-07**（提供完整船 GM 作基准对照）
 *
 * 本实验负责的**交付物**：
 *   导出 `sd07_damage.json`，`extra.cases` 逐舱给出进水量/新吃水/横倾/纵倾/残存 GM/
 *   能否漂浮/是否满足一舱不沉；`metrics` 给最危险舱与全船结论。
 *
 * 为什么破舱用**损失浮力法**而不是增加重量法（内核如此，界面也如此解释）：
 *   增加重量法要把进水当成"在舱内某处加一块重量"，于是必须知道舱内自由液面的形状与
 *   惯性矩才能算自由液面修正；本平台的数据只有"舱室长方体 + 渗透率"，没有舱内几何。
 *   损失浮力法则把破口视为与舷外连通：该舱不再提供浮力，于是"完整船浸水体 − μ·破损舱"
 *   仍等于原排水体积，解出新水线即可，**天然没有自由液面项**。
 *   代价是：不能回答"进水过程中"的瞬态，只能回答"最终平衡态"。
 *
 * 为什么完整船用 boxSection = true：
 *   SD-04 的舱室是轴对齐长方体，隐含"船体是矩形驳船"。若这里改用变半宽剖面，
 *   舱室就可能落在船体半宽包络之外，内核会直接判"损失浮力法不适用"。
 *   口径一致（矩形舱 ↔ 矩形船体）才能让逐舱试破有意义。
 */

#ifndef DAMAGE_STABILITY_DIALOG_HH_
#define DAMAGE_STABILITY_DIALOG_HH_

#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "DamageStabilityCore.hh"   // FloodOne / Compartment / DamageResult
#include "StabilityGzCore.hh"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// 逐舱残存 GM 柱状图（QPainter 自绘，带 0.15 m 参考线）
///
/// 为什么不画"残存 GZ 曲线"：DamageStabilityCore 对外只给 gzMaxResidual 与 gmResidual，
/// 逐角度的残存 GZ 求解器在 .cc 的匿名命名空间里（非对外 API）。这里不复制一份内核，
/// 而是把"逐舱残存 GM"画清楚 —— 破舱教学真正要看的是哪一舱破得最狠。
class DamageGmBarWidget : public QWidget
{
  Q_OBJECT

public:
  explicit DamageGmBarWidget(QWidget *_parent = nullptr);

  void setData(const QStringList &_names, const std::vector<double> &_gm,
               const std::vector<bool> &_canFloat);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  QStringList names_;
  std::vector<double> gm_;
  std::vector<bool> canFloat_;
};

class DamageStabilityDialog : public QDialog
{
  Q_OBJECT

public:
  explicit DamageStabilityDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用）。成功返回 true。
  bool exportTo(const QString &_path);

  /// 无交互导入（离屏测试用；失败时 errMsg() 给原因，不弹窗）
  bool importSd04(const QString &_path);
  bool importHs08(const QString &_path);
  QString errMsg() const { return errMsg_; }

  /// 供接线/离屏测试读取
  int caseCount() const { return static_cast<int>(results_.size()); }
  bool oneCompartmentStandard() const { return oneCompartmentStandard_; }
  double worstGmResidual() const { return worstGmResidual_; }

private slots:
  void onImportSd04();
  void onImportHs08();
  void onCompute();
  void onExport();

private:
  void buildUi();
  /// 读界面 → 组 GzInput → 逐舱 FloodOne → 表/图/标签/缓存。false = 输入非法
  bool refresh();
  void fillTable();
  void updateSummary();
  void updateCache();

  // === 输入 ===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *draftSpin_ = nullptr;
  QDoubleSpinBox *kgSpin_ = nullptr;
  QDoubleSpinBox *densitySpin_ = nullptr;
  QComboBox *floodCombo_ = nullptr;   ///< 0 = 全部单舱逐一试破；k = 指定第 k 舱

  // === 输出 ===
  QTableWidget *resultTable_ = nullptr;
  DamageGmBarWidget *barWidget_ = nullptr;
  QLabel *upstreamLabel_ = nullptr;
  QLabel *summaryLabel_ = nullptr;

  // === 上游数据（真消费） ===
  bool sd04Imported_ = false;
  double sd04TotalVolume_ = 0.0;
  int sd04NCompartments_ = 0;
  bool hs08Imported_ = false;
  double hs08Gm0_ = 0.0;

  // === 状态 ===
  std::vector<keship::Compartment> comps_;
  std::vector<keship::DamageResult> results_;
  QStringList caseNames_;
  double intactGm0M_ = 0.0;
  double worstGmResidual_ = 0.0;
  QString worstCompartment_;
  int nCanFloat_ = 0;
  bool oneCompartmentStandard_ = false;
  QString errMsg_;

  QJsonObject lastObject_, lastCondition_, lastParams_, lastMetrics_, lastExtra_;
};

#endif  // DAMAGE_STABILITY_DIALOG_HH_
