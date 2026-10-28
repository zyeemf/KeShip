/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-08 完整稳性综合校核 —— 实验对话框
 *
 * 链位置（L2 结构建模→稳性链）：
 *   SD-04 舱室划分 → HS-07 浮体布局设计 → **HS-08 完整稳性综合校核** → SD-07 破舱稳性仿真
 *
 * 本实验负责的**交付物**：
 *   导出 `hs08_gz.json`：`metrics` 给 GZ 曲线特征值与 IMO 五项判据结论，
 *   `extra.curve` 给逐点 GZ(φ)。SD-07 读 `metrics.gm0M` 当完整船基准 GM。
 *
 * 为什么界面必须显式标出 boxSection（本文件最要紧的一条）：
 *   StabilityGzCore 的 GZ 只由"各站水线半宽 + 吃水 + KG"决定，**不含舭部圆弧与外飘**。
 *   · boxSection = true  → 强制 b = B/2，即"矩形驳船"，有壁侧解析式可手算校核；
 *   · boxSection = false → 按站表逐站变半宽，剖面仍是"舷侧直立"的等价船。
 *   两种都不是真实船型的 GZ。若界面上不写清楚，学生会把"箱形假设算出的 GZmax"
 *   当成真船指标去对标规范 —— 这是本实验最容易产生的误导，故假设标签常驻显示。
 *
 * 站表来源（三选一，界面下拉）：
 *   ① 矩形驳船：不需要站表，boxSection = true（教学基准，可与 wall-sided 解析式对照）；
 *   ② 数学船型：ShipHullGenerator 按 L/B/D 生成型值表，取**吃水处水线**沿站位的半宽，
 *      再按该水线最大半宽归一到 B/2（见 BuildStationsFromLines 注释）；
 *   ③ SD-08 型线交付物：读 `extra.stations/waterlines/halfBreadths`，取同一水线。
 */

#ifndef GZ_STABILITY_DIALOG_HH_
#define GZ_STABILITY_DIALOG_HH_

#include <vector>

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "StabilityGzCore.hh"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// GZ(φ) 曲线（QPainter 自绘：曲线 + 零轴 + GZmax 标记 + 消失角竖线）
class GzChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit GzChartWidget(QWidget *_parent = nullptr);

  void setCurve(const std::vector<keship::GzPoint> &_pts, double _gzMax,
                double _angleGzMax, double _vanishingDeg);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  std::vector<keship::GzPoint> pts_;
  double gzMax_ = 0.0;
  double angleGzMax_ = 0.0;
  double vanishDeg_ = 90.0;
};

class GzStabilityDialog : public QDialog
{
  Q_OBJECT

public:
  explicit GzStabilityDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用）。成功返回 true。
  bool exportTo(const QString &_path);

  /// 无交互导入（离屏测试用；失败时 errMsg() 给原因，不弹窗）
  bool importHs07(const QString &_path);
  bool importSd08(const QString &_path);
  /// 无交互切换站表来源（0=矩形驳船 1=数学船型 2=SD-08）并重算
  bool setStationSource(int _index);
  QString errMsg() const { return errMsg_; }

  /// 供接线/离屏测试读取最近一次结果
  double gzMaxM() const { return summary_.gzMaxM; }
  bool allPass() const { return summary_.allPass; }
  bool boxSection() const { return boxSection_; }

private slots:
  void onSourceChanged(int _index);
  void onImportHs07();
  void onImportSd08();
  void onCompute();
  void onExport();

private:
  void buildUi();
  /// 读界面 → 组站表 → GzInput → 曲线与汇总 → 表/图/标签/缓存。false = 输入非法
  bool refresh();
  void fillCriteriaTable();
  void updateCache();

  /// 由型值表（stations × waterlines × halfBreadths）取吃水处水线，归一为 HullStation 序列
  std::vector<keship::HullStation> buildStationsFromLines(
      const std::vector<double> &_st, const std::vector<double> &_wl,
      const std::vector<std::vector<double>> &_hb, double _draft, double _B,
      int *_wlIndex) const;

  // === 输入 ===
  QComboBox *sourceCombo_ = nullptr;
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *draftSpin_ = nullptr;
  QDoubleSpinBox *kgSpin_ = nullptr;
  QDoubleSpinBox *densitySpin_ = nullptr;

  // === 输出 ===
  GzChartWidget *chart_ = nullptr;
  QTableWidget *criteriaTable_ = nullptr;
  QLabel *assumptionLabel_ = nullptr;
  QLabel *resultLabel_ = nullptr;
  QLabel *upstreamLabel_ = nullptr;

  // === 状态 ===
  bool boxSection_ = true;
  QString stationSource_ = "box";
  std::vector<keship::HullStation> stations_;
  std::vector<keship::GzPoint> curve_;
  keship::GzSummary summary_;
  double displacementM3_ = 0.0;
  int waterlineIndex_ = -1;

  bool hasSd08_ = false;
  QJsonArray sd08Stations_, sd08Waterlines_, sd08HalfB_;

  bool hs07Imported_ = false;
  double upstreamKgM_ = 0.0;
  double upstreamBuoyancyN_ = 0.0;

  QString errMsg_;
  QJsonObject lastObject_, lastCondition_, lastParams_, lastMetrics_, lastExtra_;
};

#endif  // GZ_STABILITY_DIALOG_HH_
