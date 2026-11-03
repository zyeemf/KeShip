/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-09 综合结构设计 —— 实验对话框
 *
 * 链位置（L3 结构设计链）：
 *   SD-01 船体结构认知 → SD-03 横剖面结构 → **SD-09 综合结构设计**
 *
 * 本实验**必须真读** SD-03 的产出（判据 B）：
 *   `sd03_midship.json` 的 metrics.sectionModulusDeckM3 / sectionModulusKeelM3
 *   经 `ValidExpDeliverable(d,"SD-03")` 校验后作为总纵弯曲应力的分母
 *   （σ = M/W）。本页还会用 extra.plates[]/stiffeners[] **独立复算**一遍 W，
 *   与 SD-03 交付值对比并显示偏差——上游若改了板厚，这里的 W 与应力都会跟着动。
 *
 * 本实验负责的**交付物**：
 *   `sd09_strength.json`：metrics 给出 stillWaterMomentKNm / waveMomentKNm /
 *   totalMomentKNm / deckStressMpa / keelStressMpa / allowableMpa /
 *   utilization / passed。
 */

#ifndef LONGITUDINAL_STRENGTH_DIALOG_HH_
#define LONGITUDINAL_STRENGTH_DIALOG_HH_

#include <string>
#include <vector>

#include <QDialog>
#include <QString>
#include <QWidget>

#include "MidshipSectionCore.hh"

class QDoubleSpinBox;
class QLabel;
class QTableWidget;
class QPaintEvent;

/// 应力柱状图（甲板 / 船底 / 许用应力），简化为柱状图
class StrengthStressWidget : public QWidget
{
public:
  explicit StrengthStressWidget(QWidget *_parent = nullptr);
  void setData(double _deckMpa, double _keelMpa, double _allowMpa, bool _valid);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  double deck_ = 0.0, keel_ = 0.0, allow_ = 0.0;
  bool valid_ = false;
};

/// SD-09 对话框
class LongitudinalStrengthDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LongitudinalStrengthDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用）。成功返回 true。
  bool exportTo(const QString &_path, std::string *_err);

  /// 无交互导入 SD-03 交付物（离屏测试用，不弹 QFileDialog）。成功返回 true。
  /// 与 onImportSd03() 走同一条代码路径 —— 测试覆盖的就是真实的导入分支。
  bool importFrom(const QString &_path, std::string *_err);

private slots:
  void onImportSd03();
  void onEstimateWave();
  void onDemoLoad();
  void onCompute();
  void onExport();

private:
  void buildUi();
  void refresh();

  // === 从 SD-03 导入的量（本页不重算，只做复算校验）===
  bool imported_ = false;
  double wDeckUp_ = 0.0;
  double wKeelUp_ = 0.0;
  double depthUp_ = 0.0;
  double recomputedWDeck_ = 0.0;
  double recomputedWKeel_ = 0.0;

  // === 输入 ===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *cbSpin_ = nullptr;
  QDoubleSpinBox *swMomentSpin_ = nullptr;
  QDoubleSpinBox *waveMomentSpin_ = nullptr;
  QDoubleSpinBox *allowableSpin_ = nullptr;

  // === 输出 ===
  StrengthStressWidget *chart_ = nullptr;
  QLabel *importLabel_ = nullptr;
  QLabel *resultLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  keship::StrengthInput in_;
  keship::StrengthResult res_;
};

#endif  // LONGITUDINAL_STRENGTH_DIALOG_HH_
