/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-05 海况等级下的操限评估 —— 实验对话框
 *
 * 链位置（L5 操纵性链）：
 *   MS-01 回转性观测 ✅ → MS-04 方向稳定性与 Z 形试验 ✅ → **MS-05 海况等级下的操限评估**
 *
 * 做法（为什么这样设计）：
 *   操纵性实验的"操限"本质上要看**同一操纵动作在不同海况下的退化**，
 *   所以本实验不是一个瞬时量测，而是"选一个海况 → 跑一遍标准操纵计划 → 记一行结果"，
 *   多次运行累积成表。三件事都能被机器校验：
 *     ① 失速比（海况中的平均航速 / 静水基准）
 *     ② 航向保持偏差（直航段航向标准差）
 *     ③ 回转指标（战术直径/进距）随海况的变化
 *
 * 复用既有核心层（不重写物理）：
 *   `TurningExperimentCore` 负责发推力、采样、算回转指标 —— 与 MS-01 是**同一份实现**，
 *   故无头台架 `keship_ctl exp ms01-run` 的结论对本实验同样成立。
 *   `WaveResponseCore::sendWave` 负责施加规则波 —— 与 MS-02/MS-03 同一份实现。
 *
 * 已知取舍：本版不含 Z 形段（Z 形需相位状态机，且 MS-04 已在静水下覆盖）；
 *   若后续要把 Z 形纳入海况评估，在 stages 里追加一段即可，指标读 `ZigzagExperimentCore`。
 */

#ifndef OPERABILITY_DIALOG_HH_
#define OPERABILITY_DIALOG_HH_

#include <memory>
#include <string>
#include <vector>

#include <QDialog>

#include <gz/transport/Node.hh>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTimer;

namespace keship {
class TurningExperimentCore;
class WaveResponseCore;
}  // namespace keship

class OperabilityDialog : public QDialog
{
  Q_OBJECT

public:
  OperabilityDialog(gz::transport::Node &_transportNode,
                    const std::string &_worldName,
                    const QStringList &_modelNames,
                    QWidget *_parent = nullptr);
  ~OperabilityDialog() override;

  /// 一行结果（一个海况的一次运行）
  struct Row {
    int level = 0;
    double hsM = 0.0, tpS = 0.0, windMs = 0.0;
    double avgSpeedMs = -1.0;     ///< 直航段平均航速
    double headingStdDeg = -1.0;  ///< 直航段航向标准差
    double tacticalM = -1.0;      ///< 战术直径
    double advanceM = -1.0;       ///< 进距
    double speedLossRatio = -1.0; ///< 相对静水基准（需先跑 0 级）
  };

private slots:
  void onApplySeaState();
  void onLevelChanged();
  void onRun();
  void onExport();
  void onTick();

private:
  void buildUi();
  void appendRow(const Row &_r);
  void refreshTable();
  const Row *calmBaseline() const;   ///< 找 level==0 的最近一行
  static void seaStateOf(int _level, double &_hs, double &_tp, double &_wind);

  gz::transport::Node &transportNode_;
  std::string worldName_;

  std::unique_ptr<keship::TurningExperimentCore> turn_;
  std::unique_ptr<keship::WaveResponseCore> wave_;

  QComboBox *modelCombo_ = nullptr;
  QComboBox *levelCombo_ = nullptr;
  QLineEdit *leftNameEdit_ = nullptr;
  QLineEdit *rightNameEdit_ = nullptr;
  QDoubleSpinBox *periodSpin_ = nullptr;
  QPushButton *applyWaveBtn_ = nullptr;
  QPushButton *runBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;
  QLabel *seaStateLabel_ = nullptr;
  QLabel *statusLabel_ = nullptr;
  QTableWidget *table_ = nullptr;
  QTimer *timer_ = nullptr;

  std::vector<Row> rows_;
  bool running_ = false;
};

#endif  // OPERABILITY_DIALOG_HH_
