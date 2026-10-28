/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-02 型线图识读 —— 实现
 *
 * 一条硬规则：**任何标准答案都必须从 LinesData 现算**。
 * 抽题时算一遍（写进题面），判定时再算一遍（JudgeQuestion）。
 * 若把答案写死在题面里，换船型 / 换水线后就会"答案对、数据错"，
 * 这种错误在纸面上看不出来，只有拿另一份型值表交叉验证才会暴露。
 */

#include "LinesReadingDialog.hh"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <random>
#include <vector>

#include <QBrush>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "../shared/ExpDeliverable.hh"

namespace {

/// 半宽题相对容差（%）：教学口径，读数误差在 0.5% 以内即算读对
constexpr double kTolPct = 0.5;

QString Fmt(double _v, int _n = 3) { return QString::number(_v, 'f', _n); }

/// 单调序列上的定位：返回左端点下标与权系数（方向自适应，供 z 递减的水线表使用）
bool Bracket(const std::vector<double> &_v, double _t, int *_i0, double *_w)
{
  const int n = static_cast<int>(_v.size());
  if (_i0 == nullptr || _w == nullptr) return false;
  *_i0 = 0;
  *_w = 0.0;
  if (n < 2) return n == 1;

  const bool up = _v[static_cast<std::size_t>(n - 1)] >= _v[0];
  if (up) {
    if (_t <= _v[0]) return true;
    if (_t >= _v[static_cast<std::size_t>(n - 1)]) { *_i0 = n - 2; *_w = 1.0; return true; }
    for (int i = 0; i + 1 < n; ++i) {
      const double a = _v[static_cast<std::size_t>(i)], b = _v[static_cast<std::size_t>(i + 1)];
      if (_t >= a && _t <= b) {
        *_i0 = i;
        *_w = (b - a > 1e-12) ? (_t - a) / (b - a) : 0.0;
        return true;
      }
    }
  } else {
    if (_t >= _v[0]) return true;
    if (_t <= _v[static_cast<std::size_t>(n - 1)]) { *_i0 = n - 2; *_w = 1.0; return true; }
    for (int i = 0; i + 1 < n; ++i) {
      const double a = _v[static_cast<std::size_t>(i)], b = _v[static_cast<std::size_t>(i + 1)];
      if (_t <= a && _t >= b) {
        *_i0 = i;
        *_w = (b - a < -1e-12) ? (_t - a) / (b - a) : 0.0;
        return true;
      }
    }
  }
  return false;
}

}  // namespace

namespace keship {
namespace sd02 {

//-----------------------------------------------------------------------------
double HalfBreadthAt(const LinesData &_ld, double _x, double _z)
{
  const int nSt = static_cast<int>(_ld.stations.size());
  const int nWl = static_cast<int>(_ld.waterlines.size());
  if (nSt < 1 || nWl < 1 || static_cast<int>(_ld.halfBreadths.size()) < nSt) return 0.0;

  int i0 = 0, j0 = 0;
  double wx = 0.0, wz = 0.0;
  if (!Bracket(_ld.stations, _x, &i0, &wx)) return 0.0;
  if (!Bracket(_ld.waterlines, _z, &j0, &wz)) return 0.0;

  const auto b00 = (_ld.halfBreadths[static_cast<std::size_t>(i0)].size() >
                    static_cast<std::size_t>(j0))
                       ? _ld.halfBreadths[static_cast<std::size_t>(i0)][static_cast<std::size_t>(j0)]
                       : 0.0;
  const int i1 = std::min(i0 + 1, nSt - 1);
  const int j1 = std::min(j0 + 1, nWl - 1);
  const double b10 =
      (static_cast<int>(_ld.halfBreadths[static_cast<std::size_t>(i1)].size()) > j0)
          ? _ld.halfBreadths[static_cast<std::size_t>(i1)][static_cast<std::size_t>(j0)]
          : 0.0;
  const double b01 =
      (static_cast<int>(_ld.halfBreadths[static_cast<std::size_t>(i0)].size()) > j1)
          ? _ld.halfBreadths[static_cast<std::size_t>(i0)][static_cast<std::size_t>(j1)]
          : 0.0;
  const double b11 =
      (static_cast<int>(_ld.halfBreadths[static_cast<std::size_t>(i1)].size()) > j1)
          ? _ld.halfBreadths[static_cast<std::size_t>(i1)][static_cast<std::size_t>(j1)]
          : 0.0;

  const double lo = b00 + wx * (b10 - b00);
  const double hi = b01 + wx * (b11 - b01);
  return lo + wz * (hi - lo);
}

//-----------------------------------------------------------------------------
int WaterlineIndexAt(const LinesData &_ld, double _z)
{
  int best = -1;
  double bestD = std::numeric_limits<double>::max();
  for (int j = 0; j < static_cast<int>(_ld.waterlines.size()); ++j) {
    const double d = std::abs(_ld.waterlines[static_cast<std::size_t>(j)] - _z);
    if (d < bestD) { bestD = d; best = j; }
  }
  return best;
}

//-----------------------------------------------------------------------------
std::vector<Question> GenerateQuestions(const LinesData &_ld, int _n, unsigned _seed)
{
  std::vector<Question> out;
  const int nSt = static_cast<int>(_ld.stations.size());
  const int nWl = static_cast<int>(_ld.waterlines.size());
  if (_n < 1 || nSt < 5 || nWl < 4) return out;

  double maxHalf = 0.0;
  for (const auto &row : _ld.halfBreadths)
    for (double v : row) maxHalf = std::max(maxHalf, v);
  if (!(maxHalf > 1e-9)) return out;

  std::mt19937 rng(_seed);
  auto rndInt = [&](int _lo, int _hi) {
    if (_hi <= _lo) return _lo;
    return _lo + static_cast<int>(rng() % static_cast<unsigned>(_hi - _lo + 1));
  };
  auto rnd01 = [&]() { return static_cast<double>(rng() % 100000u) / 100000.0; };

  const double x0 = _ld.stations.front(), x1 = _ld.stations.back();
  const double z0 = _ld.waterlines.front(), z1 = _ld.waterlines.back();
  const int kHalf = static_cast<int>(QType::kHalfBreadth);
  const int kWl = static_cast<int>(QType::kWaterline);

  out.reserve(static_cast<std::size_t>(_n));
  for (int k = 0; k < _n; ++k) {
    // 前 3 题固定覆盖三种题型，其余随机 —— 保证"每套题都练到三种读法"
    const int t = (k < 3) ? k : rndInt(0, 2);
    Question q;
    q.type = static_cast<QType>(t);

    if (t == kHalf || t == kWl) {
      // 只取内部站位/水线：艏艉端半宽趋零，问那里读数是 0，没有教学价值
      int i = 2, j = 1;
      for (int tries = 0; tries < 60; ++tries) {
        i = rndInt(2, nSt - 3);
        j = rndInt(1, nWl - 2);
        if (static_cast<int>(_ld.halfBreadths[static_cast<std::size_t>(i)].size()) > j &&
            _ld.halfBreadths[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] >
                0.08 * maxHalf) {
          break;
        }
      }
      q.stationIdx = i;
      q.waterlineIdx = j;
      if (t == kHalf) {
        q.correctNumber = _ld.halfBreadths[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
      } else {
        q.targetHalfBreadth =
            _ld.halfBreadths[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
        q.correctNumber = static_cast<double>(j);
      }
    } else {
      // 判断内外：取船中段、吃水范围内的一点，横向按半宽的 0.7~1.3 倍偏置，
      // 使"是/否"两种答案都不至于一眼看穿
      double x = x0, z = z0, b = 0.0;
      for (int tries = 0; tries < 60; ++tries) {
        x = x0 + (x1 - x0) * (0.20 + 0.60 * rnd01());
        z = z0 + (z1 - z0) * (0.15 + 0.70 * rnd01());
        b = HalfBreadthAt(_ld, x, z);
        if (b > 0.15 * maxHalf) break;
      }
      static const double kFactor[4] = {0.72, 0.88, 1.12, 1.28};
      const double f = kFactor[rndInt(0, 3)];
      const double sign = (rndInt(0, 1) == 0) ? -1.0 : 1.0;
      q.probeX = x;
      q.probeZ = z;
      q.probeY = sign * b * f;
      q.waterlineIdx = WaterlineIndexAt(_ld, z);
      q.correctBool = (b > 0.0) && (std::abs(q.probeY) <= b);
    }
    out.push_back(q);
  }
  return out;
}

//-----------------------------------------------------------------------------
bool JudgeQuestion(Question &_q, const LinesData &_ld, double _tolPct)
{
  const int nSt = static_cast<int>(_ld.stations.size());
  const int nWl = static_cast<int>(_ld.waterlines.size());
  _q.passed = false;
  // 未作答：不判对，也不把"空"当 0 去蒙对
  const bool answered = _q.answered &&
                        (_q.numeric() ? std::isfinite(_q.studentNumber) : true);
  if (!answered) return true;

  switch (_q.type) {
    case QType::kHalfBreadth: {
      if (_q.stationIdx < 0 || _q.stationIdx >= nSt || _q.waterlineIdx < 0 || _q.waterlineIdx >= nWl) {
        return false;
      }
      const auto &row = _ld.halfBreadths[static_cast<std::size_t>(_q.stationIdx)];
      if (_q.waterlineIdx >= static_cast<int>(row.size())) return false;
      const double c = row[static_cast<std::size_t>(_q.waterlineIdx)];
      _q.correctNumber = c;
      const double tol = std::max(std::abs(c) * std::max(_tolPct, 0.0) / 100.0, 1e-9);
      _q.passed = std::isfinite(_q.studentNumber) && std::abs(_q.studentNumber - c) <= tol;
      return true;
    }
    case QType::kWaterline: {
      if (_q.stationIdx < 0 || _q.stationIdx >= nSt) return false;
      const auto &row = _ld.halfBreadths[static_cast<std::size_t>(_q.stationIdx)];
      int bestJ = -1;
      double bestD = std::numeric_limits<double>::max();
      for (int j = 0; j < static_cast<int>(row.size()); ++j) {
        const double d = std::abs(row[static_cast<std::size_t>(j)] - _q.targetHalfBreadth);
        if (d < bestD) { bestD = d; bestJ = j; }
      }
      if (bestJ < 0) return false;
      _q.waterlineIdx = bestJ;
      _q.correctNumber = static_cast<double>(bestJ);
      // 水线序号必须在整数格上精确落位（±0.5 视为写同一个序号）
      _q.passed = std::isfinite(_q.studentNumber) &&
                  std::abs(_q.studentNumber - static_cast<double>(bestJ)) < 0.5;
      return true;
    }
    case QType::kInside: {
      const double b = HalfBreadthAt(_ld, _q.probeX, _q.probeZ);
      _q.correctBool = (b > 0.0) && (std::abs(_q.probeY) <= b);
      _q.passed = (_q.studentBool == _q.correctBool);
      return true;
    }
  }
  return false;
}

//-----------------------------------------------------------------------------
int ScoreOf(const std::vector<Question> &_qs)
{
  int s = 0;
  for (const auto &q : _qs) {
    if (q.passed) ++s;
  }
  return s;
}

//-----------------------------------------------------------------------------
QString TypeName(const Question &_q)
{
  switch (_q.type) {
    case QType::kHalfBreadth: return "求半宽";
    case QType::kWaterline: return "求水线";
    case QType::kInside: return "判断内外";
  }
  return "未知";
}

//-----------------------------------------------------------------------------
QJsonObject QuestionToJson(const Question &_q)
{
  QJsonObject o;
  o["type"] = (_q.type == QType::kHalfBreadth)
                  ? QString("halfBreadth")
                  : (_q.type == QType::kWaterline ? QString("waterline") : QString("inside"));
  o["stationIdx"] = _q.stationIdx;
  o["waterlineIdx"] = _q.waterlineIdx;
  o["answered"] = _q.answered &&
                  (_q.numeric() ? std::isfinite(_q.studentNumber) : true);
  // 未作答写成 null，而不是 0：0 是一个"真实但错误"的答案，会污染批阅统计
  o["studentAnswer"] = (_q.numeric() && !std::isfinite(_q.studentNumber))
                           ? QJsonValue(QJsonValue::Null)
                           : (_q.numeric() ? QJsonValue(_q.studentNumber)
                                           : QJsonValue(_q.studentBool));
  o["correctAnswer"] =
      _q.numeric() ? QJsonValue(_q.correctNumber) : QJsonValue(_q.correctBool);
  o["passed"] = _q.passed;
  // 题面已知量：没有它们，判断题无法被第三方复核
  if (_q.type == QType::kInside) {
    o["probeX"] = _q.probeX;
    o["probeY"] = _q.probeY;
    o["probeZ"] = _q.probeZ;
  }
  if (_q.type == QType::kWaterline) o["targetHalfBreadth"] = _q.targetHalfBreadth;
  return o;
}

//-----------------------------------------------------------------------------
QString QuestionPrompt(const Question &_q, const LinesData &_ld)
{
  const auto xAt = [&](int _i) {
    return (_i >= 0 && _i < static_cast<int>(_ld.stations.size()))
               ? _ld.stations[static_cast<std::size_t>(_i)]
               : 0.0;
  };
  const auto zAt = [&](int _j) {
    return (_j >= 0 && _j < static_cast<int>(_ld.waterlines.size()))
               ? _ld.waterlines[static_cast<std::size_t>(_j)]
               : 0.0;
  };

  switch (_q.type) {
    case QType::kHalfBreadth:
      return QString("站位 #%1（x = %2 m）与第 %3 条水线（z = %4 m）交点处的半宽 y = ? m")
          .arg(_q.stationIdx)
          .arg(Fmt(xAt(_q.stationIdx)))
          .arg(_q.waterlineIdx)
          .arg(Fmt(zAt(_q.waterlineIdx)));
    case QType::kWaterline:
      return QString("站位 #%1（x = %2 m）上，半宽 y = %3 m 在第几条水线？"
                     "（填序号，0 = 最上面）")
          .arg(_q.stationIdx)
          .arg(Fmt(xAt(_q.stationIdx)))
          .arg(Fmt(_q.targetHalfBreadth, 4));
    case QType::kInside:
      return QString("点 (x = %1, y = %2, z = %3) m 是否在船体内部？")
          .arg(Fmt(_q.probeX))
          .arg(Fmt(_q.probeY, 4))
          .arg(Fmt(_q.probeZ));
  }
  return QString();
}

}  // namespace sd02
}  // namespace keship

// ═══════════════════════════════════════════════════════════════════════════
// LinesReadingView —— 三视图（简化：能看清对应关系即可，不画等值线）
// ═══════════════════════════════════════════════════════════════════════════

LinesReadingView::LinesReadingView(QWidget *_parent) : QWidget(_parent)
{
  // 240 而不是 300：三视图下方还有练习表，窗口 900 px 高时若此处置 300，
  // 布局会挤压得画布底部的坐标刻度被裁掉（"看得见图、读不出数"= 识读实验失效）
  setMinimumHeight(240);
}

void LinesReadingView::setLines(const keship::LinesData &_lines)
{
  lines_ = _lines;
  hasData_ = !_lines.stations.empty() && !_lines.waterlines.empty();
  update();
}

void LinesReadingView::setHighlight(int _stationIdx, int _waterlineIdx)
{
  hlStation_ = _stationIdx;
  hlWaterline_ = _waterlineIdx;
  update();
}

void LinesReadingView::paintEvent(QPaintEvent *)
{
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor("#fcfcfc"));

  if (!hasData_ || width() < 300 || height() < 150) {
    p.setPen(QColor("#888"));
    p.drawText(rect(), Qt::AlignCenter, "尚无型线数据");
    return;
  }

  const double gap = 8.0;
  const double pw = (width() - 2.0 * gap) / 3.0;
  const QRectF rBody(0.0, 0.0, pw, height());
  const QRectF rHalf(pw + gap, 0.0, pw, height());
  const QRectF rProf(2.0 * (pw + gap), 0.0, pw, height());
  paintBodyPlan(p, rBody);
  paintHalfBreadth(p, rHalf);
  paintProfile(p, rProf);
}

namespace {

struct Panel {
  QRectF plot;
  QRectF title;
};

Panel MakePanel(const QRectF &_r, const QString &_text, QPainter &_p)
{
  Panel pn;
  pn.title = QRectF(_r.left(), _r.top(), _r.width(), 16.0);
  pn.plot = QRectF(_r.left() + 40.0, _r.top() + 20.0, _r.width() - 52.0, _r.height() - 54.0);
  _p.setPen(QPen(QColor("#cfcfcf"), 1.0));
  _p.drawRect(pn.plot);
  QFont f = _p.font();
  f.setPointSize(9);
  f.setBold(true);
  _p.setFont(f);
  _p.setPen(QColor("#333"));
  _p.drawText(pn.title, Qt::AlignHCenter | Qt::AlignVCenter, _text);
  f.setBold(false);
  f.setPointSize(7);
  _p.setFont(f);
  return pn;
}

double MaxHalf(const keship::LinesData &_ld)
{
  double m = 0.0;
  for (const auto &row : _ld.halfBreadths)
    for (double v : row) m = std::max(m, v);
  return std::max(m, 1e-6);
}

}  // namespace

void LinesReadingView::paintBodyPlan(QPainter &_p, const QRectF &_r) const
{
  const Panel pn = MakePanel(_r, "横剖线图（站位剖面）", _p);
  const auto &ld = lines_;
  const int nSt = static_cast<int>(ld.stations.size());
  const int nWl = static_cast<int>(ld.waterlines.size());
  if (nSt < 2 || nWl < 2) return;

  const double yMax = MaxHalf(ld) * 1.05;
  const double zMin = ld.waterlines.back(), zMax = ld.waterlines.front();

  auto tx = [&](double _y) { return pn.plot.left() + (_y / yMax) * pn.plot.width(); };
  auto ty = [&](double _z) {
    const double d = (zMax - zMin);
    const double t = (d > 1e-12) ? (_z - zMin) / d : 0.0;
    return pn.plot.bottom() - t * pn.plot.height();
  };

  _p.setPen(QPen(QColor("#e8e8e8"), 1.0));
  for (int k = 1; k < 4; ++k) {
    const double y = yMax * k / 4.0;
    _p.drawLine(QPointF(tx(y), pn.plot.top()), QPointF(tx(y), pn.plot.bottom()));
  }

  // 水线（水平参考线）
  _p.setPen(QPen(QColor("#dbe6f0"), 1.0, Qt::DotLine));
  for (int j = 0; j < nWl; j += std::max(1, nWl / 6)) {
    _p.drawLine(QPointF(pn.plot.left(), ty(ld.waterlines[static_cast<std::size_t>(j)])),
                QPointF(pn.plot.right(), ty(ld.waterlines[static_cast<std::size_t>(j)])));
  }

  const int step = std::max(1, nSt / 8);
  for (int i = 0; i < nSt; i += step) {
    const bool hl = (i == hlStation_);
    _p.setPen(QPen(hl ? QColor("#c0392b") : QColor("#2f6fbf"), hl ? 2.4 : 1.2));
    QPainterPath path;
    for (int j = 0; j < nWl; ++j) {
      const auto &row = ld.halfBreadths[static_cast<std::size_t>(i)];
      if (j >= static_cast<int>(row.size())) break;
      const QPointF pt(tx(row[static_cast<std::size_t>(j)]),
                       ty(ld.waterlines[static_cast<std::size_t>(j)]));
      if (j == 0) path.moveTo(pt); else path.lineTo(pt);
    }
    _p.setBrush(Qt::NoBrush);
    _p.drawPath(path);
  }
  // 高亮站位若不是抽样点，补画一次（否则"高亮没出现"会被当成程序坏了）
  if (hlStation_ >= 0 && hlStation_ < nSt && hlStation_ % step != 0) {
    _p.setPen(QPen(QColor("#c0392b"), 2.4));
    QPainterPath path;
    for (int j = 0; j < nWl; ++j) {
      const auto &row = ld.halfBreadths[static_cast<std::size_t>(hlStation_)];
      if (j >= static_cast<int>(row.size())) break;
      const QPointF pt(tx(row[static_cast<std::size_t>(j)]),
                       ty(ld.waterlines[static_cast<std::size_t>(j)]));
      if (j == 0) path.moveTo(pt); else path.lineTo(pt);
    }
    _p.drawPath(path);
  }

  if (hlWaterline_ >= 0 && hlWaterline_ < nWl) {
    const double z = ld.waterlines[static_cast<std::size_t>(hlWaterline_)];
    _p.setPen(QPen(QColor("#e67e22"), 1.6, Qt::DashLine));
    _p.drawLine(QPointF(pn.plot.left(), ty(z)), QPointF(pn.plot.right(), ty(z)));
  }

  _p.setPen(QColor("#555"));
  // 半宽刻度：识读题要学生"从图上读数"，看不到量纲就无法作答
  for (int k = 0; k <= 2; ++k) {
    const double y = yMax * 0.5 * k;
    _p.drawText(QRectF(tx(y) - 20.0, pn.plot.bottom() + 2, 40, 14),
                Qt::AlignHCenter, Fmt(y, 2));
  }
  _p.drawText(QRectF(pn.plot.left(), pn.plot.bottom() + 18, pn.plot.width(), 14),
              Qt::AlignHCenter, "y / m");
  _p.drawText(QRectF(_r.left() + 1, pn.plot.top(), 38, 14), Qt::AlignRight,
              Fmt(zMax, 1));
  _p.drawText(QRectF(_r.left() + 1, pn.plot.bottom() - 14, 38, 14), Qt::AlignRight,
              Fmt(zMin, 1));
}
void LinesReadingView::paintHalfBreadth(QPainter &_p, const QRectF &_r) const
{
  const Panel pn = MakePanel(_r, "半宽水线图（俯视）", _p);
  const auto &ld = lines_;
  const int nSt = static_cast<int>(ld.stations.size());
  const int nWl = static_cast<int>(ld.waterlines.size());
  if (nSt < 2 || nWl < 2) return;

  const double xMin = ld.stations.front(), xMax = ld.stations.back();
  const double yMax = MaxHalf(ld) * 1.05;

  auto tx = [&](double _x) {
    const double d = xMax - xMin;
    const double t = (d > 1e-12) ? (_x - xMin) / d : 0.0;
    return pn.plot.left() + t * pn.plot.width();
  };
  auto ty = [&](double _y) { return pn.plot.bottom() - (_y / yMax) * pn.plot.height(); };

  _p.setPen(QPen(QColor("#efefef"), 1.0));
  for (int k = 1; k < 4; ++k) {
    const double y = yMax * k / 4.0;
    _p.drawLine(QPointF(pn.plot.left(), ty(y)), QPointF(pn.plot.right(), ty(y)));
  }

  const int step = std::max(1, nWl / 6);
  for (int j = 0; j < nWl; j += step) {
    const bool hl = (j == hlWaterline_);
    _p.setPen(QPen(hl ? QColor("#e67e22") : QColor("#2f6fbf"), hl ? 2.4 : 1.2));
    QPainterPath path;
    bool first = true;
    for (int i = 0; i < nSt; ++i) {
      const auto &row = ld.halfBreadths[static_cast<std::size_t>(i)];
      if (j >= static_cast<int>(row.size())) continue;
      const QPointF pt(tx(ld.stations[static_cast<std::size_t>(i)]),
                       ty(row[static_cast<std::size_t>(j)]));
      if (first) { path.moveTo(pt); first = false; } else { path.lineTo(pt); }
    }
    _p.drawPath(path);
  }
  if (hlWaterline_ >= 0 && hlWaterline_ < nWl && hlWaterline_ % step != 0) {
    _p.setPen(QPen(QColor("#e67e22"), 2.4));
    QPainterPath path;
    bool first = true;
    for (int i = 0; i < nSt; ++i) {
      const auto &row = ld.halfBreadths[static_cast<std::size_t>(i)];
      if (hlWaterline_ >= static_cast<int>(row.size())) continue;
      const QPointF pt(tx(ld.stations[static_cast<std::size_t>(i)]),
                       ty(row[static_cast<std::size_t>(hlWaterline_)]));
      if (first) { path.moveTo(pt); first = false; } else { path.lineTo(pt); }
    }
    _p.drawPath(path);
  }

  if (hlStation_ >= 0 && hlStation_ < nSt) {
    const double x = ld.stations[static_cast<std::size_t>(hlStation_)];
    _p.setPen(QPen(QColor("#c0392b"), 1.6, Qt::DashLine));
    _p.drawLine(QPointF(tx(x), pn.plot.top()), QPointF(tx(x), pn.plot.bottom()));
  }

  _p.setPen(QColor("#555"));
  // 左轴：半宽刻度；下轴：站位刻度。识读题要学生"从图上读数"，没有刻度就只能靠猜
  for (int k = 1; k <= 2; ++k) {   // 0 与横轴原点重合，跳过以免两个字叠在一起
    const double y = yMax * 0.5 * k;
    _p.drawText(QRectF(pn.plot.left() - 40.0, ty(y) - 7, 38, 14),
                Qt::AlignRight | Qt::AlignVCenter, Fmt(y, 2));
  }
  for (int k = 0; k <= 2; ++k) {
    const double x = xMin + 0.5 * (xMax - xMin) * k;
    _p.drawText(QRectF(tx(x) - 20.0, pn.plot.bottom() + 2, 40, 14),
                Qt::AlignHCenter, Fmt(x, 2));
  }
  _p.drawText(QRectF(pn.plot.left(), pn.plot.bottom() + 18, pn.plot.width(), 14),
              Qt::AlignHCenter, "x / m");
  _p.drawText(QRectF(pn.plot.left(), pn.plot.top() - 12, pn.plot.width(), 12),
              Qt::AlignRight, "y / m");
}

void LinesReadingView::paintProfile(QPainter &_p, const QRectF &_r) const
{
  const Panel pn = MakePanel(_r, "纵剖线图（侧视）", _p);
  const auto &ld = lines_;
  const int nSt = static_cast<int>(ld.stations.size());
  const int nWl = static_cast<int>(ld.waterlines.size());
  if (nSt < 2 || nWl < 2) return;

  const double xMin = ld.stations.front(), xMax = ld.stations.back();
  const double zMin = ld.waterlines.back(), zMax = ld.waterlines.front();
  const double yMax = MaxHalf(ld);

  auto tx = [&](double _x) {
    const double d = xMax - xMin;
    const double t = (d > 1e-12) ? (_x - xMin) / d : 0.0;
    return pn.plot.left() + t * pn.plot.width();
  };
  auto ty = [&](double _z) {
    const double d = zMax - zMin;
    const double t = (d > 1e-12) ? (_z - zMin) / d : 0.0;
    return pn.plot.bottom() - t * pn.plot.height();
  };

  _p.setPen(QPen(QColor("#dbe6f0"), 1.0, Qt::DotLine));
  for (int j = 0; j < nWl; j += std::max(1, nWl / 6)) {
    _p.drawLine(QPointF(pn.plot.left(), ty(ld.waterlines[static_cast<std::size_t>(j)])),
                QPointF(pn.plot.right(), ty(ld.waterlines[static_cast<std::size_t>(j)])));
  }

  // 甲板边线 / 龙骨线
  _p.setPen(QPen(QColor("#c0392b"), 1.8));
  _p.drawLine(QPointF(pn.plot.left(), ty(zMax)), QPointF(pn.plot.right(), ty(zMax)));
  _p.setPen(QPen(QColor("#1f4e79"), 1.8));
  _p.drawLine(QPointF(pn.plot.left(), ty(zMin)), QPointF(pn.plot.right(), ty(zMin)));

  // 纵剖线（buttock）：y = 0.25 / 0.5 / 0.75 倍最大半宽处的水线交点轨迹
  static const double kFrac[3] = {0.25, 0.5, 0.75};
  static const QColor kCol[3] = {QColor("#1a7f37"), QColor("#a0602e"), QColor("#7d3cb5")};
  for (int b = 0; b < 3; ++b) {
    const double yt = yMax * kFrac[b];
    _p.setPen(QPen(kCol[b], 1.4));
    QPainterPath path;
    bool first = true;
    for (int i = 0; i < nSt; ++i) {
      const auto &row = ld.halfBreadths[static_cast<std::size_t>(i)];
      for (int j = 0; j + 1 < nWl && j + 1 < static_cast<int>(row.size()); ++j) {
        const double b0 = row[static_cast<std::size_t>(j)];
        const double b1 = row[static_cast<std::size_t>(j + 1)];
        if ((b0 - yt) * (b1 - yt) > 0.0) continue;
        const double den = b0 - b1;
        const double w = (std::abs(den) > 1e-12) ? (b0 - yt) / den : 0.0;
        const double z = ld.waterlines[static_cast<std::size_t>(j)] +
                         w * (ld.waterlines[static_cast<std::size_t>(j + 1)] -
                              ld.waterlines[static_cast<std::size_t>(j)]);
        const QPointF pt(tx(ld.stations[static_cast<std::size_t>(i)]), ty(z));
        if (first) { path.moveTo(pt); first = false; } else { path.lineTo(pt); }
        break;  // 每站只取最上面一次穿越（半宽随吃水单调减小）
      }
    }
    _p.setBrush(Qt::NoBrush);
    _p.drawPath(path);
  }

  if (hlStation_ >= 0 && hlStation_ < nSt) {
    const double x = ld.stations[static_cast<std::size_t>(hlStation_)];
    _p.setPen(QPen(QColor("#c0392b"), 1.6, Qt::DashLine));
    _p.drawLine(QPointF(tx(x), pn.plot.top()), QPointF(tx(x), pn.plot.bottom()));
  }

  _p.setPen(QColor("#555"));
  for (int k = 0; k <= 2; ++k) {
    const double x = xMin + 0.5 * (xMax - xMin) * k;
    _p.drawText(QRectF(tx(x) - 20.0, pn.plot.bottom() + 2, 40, 14),
                Qt::AlignHCenter, Fmt(x, 2));
  }
  _p.drawText(QRectF(pn.plot.left(), pn.plot.bottom() + 18, pn.plot.width(), 14),
              Qt::AlignHCenter, "x / m");
  _p.drawText(QRectF(_r.left() + 1, pn.plot.top(), 38, 14), Qt::AlignRight, Fmt(zMax, 1));
  _p.drawText(QRectF(_r.left() + 1, pn.plot.bottom() - 14, 38, 14), Qt::AlignRight,
              Fmt(zMin, 1));
}

// ═══════════════════════════════════════════════════════════════════════════
// LinesReadingDialog
// ═══════════════════════════════════════════════════════════════════════════

LinesReadingDialog::LinesReadingDialog(QWidget *_parent) : QDialog(_parent)
{
  setWindowTitle("SD-02 型线图识读 —— 三视图对照 + 随机识读练习");
  resize(1180, 900);
  seed_ = 20261028u;  // 默认固定种子：同一堂课的学生看到同一组题，便于讲评
  buildUi();
  refresh();
}

void LinesReadingDialog::buildUi()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      "<b>实验目的（识图训练）：</b>型线图由<b>横剖线图</b>（看横剖面形状）、"
      "<b>半宽水线图</b>（俯视，看水平剖面）、<b>纵剖线图</b>（侧视，看纵向轮廓）三视图组成，"
      "同一份数据在三个视图里是<b>同一条曲线的不同投影</b>。<br>"
      "本页随机抽 5 题考查「能不能从图上读出来」：给站位与水线求半宽、给半宽反查水线、"
      "判断某点是否在船体内。答案一律由型值表现算，不写死。", this);
  intro->setWordWrap(true);
  mainLayout->addWidget(intro);

  auto *srcBox = new QGroupBox("型线来源", this);
  auto *srcLayout = new QHBoxLayout(srcBox);

  auto *formL = new QFormLayout();
  sourceCombo_ = new QComboBox(srcBox);
  sourceCombo_->addItem("按 L/B/D 现场生成（Wigley 经典）");
  sourceCombo_->addItem("导入 SD-08 型线（sd08_lines.json）");
  formL->addRow("来源：", sourceCombo_);

  auto *formR = new QFormLayout();
  lengthSpin_ = new QDoubleSpinBox(srcBox);
  lengthSpin_->setRange(2.0, 60.0); lengthSpin_->setValue(6.0);
  lengthSpin_->setSingleStep(0.5); lengthSpin_->setDecimals(3); lengthSpin_->setSuffix(" m");
  formR->addRow("船长 L：", lengthSpin_);
  breadthSpin_ = new QDoubleSpinBox(srcBox);
  breadthSpin_->setRange(0.5, 20.0); breadthSpin_->setValue(1.5);
  breadthSpin_->setSingleStep(0.1); breadthSpin_->setDecimals(3); breadthSpin_->setSuffix(" m");
  formR->addRow("型宽 B：", breadthSpin_);
  depthSpin_ = new QDoubleSpinBox(srcBox);
  depthSpin_->setRange(0.3, 12.0); depthSpin_->setValue(1.0);
  depthSpin_->setSingleStep(0.05); depthSpin_->setDecimals(3); depthSpin_->setSuffix(" m");
  formR->addRow("型深 D：", depthSpin_);

  srcLayout->addLayout(formL);
  srcLayout->addSpacing(24);
  srcLayout->addLayout(formR);

  auto *btnColumn = new QVBoxLayout();
  auto *importBtn = new QPushButton("导入 SD-08 型线", srcBox);
  auto *regenBtn = new QPushButton("按当前参数重新生成", srcBox);
  btnColumn->addWidget(importBtn);
  btnColumn->addWidget(regenBtn);
  btnColumn->addStretch();
  srcLayout->addLayout(btnColumn);

  mainLayout->addWidget(srcBox);

  auto *viewBox = new QGroupBox("三视图（红虚线 = 题目所指站位/水线）", this);
  auto *viewLayout = new QVBoxLayout(viewBox);
  view_ = new LinesReadingView(viewBox);
  viewLayout->addWidget(view_);
  mainLayout->addWidget(viewBox, 3);

  auto *quizBox = new QGroupBox("识读练习（5 题）", this);
  auto *quizLayout = new QVBoxLayout(quizBox);

  promptLabel_ = new QLabel(this);
  promptLabel_->setWordWrap(true);
  promptLabel_->setTextFormat(Qt::RichText);
  promptLabel_->setStyleSheet("color:#555;");
  quizLayout->addWidget(promptLabel_);

  quizTable_ = new QTableWidget(quizBox);
  quizTable_->setColumnCount(6);
  quizTable_->setHorizontalHeaderLabels(
      QStringList() << "题号" << "题型" << "题目" << "你的答案" << "正确值" << "判定");
  quizTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  quizTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
  quizTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  quizTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
  quizTable_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
  quizTable_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
  quizTable_->setColumnWidth(0, 48);
  quizTable_->setColumnWidth(1, 76);
  quizTable_->setColumnWidth(3, 96);
  quizTable_->setColumnWidth(4, 96);
  quizTable_->setColumnWidth(5, 60);
  quizTable_->verticalHeader()->setVisible(false);
  quizTable_->verticalHeader()->setDefaultSectionSize(24);
  // 固定高：5 行题必须一屏看全（滚动条只用于极端缩放），不参与纵向拉伸
  quizTable_->setFixedHeight(24 * 5 + 34);
  quizLayout->addWidget(quizTable_);

  auto *btnRow = new QHBoxLayout();
  auto *newBtn = new QPushButton("换一组题", quizBox);
  auto *submitBtn = new QPushButton("提交答案", quizBox);
  auto *exportBtn = new QPushButton("导出识读结果（SD-02 交付物）", quizBox);
  btnRow->addWidget(newBtn);
  btnRow->addWidget(submitBtn);
  btnRow->addStretch();
  btnRow->addWidget(exportBtn);
  quizLayout->addLayout(btnRow);

  scoreLabel_ = new QLabel(this);
  scoreLabel_->setWordWrap(true);
  scoreLabel_->setTextFormat(Qt::RichText);
  quizLayout->addWidget(scoreLabel_);

  mainLayout->addWidget(quizBox, 2);

  statusLabel_ = new QLabel(this);
  statusLabel_->setStyleSheet("color:#555;");
  mainLayout->addWidget(statusLabel_);

  connect(sourceCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &LinesReadingDialog::onSourceChanged);
  connect(importBtn, &QPushButton::clicked, this, &LinesReadingDialog::onImportSd08);
  connect(regenBtn, &QPushButton::clicked, this, &LinesReadingDialog::onRegenerate);
  connect(newBtn, &QPushButton::clicked, this, &LinesReadingDialog::onNewQuiz);
  connect(submitBtn, &QPushButton::clicked, this, &LinesReadingDialog::onSubmit);
  connect(exportBtn, &QPushButton::clicked, this, &LinesReadingDialog::onExport);
}

//-----------------------------------------------------------------------------
bool LinesReadingDialog::refresh()
{
  const int src = sourceCombo_->currentIndex();
  if (src == 0) {
    const double L = lengthSpin_->value();
    const double B = breadthSpin_->value();
    const double D = depthSpin_->value();
    if (!(L > 1e-6) || !(B > 1e-6) || !(D > 1e-6)) {
      errMsg_ = "主尺度必须为正。";
      return false;
    }
    keship::ShipHullGenerator gen(
        keship::ShipHullSpec::WigleyClassic(L, B, D, 500.0));
    lines_ = gen.lines();
    lineSource_ = "generated";
  } else {
    if (!hasImported_) {
      errMsg_ = "尚未导入 SD-08 型线：请先点「导入 SD-08 型线」。";
      return false;
    }
    lineSource_ = "imported";
  }

  if (lines_.stations.empty() || lines_.waterlines.empty()) {
    errMsg_ = "型线数据为空。";
    return false;
  }

  view_->setLines(lines_);
  buildQuiz();

  statusLabel_->setText(
      QString("型线来源：%1　｜　%2 站位 × %3 水线　｜　最大半宽 %4 m　｜　"
              "船舶惯例：x 从艉到艏、z = 0 为水线面（向下为负）")
          .arg(lineSource_ == "imported" ? QString("导入的 SD-08 型线") : QString("现场生成"))
          .arg(static_cast<int>(lines_.stations.size()))
          .arg(static_cast<int>(lines_.waterlines.size()))
          .arg(Fmt([&] {
                 double m = 0.0;
                 for (const auto &row : lines_.halfBreadths)
                   for (double v : row) m = std::max(m, v);
                 return m;
               }(), 4)));
  return true;
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::buildQuiz()
{
  ++seed_;
  questions_ = keship::sd02::GenerateQuestions(lines_, 5, seed_);
  numEdits_.assign(questions_.size(), nullptr);
  boolCombos_.assign(questions_.size(), nullptr);

  quizTable_->clearContents();
  quizTable_->setRowCount(static_cast<int>(questions_.size()));

  for (int r = 0; r < static_cast<int>(questions_.size()); ++r) {
    const auto &q = questions_[static_cast<std::size_t>(r)];
    auto *idx = new QTableWidgetItem(QString::number(r + 1));
    idx->setTextAlignment(Qt::AlignCenter);
    quizTable_->setItem(r, 0, idx);

    auto *tp = new QTableWidgetItem(keship::sd02::TypeName(q));
    tp->setTextAlignment(Qt::AlignCenter);
    quizTable_->setItem(r, 1, tp);

    auto *pr = new QTableWidgetItem(keship::sd02::QuestionPrompt(q, lines_));
    pr->setToolTip(pr->text());
    quizTable_->setItem(r, 2, pr);

    if (q.numeric()) {
      auto *edit = new QLineEdit(quizTable_);
      edit->setPlaceholderText(q.type == keship::sd02::QType::kHalfBreadth
                                   ? "半宽 / m"
                                   : "水线序号");
      quizTable_->setCellWidget(r, 3, edit);
      numEdits_[static_cast<std::size_t>(r)] = edit;
    } else {
      auto *combo = new QComboBox(quizTable_);
      combo->addItem("（请选择）", -1);   // 默认不预选：预选"否"会把未作答当答对/答错
      combo->addItem("是（在船体内）", 1);
      combo->addItem("否（在船体外）", 0);
      combo->setCurrentIndex(0);
      quizTable_->setCellWidget(r, 3, combo);
      boolCombos_[static_cast<std::size_t>(r)] = combo;
    }

    quizTable_->setItem(r, 4, new QTableWidgetItem("—"));
    auto *verdict = new QTableWidgetItem("未提交");
    verdict->setTextAlignment(Qt::AlignCenter);
    quizTable_->setItem(r, 5, verdict);
  }

  // 只高亮题面已知量：求半宽题（站位+水线都已知）两条都给；
  // 求水线题只给站位（否则等于把答案画出来）；判断题不给任何高亮。
  int hlSt = -1, hlWl = -1;
  if (!questions_.empty()) {
    const auto &q0 = questions_.front();
    hlSt = q0.stationIdx;
    if (q0.type == keship::sd02::QType::kHalfBreadth) hlWl = q0.waterlineIdx;
  }
  view_->setHighlight(hlSt, hlWl);

  scoreLabel_->setText("<b>当前得分：0 / " +
                       QString::number(static_cast<int>(questions_.size())) +
                       "</b>　（作答后点「提交答案」逐题判定）");
  promptLabel_->setText(
      "读数说明：横剖线图纵轴为 z（水线面 z = 0，向下为负）、横轴为半宽 y；"
      "半宽水线图横轴为站位 x、纵轴为半宽 y。题面给出的站位/水线已在图上用红虚线标出。");
  updateCache();
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::fillResultCells()
{
  for (int r = 0; r < static_cast<int>(questions_.size()); ++r) {
    const auto &q = questions_[static_cast<std::size_t>(r)];
    const QString right = q.numeric() ? QString::number(q.correctNumber, 'f',
                                                        q.type == keship::sd02::QType::kHalfBreadth ? 4 : 0)
                                      : (q.correctBool ? "是" : "否");
    auto *it = new QTableWidgetItem(right);
    it->setTextAlignment(Qt::AlignCenter);
    quizTable_->setItem(r, 4, it);

    const bool answered = q.answered &&
                          (q.numeric() ? std::isfinite(q.studentNumber) : true);
    auto *verdict = new QTableWidgetItem(answered ? (q.passed ? "正确" : "错误") : "未作答");
    verdict->setTextAlignment(Qt::AlignCenter);
    verdict->setForeground(QBrush(QColor(!answered ? "#888" : (q.passed ? "#1a7f37" : "#c0392b"))));
    quizTable_->setItem(r, 5, verdict);
  }
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::updateCache()
{
  lastObject_ = QJsonObject();
  lastObject_["type"] = "lines_reading";
  lastObject_["source"] = lineSource_;
  lastObject_["lengthM"] = lengthSpin_->value();
  lastObject_["breadthM"] = breadthSpin_->value();
  lastObject_["depthM"] = depthSpin_->value();

  lastParams_ = QJsonObject();
  lastParams_["toleranceHalfBreadthPct"] = kTolPct;
  lastParams_["waterlineIndexTolerance"] = QString("exact（±0.5 视为同一序号）");
  lastParams_["insideAnswer"] = QString("boolean（true = 在船体内）");
  lastParams_["nQuestions"] = static_cast<int>(questions_.size());
  lastParams_["randomSeed"] = static_cast<int>(seed_);
  lastParams_["halfBreadthLookup"] = QString("bilinear（站位 × 水线）");

  double maxHalf = 0.0;
  for (const auto &row : lines_.halfBreadths)
    for (double v : row) maxHalf = std::max(maxHalf, v);

  lastMetrics_ = QJsonObject();
  lastMetrics_["score"] = score();
  lastMetrics_["totalQuestions"] = static_cast<int>(questions_.size());
  lastMetrics_["accuracy"] = questions_.empty()
                                 ? 0.0
                                 : static_cast<double>(score()) /
                                       static_cast<double>(questions_.size());
  lastMetrics_["nStations"] = static_cast<int>(lines_.stations.size());
  lastMetrics_["nWaterlines"] = static_cast<int>(lines_.waterlines.size());
  lastMetrics_["maxHalfBreadthM"] = maxHalf;

  QJsonArray arr;
  for (const auto &q : questions_) arr.append(keship::sd02::QuestionToJson(q));
  lastExtra_ = QJsonObject();
  lastExtra_["questions"] = arr;
}

//-----------------------------------------------------------------------------
int LinesReadingDialog::score() const
{
  return keship::sd02::ScoreOf(questions_);
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::onSourceChanged(int _index)
{
  if (_index == 1 && !hasImported_) {
    statusLabel_->setText("来源已切到「导入 SD-08 型线」，但尚未导入文件 —— 请点「导入 SD-08 型线」。");
    return;
  }
  refresh();
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::onImportSd08()
{
  const QString path = QFileDialog::getOpenFileName(
      this, "导入 SD-08 型线交付物", "sd08_lines.json", "JSON (*.json)");
  if (path.isEmpty()) return;
  if (!importSd08(path)) {
    QMessageBox::warning(this, "导入 SD-08 型线",
        QString::fromStdString("无法使用该文件：\n" + errMsg_.toStdString()));
  }
}

//-----------------------------------------------------------------------------
bool LinesReadingDialog::importSd08(const QString &_path)
{
  keship::ExpDeliverable d;
  std::string err;
  // 串号拒绝：拿 HS-08 的 GZ 交付物当型线用，会让"识读"读到一堆空数组
  if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err) ||
      !keship::ValidExpDeliverable(d, "SD-08", &err)) {
    errMsg_ = QString::fromStdString(err);
    return false;
  }

  const QJsonArray st = d.extra["stations"].toArray();
  const QJsonArray wl = d.extra["waterlines"].toArray();
  const QJsonArray hb = d.extra["halfBreadths"].toArray();
  if (st.isEmpty() || wl.isEmpty() || hb.isEmpty()) {
    errMsg_ = "文件缺少 extra.stations / waterlines / halfBreadths，无法构造型线。";
    return false;
  }

  keship::LinesData ld;
  for (const auto &v : st) ld.stations.push_back(v.toDouble());
  for (const auto &v : wl) ld.waterlines.push_back(v.toDouble());
  for (const auto &rv : hb) {
    std::vector<double> row;
    for (const auto &v : rv.toArray()) row.push_back(v.toDouble());
    ld.halfBreadths.push_back(row);
  }
  if (ld.halfBreadths.size() != ld.stations.size()) {
    errMsg_ = "型值表行数与站位数不一致。";
    return false;
  }

  lines_ = ld;
  hasImported_ = true;
  importedObject_ = d.object;

  {
    QSignalBlocker b1(sourceCombo_);
    QSignalBlocker b2(lengthSpin_);
    QSignalBlocker b3(breadthSpin_);
    QSignalBlocker b4(depthSpin_);
    sourceCombo_->setCurrentIndex(1);
    lengthSpin_->setValue(d.object["lengthM"].toDouble(lengthSpin_->value()));
    breadthSpin_->setValue(d.object["breadthM"].toDouble(breadthSpin_->value()));
    depthSpin_->setValue(d.object["depthM"].toDouble(depthSpin_->value()));
  }

  if (!refresh()) return false;
  statusLabel_->setText(
      QString("已导入 SD-08 型线：<code>%1</code>（%2 站 × %3 水线，L = %4 m）")
          .arg(_path)
          .arg(static_cast<int>(lines_.stations.size()))
          .arg(static_cast<int>(lines_.waterlines.size()))
          .arg(Fmt(d.object["lengthM"].toDouble(), 2)));
  std::cout << "SD-02: 已导入 SD-08 型线，站数 " << lines_.stations.size() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::onRegenerate()
{
  if (sourceCombo_->currentIndex() != 0) {
    QMessageBox::information(this, "重新生成",
        "当前来源是「导入 SD-08 型线」。若要按 L/B/D 重新生成，请先把来源切回「现场生成」。");
    return;
  }
  if (!refresh()) {
    QMessageBox::warning(this, "重新生成", errMsg_);
  }
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::onNewQuiz()
{
  buildQuiz();
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::onSubmit()
{
  for (int r = 0; r < static_cast<int>(questions_.size()); ++r) {
    auto &q = questions_[static_cast<std::size_t>(r)];
    if (q.numeric()) {
      QLineEdit *edit = numEdits_[static_cast<std::size_t>(r)];
      bool ok = false;
      const double v = edit ? edit->text().trimmed().toDouble(&ok) : 0.0;
      q.studentNumber = ok ? v : std::numeric_limits<double>::quiet_NaN();
      q.answered = ok;
    } else {
      QComboBox *combo = boolCombos_[static_cast<std::size_t>(r)];
      const int data = combo ? combo->currentData().toInt() : -1;
      q.answered = (data >= 0);
      q.studentBool = (data == 1);
    }
    keship::sd02::JudgeQuestion(q, lines_, kTolPct);
  }

  fillResultCells();
  const int s = score();
  const int n = static_cast<int>(questions_.size());
  scoreLabel_->setText(
      QString("<b>得分：%1 / %2</b>　（正确率 %3%）")
          .arg(s).arg(n)
          .arg(n > 0 ? Fmt(100.0 * s / n, 1) : QString("0.0")));
  updateCache();

  std::cout << "SD-02: 提交判定，得分 " << s << " / " << n << std::endl;
}

//-----------------------------------------------------------------------------
bool LinesReadingDialog::exportTo(const QString &_path)
{
  if (_path.isEmpty()) return false;
  if (questions_.empty()) {
    errMsg_ = "尚未生成识读题。";
    return false;
  }
  // 注意：这里**不能**调 refresh() —— 它会重新抽题（seed++），把学生刚做的答案换掉。
  // 导出前只按当前作答判定一次：导出的分数必须与屏幕上看到的一致。
  onSubmit();

  keship::ExpDeliverable d;
  d.expId = "SD-02";
  d.createdAt = keship::NowIso8601();
  d.object = lastObject_;
  d.params = lastParams_;
  d.metrics = lastMetrics_;
  d.extra = lastExtra_;

  std::string err;
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    errMsg_ = QString::fromStdString(err);
    std::cerr << "SD-02: 导出失败 " << err << std::endl;
    return false;
  }
  std::cout << "SD-02: 已导出交付物 " << _path.toStdString() << std::endl;
  return true;
}

//-----------------------------------------------------------------------------
void LinesReadingDialog::onExport()
{
  const QString path = QFileDialog::getSaveFileName(
      this, "导出 SD-02 识读结果", "sd02_lines_reading.json", "JSON (*.json)");
  if (path.isEmpty()) return;

  if (!exportTo(path)) {
    QMessageBox::warning(this, "导出识读结果",
        QString("导出失败：%1\n%2").arg(path).arg(errMsg_));
    return;
  }
  QMessageBox::information(this, "导出识读结果",
      QString("已导出：%1\n\n得分 %2 / %3（正确率 %4%）\n"
              "该文件是**非链**交付物：仅供批阅与归档，不被下游实验引用。")
          .arg(path).arg(score()).arg(static_cast<int>(questions_.size()))
          .arg(questions_.empty() ? QString("0.0")
                                  : Fmt(100.0 * score() / questions_.size(), 1)));
}
