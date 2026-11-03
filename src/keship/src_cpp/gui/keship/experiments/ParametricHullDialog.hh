/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-08 参数化型线设计 —— 实验对话框
 *
 * 链位置（L1 船型设计链）：
 *   HS-01 型线图与静水力曲线 → **SD-08 参数化型线设计** → HS-05 数学船型稳性对比 → HS-09 编程计算排水量
 *
 * 本实验负责的**交付物**（判据 B 要求）：
 *   导出 `sd08_lines.json`（schema keship.exp.v1），含主尺度、型值表（stations/waterlines/
 *   halfBreadths）与静水力指标；下游 HS-05 读入后必须复现同一船型的 GM。
 *
 * 为什么做成"参数化 + 反解 Cb"：
 *   平台的 ShipHullSpec 只有 L/B/D/rho + 两个形状指数，**没有 Cb 输入**；
 *   而课程知识点要的是"主尺度与型线系数"的关系。故本对话框除直接给定形状指数外，
 *   提供「按目标 Cb 反解水线面指数 p」——对 p 单调二分，把 Cb 从"算出来是多少"
 *   变成"要多少给多少"。这既是教学内容，也是 HS-05 / RP-05 做参数扫描的输入口。
 */

#ifndef PARAMETRIC_HULL_DIALOG_HH_
#define PARAMETRIC_HULL_DIALOG_HH_

#include <memory>
#include <string>

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace keship {
class ShipHullGenerator;
}  // namespace keship

class ParametricHullDialog : public QDialog
{
  Q_OBJECT

public:
  explicit ParametricHullDialog(QWidget *parent = nullptr);

  /// 当前达成的方形系数（体积口径，供测试与下游读取）
  double achievedCb() const { return cbAchieved_; }

  /// 无交互导出（离屏测试用，不弹 QFileDialog）。成功返回 true。
  /// 与 onExport() 共用同一段组装代码，测试因此覆盖真实导出分支。
  bool exportTo(const QString &_path, std::string *_err);

private slots:
  void onFamilyChanged(int _idx);
  void onRegenerate();
  void onSolveCb();
  void onExport();

private:
  void buildUi();
  void refresh();          ///< 重建船型 → 刷新型值表与静水力
  void fillLinesTable();
  void fillHydroLabels();
  QString currentFamilyName() const;

  // === 输入 ===
  QComboBox *familyCombo_ = nullptr;
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *densitySpin_ = nullptr;
  QDoubleSpinBox *wlExpSpin_ = nullptr;
  QDoubleSpinBox *secExpSpin_ = nullptr;
  QDoubleSpinBox *targetCbSpin_ = nullptr;

  // === 输出 ===
  QTableWidget *linesTable_ = nullptr;
  QLabel *hydroLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  // === 计算状态 ===
  std::unique_ptr<keship::ShipHullGenerator> hull_;
  double cbAchieved_ = 0.0;
  int lastStationStride_ = 1;
  int lastWaterlineStride_ = 1;
};

#endif  // PARAMETRIC_HULL_DIALOG_HH_
