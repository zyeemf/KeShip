/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* MS-06 RAO–T 对比曲线（学生 vs 内置） */

#ifndef RAO_CURVE_WIDGET_HH_
#define RAO_CURVE_WIDGET_HH_

#include <QWidget>
#include <QPaintEvent>

#include <utility>
#include <vector>

class RaoCurveWidget : public QWidget {
  Q_OBJECT

 public:
  explicit RaoCurveWidget(QWidget *_parent = nullptr);
  /// (周期 s, RAO °/m)
  void SetData(const std::vector<std::pair<double, double>> &_builtin,
               const std::vector<std::pair<double, double>> &_student);
  void Clear();

 protected:
  void paintEvent(QPaintEvent *_event) override;

 private:
  std::vector<std::pair<double, double>> builtin_;
  std::vector<std::pair<double, double>> student_;
};

#endif
