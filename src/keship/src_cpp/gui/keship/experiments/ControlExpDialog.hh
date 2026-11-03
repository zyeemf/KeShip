/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-01~SC-12 控制设计链（L6）+ 感知→控制链（L7）—— **通用实验对话框**（薄壳）
 *
 * 链位置：
 *   L6：SC-01 开环闭环 → SC-02 PID 参数 → SC-03 扰动 → SC-04 航向 → SC-05 航速 →
 *       SC-06 LOS 循迹* → SC-07 DP → SC-09 自整定 → SC-10 航路点 → SC-11 COLREGs → SC-12 编队
 *   L7：SC-08 位姿估计 → **SC-06**（导入 SC-08 估计 → `useEstimatedPose` 闭环）
 *
 * 为什么是「一个对话框 + expId 开关」而不是 11 个类：
 *   11 个实验的骨架完全相同（参数区 → 定时器驱动 `core->sample()` → 曲线 → 指标 → 交付物），
 *   差异只在「哪些参数可见 / 用哪条控制律 / 分几个阶段跑」。11 份 GUI 必然字段漂移，
 *   故按 expId 配置可见组与阶段（见 `buildPhases()`），逻辑全部落在 `ControlExpCore`。
 *
 * 交付物（`ExpDeliverable`，schema keship.exp.v1，expId = 传入的 _expId）：
 *   metrics = `ControlMetrics` **全部字段** + 评分（SC-10）+ 阶段数
 *   extra.series = 时程（t/yaw/lateral/thrustL/thrustR），上限 2000 点（超了等间隔抽稀）
 *   extra.runs   = 多阶段运行（SC-01 对照 / SC-02 扫描 / SC-09 识别+验证）逐轮指标
 *   extra.sc08   = SC-06 的感知来源与漂移统计（L7 溯源，**不含任何伪造数据**）
 */

#ifndef CONTROL_EXP_DIALOG_HH_
#define CONTROL_EXP_DIALOG_HH_

#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <gz/transport/Node.hh>

#include "../shared/ExpDeliverable.hh"
#include "ControlLaws.hh"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace keship {
class ControlExpCore;
}

/// 时程曲线：航向（含目标线）/ 横向偏差 / 双桨推力 —— QPainter 自绘，无子控件、无信号槽
class ControlChartWidget : public QWidget
{
public:
  explicit ControlChartWidget(QWidget *_parent = nullptr);
  void setTrail(const std::vector<keship::ControlSample> &_pts);
  void clearTrail();
  void setYawTarget(double _deg) { hasTarget_ = true; targetDeg_ = _deg; }
  void clearYawTarget() { hasTarget_ = false; }

protected:
  void paintEvent(QPaintEvent *_e) override;

private:
  std::vector<keship::ControlSample> pts_;
  bool hasTarget_ = false;
  double targetDeg_ = 0.0;
};

class ControlExpDialog : public QDialog
{
  Q_OBJECT

public:
  ControlExpDialog(gz::transport::Node &_node,
                   const std::string &_worldName,
                   const QStringList &_models,
                   const QString &_expId,
                   QWidget *_parent = nullptr);
  ~ControlExpDialog() override;

  /// 导出当前结果到指定路径（无头测试/脚本入口，与「导出交付物」按钮同一份实现）
  /// ★ 2026-11-03：新增 _err 出参 —— 此前失败原因只打到 stdout，驱动拿不到，界面/脚本只能看到 FAIL
  bool exportTo(const QString &_path, std::string *_err = nullptr);

  /// SC-06：导入 SC-08 的航向估计（既支持 SC-08 现有 CSV，也支持 keship.exp.v1 JSON）
  bool importSc08(const QString &_path, QString *_err = nullptr);

  QString expId() const { return expId_; }
  /// 交付物默认文件名（与报告 §3 的表一致）
  static QString defaultFileName(const QString &_expId);

private slots:
  void onStart();
  void onStop();
  void onExport();
  void onTick();
  void onAddWaypoint();
  void onRemoveWaypoint();
  void onPickSc08();
  void onAutoTune();

private:
  /// 一个运行阶段（SC-01 两阶段对照 / SC-02 参数扫描 / SC-09 识别与验证 / 其余单阶段）
  struct Phase {
    QString label;
    keship::ControlParams params;
    std::vector<std::pair<double, double>> wps;
    double seconds = 60.0;
  };
  /// 一轮运行的结果（表格 + 交付物 extra.runs）
  struct RunRow {
    QString label;
    double sweptValue = 0.0;          ///< 扫描参数量值（非扫描轮为 NaN）
    keship::ControlParams params;
    keship::ControlMetrics m;
    double elapsedS = 0.0;
  };

  void buildUi();
  void applyExpDefaults();
  void updateVisibility();
  void syncParamsToCore();
  keship::ControlParams paramsFromUi() const;
  std::vector<std::pair<double, double>> waypointsFromTable() const;
  void fillWaypointTable(const std::vector<std::pair<double, double>> &_wps);

  std::vector<Phase> buildPhases() const;
  void beginPhase(const Phase &_ph);
  void finishPhase();
  void settleAll();
  void updateReadout();
  void refreshMetrics();
  void refreshRunTable();
  void setRunningUi(bool _running);

  bool buildDeliverable(keship::ExpDeliverable &_d, std::string *_err) const;
  void applySc08DriftToCore();

  gz::transport::Node &node_;
  std::string worldName_;
  QString expId_;
  QStringList models_;

  std::unique_ptr<keship::ControlExpCore> core_;

  QComboBox *modelCombo_ = nullptr;
  QLineEdit *thrLEdit_ = nullptr;
  QLineEdit *thrREdit_ = nullptr;
  QDoubleSpinBox *periodSpin_ = nullptr;
  QDoubleSpinBox *simSecSpin_ = nullptr;

  QGroupBox *pidGroup_ = nullptr;
  QDoubleSpinBox *kpSpin_ = nullptr;
  QDoubleSpinBox *kiSpin_ = nullptr;
  QDoubleSpinBox *kdSpin_ = nullptr;
  QDoubleSpinBox *baseSpin_ = nullptr;
  QDoubleSpinBox *limitSpin_ = nullptr;

  QGroupBox *headingGroup_ = nullptr;
  QDoubleSpinBox *headingSpin_ = nullptr;

  QGroupBox *speedGroup_ = nullptr;
  QDoubleSpinBox *speedSpin_ = nullptr;

  QGroupBox *pointGroup_ = nullptr;
  QDoubleSpinBox *targetXSpin_ = nullptr;
  QDoubleSpinBox *targetYSpin_ = nullptr;

  QGroupBox *losGroup_ = nullptr;
  QDoubleSpinBox *lookaheadSpin_ = nullptr;
  QTableWidget *wpTable_ = nullptr;
  QPushButton *addWpBtn_ = nullptr;
  QPushButton *delWpBtn_ = nullptr;

  QGroupBox *distGroup_ = nullptr;
  QCheckBox *disturbCheck_ = nullptr;
  QDoubleSpinBox *windSpin_ = nullptr;
  QDoubleSpinBox *currentSpin_ = nullptr;
  QDoubleSpinBox *currentDirSpin_ = nullptr;

  QGroupBox *obsGroup_ = nullptr;
  QDoubleSpinBox *obsXSpin_ = nullptr;
  QDoubleSpinBox *obsYSpin_ = nullptr;
  QDoubleSpinBox *obsRSpin_ = nullptr;

  QGroupBox *formGroup_ = nullptr;
  QDoubleSpinBox *formXSpin_ = nullptr;
  QDoubleSpinBox *formYSpin_ = nullptr;

  QGroupBox *sweepGroup_ = nullptr;
  QComboBox *sweepCombo_ = nullptr;
  QDoubleSpinBox *sweepFromSpin_ = nullptr;
  QDoubleSpinBox *sweepToSpin_ = nullptr;
  QDoubleSpinBox *sweepStepSpin_ = nullptr;
  QCheckBox *sweepCheck_ = nullptr;

  QGroupBox *sc08Group_ = nullptr;
  QLineEdit *sc08PathEdit_ = nullptr;
  QPushButton *sc08BrowseBtn_ = nullptr;
  QLabel *sc08InfoLabel_ = nullptr;

  QGroupBox *tuneGroup_ = nullptr;
  QDoubleSpinBox *probeBaseSpin_ = nullptr;
  QDoubleSpinBox *probeDiffSpin_ = nullptr;
  QPushButton *tuneBtn_ = nullptr;
  QLabel *tuneLabel_ = nullptr;

  QPushButton *runBtn_ = nullptr;
  QPushButton *stopBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QLabel *metricsLabel_ = nullptr;
  QLabel *scoreLabel_ = nullptr;
  ControlChartWidget *chart_ = nullptr;
  QTableWidget *runTable_ = nullptr;
  QTimer *timer_ = nullptr;

  std::vector<Phase> queue_;
  int phaseIndex_ = -1;
  Phase current_;
  std::vector<RunRow> runs_;
  std::vector<keship::ControlSample> phaseTrail_;
  std::vector<std::pair<double, double>> sc08Drift_;
  QString sc08File_;
  bool useSc08_ = false;
  bool running_ = false;
  bool tuneOnly_ = false;   ///< SC-09「只做识别」：跑完识别阶段即停，不追加验证
  double sweptValue_ = std::nan("");
};

#endif  // CONTROL_EXP_DIALOG_HH_
