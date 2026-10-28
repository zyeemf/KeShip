/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-07 浮体布局设计 —— 实验对话框
 *
 * 链位置（L2 结构建模→稳性链）：
 *   SD-04 舱室划分 → **HS-07 浮体布局设计** → HS-08 完整稳性综合校核
 *
 * 本实验负责的**交付物**：
 *   导出 `hs07_buoyancy.json`，`extra.buoys` 逐舱给出浮力体的位置/尺寸/浸水体积/浮力，
 *   `metrics.kgM` 给出本布局下的**重心高估算值** —— HS-08 直接读它当 KG，
 *   于是"舱室怎么分 → 重量怎么分布 → 初稳性高多少"这条因果链在数据上是通的。
 *
 * 为什么浮力体 = 舱室在水线以下的几何体积（不乘渗透率）：
 *   浮力体是**几何排水体积**，用来支撑船重；渗透率描述的是"破舱后这个舱能进多少水"，
 *   是破舱工况的属性（SD-07 用），不是完整船的浮力属性。
 *   若这里乘 μ，完整船浮力会凭空少 5%，学生算出的 KG/GM 全部偏，
 *   而且会与 HS-08 的 ∇=L·B·T 对不上账。
 *
 * 为什么 KG 用"舱室体积加权垂心"而不是浮心：
 *   KG 是**重心**，取决于重量分布；浮心 KB 才是浮力几何中心。
 *   本平台没有逐件重量清单，故采用"重量按舱容均匀分布"这一明确假设：
 *     KG ≈ Σ(V_i · z_ci) / Σ V_i
 *   对整层矩形舱（z1=0、z2=D）即 KG = D/2 —— 正好是 HS-08 的基准工况。
 *   界面同时显示 KB 供对照：KG 与 KB 谁高，直接决定 GM 的正负。
 */

#ifndef BUOYANCY_LAYOUT_DIALOG_HH_
#define BUOYANCY_LAYOUT_DIALOG_HH_

#include <vector>

#include <QDialog>
#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "DamageStabilityCore.hh"   // keship::Compartment（与 SD-04 共用同一类型）

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// 侧视示意图（QPainter 自绘：船体轮廓 + 水线 + 各浮力体）
class BuoySideViewWidget : public QWidget
{
  Q_OBJECT

public:
  struct Box {
    double x1 = 0, x2 = 0, z1 = 0, z2 = 0;
    double immersedTop = 0;   ///< 浸水面上限（= min(z2, T)）
  };

  explicit BuoySideViewWidget(QWidget *_parent = nullptr);

  void setLayoutData(double _L, double _D, double _draft,
                     const std::vector<Box> &_boxes);

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  double L_ = 6.0, D_ = 1.0, draft_ = 0.3;
  std::vector<Box> boxes_;
};

class BuoyancyLayoutDialog : public QDialog
{
  Q_OBJECT

public:
  explicit BuoyancyLayoutDialog(QWidget *_parent = nullptr);

  /// 无交互导出（离屏测试用）。成功返回 true。
  bool exportTo(const QString &_path);

  /// 无交互导入 SD-04 舱室表（离屏测试用；失败时 errMsg() 给原因，不弹窗）。
  /// 界面的「导入 SD-04」按钮只是"选路径 + 调本函数 + 弹提示"这一层薄壳。
  bool importSd04(const QString &_path);
  QString errMsg() const { return errMsg_; }

  /// 当前浮力体数量 / 总浮力 N（供接线与离屏测试核对）
  int buoyCount() const { return static_cast<int>(buoys_.size()); }
  double totalBuoyancyN() const { return totalBuoyancyN_; }
  double kgM() const { return kgM_; }

private slots:
  void onImportSd04();
  void onRecompute();
  void onExport();

private:
  void buildUi();
  /// 读界面 + 已有舱室 → 浮力体 → 表/图/标签/缓存。false = 输入非法。
  bool refresh();
  void fillTable();
  void updateView();
  void updateCache();

  // === 输入 ===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *draftSpin_ = nullptr;
  QDoubleSpinBox *densitySpin_ = nullptr;
  QDoubleSpinBox *kgSpin_ = nullptr;
  QCheckBox *kgAutoCheck_ = nullptr;

  // === 输出 ===
  QTableWidget *buoyTable_ = nullptr;
  BuoySideViewWidget *sideView_ = nullptr;
  QLabel *summaryLabel_ = nullptr;
  QLabel *kgLabel_ = nullptr;
  QLabel *upstreamLabel_ = nullptr;

  // === 上游数据（真消费） ===
  bool sd04Imported_ = false;
  std::vector<keship::Compartment> comps_;
  double upstreamTotalVolume_ = 0.0;   ///< SD-04 的 metrics.totalVolumeM3
  int upstreamNCompartments_ = 0;      ///< SD-04 的 metrics.nCompartments
  QString upstreamSource_;

  // === 计算结果 ===
  struct Buoy {
    QString name;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0;
    double immersedTop = 0, immersedH = 0, volume = 0;
    double cx = 0, cy = 0, cz = 0;
    double forceN = 0;
    double fullVolume = 0, fullCz = 0;
  };
  std::vector<Buoy> buoys_;
  double totalBuoyancyN_ = 0.0;
  double kgM_ = 0.0;
  double kgEstimatedM_ = 0.0;
  double kbM_ = 0.0;
  double totalVolume_ = 0.0;
  QString errMsg_;

  QJsonObject lastObject_, lastCondition_, lastParams_, lastMetrics_, lastExtra_;
};

#endif  // BUOYANCY_LAYOUT_DIALOG_HH_
