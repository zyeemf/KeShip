/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-06 RAO 编程计算实践：内置样例 → 学生写代码 → 对话框内运行 → 与内置算法对比 */

#ifndef MS06_CODE_PRACTICE_DIALOG_HH_
#define MS06_CODE_PRACTICE_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>

#include <vector>

#include "WaveResponseCore.hh"

class MS06CodePracticeDialog : public QDialog {
  Q_OBJECT

 public:
  explicit MS06CodePracticeDialog(QWidget *_parent = nullptr);

 private slots:
  void onTaskChanged();
  void onLanguageChanged();
  void onRunCode();
  void onShowHint();
  void onResetTemplate();

 private:
  void setupUI();
  void loadBuiltinSample();
  void updateBuiltinResult();
  void updateTemplate();
  QString buildTemplate() const;

  QComboBox *dataCombo_ = nullptr;
  QComboBox *taskCombo_ = nullptr;
  QRadioButton *cRadio_ = nullptr;
  QRadioButton *pythonRadio_ = nullptr;
  QLabel *builtinLabel_ = nullptr;
  QPlainTextEdit *codeEdit_ = nullptr;
  QPlainTextEdit *outputEdit_ = nullptr;
  QPushButton *runBtn_ = nullptr;
  QPushButton *hintBtn_ = nullptr;
  QPushButton *resetBtn_ = nullptr;
  QLabel *resultLabel_ = nullptr;
  QLabel *verdictLabel_ = nullptr;

  std::vector<keship::WaveSample> sample_;  ///< 内置样例时程（T=5 s 横浪实测）
  double waveAmp_ = 0.5;
  double wavePeriod_ = 5.0;
  double builtinRao_ = -1.0;      ///< 内置算法（computeMetrics，与 MS-03 同一实现）
  double builtinRollAmp_ = -1.0;
};

#endif
