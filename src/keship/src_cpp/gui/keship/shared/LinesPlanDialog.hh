/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 型线图对话框 — Phase B */

#ifndef LINES_PLAN_DIALOG_HH_
#define LINES_PLAN_DIALOG_HH_

#include <QDialog>
#include <QWidget>
#include <QPaintEvent>
#include <vector>

#include "ShipHullGenerator.hh"

/// 横剖线图 (Body Plan): y vs z, 各站位剖面叠加
class BodyPlanWidget : public QWidget {
  Q_OBJECT
 public:
  explicit BodyPlanWidget(QWidget *_parent = nullptr);
  void setLines(const keship::LinesData &_lines);
 protected:
  void paintEvent(QPaintEvent *_event) override;
 private:
  keship::LinesData data_;
  bool hasData_ = false;
};

/// 半宽水线图 (Half-Breadth Plan): x vs y, 各水线叠加
class HalfBreadthWidget : public QWidget {
  Q_OBJECT
 public:
  explicit HalfBreadthWidget(QWidget *_parent = nullptr);
  void setLines(const keship::LinesData &_lines);
 protected:
  void paintEvent(QPaintEvent *_event) override;
 private:
  keship::LinesData data_;
  bool hasData_ = false;
};

/// 纵剖线图 (Sheer Plan): x vs z, 中纵剖面 + 甲板边线 + 舭部线
class SheerPlanWidget : public QWidget {
  Q_OBJECT
 public:
  explicit SheerPlanWidget(QWidget *_parent = nullptr);
  void setLines(const keship::LinesData &_lines);
 protected:
  void paintEvent(QPaintEvent *_event) override;
 private:
  keship::LinesData data_;
  bool hasData_ = false;
};

/// 型线图对话框
class LinesPlanDialog : public QDialog {
  Q_OBJECT
 public:
  LinesPlanDialog(const keship::ShipHullSpec &_spec, QWidget *_parent = nullptr);
};

#endif  // LINES_PLAN_DIALOG_HH_
