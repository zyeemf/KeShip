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

private:
  void syncParamsToCore();
  void updateReadout();
  void showResults();

  std::string mode_;

  QComboBox      *modelCombo_ = nullptr;
  QDoubleSpinBox *waveAmpSpin_ = nullptr;     ///< 波幅 (m)
  QDoubleSpinBox *wavePeriodSpin_ = nullptr;  ///< 波浪周期 (s)
  QDoubleSpinBox *gainSpin_ = nullptr;        ///< 增益
  QDoubleSpinBox *periodSpin_ = nullptr;      ///< 采样周期 (s)

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

  QTimer *timer_ = nullptr;
  std::unique_ptr<keship::WaveResponseCore> core_;
};

#endif  // WAVE_RESPONSE_DIALOG_HH_
