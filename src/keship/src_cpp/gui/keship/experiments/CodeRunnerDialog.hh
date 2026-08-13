/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase D: 学生编程模块对话框 */

#ifndef CODE_RUNNER_DIALOG_HH_
#define CODE_RUNNER_DIALOG_HH_

#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>

#include "ShipHullGenerator.hh"

class CodeRunnerDialog : public QDialog {
  Q_OBJECT
 public:
  CodeRunnerDialog(QWidget *_parent = nullptr);
  ~CodeRunnerDialog() override = default;

 private slots:
  void onTypeChanged();
  void onLanguageChanged();
  void onRunCode();
  void onShowHint();
  void onResetTemplate();
  void onProcessFinished(int _exitCode, QProcess::ExitStatus _status);

 private:
  void setupUI();
  void updateTemplate();
  void updateStatus();
  QString generatePythonTemplate() const;
  QString generateCTemplate() const;
  QString pythonPreamble() const;
  QString cPreamble() const;

  QComboBox *typeCombo_;
  QDoubleSpinBox *lengthSpin_, *breadthSpin_, *depthSpin_;
  QDoubleSpinBox *densitySpin_;
  QDoubleSpinBox *draftSpin_;

  QRadioButton *pythonRadio_, *cRadio_;
  QPlainTextEdit *codeEdit_;
  QLabel *answerLabel_, *resultLabel_, *verdictLabel_;
  QPlainTextEdit *outputEdit_;
  QPushButton *runBtn_, *hintBtn_, *resetBtn_;

  QProcess *process_;
  keship::ShipHullSpec spec_;
  double standardVolume_ = 0.0;
};

#endif
