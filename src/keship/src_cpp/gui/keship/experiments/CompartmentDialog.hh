/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-04 舱室划分 —— 实验对话框
 *
 * 链位置（L2 结构建模→稳性链）：
 *   **SD-04 舱室划分** → HS-07 浮体布局设计 → HS-08 完整稳性综合校核
 *                     ↘ SD-07 破舱稳性仿真
 *
 * 本实验负责的**交付物**（判据 B 要求）：
 *   导出 `sd04_compartments.json`（schema keship.exp.v1），`extra.compartments`
 *   逐舱给出轴对齐长方体（x1/x2/y1/y2/z1/z2）+ 体积 + 渗透率 μ。
 *   下游 HS-07（布置浮力体）与 SD-07（逐舱试破）都必须真读这份文件。
 *
 * 为什么舱室结构复用 keship::Compartment 而不是在本文件另立一个 struct：
 *   SD-07 的破舱内核 FloodOne 吃的就是 keship::Compartment；若这里另写一份
 *   "看起来一样"的结构，字段（尤其 permeability 的语义）一旦漂移，
 *   JSON 里写出去的东西和内核读进来的东西就会悄悄不等 —— 这类错在界面上看不出来。
 *   共用同一个类型 = 编译期就保证字段一致。
 *
 * 为什么用"舱壁位置 → 舱室网格"而不是让用户逐个画舱：
 *   教学里学生要体会的是"加一道横舱壁，舱容曲线怎么变、破舱后果怎么变"；
 *   逐舱点选会把注意力从"分舱原则"引到"填表"。故界面只让用户增删舱壁位置，
 *   舱室由横/纵舱壁的笛卡尔积自动生成，渗透率再逐舱微调。
 *
 * 几何口径（与 StabilityGzCore / DamageStabilityCore 全模块一致）：
 *   x 从艉(0) 向艏(L)、y 指向右舷（−B/2 … +B/2）、z 从龙骨基线(0) 向上到型深 D。
 *   舱室为轴对齐长方体，z1=0、z2=D 是"整层舱"的默认值（本实验不做双层底）。
 */

#ifndef COMPARTMENT_DIALOG_HH_
#define COMPARTMENT_DIALOG_HH_

#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "DamageStabilityCore.hh"   // 复用 keship::Compartment（见文件头说明）

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QTableWidget;

/// 沿船长的舱容累积曲线（QPainter 自绘）
///
/// 为什么自绘而不引 QtCharts：平台其余图表（HS-05 散点、MS-02 时程）都是 QPainter
/// 自绘，多一个模块就多一条运行时依赖；本图只需一条折线 + 一条参考线。
class CompartmentChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit CompartmentChartWidget(QWidget *_parent = nullptr);

  /// _xs/_vs/_vps：累积总舱容 / 累积可浸舱容（均已按 m³）；_L 船长
  void setCurve(const std::vector<double> &_xs, const std::vector<double> &_vs,
                const std::vector<double> &_vps, double _L, double _totalV);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  std::vector<double> xs_, vs_, vps_;
  double L_ = 6.0;
  double totalV_ = 0.0;
};

class CompartmentDialog : public QDialog
{
  Q_OBJECT

public:
  explicit CompartmentDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用，不弹 QFileDialog）。成功返回 true。
  /// 写的是**当前界面算出的**这一版舱室表（导出前会重算一次，保证文件自洽）。
  bool exportTo(const QString &_path);

  /// 当前舱室数（供接线/离屏测试核对，不写文件）
  int compartmentCount() const { return static_cast<int>(comps_.size()); }
  /// 当前总舱容 m³
  double totalVolumeM3() const { return totalVolume_; }
  /// 无交互设置舱壁位置（离屏测试用；0/L、±B/2 仍是隐含端点）。返回是否成功。
  bool setBulkheads(const std::vector<double> &_transverse,
                    const std::vector<double> &_longitudinal);
  QString errMsg() const { return errMsg_; }

private slots:
  void onAddTransverse();
  void onRemoveTransverse();
  void onAddLongitudinal();
  void onRemoveLongitudinal();
  void onRebuild();
  void onExport();
  void onCellChanged(int _row, int _col);

private:
  void buildUi();
  /// 读界面 → 生成舱室 → 刷表/图/标签/缓存。返回 false 表示输入非法（_errMsg 给原因），
  /// 此时**不**改动已有结果，避免"半套数据"被导出。
  bool refresh();
  /// 由 comps_ 重算体积/可浸体积并刷新表格右三列（不重建行，避免编辑时丢焦点）
  void recomputeVolumes();
  void refillTable();
  void updateChartAndSummary();
  void updateCache();

  std::vector<double> bulkheadPositions(bool _transverse) const;

  // === 输入 ===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *permSpin_ = nullptr;      ///< 新建舱室的默认渗透率
  QDoubleSpinBox *bulkheadSpin_ = nullptr;  ///< 待添加舱壁位置
  QListWidget *xList_ = nullptr;            ///< 横向舱壁（内部位置，0/L 为隐含端点）
  QListWidget *yList_ = nullptr;            ///< 纵向舱壁（内部位置，±B/2 为隐含舷侧）

  // === 输出 ===
  QTableWidget *compTable_ = nullptr;
  CompartmentChartWidget *chart_ = nullptr;
  QLabel *summaryLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  // === 状态 ===
  std::vector<keship::Compartment> comps_;
  std::vector<double> curveX_, curveV_, curveVp_;
  double totalVolume_ = 0.0;
  double totalPermVolume_ = 0.0;
  QString errMsg_;

  // === 交付物缓存（导出写"这次算的"，不写导出瞬间界面上可能被改过的值） ===
  QJsonObject lastObject_, lastCondition_, lastParams_, lastMetrics_, lastExtra_;
};

#endif  // COMPARTMENT_DIALOG_HH_
