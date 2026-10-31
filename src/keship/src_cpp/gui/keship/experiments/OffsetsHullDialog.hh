/*
 * Copyright (C) 2024-2026 KeShip Team
 */
/* SD-10 型值表编程生成可浮船型 —— 实验对话框
 *
 * 链位置（L1 船型设计链，接在 SD-08 之后）：
 *   SD-08 参数化型线设计 → **SD-10 型值表编程生成可浮船型** → HS-05 稳性对比 / HS-09 排水量
 *
 * 与 SD-08 的分工：
 *   SD-08 = 平台按参数算，学生只调指数；
 *   SD-10 = **学生自己写代码算型值点**，平台据此造出真能浮的船。
 *   学生改一行公式，船的形状与吃水必须随之变化 —— 这是本实验的验收判据。
 *
 * 数据契约（学生侧，模板注释里也写一遍）：
 *   每行一个型值点，格式 x,z,halfBreadth，逗号分隔，单位 m
 *   只给右舷（halfBreadth >= 0），左舷由平台镜像；行序不限
 *
 * 交付物 `sd10_offsets_hull.json`（schema keship.exp.v1，expId = "SD-10"）：
 *   metrics：draftM / volumeD / tableVolume / volumeErrPct / GMd / nStations / nWaterlines
 *   extra：stations / waterlines / halfBreadths（型值表本体，供下游复现同一船型）
 */

#ifndef OFFSETS_HULL_DIALOG_HH_
#define OFFSETS_HULL_DIALOG_HH_

#include <memory>
#include <string>

#include <QDialog>

#include "HullOffsets.hh"
#include "ShipHullGenerator.hh"

class QDoubleSpinBox;
class QSpinBox;
class QRadioButton;
class QPlainTextEdit;
class QTableWidget;
class QLabel;
class QPushButton;

class OffsetsHullDialog : public QDialog
{
  Q_OBJECT

public:
  explicit OffsetsHullDialog(QWidget *parent = nullptr);
  ~OffsetsHullDialog() override = default;

  // ═══ 无交互入口（离屏驱动测试用；与界面槽共用同一段逻辑）═══
  /// 直接喂型值表文本：解析 → 六条校验 → 建船。成功返回 true。
  bool applyCsvText(const QString &_csv, std::string *_err);
  /// 导出交付物 sd10_offsets_hull.json
  bool exportTo(const QString &_path, std::string *_err);

  const keship::OffsetsReport &report() const { return report_; }
  const keship::ShipHullSpec &spec() const { return spec_; }
  /// 已建好的船体（未通过校验时为空）
  const keship::ShipHullGenerator *hull() const { return hull_.get(); }

signals:
  /// 用户点「生成可浮船型」时发出，由外部负责 spawn（与 ShipHullDialog 同模式）
  void spawnRequested(keship::ShipHullSpec spec, std::string name);

private slots:
  void onRunCode();
  void onResetTemplate();
  void onLanguageChanged();
  void onSpawn();
  void onExport();

private:
  void buildUi();
  void fillTable();
  void showReport();
  void rebuildSpec();
  QString buildTemplate() const;

  // === 输入 ===
  QDoubleSpinBox *lengthSpin_ = nullptr;
  QDoubleSpinBox *breadthSpin_ = nullptr;
  QDoubleSpinBox *depthSpin_ = nullptr;
  QDoubleSpinBox *densitySpin_ = nullptr;
  QSpinBox *nxSpin_ = nullptr;
  QSpinBox *nzSpin_ = nullptr;
  QRadioButton *pythonRadio_ = nullptr;
  QRadioButton *cRadio_ = nullptr;
  QPlainTextEdit *codeEdit_ = nullptr;
  QPlainTextEdit *logEdit_ = nullptr;

  // === 输出 ===
  QTableWidget *table_ = nullptr;
  QLabel *reportLabel_ = nullptr;
  QLabel *hydroLabel_ = nullptr;
  QPushButton *runBtn_ = nullptr;
  QPushButton *spawnBtn_ = nullptr;
  QPushButton *exportBtn_ = nullptr;

  // === 状态 ===
  keship::LinesData lines_;
  keship::OffsetsReport report_;
  keship::ShipHullSpec spec_;
  std::unique_ptr<keship::ShipHullGenerator> hull_;
};

#endif  // OFFSETS_HULL_DIALOG_HH_
