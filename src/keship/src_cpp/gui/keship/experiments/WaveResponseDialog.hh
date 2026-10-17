/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-02 升沉与纵摇响应 / MS-03 横摇 RAO 测量 — 对话框（**薄壳**）
 *
 * 同一对话框两种模式（由构造参数 _mode 决定标题与结果栏）：
 *   · "ms02"：观察 heave / pitch / roll 时程与响应幅值
 *   · "ms03"：以 RAO 为主（改变波浪周期逐点测量 ⇒ 找共振峰）
 * ⚠ 逻辑全部委托 `keship::WaveResponseCore`（与 `keship_ctl exp ms02-run|ms03-run` 共用）。
 */

#ifndef WAVE_RESPONSE_DIALOG_HH_
#define WAVE_RESPONSE_DIALOG_HH_

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QWidget>
#include <QPaintEvent>

#include <memory>
#include <string>
#include <vector>

#include <gz/transport/Node.hh>

#include "WaveResponseCore.hh"
#include "../shared/WaveInertiaIO.hh"

/// 响应时程图（自绘：三条曲线 roll / pitch / heave）
class WaveChartWidget : public QWidget {
  Q_OBJECT
public:
  explicit WaveChartWidget(QWidget *_parent = nullptr);
  void setTrail(const std::vector<keship::WaveSample> &_pts);
  void clearTrail();
protected:
  void paintEvent(QPaintEvent *_event) override;
private:
  std::vector<keship::WaveSample> pts_;
};

class WaveResponseDialog : public QDialog {
  Q_OBJECT
public:
  /// @param _mode "ms02" 或 "ms03"
  WaveResponseDialog(gz::transport::Node &_transportNode,
                     const std::string &_worldName,
                     const QStringList &_models,
                     const std::string &_mode,
                     QWidget *_parent = nullptr);
  ~WaveResponseDialog() override;

  /// 把 RAO 表内容写入 CSV（纯函数、无 GUI 交互；onExportRao 与回归测试共用）
  static bool WriteRaoTableCsv(const QTableWidget *_table,
                               const std::string &_path);

  /// MS-06 学生提交的一行比对结果
  struct StudentCompareRow {
    double period = 0.0;   ///< 波浪周期 (s)
    double stuRao = -1.0;  ///< 学生 RAO (°/m；缺测为 -1)
    double refRao = -1.0;  ///< 平台参考 RAO (°/m)
    double errPct = -1.0;  ///< 误差 %（缺测为 -1）
  };

  /// MS-06：写练习数据包（poses CSV × N + wave_params.json + task.md）
  /// 纯函数、无 GUI 交互；onExportPacket 与回归测试共用
  static bool WritePracticePacket(
      const std::string &_dir,
      const std::vector<std::vector<keship::WaveSample>> &_rounds,
      const std::vector<double> &_periods, const std::vector<double> &_amps,
      const std::vector<bool> &_mooring = {});

  /// MS-06：解析并比对 student_rao.csv（周期匹配容差 0.5 s）
  /// 纯函数、无 GUI 交互；onSubmitStudent 与回归测试共用
  static std::vector<StudentCompareRow> CompareStudentCsv(
      const std::string &_path, const std::vector<double> &_periods,
      const std::vector<double> &_refRao, std::string *_err);

private slots:
  void onStart();
  void onStop();
  void onClear();
  void onTick();
  void onExport();
  void onApplyWave();
  void onRecordResult();   ///< MS-03：把本次结果追加到 RAO 记录表
  void onDeleteRow();      ///< MS-03：删除 RAO 表选中行
  void onExportRao();      ///< MS-03：导出 RAO–周期汇总表 CSV
  void onExportPacket();   ///< MS-06：导出练习数据包（位姿 CSV×N + 参数 + 任务书）
  void onSubmitStudent();  ///< MS-06：提交学生结果 CSV 并与参考值比对
  void onImportGm();       ///< MS-03：导入 HS-04 GM 交付物
  void onMooringToggled(bool _on);  ///< 软系泊开关（默认不勾选）
  void onShowRaoHelp();             ///< RAO 教学+技术说明
  void onMooringHeartbeat();        ///< 系泊心跳与状态刷新
  void updateGmCompare();  ///< MS-03：刷新理论 T₀ vs 实测峰值周期对照行
  /// 按交付物的「船模信息」重建同一对象并设为实验对象（张老师方案）
  bool ensureObjectFromDeliverable(const keship::GmDeliverable &_d);

private:
  void syncParamsToCore();
  void updateMooringStatusLabel();
  void updateReadout();
  void showResults();

  std::string mode_;

  QComboBox      *modelCombo_ = nullptr;
  QDoubleSpinBox *waveAmpSpin_ = nullptr;     ///< 波幅 (m)
  QDoubleSpinBox *wavePeriodSpin_ = nullptr;  ///< 波浪周期 (s)
  QDoubleSpinBox *gainSpin_ = nullptr;        ///< 增益
  QDoubleSpinBox *periodSpin_ = nullptr;      ///< 采样周期 (s)
  QDoubleSpinBox *directionSpin_ = nullptr;   ///< 波浪方向 (°)：ms02 顺浪 0，ms03/06 横浪 90
  QCheckBox   *mooringCheck_ = nullptr;       ///< 水平软系泊（默认不勾选）
  QLabel      *mooringStatusLabel_ = nullptr; ///< 系泊物理端确认状态
  QPushButton *helpBtn_ = nullptr;            ///< RAO 说明
  QTimer      *mooringTimer_ = nullptr;       ///< 系泊心跳（200 ms）

  QPushButton *applyWaveBtn_ = nullptr;
  QPushButton *startBtn_ = nullptr;
  QPushButton *stopBtn_  = nullptr;
  QPushButton *clearBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  WaveChartWidget *chart_ = nullptr;
  QLabel *readoutLabel_ = nullptr;
  QLabel *resultLabel_  = nullptr;

  // ── MS-03 专用：RAO 累积记录表（操作类实验的数据汇总）──
  QLabel       *guideLabel_   = nullptr;  ///< 操作步骤说明条
  QTableWidget *raoTable_     = nullptr;  ///< (周期, 幅值, RAO, 受迫周期) 累积表
  QPushButton  *recordBtn_    = nullptr;  ///< 记录本次结果（停止后可点）
  QPushButton  *deleteRowBtn_ = nullptr;  ///< 删除选中行
  QPushButton  *exportRaoBtn_ = nullptr;  ///< 导出 RAO 汇总表
  QPushButton  *importGmBtn_ = nullptr;   ///< 导入 HS-04 GM 交付物
  QLabel       *gmCompareLabel_ = nullptr;///< 理论 T₀ vs 实测峰值周期对照行
  gz::transport::Node &node_;             ///< 传输节点（生成对象用）
  std::string worldName_;                 ///< 世界名（生成对象用）
  std::string objectName_;                ///< 当前实验对象名（交付物导入后）
  keship::GmDeliverable gmDeliverable_;   ///< 已导入的交付物
  bool hasGm_ = false;                    ///< 是否已导入交付物

  // ── MS-06 专用：编程练习数据收集与学生比对 ──
  QPushButton *packetBtn_ = nullptr;      ///< 导出练习数据包
  QPushButton *submitBtn_ = nullptr;      ///< 提交学生结果
  QTableWidget *studentTable_ = nullptr;  ///< 学生结果比对表
  QLabel *studentResultLabel_ = nullptr;  ///< 比对总判定
  std::vector<std::vector<keship::WaveSample>> rounds_;  ///< 各轮位姿时程
  std::vector<double> roundPeriods_;      ///< 各轮波浪周期 (s)
  std::vector<double> roundAmps_;         ///< 各轮有效波幅 (m)
  std::vector<bool> roundMooring_;        ///< 各轮开始时系泊是否已生效
  std::vector<double> refRao_;            ///< 各轮参考 RAO (°/m，平台计算)

  bool freeRunWarned_ = false;  ///< 自由响应提示每对话框只弹一次
  QTimer *timer_ = nullptr;
  std::unique_ptr<keship::WaveResponseCore> core_;
};

#endif  // WAVE_RESPONSE_DIALOG_HH_
