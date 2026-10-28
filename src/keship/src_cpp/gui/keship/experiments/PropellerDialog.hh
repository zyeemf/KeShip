/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— RP-03 螺旋桨敞水特性 / RP-04 推进性能匹配 对话框（**薄壳**）
 *
 * 链位置（L8 阻力→推进链）：
 *   RP-02 阻力曲线 → RP-04 推进性能匹配（船-桨平衡）
 *   RP-03 螺旋桨敞水特性（独立支线，只依赖桨几何）
 *
 * 为什么 RP-03 与 RP-04 合成一个对话框：
 *   匹配（RP-04）必须知道桨的 KT(J)/KQ(J)，也就是 RP-03 的那两张图；
 *   拆成两个窗口，学生会以为「敞水」和「匹配」是两门互不相干的知识点。
 *   同一界面里先看敞水曲线、再看工作点落在曲线的哪个 J 上，
 *   「n 由平衡式定、J 由 n 与 Va 定」这层因果才看得见。
 *
 * 交付物：
 *   RP-03 → rp03_openwater.json      （KT/KQ/η0 曲线数组 + 峰值点）
 *   RP-04 → rp04_matching.json       （工作点 n/T/Q/P_D/QPC + 残差）
 *   schema 统一 keship.exp.v1。
 */

#ifndef PROPELLER_DIALOG_HH_
#define PROPELLER_DIALOG_HH_

#include <vector>

#include <QColor>
#include <QDialog>
#include <QJsonObject>
#include <QPaintEvent>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "PropellerCore.hh"

class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPainter;
class QPushButton;
class QSpinBox;
class QTableWidget;

/* 敞水特性图（自绘：KT、KQ、η0 三条曲线 + 峰值/工作点标记）
 *
 * 三条曲线共用一根纵轴（都是无量纲系数/效率），横轴为进速系数 J。
 * 注意 KQ 只有 KT 的 1/10 量级，会贴着横轴走 —— 这是 B 系列敞水图的常态，
 * 不影响形状判读；要读精确值请看下方表格（KQ 给到 5 位小数）。
 */
class PropellerChartWidget : public QWidget
{
  Q_OBJECT

public:
  explicit PropellerChartWidget(QWidget *_parent = nullptr);

  /// 用一组敞水点重建三条曲线（J 必须已按升序排列）
  void SetData(const std::vector<keship::OpenWaterPoint> &_pts);
  /// 标记一个点（η0 峰值、或 RP-04 的工作点）
  void AddMarker(double _x, double _y, const QString &_label, const QColor &_color);
  void Clear();

protected:
  void paintEvent(QPaintEvent *_event) override;

private:
  struct Marker {
    double x = 0.0;
    double y = 0.0;
    QString label;
    QColor color;
  };

  std::vector<keship::OpenWaterPoint> pts_;
  std::vector<Marker> markers_;
};

/* 推进实验对话框：顶部下拉切换 RP-03 / RP-04 */
class PropellerDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _expId 初始模式（"RP-03" 或 "RP-04"），其它值退回 RP-03
  explicit PropellerDialog(QWidget *_parent = nullptr,
                           const QString &_expId = "RP-03");

  QString currentExpId() const { return expId_; }
  void setMode(const QString &_expId);
  void computeCurrent();
  /// 无交互导出交付物（离屏测试用；成功返回 true）
  bool exportTo(const QString &_path);
  /// 最近一次匹配是否收敛（未算或未收敛时 false）
  bool lastConverged() const { return lastConverged_; }

private slots:
  void onModeChanged(int _index);
  void onCompute();
  void onExport();

private:
  void buildUi();
  void applyMode();

  keship::PropellerInput buildPropeller() const;
  keship::ResistanceInput buildHull() const;
  void storeCommon();

  void computeRp03();
  void computeRp04();

  void setTableShape(int _rows, int _cols, const QStringList &_headers);
  void setStatus(const QString &_text);
  void showSummary(const QString &_richText);

  // === 输入：桨 ===
  QComboBox *modeCombo_ = nullptr;
  QDoubleSpinBox *diameterSpin_ = nullptr;
  QDoubleSpinBox *pitchSpin_ = nullptr;
  QDoubleSpinBox *areaSpin_ = nullptr;
  QDoubleSpinBox *bladeSpin_ = nullptr;
  QDoubleSpinBox *jMaxSpin_ = nullptr;
  QSpinBox *pointsSpin_ = nullptr;

  // === 输入：船体（仅 RP-04）===
  QGroupBox *hullBox_ = nullptr;
  QFormLayout *hullForm_ = nullptr;
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *draftSpin_ = nullptr;
  QDoubleSpinBox *cbSpin_ = nullptr;
  QDoubleSpinBox *cmSpin_ = nullptr;
  QDoubleSpinBox *cwpSpin_ = nullptr;

  // === 输入：匹配（仅 RP-04）===
  QGroupBox *matchBox_ = nullptr;
  QDoubleSpinBox *speedSpin_ = nullptr;
  QDoubleSpinBox *wakeSpin_ = nullptr;
  QDoubleSpinBox *deductionSpin_ = nullptr;
  QDoubleSpinBox *windSpin_ = nullptr;
  QDoubleSpinBox *frontalSpin_ = nullptr;

  // === 输出 ===
  QLabel *summaryLabel_ = nullptr;
  PropellerChartWidget *chart_ = nullptr;
  QTableWidget *table_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QPushButton *computeBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  // === 计算状态 ===
  QString expId_ = "RP-03";
  QJsonObject lastObject_;
  QJsonObject lastParams_;
  QJsonObject lastCondition_;
  QJsonObject lastMetrics_;
  QJsonObject lastExtra_;
  bool lastConverged_ = false;
};

#endif  // PROPELLER_DIALOG_HH_
