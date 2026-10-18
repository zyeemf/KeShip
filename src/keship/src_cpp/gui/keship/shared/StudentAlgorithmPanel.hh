/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-06 学生算法面板：吃一轮记录的时程，学生写代码算 RAO（**不显示内置值**） */

#ifndef STUDENT_ALGORITHM_PANEL_HH_
#define STUDENT_ALGORITHM_PANEL_HH_

#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QWidget>

#include <vector>

#include "WaveResponseCore.hh"

class StudentAlgorithmPanel : public QWidget {
  Q_OBJECT

 public:
  explicit StudentAlgorithmPanel(QWidget *_parent = nullptr);

  /// 设置本轮数据（每记录一轮调用一次；同时生成数据内嵌模板）
  void SetRound(const std::vector<keship::WaveSample> &_trail, double _waveAmp,
                double _period);
  /// 学生当前代码与语言（提交验收时复用）
  QString CurrentCode() const;
  bool IsPython() const;
  bool HasRound() const { return hasRound_; }

  void ResetTemplate();

 signals:
  void studentRaoReady(double _rao);
  void runFailed(const QString &_msg);

 private slots:
  void onRun();
  void onLanguageChanged();
  void onShowHint();

 private:
  QString buildTemplate() const;

  QRadioButton *pythonRadio_ = nullptr;
  QRadioButton *cRadio_ = nullptr;
  QPlainTextEdit *codeEdit_ = nullptr;
  QPushButton *runBtn_ = nullptr;
  QPushButton *hintBtn_ = nullptr;
  QPushButton *resetBtn_ = nullptr;
  QLabel *statusLabel_ = nullptr;

  std::vector<keship::WaveSample> trail_;
  double waveAmp_ = 0.5;
  double period_ = 5.0;
  bool hasRound_ = false;
};

#endif
