/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* L8 阻力→推进链 —— RP-01 / RP-02 / RP-05 / RP-06 阻力实验对话框（**薄壳**）
 *
 * 链位置（L8 阻力→推进链）：
 *   RP-01 阻力成分分解 → RP-02 阻力曲线测定 → RP-05 船型阻力优化 → RP-06 尺度换算与实船外推
 *                                                              ↓
 *                                                    RP-03 敞水 → RP-04 推进匹配
 *
 * 为什么四个实验挤进一个对话框（顶部下拉切换）：
 *   四项用的是**同一套船体输入**与**同一个内核**（keship::ResistanceCore），
 *   差别只在于「扫什么」：RP-01 扫 0 个量（单点分解），RP-02 扫航速，RP-05 扫 Cb，
 *   RP-06 扫尺度。分成四个窗口会让教师重复输入同一船型四次，且学生看不出
 *   「曲线是单点的连续化」这层关系。故做成一个对话框 + 模式切换，
 *   输入区共享、结果区按模式重建、交付物按模式换 expId 与文件名。
 *
 * 交付物（判据 B：后一节点的输入必须是前一节点产出的可机器校验文件）：
 *   RP-01 → rp01_resistance.json      （成分占比，下游做量级校核）
 *   RP-02 → rp02_curve.json           （R–V 曲线数组，下游可拟合幂律）
 *   RP-05 → rp05_optimization.json    （最优 Cb，SD-08 / HS-05 可直接拿去建模）
 *   RP-06 → rp06_extrapolation.json   （模型→实船换算系数与实船阻力）
 *   schema 统一 keship.exp.v1，写法与 SD-08 / MS-05 一致。
 */

#ifndef RESISTANCE_DIALOG_HH_
#define RESISTANCE_DIALOG_HH_

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

#include "ResistanceCore.hh"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QGroupBox;
class QLabel;
class QPainter;
class QPushButton;
class QSpinBox;
class QTableWidget;

/* 阻力链通用 XY 曲线控件（R–V、R–Cb、模型/实船对照三处共用）
 *
 * 为什么自绘而不引 QtCharts：平台其余图表（MS-02 时程图、MS-06 RAO 图）都是
 * QPainter 自绘，多引一个模块就多一条运行时依赖；教学图只需要折线 + 图例 +
 * 一个「峰值/最优点」标记，自绘 120 行足够且与既有风格统一。
 */
class ResistanceChartWidget : public QWidget
{
  Q_OBJECT

public:
  /// 一条曲线：折线点 + 图例名 + 颜色（虚线用于「参考/对比」曲线）
  struct Series {
    QString name;
    QColor color;
    std::vector<QPointF> pts;
    bool dashed = false;
  };

  explicit ResistanceChartWidget(QWidget *_parent = nullptr);

  void SetAxes(const QString &_title, const QString &_xLabel, const QString &_yLabel);
  /// 设置曲线族（会清掉占比条）
  void SetSeries(const std::vector<Series> &_series);
  /// 叠加一个标记点（η0 峰值 / 最优 Cb / 工作点），可多次调用
  void AddMarker(double _x, double _y, const QString &_label, const QColor &_color);
  /// 改画「占比条」（RP-01 的成分占比）；会清掉曲线与标记
  void SetBars(const QStringList &_names, const std::vector<double> &_values,
               const QString &_unit);
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

  void paintPlot(QPainter &_p, const QRectF &_plot);
  void paintBars(QPainter &_p, const QRectF &_plot);

  QString title_;
  QString xLabel_;
  QString yLabel_;
  std::vector<Series> series_;
  std::vector<Marker> markers_;
  QStringList barNames_;
  std::vector<double> barValues_;
  QString barUnit_;
};

/* 阻力实验对话框：顶部下拉切换 RP-01 / RP-02 / RP-05 / RP-06 */
class ResistanceDialog : public QDialog
{
  Q_OBJECT

public:
  /// @param _expId 初始模式（"RP-01"/"RP-02"/"RP-05"/"RP-06"），非法值退回 RP-01
  explicit ResistanceDialog(QWidget *_parent = nullptr,
                            const QString &_expId = "RP-01");

  /// 当前模式（= 交付物 expId）
  QString currentExpId() const { return expId_; }
  /// 供接线/离屏测试切换模式（不弹窗、不写文件）
  void setMode(const QString &_expId);
  /// 按当前输入与模式算一遍（构造末尾与「计算」按钮都会调它）
  void computeCurrent();
  /// 无交互导出交付物（离屏测试用；成功返回 true）。写的是**最近一次计算**的参数与结果。
  bool exportTo(const QString &_path);
  /// 最近一次 RP-05 扫描出的最优方形系数（未扫过时为 0）
  double lastBestCb() const { return bestCb_; }

private slots:
  void onModeChanged(int _index);
  void onCompute();
  void onExport();

private:
  void buildUi();
  void applyMode();
  void syncVolume();

  /// 由界面控件装配内核输入（∇ 是否随 Cb·L·B·T 自洽由勾选框决定）
  keship::ResistanceInput buildInput() const;
  /// 四个模式都要写的 object/params/condition 公共段
  void storeCommon(const keship::ResistanceInput &_in, const QString &_note);

  void computeRp01();
  void computeRp02();
  void computeRp05();
  void computeRp06();

  void setTableShape(int _rows, int _cols, const QStringList &_headers);
  void setStatus(const QString &_text);
  void showSummary(const QString &_richText);

  // === 输入：船体主尺度 ===
  QComboBox *modeCombo_ = nullptr;
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *draftSpin_ = nullptr;
  QDoubleSpinBox *cbSpin_ = nullptr;
  QDoubleSpinBox *cmSpin_ = nullptr;
  QDoubleSpinBox *cwpSpin_ = nullptr;
  QDoubleSpinBox *volumeSpin_ = nullptr;
  QCheckBox *selfCheck_ = nullptr;

  // === 输入：工况与环境 ===
  QFormLayout *condForm_ = nullptr;        ///< 用于随模式隐藏「航速/风」三类行
  QDoubleSpinBox *speedSpin_ = nullptr;    ///< 设计航速（RP-01/05 的单点评定速度）
  QDoubleSpinBox *windSpin_ = nullptr;
  QDoubleSpinBox *frontalSpin_ = nullptr;

  // === 输入：按模式显示 ===
  QGroupBox *rangeBox_ = nullptr;          ///< RP-02 / RP-05：扫速区间与点数
  QDoubleSpinBox *vMinSpin_ = nullptr;
  QDoubleSpinBox *vMaxSpin_ = nullptr;
  QSpinBox *pointsSpin_ = nullptr;
  QSpinBox *cbSamplesSpin_ = nullptr;
  QGroupBox *scaleBox_ = nullptr;          ///< RP-06：缩尺比与实船航速
  QDoubleSpinBox *lambdaSpin_ = nullptr;
  QDoubleSpinBox *shipSpeedSpin_ = nullptr;
  QLabel *scaleNote_ = nullptr;

  // === 输出 ===
  QLabel *summaryLabel_ = nullptr;
  ResistanceChartWidget *chart_ = nullptr;
  QTableWidget *table_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QPushButton *computeBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  // === 计算状态（导出时原样落盘，避免「导出值与界面值不同源」）===
  QString expId_ = "RP-01";
  QJsonObject lastObject_;
  QJsonObject lastParams_;
  QJsonObject lastCondition_;
  QJsonObject lastMetrics_;
  QJsonObject lastExtra_;
  double bestCb_ = 0.0;
};

#endif  // RESISTANCE_DIALOG_HH_
