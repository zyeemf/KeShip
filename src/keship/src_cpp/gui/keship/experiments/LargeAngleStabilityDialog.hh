/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-06 大倾角稳性 —— 原理验证实验对话框
 *
 * 与 HS-08 的分工（两项**不能**重复，否则等于同一实验做两遍）：
 *   · HS-06（本文件）= **原理验证**：回答"小倾角公式从多少度开始失效"。
 *     把 wall-sided 解析式  GZ = sinφ·(GM + ½·BM·tan²φ)  与 StabilityGzCore 的
 *     等体积倾斜数值积分**并排**比较，标出三个特征角：
 *         舭部出水角      atan(2T/B)
 *         甲板边缘入水角  atan(2(D−T)/B)
 *         wall-sided 失效点（两法相对偏差**首次** > 5% 的角度）
 *   · HS-08 = **综合校核**：IMO 五项衡准 + 分舱（见 GzStabilityDialog）。
 *
 * 为什么必须并排两条曲线：
 *   学生最常见的误解是"GZ 曲线随便一条就行"。真相是：wall-sided 式只在
 *   "舷侧直立、水线同切两舷、甲板未入水" 时成立；舭部一出水（21.8°）前提就破，
 *   但**数值上**要到 26° 左右偏差才到 5%。把"公式还在算、数已经错了"画出来，
 *   比任何文字说明都直观 —— 故失效域内的解析曲线用**浅色**继续画，而不是截断。
 *
 * 交付物 `hs06_gz.json`（schema keship.exp.v1，expId = "HS-06"）：
 *   metrics：gm0M / gzMaxM / angleGzMaxDeg / bilgeEmergenceDeg / deckImmersionDeg /
 *            wallSidedValidUntilDeg / maxDeviationPct / boxSection
 *   extra.table：逐角度 {phi, gzNumeric, gzWallSided, devPct, valid}
 *   本实验是**非链**实验（认知/原理验证类），不产出被下游引用的交付物；
 *   但交付物仍写成与链上实验同一 schema，便于统一批阅与归档。
 *
 * 解析式的 GM/BM 口径（与数值内核必须一致，否则对比无意义）：
 *   内核把每站视为"舷侧直立的矩形剖面（0..D）"，正浮吃水 T ⇒ KB = T/2，
 *   BM = Ix/∇，其中 ∇ = ∫2b·T dx、Ix = ∫(2/3)b³ dx（正浮水线面惯性矩）。
 *   矩形剖面时 b ≡ B/2，退化为 BM = B²/(12T)，可与手算逐位对账。
 */

#ifndef LARGE_ANGLE_STABILITY_DIALOG_HH_
#define LARGE_ANGLE_STABILITY_DIALOG_HH_

#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "StabilityGzCore.hh"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace keship {
namespace hs06 {

/// wall-sided 解析式所需的静水力（全部由站表 + 吃水现算，无经验系数）
struct WallSidedModel {
  double volumeM3 = 0.0;  ///< ∇ = ∫2b·T dx
  double ixM4 = 0.0;      ///< Ix = ∫(2/3)b³ dx（正浮水线面惯性矩）
  double kbM = 0.0;       ///< KB = T/2（直立舷侧矩形剖面）
  double bmM = 0.0;       ///< BM = Ix/∇
  double gmM = 0.0;       ///< GM = KB + BM − KG
  bool valid = false;
};

/// 由站表构造解析式模型。_boxSection = true 时忽略站表，b ≡ B/2（矩形驳船）。
WallSidedModel BuildWallSidedModel(const std::vector<HullStation> &_stations,
                                   bool _boxSection, double _B, double _L,
                                   double _T, double _KG);

/// wall-sided 解析式 GZ = sinφ·(GM + ½·BM·tan²φ)。φ 以度给定，|φ| ≥ 89.9 时按 89.9 取值。
double WallSidedGz(double _phiDeg, const WallSidedModel &_m);

/// 三个特征角中的前两个（第三个由 SampleWallSided 的偏差扫描给出）
struct FeatureAngles {
  double bilgeDeg = 0.0;  ///< 舭部出水角 atan(2T/B)
  double deckDeg = 0.0;   ///< 甲板边缘入水角 atan(2(D−T)/B)
};

FeatureAngles ComputeFeatureAngles(double _B, double _T, double _D);

/// 逐角度对比行
struct SampleRow {
  double phiDeg = 0.0;
  double gzNumeric = 0.0;
  double gzWallSided = 0.0;
  double devPct = 0.0;   ///< |数值−解析| / |解析| × 100（解析为 0 时按 0 处理）
  bool valid = false;    ///< devPct ≤ 容差（即"解析式还能用"）
};

struct SampleTable {
  std::vector<SampleRow> rows;
  double maxDevPct = 0.0;
  double validUntilDeg = 0.0;  ///< 偏差首次 > 容差的角度；全程不超则取 _maxDeg
};

/// 逐角度（含 0 与 _maxDeg 两端点）对比数值与解析 GZ。
SampleTable SampleWallSided(const GzInput &_in, const WallSidedModel &_m,
                            double _maxDeg, double _stepDeg, double _tolPct);

}  // namespace hs06
}  // namespace keship

/// GZ(φ) 对比曲线（QPainter 自绘：数值实线 + 解析虚线 + 三条特征角竖线）
class LargeAngleChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit LargeAngleChartWidget(QWidget *_parent = nullptr);

  void setData(const std::vector<keship::hs06::SampleRow> &_rows,
               const keship::hs06::FeatureAngles &_feat,
               double _validUntilDeg);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  std::vector<keship::hs06::SampleRow> rows_;
  keship::hs06::FeatureAngles feat_;
  double validUntilDeg_ = 0.0;
};

class LargeAngleStabilityDialog : public QDialog
{
  Q_OBJECT

public:
  explicit LargeAngleStabilityDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用）：先按当前输入重算，再写交付物
  bool exportTo(const QString &_path);
  /// 无交互切换剖面假设（true = 矩形剖面 b=B/2；false = 按 L/B/D 生成数学船型）并重算
  bool setBoxSection(bool _box);
  QString errMsg() const { return errMsg_; }

  /// 供接线/离屏测试读取最近一次结果
  double gm0M() const { return model_.gmM; }
  double bmM() const { return model_.bmM; }
  double gzMaxM() const { return summary_.gzMaxM; }
  double bilgeEmergenceDeg() const { return feat_.bilgeDeg; }
  double deckImmersionDeg() const { return feat_.deckDeg; }
  double wallSidedValidUntilDeg() const { return table_.validUntilDeg; }
  double maxDeviationPct() const { return table_.maxDevPct; }

private slots:
  void onCompute();
  void onExport();

private:
  void buildUi();
  /// 读界面 → GzInput → 数值曲线 + 解析模型 + 对比表。false = 输入非法
  bool refresh();
  void fillTable();
  void updateCache();

  /// 由型值表取"吃水处水线"沿站位的半宽，并按该水线最大半宽归一到 B/2
  std::vector<keship::HullStation> buildStationsFromLines(double _draft, double _B) const;

  // === 输入 ===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *draftSpin_ = nullptr;
  QDoubleSpinBox *kgSpin_ = nullptr;
  QDoubleSpinBox *densitySpin_ = nullptr;
  QCheckBox *boxCheck_ = nullptr;

  // === 输出 ===
  LargeAngleChartWidget *chart_ = nullptr;
  QTableWidget *tableWidget_ = nullptr;
  QLabel *conclusionLabel_ = nullptr;
  QLabel *assumptionLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  // === 计算状态 ===
  std::vector<keship::HullStation> stations_;
  keship::GzInput input_;
  keship::hs06::WallSidedModel model_;
  keship::hs06::FeatureAngles feat_;
  keship::hs06::SampleTable table_;
  keship::GzSummary summary_;

  QString errMsg_;
  QJsonObject lastObject_, lastCondition_, lastParams_, lastMetrics_, lastExtra_;
};

#endif  // LARGE_ANGLE_STABILITY_DIALOG_HH_
