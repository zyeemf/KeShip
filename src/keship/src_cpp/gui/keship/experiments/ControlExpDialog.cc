/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SC-01~SC-12 控制设计链（L6）+ 感知→控制链（L7）—— 通用实验对话框（薄壳）实现
 *
 * 与 `OperabilityDialog` 同一体例：定时器按采样周期驱动 `core->sample()`，
 * 曲线/指标只读 `core->trail()` 与 `core->metrics()`；本文件不含任何控制律算式。
 */

#include "ControlExpDialog.hh"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

#include "ControlExpCore.hh"
#include "TaskScoring.hh"

namespace {

QString ExpTitle(const QString &_id) {
  if (_id == "SC-01") return QStringLiteral("SC-01 开环与闭环响应对比");
  if (_id == "SC-02") return QStringLiteral("SC-02 PID 参数效应");
  if (_id == "SC-03") return QStringLiteral("SC-03 环境扰动影响");
  if (_id == "SC-04") return QStringLiteral("SC-04 航向控制");
  if (_id == "SC-05") return QStringLiteral("SC-05 航速控制");
  if (_id == "SC-06") return QStringLiteral("SC-06 LOS 循迹跟踪（L7 感知→控制节点）");
  if (_id == "SC-07") return QStringLiteral("SC-07 动力定位");
  if (_id == "SC-09") return QStringLiteral("SC-09 PID 自整定");
  if (_id == "SC-10") return QStringLiteral("SC-10 航路点路径跟随");
  if (_id == "SC-11") return QStringLiteral("SC-11 COLREGs 避碰");
  if (_id == "SC-12") return QStringLiteral("SC-12 多船协同与编队");
  return QStringLiteral("控制实验");
}

QString ExpIntro(const QString &_id) {
  if (_id == "SC-01") {
    return QStringLiteral(
        "<b>实验目的：</b>同一条船、同一目标航向下，比较<b>开环（固定推力、无反馈）</b>与"
        "<b>闭环（航向 PID 差动）</b>的响应差异。<br>"
        "点「开始」会<b>连跑两轮</b>（开环 → 闭环），两轮指标进同一张表，可直接对比稳态误差与 RMSE。");
  }
  if (_id == "SC-02") {
    return QStringLiteral(
        "<b>实验目的：</b>观察 Kp / Ki / Kd 各自的作用（响应快慢、稳态偏差、超调振荡）。<br>"
        "勾选「启用参数扫描」后，按起始/终止/步长依次跑多轮，逐轮指标进表 —— 这就是<b>参数效应曲线</b>。");
  }
  if (_id == "SC-03") {
    return QStringLiteral(
        "<b>实验目的：</b>风/流扰动下控制品质如何退化。<br>"
        "「开始」连跑两轮：<b>无扰动基准</b> → <b>有扰动</b>，比较航向稳态误差与航速误差。<br>"
        "<i>本版扰动以「控制器观测偏置」形式注入（教学级简化，见报告 §5）</i>。");
  }
  if (_id == "SC-04") {
    return QStringLiteral(
        "<b>实验目的：</b>给定目标航向，调 PID 使船稳定转向并保持。看调节时间、超调与稳态误差。");
  }
  if (_id == "SC-05") {
    return QStringLiteral(
        "<b>实验目的：</b>给定目标航速，PID 输出<b>共模</b>推力（两桨同加同减）。"
        "平台不含舵，本实验<b>不控航向</b>（差动恒 0）。");
  }
  if (_id == "SC-06") {
    return QStringLiteral(
        "<b>实验目的：</b>LOS 制导 + 航向 PID 跟踪折线航路（横向偏差 → 期望航向）。<br>"
        "<b>L7 节点：</b>可「导入 SC-08 位姿估计交付物」，把航向反馈换成 SC-08 的<b>估计值</b>，"
        "观察积分漂移如何进入控制回路。");
  }
  if (_id == "SC-07") {
    return QStringLiteral(
        "<b>实验目的：</b>动力定位 —— 位置误差经极坐标分解为「期望航向 + 期望航速」，"
        "再由航向环与航速环合成双桨推力。看保持误差 dpHoldErr。");
  }
  if (_id == "SC-09") {
    return QStringLiteral(
        "<b>实验目的：</b>不做手工试凑，用<b>开环阶跃响应</b>识别 K/T/L，再按 "
        "Ziegler–Nichols 反应曲线法算出 PID 初值，最后复跑验证。<br>"
        "点「开始」：① 开环阶跃识别（自动填 Kp/Ki/Kd）→ ② 用整定值闭环复跑。");
  }
  if (_id == "SC-10") {
    return QStringLiteral(
        "<b>实验目的：</b>多航路点路径跟随（设计类）。交付物里带 <code>TaskScoring</code> 三指标评分，"
        "便于与其它同学的结果对比。");
  }
  if (_id == "SC-11") {
    return QStringLiteral(
        "<b>实验目的：</b>单障碍避碰：按相对方位判定对遇/交叉，输出航向偏置后再由航向 PID 执行。<br>"
        "看最小 DCPA（净空）。<i>本版只建模单个<b>静止</b>障碍，见报告 §5</i>。");
  }
  if (_id == "SC-12") {
    return QStringLiteral(
        "<b>实验目的：</b>编队站位保持：目标点 = 虚拟领航者位置 + 编队偏移，按 DP 律跟随。<br>"
        "<i>单船条件下没有真实僚船，见报告 §5</i>。");
  }
  return QStringLiteral("控制实验");
}

keship::ControlLaw LawForExp(const QString &_id) {
  if (_id == "SC-05") return keship::ControlLaw::kPidSpeed;
  if (_id == "SC-06" || _id == "SC-10") return keship::ControlLaw::kLos;
  if (_id == "SC-07") return keship::ControlLaw::kDp;
  if (_id == "SC-11") return keship::ControlLaw::kColregs;
  if (_id == "SC-12") return keship::ControlLaw::kFormation;
  return keship::ControlLaw::kPidHeading;
}

QString Fmt(double _v, int _dec = 2) {
  if (!std::isfinite(_v)) return QStringLiteral("—");
  if (std::abs(_v + 1.0) < 1e-12) return QStringLiteral("—");   // 约定 −1 = 无定义
  return QString::number(_v, 'f', _dec);
}

/// 12 个指标的统一字段名/中文名/小数位（交付物 metrics 键 = 这里的 key，逐字一致）
struct MetricField {
  const char *key;
  const char *cn;
  double keship::ControlMetrics::*member;
  int dec;
};

const MetricField kMetricFields[] = {
    {"headingRmseDeg",     "航向 RMSE (°)",        &keship::ControlMetrics::headingRmseDeg,     3},
    {"headingSteadyErrDeg","航向稳态误差 (°)",     &keship::ControlMetrics::headingSteadyErrDeg, 3},
    {"settlingTimeS",      "调节时间 (s)",         &keship::ControlMetrics::settlingTimeS,       2},
    {"overshootPct",       "超调 (%)",             &keship::ControlMetrics::overshootPct,        2},
    {"speedSteadyErrPct",  "航速稳态误差 (%)",     &keship::ControlMetrics::speedSteadyErrPct,   2},
    {"lateralRmsM",        "横向偏差 RMS (m)",     &keship::ControlMetrics::lateralRmsM,         3},
    {"maxLateralM",        "最大横向偏差 (m)",     &keship::ControlMetrics::maxLateralM,         3},
    {"dpHoldErrM",         "DP/站位保持误差 (m)",  &keship::ControlMetrics::dpHoldErrM,          3},
    {"arrivalRate",        "航路点到达率",         &keship::ControlMetrics::arrivalRate,         3},
    {"meanThrustN",        "平均推力 (N)",         &keship::ControlMetrics::meanThrustN,         1},
    {"minDcpaM",           "最小 DCPA 净空 (m)",   &keship::ControlMetrics::minDcpaM,            2},
    {"formationErrM",      "编队误差 (m)",         &keship::ControlMetrics::formationErrM,       3},
};

/// 双精度安全取值（Qt 的 toDouble 在 NaN 时给 0，这里显式区分）
double SafeDouble(double _v) { return std::isfinite(_v) ? _v : -1.0; }

}  // namespace

// ══════════════════════════════════════════════════════════════════════════
// 时程曲线（三栏：航向 / 横向偏差 / 推力）
// ══════════════════════════════════════════════════════════════════════════
ControlChartWidget::ControlChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumHeight(360);
  setAutoFillBackground(true);
}

void ControlChartWidget::setTrail(const std::vector<keship::ControlSample> &_pts) {
  pts_ = _pts;
  update();
}

void ControlChartWidget::clearTrail() { pts_.clear(); update(); }

void ControlChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing, true);
  p.fillRect(rect(), QColor(250, 250, 252));
  p.setPen(QColor(205, 210, 220));
  p.drawRect(rect().adjusted(0, 0, -1, -1));

  if (pts_.size() < 2) {
    p.setPen(QColor(150, 155, 165));
    p.drawText(rect(), Qt::AlignCenter,
               QStringLiteral("尚无数据 — 选好参数后点「开始」"));
    return;
  }

  const double tMax = std::max(1e-6, pts_.back().t);
  const double left = 52.0, right = 12.0;
  const double W = width() - left - right;
  const double gap = 8.0;
  const double topPad = 18.0;
  const double panelH = (height() - topPad - gap * 2.0 - 6.0) / 3.0;

  /// 一栏：title + 若干曲线（可带参考线）
  auto drawPanel = [&](int _idx, const QString &_title,
                       const std::vector<std::pair<QString, QColor>> &_names,
                       const std::function<double(const keship::ControlSample &)> &_get0,
                       const std::function<double(const keship::ControlSample &)> &_get1,
                       bool _hasRef, double _ref) {
    const double y0 = topPad + _idx * (panelH + gap);
    const QRectF box(left, y0 + 12.0, W, panelH - 16.0);

    double lo = 0.0, hi = 0.0;
    bool first = true;
    for (const auto &s : pts_) {
      for (int k = 0; k < static_cast<int>(_names.size()); ++k) {
        const double v = (k == 0) ? _get0(s) : _get1(s);
        if (first) { lo = hi = v; first = false; }
        lo = std::min(lo, v); hi = std::max(hi, v);
      }
    }
    if (_hasRef) { lo = std::min(lo, _ref); hi = std::max(hi, _ref); }
    if (hi - lo < 1e-6) { hi += 1.0; lo -= 1.0; }
    const double pad = (hi - lo) * 0.12;
    lo -= pad; hi += pad;

    auto toPix = [&](double _t, double _v) {
      return QPointF(box.left() + (_t / tMax) * box.width(),
                     box.bottom() - ((_v - lo) / (hi - lo)) * box.height());
    };

    p.setPen(QColor(232, 235, 242));
    for (int i = 1; i < 4; ++i) {
      const double gy = box.top() + box.height() * i / 4.0;
      p.drawLine(QPointF(box.left(), gy), QPointF(box.right(), gy));
    }
    p.setPen(QColor(120, 125, 135));
    p.drawText(QPointF(8.0, y0 + 10.0), _title);
    p.drawText(QPointF(4.0, box.top() + 8.0), QString::number(hi, 'f', 2));
    p.drawText(QPointF(4.0, box.bottom()), QString::number(lo, 'f', 2));

    if (_hasRef) {
      QPen rp(QColor(210, 80, 70), 1.0, Qt::DashLine);
      p.setPen(rp);
      p.drawLine(toPix(0.0, _ref), toPix(tMax, _ref));
    }

    for (int k = 0; k < static_cast<int>(_names.size()); ++k) {
      QPainterPath path;
      bool started = false;
      for (const auto &s : pts_) {
        const double v = (k == 0) ? _get0(s) : _get1(s);
        const QPointF pt = toPix(s.t, v);
        if (!started) { path.moveTo(pt); started = true; } else { path.lineTo(pt); }
      }
      p.setPen(QPen(_names[k].second, 1.8));
      p.drawPath(path);
    }

    // 图例
    double lx = box.left() + 6.0;
    for (const auto &nm : _names) {
      p.setPen(QPen(nm.second, 2.0));
      p.drawLine(QPointF(lx, y0 + 5.0), QPointF(lx + 14.0, y0 + 5.0));
      p.setPen(QColor(90, 95, 105));
      p.drawText(QPointF(lx + 18.0, y0 + 9.0), nm.first);
      lx += 22.0 + QFontMetrics(font()).horizontalAdvance(nm.first);
    }
  };

  const auto getYaw = [](const keship::ControlSample &s) { return s.yawDeg; };
  const auto getLat = [](const keship::ControlSample &s) { return s.lateralErrM; };
  const auto getL = [](const keship::ControlSample &s) { return s.thrustL; };
  const auto getR = [](const keship::ControlSample &s) { return s.thrustR; };

  drawPanel(0, QStringLiteral("航向 yaw (蓝色) / 目标 (红色虚线)"),
            {{QStringLiteral("yaw"), QColor(40, 90, 200)}}, getYaw, getYaw,
            hasTarget_, targetDeg_);
  drawPanel(1, QStringLiteral("横向偏差 / 到目标距离 (m)"),
            {{QStringLiteral("误差"), QColor(40, 150, 90)}}, getLat, getLat, false, 0.0);
  drawPanel(2, QStringLiteral("双桨推力 (N)：左 / 右"),
            {{QStringLiteral("左桨"), QColor(220, 120, 30)},
             {QStringLiteral("右桨"), QColor(150, 60, 170)}},
            getL, getR, false, 0.0);
}

// ══════════════════════════════════════════════════════════════════════════
// 构造 / 析构
// ══════════════════════════════════════════════════════════════════════════
ControlExpDialog::ControlExpDialog(gz::transport::Node &_node,
                                   const std::string &_worldName,
                                   const QStringList &_models,
                                   const QString &_expId,
                                   QWidget *_parent)
    : QDialog(_parent), node_(_node), worldName_(_worldName), expId_(_expId),
      models_(_models) {
  core_ = std::make_unique<keship::ControlExpCore>(node_, worldName_);
  setWindowTitle(ExpTitle(expId_));
  resize(1180, 780);
  buildUi();
  applyExpDefaults();
  updateVisibility();

  timer_ = new QTimer(this);
  connect(timer_, &QTimer::timeout, this, &ControlExpDialog::onTick);
  connect(runBtn_, &QPushButton::clicked, this, &ControlExpDialog::onStart);
  connect(stopBtn_, &QPushButton::clicked, this, &ControlExpDialog::onStop);
  connect(exportBtn_, &QPushButton::clicked, this, &ControlExpDialog::onExport);
  connect(addWpBtn_, &QPushButton::clicked, this, &ControlExpDialog::onAddWaypoint);
  connect(delWpBtn_, &QPushButton::clicked, this, &ControlExpDialog::onRemoveWaypoint);
  connect(sc08BrowseBtn_, &QPushButton::clicked, this, &ControlExpDialog::onPickSc08);
  connect(tuneBtn_, &QPushButton::clicked, this, &ControlExpDialog::onAutoTune);
  stopBtn_->setEnabled(false);

  std::cout << "SC: 控制实验对话框已创建（" << expId_.toStdString()
            << "，核心层 ControlExpCore）" << std::endl;
}

ControlExpDialog::~ControlExpDialog() {
  if (core_ && core_->running()) core_->stop();
}

// ══════════════════════════════════════════════════════════════════════════
// 界面
// ══════════════════════════════════════════════════════════════════════════
void ControlExpDialog::buildUi() {
  auto *root = new QVBoxLayout(this);

  auto *intro = new QLabel(ExpIntro(expId_), this);
  intro->setWordWrap(true);
  root->addWidget(intro);

  auto *body = new QHBoxLayout();
  root->addLayout(body, 1);

  // ── 左：参数区（可滚动）──────────────────────────────────────────────
  auto *scroll = new QScrollArea(this);
  scroll->setWidgetResizable(true);
  scroll->setMinimumWidth(430);
  scroll->setMaximumWidth(470);
  auto *leftWrap = new QWidget(scroll);
  auto *left = new QVBoxLayout(leftWrap);

  auto mkSpin = [&](QWidget *_p, double _lo, double _hi, double _val, double _step,
                    int _dec, const QString &_suffix = QString()) {
    auto *s = new QDoubleSpinBox(_p);
    s->setRange(_lo, _hi);
    s->setDecimals(_dec);
    s->setSingleStep(_step);
    s->setValue(_val);
    if (!_suffix.isEmpty()) s->setSuffix(_suffix);
    s->setKeyboardTracking(false);
    return s;
  };

  auto *targetBox = new QGroupBox(QStringLiteral("对象与运行"), leftWrap);
  auto *targetForm = new QFormLayout(targetBox);
  modelCombo_ = new QComboBox(targetBox);
  modelCombo_->addItems(models_.isEmpty() ? QStringList{QStringLiteral("GT1")} : models_);
  targetForm->addRow(QStringLiteral("船模："), modelCombo_);
  thrLEdit_ = new QLineEdit(QStringLiteral("thrust_left"), targetBox);
  thrREdit_ = new QLineEdit(QStringLiteral("thrust_right"), targetBox);
  auto *thrRow = new QHBoxLayout();
  thrRow->addWidget(new QLabel(QStringLiteral("左推进器"), targetBox));
  thrRow->addWidget(thrLEdit_);
  thrRow->addSpacing(8);
  thrRow->addWidget(new QLabel(QStringLiteral("右推进器"), targetBox));
  thrRow->addWidget(thrREdit_);
  targetForm->addRow(thrRow);
  periodSpin_ = mkSpin(targetBox, 0.02, 1.0, 0.1, 0.02, 3, QStringLiteral(" s"));
  targetForm->addRow(QStringLiteral("采样周期："), periodSpin_);
  simSecSpin_ = mkSpin(targetBox, 5.0, 600.0, 60.0, 5.0, 0, QStringLiteral(" s"));
  targetForm->addRow(QStringLiteral("仿真时长："), simSecSpin_);
  left->addWidget(targetBox);

  // PID
  pidGroup_ = new QGroupBox(QStringLiteral("控制参数（平台不含舵：转向靠双桨差动）"), leftWrap);
  auto *pidForm = new QFormLayout(pidGroup_);
  kpSpin_ = mkSpin(pidGroup_, 0.0, 50.0, 1.5, 0.1, 3);
  kiSpin_ = mkSpin(pidGroup_, 0.0, 20.0, 0.05, 0.01, 4);
  kdSpin_ = mkSpin(pidGroup_, 0.0, 50.0, 0.5, 0.1, 3);
  baseSpin_ = mkSpin(pidGroup_, 0.0, 500.0, 100.0, 10.0, 1, QStringLiteral(" N"));
  limitSpin_ = mkSpin(pidGroup_, 0.0, 300.0, 80.0, 10.0, 1, QStringLiteral(" N"));
  pidForm->addRow(QStringLiteral("Kp："), kpSpin_);
  pidForm->addRow(QStringLiteral("Ki："), kiSpin_);
  pidForm->addRow(QStringLiteral("Kd："), kdSpin_);
  pidForm->addRow(QStringLiteral("基准推力："), baseSpin_);
  pidForm->addRow(QStringLiteral("差动限幅："), limitSpin_);
  left->addWidget(pidGroup_);

  // 目标航向
  headingGroup_ = new QGroupBox(QStringLiteral("目标航向"), leftWrap);
  auto *hForm = new QFormLayout(headingGroup_);
  headingSpin_ = mkSpin(headingGroup_, -180.0, 180.0, 30.0, 5.0, 1, QStringLiteral(" °"));
  hForm->addRow(QStringLiteral("目标航向："), headingSpin_);
  left->addWidget(headingGroup_);

  // 目标航速
  speedGroup_ = new QGroupBox(QStringLiteral("目标航速"), leftWrap);
  auto *sForm = new QFormLayout(speedGroup_);
  speedSpin_ = mkSpin(speedGroup_, 0.0, 5.0, 1.0, 0.1, 2, QStringLiteral(" m/s"));
  sForm->addRow(QStringLiteral("目标航速："), speedSpin_);
  left->addWidget(speedGroup_);

  // 目标点
  pointGroup_ = new QGroupBox(QStringLiteral("目标点（世界坐标）"), leftWrap);
  auto *pForm = new QFormLayout(pointGroup_);
  targetXSpin_ = mkSpin(pointGroup_, -500.0, 500.0, 10.0, 1.0, 2, QStringLiteral(" m"));
  targetYSpin_ = mkSpin(pointGroup_, -500.0, 500.0, 0.0, 1.0, 2, QStringLiteral(" m"));
  pForm->addRow(QStringLiteral("目标 X："), targetXSpin_);
  pForm->addRow(QStringLiteral("目标 Y："), targetYSpin_);
  left->addWidget(pointGroup_);

  // LOS 航路点
  losGroup_ = new QGroupBox(QStringLiteral("航路点（LOS 前视循迹）"), leftWrap);
  auto *lForm = new QFormLayout(losGroup_);
  lookaheadSpin_ = mkSpin(losGroup_, 0.5, 50.0, 2.0, 0.5, 1, QStringLiteral(" m"));
  lForm->addRow(QStringLiteral("前视距离 Δ："), lookaheadSpin_);
  wpTable_ = new QTableWidget(0, 2, losGroup_);
  wpTable_->setHorizontalHeaderLabels({QStringLiteral("X (m)"), QStringLiteral("Y (m)")});
  wpTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  wpTable_->verticalHeader()->setVisible(false);
  wpTable_->setMinimumHeight(150);
  lForm->addRow(wpTable_);
  auto *wpRow = new QHBoxLayout();
  addWpBtn_ = new QPushButton(QStringLiteral("加航路点"), losGroup_);
  delWpBtn_ = new QPushButton(QStringLiteral("删末行"), losGroup_);
  wpRow->addWidget(addWpBtn_);
  wpRow->addWidget(delWpBtn_);
  lForm->addRow(wpRow);
  left->addWidget(losGroup_);

  // 扰动
  distGroup_ = new QGroupBox(QStringLiteral("环境扰动（教学级简化，见报告 §5）"), leftWrap);
  auto *dForm = new QFormLayout(distGroup_);
  disturbCheck_ = new QCheckBox(QStringLiteral("启用扰动"), distGroup_);
  windSpin_ = mkSpin(distGroup_, 0.0, 25.0, 6.0, 1.0, 1, QStringLiteral(" m/s"));
  currentSpin_ = mkSpin(distGroup_, 0.0, 3.0, 0.4, 0.1, 2, QStringLiteral(" m/s"));
  currentDirSpin_ = mkSpin(distGroup_, -180.0, 180.0, 90.0, 15.0, 0, QStringLiteral(" °"));
  dForm->addRow(disturbCheck_);
  dForm->addRow(QStringLiteral("风速："), windSpin_);
  dForm->addRow(QStringLiteral("流速："), currentSpin_);
  dForm->addRow(QStringLiteral("流向："), currentDirSpin_);
  left->addWidget(distGroup_);

  // 障碍
  obsGroup_ = new QGroupBox(QStringLiteral("障碍（简化单障碍）"), leftWrap);
  auto *oForm = new QFormLayout(obsGroup_);
  obsXSpin_ = mkSpin(obsGroup_, -500.0, 500.0, 30.0, 1.0, 2, QStringLiteral(" m"));
  obsYSpin_ = mkSpin(obsGroup_, -500.0, 500.0, 0.0, 1.0, 2, QStringLiteral(" m"));
  obsRSpin_ = mkSpin(obsGroup_, 0.5, 50.0, 1.5, 0.5, 1, QStringLiteral(" m"));
  oForm->addRow(QStringLiteral("障碍 X："), obsXSpin_);
  oForm->addRow(QStringLiteral("障碍 Y："), obsYSpin_);
  oForm->addRow(QStringLiteral("障碍半径："), obsRSpin_);
  left->addWidget(obsGroup_);

  // 编队
  formGroup_ = new QGroupBox(QStringLiteral("编队偏移（虚拟领航者）"), leftWrap);
  auto *fmForm = new QFormLayout(formGroup_);
  formXSpin_ = mkSpin(formGroup_, -100.0, 100.0, -3.0, 1.0, 2, QStringLiteral(" m"));
  formYSpin_ = mkSpin(formGroup_, -100.0, 100.0, 0.0, 1.0, 2, QStringLiteral(" m"));
  fmForm->addRow(QStringLiteral("纵向偏移 +X："), formXSpin_);
  fmForm->addRow(QStringLiteral("横向偏移 +Y："), formYSpin_);
  left->addWidget(formGroup_);

  // 参数扫描
  sweepGroup_ = new QGroupBox(QStringLiteral("参数扫描（SC-02）"), leftWrap);
  auto *swForm = new QFormLayout(sweepGroup_);
  sweepCheck_ = new QCheckBox(QStringLiteral("启用参数扫描"), sweepGroup_);
  sweepCombo_ = new QComboBox(sweepGroup_);
  sweepCombo_->addItem(QStringLiteral("Kp"), 0);
  sweepCombo_->addItem(QStringLiteral("Ki"), 1);
  sweepCombo_->addItem(QStringLiteral("Kd"), 2);
  sweepFromSpin_ = mkSpin(sweepGroup_, 0.0, 50.0, 0.5, 0.5, 3);
  sweepToSpin_ = mkSpin(sweepGroup_, 0.0, 50.0, 3.0, 0.5, 3);
  sweepStepSpin_ = mkSpin(sweepGroup_, 0.01, 10.0, 0.5, 0.1, 3);
  swForm->addRow(sweepCheck_);
  swForm->addRow(QStringLiteral("扫描参数："), sweepCombo_);
  swForm->addRow(QStringLiteral("起始："), sweepFromSpin_);
  swForm->addRow(QStringLiteral("终止："), sweepToSpin_);
  swForm->addRow(QStringLiteral("步长："), sweepStepSpin_);
  left->addWidget(sweepGroup_);

  // SC-08 导入
  sc08Group_ = new QGroupBox(QStringLiteral("感知输入（L7：SC-08 位姿估计）"), leftWrap);
  auto *scForm = new QFormLayout(sc08Group_);
  sc08PathEdit_ = new QLineEdit(sc08Group_);
  sc08PathEdit_->setPlaceholderText(QStringLiteral("sc08_estimation.csv 或 sc08_pose.json"));
  sc08BrowseBtn_ = new QPushButton(QStringLiteral("导入…"), sc08Group_);
  auto *scRow = new QHBoxLayout();
  scRow->addWidget(sc08PathEdit_);
  scRow->addWidget(sc08BrowseBtn_);
  scForm->addRow(scRow);
  sc08InfoLabel_ = new QLabel(QStringLiteral("未导入：航向反馈用位姿真值"), sc08Group_);
  sc08InfoLabel_->setWordWrap(true);
  sc08InfoLabel_->setStyleSheet("color:#555;");
  scForm->addRow(sc08InfoLabel_);
  left->addWidget(sc08Group_);

  // 自整定
  tuneGroup_ = new QGroupBox(QStringLiteral("自整定（SC-09）"), leftWrap);
  auto *tForm = new QFormLayout(tuneGroup_);
  probeBaseSpin_ = mkSpin(tuneGroup_, 0.0, 500.0, 100.0, 10.0, 1, QStringLiteral(" N"));
  probeDiffSpin_ = mkSpin(tuneGroup_, 5.0, 100.0, 20.0, 5.0, 1, QStringLiteral(" N"));
  tuneBtn_ = new QPushButton(QStringLiteral("只做识别并填增益（不跑验证）"), tuneGroup_);
  tuneLabel_ = new QLabel(QStringLiteral("未整定"), tuneGroup_);
  tuneLabel_->setWordWrap(true);
  tuneLabel_->setStyleSheet("color:#555;");
  tForm->addRow(QStringLiteral("识别基准推力："), probeBaseSpin_);
  tForm->addRow(QStringLiteral("识别差动量 Δ："), probeDiffSpin_);
  tForm->addRow(tuneBtn_);
  tForm->addRow(tuneLabel_);
  left->addWidget(tuneGroup_);

  left->addStretch(1);
  scroll->setWidget(leftWrap);
  body->addWidget(scroll);

  // ── 右：指标 + 曲线 + 逐轮表 ─────────────────────────────────────────
  auto *rightWrap = new QWidget(this);
  auto *right = new QVBoxLayout(rightWrap);

  auto *btnRow = new QHBoxLayout();
  runBtn_ = new QPushButton(QStringLiteral("开始"), rightWrap);
  stopBtn_ = new QPushButton(QStringLiteral("停止"), rightWrap);
  exportBtn_ = new QPushButton(QStringLiteral("导出交付物"), rightWrap);
  runBtn_->setMinimumHeight(32);
  stopBtn_->setMinimumHeight(32);
  exportBtn_->setMinimumHeight(32);
  btnRow->addWidget(runBtn_);
  btnRow->addWidget(stopBtn_);
  btnRow->addStretch();
  btnRow->addWidget(exportBtn_);
  right->addLayout(btnRow);

  metricsLabel_ = new QLabel(QStringLiteral("指标：—"), rightWrap);
  metricsLabel_->setStyleSheet("font-family: monospace;");
  metricsLabel_->setWordWrap(true);
  right->addWidget(metricsLabel_);

  scoreLabel_ = new QLabel(QString(), rightWrap);
  scoreLabel_->setStyleSheet("font-family: monospace; font-weight: bold; color:#2050a0;");
  scoreLabel_->setWordWrap(true);
  right->addWidget(scoreLabel_);

  chart_ = new ControlChartWidget(rightWrap);
  right->addWidget(chart_, 1);

  runTable_ = new QTableWidget(0, 6, rightWrap);
  runTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  runTable_->setHorizontalHeaderLabels({QStringLiteral("阶段"), QStringLiteral("扫描值"),
                                        QStringLiteral("航向RMSE(°)"),
                                        QStringLiteral("横向RMS(m)"),
                                        QStringLiteral("到达率/保持"),
                                        QStringLiteral("平均推力(N)")});
  runTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  runTable_->verticalHeader()->setVisible(false);
  runTable_->setMaximumHeight(160);
  right->addWidget(runTable_);

  statusLabel_ = new QLabel(QStringLiteral("就绪。"), rightWrap);
  statusLabel_->setWordWrap(true);
  right->addWidget(statusLabel_);

  body->addWidget(rightWrap, 1);
}

//-----------------------------------------------------------------------------
void ControlExpDialog::applyExpDefaults() {
  // 每个 expId 的默认参数（与报告 §3 的表逐字一致）
  if (expId_ == "SC-01") {
    headingSpin_->setValue(30.0); kpSpin_->setValue(1.5); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.5); simSecSpin_->setValue(40.0);
  } else if (expId_ == "SC-02") {
    headingSpin_->setValue(30.0); kpSpin_->setValue(1.0); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.3); simSecSpin_->setValue(40.0);
    sweepCheck_->setChecked(true);
    sweepCombo_->setCurrentIndex(0);
    sweepFromSpin_->setValue(0.5); sweepToSpin_->setValue(3.0); sweepStepSpin_->setValue(0.5);
  } else if (expId_ == "SC-03") {
    headingSpin_->setValue(30.0); kpSpin_->setValue(1.5); kiSpin_->setValue(0.10);
    kdSpin_->setValue(0.5); simSecSpin_->setValue(45.0);
    disturbCheck_->setChecked(true);
    windSpin_->setValue(6.0); currentSpin_->setValue(0.4); currentDirSpin_->setValue(90.0);
  } else if (expId_ == "SC-04") {
    headingSpin_->setValue(45.0); kpSpin_->setValue(1.5); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.5); simSecSpin_->setValue(40.0);
  } else if (expId_ == "SC-05") {
    speedSpin_->setValue(1.2); kpSpin_->setValue(40.0); kiSpin_->setValue(2.0);
    kdSpin_->setValue(0.0); simSecSpin_->setValue(40.0);
  } else if (expId_ == "SC-06") {
    headingSpin_->setValue(0.0); kpSpin_->setValue(1.8); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.6); lookaheadSpin_->setValue(2.0); simSecSpin_->setValue(120.0);
    fillWaypointTable({{0.0, 0.0}, {15.0, 0.0}, {30.0, 10.0}, {45.0, 10.0}});
  } else if (expId_ == "SC-07") {
    targetXSpin_->setValue(10.0); targetYSpin_->setValue(5.0);
    kpSpin_->setValue(2.0); kiSpin_->setValue(0.10); kdSpin_->setValue(0.6);
    simSecSpin_->setValue(70.0);
  } else if (expId_ == "SC-09") {
    headingSpin_->setValue(30.0); kpSpin_->setValue(1.5); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.5); simSecSpin_->setValue(40.0);
    probeBaseSpin_->setValue(100.0); probeDiffSpin_->setValue(20.0);
  } else if (expId_ == "SC-10") {
    headingSpin_->setValue(0.0); kpSpin_->setValue(1.8); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.6); lookaheadSpin_->setValue(2.5); simSecSpin_->setValue(150.0);
    fillWaypointTable({{0.0, 0.0}, {12.0, 0.0}, {24.0, 8.0}, {36.0, 8.0}, {48.0, 0.0}});
  } else if (expId_ == "SC-11") {
    headingSpin_->setValue(0.0); kpSpin_->setValue(2.0); kiSpin_->setValue(0.05);
    kdSpin_->setValue(0.6); simSecSpin_->setValue(90.0);
    obsXSpin_->setValue(30.0); obsYSpin_->setValue(0.0); obsRSpin_->setValue(1.5);
  } else if (expId_ == "SC-12") {
    headingSpin_->setValue(0.0); kpSpin_->setValue(2.0); kiSpin_->setValue(0.10);
    kdSpin_->setValue(0.6); simSecSpin_->setValue(70.0);
    formXSpin_->setValue(-3.0); formYSpin_->setValue(0.0);
  }
  chart_->setYawTarget(headingSpin_->value());
}

void ControlExpDialog::updateVisibility() {
  // 每个 expId 只显示**真正起作用**的控件组（避免"能调但无效"的假控件）
  const bool showPid = true;   // 所有实验都有 PID 参数（SC-05 是航速环增益）
  const bool showHeading = (expId_ == "SC-01" || expId_ == "SC-02" || expId_ == "SC-03" ||
                            expId_ == "SC-04" || expId_ == "SC-09" || expId_ == "SC-11");
  const bool showSpeed = (expId_ == "SC-05" || expId_ == "SC-07" || expId_ == "SC-12");

  pidGroup_->setVisible(showPid);
  headingGroup_->setVisible(showHeading);
  speedGroup_->setVisible(showSpeed);
  pointGroup_->setVisible(expId_ == "SC-07");
  losGroup_->setVisible(expId_ == "SC-06" || expId_ == "SC-10");
  distGroup_->setVisible(expId_ == "SC-03");
  obsGroup_->setVisible(expId_ == "SC-11");
  formGroup_->setVisible(expId_ == "SC-12");
  sweepGroup_->setVisible(expId_ == "SC-02");
  sc08Group_->setVisible(expId_ == "SC-06");
  tuneGroup_->setVisible(expId_ == "SC-09");

  const bool hasWp = (expId_ == "SC-06" || expId_ == "SC-10");
  wpTable_->setVisible(hasWp);
  addWpBtn_->setVisible(hasWp);
  delWpBtn_->setVisible(hasWp);

  statusLabel_->setText(
      QStringLiteral("就绪。控制律：%1（%2 s 仿真时间）")
          .arg(QString::fromUtf8(keship::ControlLawName(LawForExp(expId_))))
          .arg(simSecSpin_->value(), 0, 'f', 0));
}

// ══════════════════════════════════════════════════════════════════════════
// 参数收集
// ══════════════════════════════════════════════════════════════════════════
keship::ControlParams ControlExpDialog::paramsFromUi() const {
  keship::ControlParams p;
  p.law = LawForExp(expId_);
  p.kp = kpSpin_->value();
  p.ki = kiSpin_->value();
  p.kd = kdSpin_->value();
  p.baseThrustN = baseSpin_->value();
  p.diffLimitN = limitSpin_->value();
  p.targetHeadingDeg = headingSpin_->value();
  p.targetSpeedMs = speedSpin_->value();
  p.targetXM = targetXSpin_->value();
  p.targetYM = targetYSpin_->value();
  p.lookaheadM = lookaheadSpin_->value();
  p.disturbance = disturbCheck_->isChecked();
  p.windMs = windSpin_->value();
  p.currentMs = currentSpin_->value();
  p.currentDirDeg = currentDirSpin_->value();
  p.useEstimatedPose = useSc08_;
  p.obstacleXM = obsXSpin_->value();
  p.obstacleYM = obsYSpin_->value();
  p.obstacleRadiusM = obsRSpin_->value();
  p.formationOffsetXM = formXSpin_->value();
  p.formationOffsetYM = formYSpin_->value();
  p.simSeconds = simSecSpin_->value();
  p.arriveRadiusM = keship::kDefaultArrivalRadiusM;
  return p;
}

std::vector<std::pair<double, double>> ControlExpDialog::waypointsFromTable() const {
  std::vector<std::pair<double, double>> wps;
  for (int r = 0; r < wpTable_->rowCount(); ++r) {
    const auto *x = wpTable_->item(r, 0);
    const auto *y = wpTable_->item(r, 1);
    if (!x || !y) continue;
    wps.emplace_back(x->text().toDouble(), y->text().toDouble());
  }
  return wps;
}

void ControlExpDialog::fillWaypointTable(
    const std::vector<std::pair<double, double>> &_wps) {
  wpTable_->setRowCount(static_cast<int>(_wps.size()));
  for (int r = 0; r < static_cast<int>(_wps.size()); ++r) {
    for (int c = 0; c < 2; ++c) {
      const double v = (c == 0) ? _wps[r].first : _wps[r].second;
      auto *it = new QTableWidgetItem(QString::number(v, 'f', 1));
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      wpTable_->setItem(r, c, it);
    }
  }
}

void ControlExpDialog::onAddWaypoint() {
  const int r = wpTable_->rowCount();
  wpTable_->insertRow(r);
  const double x = (r > 0) ? wpTable_->item(r - 1, 0)->text().toDouble() + 15.0 : 0.0;
  const double y = (r > 0) ? wpTable_->item(r - 1, 1)->text().toDouble() : 0.0;
  auto *ix = new QTableWidgetItem(QString::number(x, 'f', 1));
  auto *iy = new QTableWidgetItem(QString::number(y, 'f', 1));
  ix->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  iy->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
  wpTable_->setItem(r, 0, ix);
  wpTable_->setItem(r, 1, iy);
}

void ControlExpDialog::onRemoveWaypoint() {
  if (wpTable_->rowCount() <= 0) return;
  wpTable_->removeRow(wpTable_->rowCount() - 1);
}

// ══════════════════════════════════════════════════════════════════════════
// 阶段编排（11 个 expId 的差异只在这里）
// ══════════════════════════════════════════════════════════════════════════
std::vector<ControlExpDialog::Phase> ControlExpDialog::buildPhases() const {
  std::vector<Phase> out;
  const auto wps = waypointsFromTable();
  const double sec = simSecSpin_->value();

  if (expId_ == "SC-01") {
    Phase open;
    open.label = QStringLiteral("开环（固定推力，无反馈）");
    open.params = paramsFromUi();
    open.params.law = keship::ControlLaw::kOpenLoop;
    open.params.openLoopDiffN = 0.0;
    open.seconds = sec;
    out.push_back(open);

    Phase closed;
    closed.label = QStringLiteral("闭环（航向 PID 差动）");
    closed.params = paramsFromUi();
    closed.params.law = keship::ControlLaw::kPidHeading;
    closed.seconds = sec;
    out.push_back(closed);
    return out;
  }

  if (expId_ == "SC-02" && sweepCheck_->isChecked()) {
    const int which = sweepCombo_->currentData().toInt();
    const double from = sweepFromSpin_->value();
    const double to = sweepToSpin_->value();
    const double step = std::max(0.01, sweepStepSpin_->value());
    const QString pname = sweepCombo_->currentText();
    int guard = 0;
    for (double v = from; (step > 0 ? v <= to + 1e-9 : v >= to - 1e-9) && guard < 20;
         v += step, ++guard) {
      Phase ph;
      ph.params = paramsFromUi();
      ph.seconds = sec;
      if (which == 0) ph.params.kp = v;
      else if (which == 1) ph.params.ki = v;
      else ph.params.kd = v;
      ph.label = QStringLiteral("%1 = %2").arg(pname).arg(v, 0, 'f', 3);
      ph.wps = wps;
      out.push_back(ph);
      // 记录扫描值（beginPhase 时通过 sweptValue_ 传递）
    }
    if (!out.empty()) return out;
  }

  if (expId_ == "SC-03") {
    Phase base;
    base.label = QStringLiteral("无扰动（基准）");
    base.params = paramsFromUi();
    base.params.disturbance = false;
    base.seconds = sec;
    out.push_back(base);

    Phase dist;
    dist.label = QStringLiteral("有扰动（风 %1 m/s · 流 %2 m/s）")
                     .arg(windSpin_->value(), 0, 'f', 1)
                     .arg(currentSpin_->value(), 0, 'f', 2);
    dist.params = paramsFromUi();
    dist.params.disturbance = true;
    dist.seconds = sec;
    out.push_back(dist);
    return out;
  }

  if (expId_ == "SC-09") {
    // ① 开环阶跃识别（不跑满：识别只需要看到响应拐点）
    Phase probe;
    probe.label = QStringLiteral("① 开环阶跃识别");
    probe.params = paramsFromUi();
    probe.params.law = keship::ControlLaw::kOpenLoop;
    probe.params.baseThrustN = probeBaseSpin_->value();
    probe.params.openLoopDiffN = probeDiffSpin_->value();
    probe.seconds = std::min(sec, 25.0);
    out.push_back(probe);
    // ② 验证阶段由 finishPhase() 在整定完成后追加
    return out;
  }

  Phase single;
  single.label = QStringLiteral("单轮运行");
  single.params = paramsFromUi();
  single.seconds = sec;
  single.wps = wps;
  out.push_back(single);
  return out;
}

void ControlExpDialog::beginPhase(const Phase &_ph) {
  current_ = _ph;
  sweptValue_ = std::nan("");
  if (expId_ == "SC-02" && sweepCheck_->isChecked()) {
    const int which = sweepCombo_->currentData().toInt();
    sweptValue_ = (which == 0) ? _ph.params.kp : (which == 1 ? _ph.params.ki : _ph.params.kd);
  }

  core_->setTarget(modelCombo_->currentText().toStdString(),
                   thrLEdit_->text().trimmed().toStdString(),
                   thrREdit_->text().trimmed().toStdString());
  core_->setParams(_ph.params);
  core_->setWaypoints(_ph.wps);
  core_->setSamplePeriod(periodSpin_->value());
  applySc08DriftToCore();

  if (!core_->start()) {
    QMessageBox::warning(this, ExpTitle(expId_),
        QStringLiteral("启动失败：未能读到船模位姿。\n请确认「船模 / 推进器名」与场景一致，"
                       "且仿真正在运行。"));
    running_ = false;
    setRunningUi(false);
    return;
  }

  chart_->clearTrail();
  // 目标线只在「目标本身就是 yaw」的控制律下画（LOS/DP/编队算的是航迹向，画了会误导）
  if (_ph.params.law == keship::ControlLaw::kPidHeading ||
      _ph.params.law == keship::ControlLaw::kOpenLoop ||
      _ph.params.law == keship::ControlLaw::kPidSpeed) {
    chart_->setYawTarget(_ph.params.targetHeadingDeg);
  } else {
    chart_->clearYawTarget();
  }
  running_ = true;
  setRunningUi(true);
  const int ms = std::max(20, static_cast<int>(periodSpin_->value() * 1000.0));
  timer_->start(ms);
  statusLabel_->setText(QStringLiteral("运行中：%1（%2 / %3），%4 s 仿真时间…")
                            .arg(_ph.label)
                            .arg(phaseIndex_ + 1)
                            .arg(static_cast<int>(queue_.size()))
                            .arg(_ph.seconds, 0, 'f', 0));
  std::cout << "SC: " << expId_.toStdString() << " 开始 " << _ph.label.toStdString()
            << "（law=" << keship::ControlLawKey(_ph.params.law) << "）" << std::endl;
}

void ControlExpDialog::finishPhase() {
  timer_->stop();
  phaseTrail_ = core_->trail();

  RunRow row;
  row.label = current_.label;
  row.sweptValue = sweptValue_;
  row.params = current_.params;
  row.m = core_->metrics();
  row.elapsedS = core_->elapsed();
  runs_.push_back(row);

  std::cout << "SC: " << expId_.toStdString() << " 完成 " << row.label.toStdString()
            << " headingRmse=" << row.m.headingRmseDeg
            << " lateralRms=" << row.m.lateralRmsM
            << " meanThrust=" << row.m.meanThrustN
            << " points=" << phaseTrail_.size() << std::endl;

  // SC-09：识别完立刻整定（可选追加验证阶段）
  if (expId_ == "SC-09" && phaseIndex_ == 0) {
    const double stepN = 2.0 * probeDiffSpin_->value();   // Δt = R − L
    const auto fit = keship::FitFirstOrderPlusDelay(phaseTrail_, stepN);
    const auto gains = keship::ZieglerNicholsPid(fit);
    if (gains.valid) {
      kpSpin_->setValue(gains.kp);
      kiSpin_->setValue(gains.ki);
      kdSpin_->setValue(gains.kd);
      baseSpin_->setValue(probeBaseSpin_->value());
      tuneLabel_->setText(QStringLiteral("K=%1 °/N · L=%2 s · T=%3 s ⇒ Kp=%4 Ki=%5 Kd=%6")
                              .arg(fit.gainDegPerN, 0, 'f', 3)
                              .arg(fit.deadTimeS, 0, 'f', 2)
                              .arg(fit.timeConstS, 0, 'f', 2)
                              .arg(gains.kp, 0, 'f', 3)
                              .arg(gains.ki, 0, 'f', 4)
                              .arg(gains.kd, 0, 'f', 3));
      if (tuneOnly_) {
        queue_.clear();          // 「只整定」：不再跑验证
      } else {
        Phase verify;
        verify.label = QStringLiteral("② 整定值闭环验证");
        verify.params = paramsFromUi();          // 已含新增益
        verify.params.law = keship::ControlLaw::kPidHeading;
        verify.seconds = simSecSpin_->value();
        queue_.push_back(verify);
      }
      std::cout << "SC-09: 整定 Kp=" << gains.kp << " Ki=" << gains.ki
                << " Kd=" << gains.kd << std::endl;
    } else {
      tuneLabel_->setText(QStringLiteral("识别失败：开环响应不足（Δ 太小 / 时长太短），"
                                         "请增大差动量或仿真时长后重试"));
      queue_.clear();
    }
  }

  refreshRunTable();
  refreshMetrics();

  ++phaseIndex_;
  if (phaseIndex_ < static_cast<int>(queue_.size())) {
    beginPhase(queue_[phaseIndex_]);
    return;
  }
  settleAll();
}

void ControlExpDialog::settleAll() {
  running_ = false;
  timer_->stop();
  setRunningUi(false);
  const auto &last = runs_.back();
  statusLabel_->setText(
      QStringLiteral("完成 %1 轮。最后一轮「%2」：航向 RMSE %3°、稳态误差 %4°、平均推力 %5 N。")
          .arg(runs_.size())
          .arg(last.label)
          .arg(Fmt(last.m.headingRmseDeg, 3))
          .arg(Fmt(last.m.headingSteadyErrDeg, 3))
          .arg(Fmt(last.m.meanThrustN, 1)));
}

void ControlExpDialog::setRunningUi(bool _running) {
  runBtn_->setEnabled(!_running);
  stopBtn_->setEnabled(_running);
  exportBtn_->setEnabled(!_running && !runs_.empty());
  modelCombo_->setEnabled(!_running);
  thrLEdit_->setEnabled(!_running);
  thrREdit_->setEnabled(!_running);
  simSecSpin_->setEnabled(!_running);
  tuneBtn_->setEnabled(!_running);
  sc08BrowseBtn_->setEnabled(!_running);
}

// ══════════════════════════════════════════════════════════════════════════
// 槽
// ══════════════════════════════════════════════════════════════════════════
void ControlExpDialog::onStart() {
  if (running_) return;
  runs_.clear();
  runTable_->setRowCount(0);
  scoreLabel_->clear();
  tuneOnly_ = false;
  queue_ = buildPhases();
  if (queue_.empty()) return;
  phaseIndex_ = 0;
  beginPhase(queue_.front());
}

void ControlExpDialog::onStop() {
  if (!running_) return;
  timer_->stop();
  core_->stop();
  // 手动停止：只保留当前这一轮，不再推进后续阶段
  if (phaseIndex_ >= 0 && phaseIndex_ + 1 < static_cast<int>(queue_.size()))
    queue_.resize(phaseIndex_ + 1);
  if (running_) finishPhase();
}

void ControlExpDialog::onTick() {
  if (!core_->running()) {
    if (running_) finishPhase();
    return;
  }
  if (!core_->sample()) return;
  chart_->setTrail(core_->trail());
  updateReadout();
  if (!core_->running()) finishPhase();   // 到时自动停（core 在 simSeconds 处 stop）
}

void ControlExpDialog::updateReadout() {
  const auto &t = core_->trail();
  if (t.empty()) return;
  const auto &s = t.back();
  QString txt = QStringLiteral("t=%1 s   yaw=%2°   v=%3 m/s   误差=%4°   横向=%5 m   "
                               "L=%6 N  R=%7 N")
                    .arg(s.t, 0, 'f', 1)
                    .arg(s.yawDeg, 0, 'f', 2)
                    .arg(s.speedMs, 0, 'f', 2)
                    .arg(s.headingErrDeg, 0, 'f', 2)
                    .arg(s.lateralErrM, 0, 'f', 2)
                    .arg(s.thrustL, 0, 'f', 1)
                    .arg(s.thrustR, 0, 'f', 1);
  if (expId_ == "SC-06" && useSc08_) {
    txt += QStringLiteral("   | 真值 %1° / 估计 %2°（差 %3°）")
               .arg(core_->lastTrueYawDeg(), 0, 'f', 2)
               .arg(core_->lastEstimatedYawDeg(), 0, 'f', 2)
               .arg(core_->lastEstimatedYawDeg() - core_->lastTrueYawDeg(), 0, 'f', 2);
  }
  statusLabel_->setText(txt);
}

void ControlExpDialog::refreshMetrics() {
  if (runs_.empty()) return;
  const auto &m = runs_.back().m;
  QString txt = QStringLiteral("指标（%1）：").arg(runs_.back().label);
  for (const auto &f : kMetricFields) {
    txt += QStringLiteral("\n  %1 = %2").arg(QString::fromUtf8(f.cn))
               .arg(Fmt(m.*(f.member), f.dec));
  }
  metricsLabel_->setText(txt);

  // SC-10 / SC-06 / SC-07 / SC-12 额外给 TaskScoring 评分（可复核的三指标加权）
  keship::TaskMetrics tm;
  tm.waypointsReached = core_->waypointsReached();
  tm.totalWaypoints = core_->waypointsTotal();
  tm.maxLateralDev = std::max(0.0, m.maxLateralM);
  tm.rmsDrift = std::max(0.0, m.dpHoldErrM);
  tm.meanPosError = std::max(0.0, m.dpHoldErrM);
  tm.meanHeadingErr = std::max(0.0, m.headingSteadyErrDeg);
  tm.meanThrust = std::max(0.0, m.meanThrustN);
  tm.maxThrust = std::max(1.0, baseSpin_->value() + limitSpin_->value());
  tm.elapsedSec = runs_.back().elapsedS;
  tm.timeLimit = simSecSpin_->value();

  QString scorer;
  keship::ScoreResult sr;
  if (expId_ == "SC-10") { sr = keship::TaskScoring::computeWaypoint(tm); scorer = QStringLiteral("航路点任务"); }
  else if (expId_ == "SC-06") { sr = keship::TaskScoring::computeTracking(tm); scorer = QStringLiteral("循迹任务"); }
  else if (expId_ == "SC-07" || expId_ == "SC-12") { sr = keship::TaskScoring::computeDp(tm); scorer = QStringLiteral("DP 任务"); }
  if (sr.valid) {
    scoreLabel_->setText(QStringLiteral("%1得分：%2 / 1.000（任务评分，见 TaskScoring）")
                             .arg(scorer).arg(sr.total, 0, 'f', 3));
  } else {
    scoreLabel_->clear();
  }
}

void ControlExpDialog::refreshRunTable() {
  auto fmt = [](double _v, int _dec) { return Fmt(_v, _dec); };
  runTable_->setRowCount(static_cast<int>(runs_.size()));
  for (int i = 0; i < static_cast<int>(runs_.size()); ++i) {
    const auto &r = runs_[i];
    auto set = [&](int c, const QString &t) {
      auto *it = new QTableWidgetItem(t);
      it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      runTable_->setItem(i, c, it);
    };
    runTable_->setItem(i, 0, new QTableWidgetItem(r.label));
    set(1, std::isnan(r.sweptValue) ? QStringLiteral("—")
                                    : QString::number(r.sweptValue, 'f', 3));
    set(2, fmt(r.m.headingRmseDeg, 3));
    set(3, fmt(r.m.lateralRmsM, 3));
    const double arrive = (r.m.arrivalRate >= 0.0) ? r.m.arrivalRate
                                                   : (r.m.dpHoldErrM >= 0.0 ? r.m.dpHoldErrM
                                                                            : r.m.formationErrM);
    set(4, fmt(arrive, 3));
    set(5, fmt(r.m.meanThrustN, 1));
  }
}

void ControlExpDialog::onExport() {
  if (runs_.empty()) {
    QMessageBox::information(this, ExpTitle(expId_),
        QStringLiteral("还没有结果。请先点「开始」跑一轮。"));
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("导出 %1 交付物").arg(expId_),
      defaultFileName(expId_), QStringLiteral("JSON (*.json)"));
  if (path.isEmpty()) return;
  QString err;
  if (!exportTo(path)) {
    QMessageBox::warning(this, ExpTitle(expId_),
                         QStringLiteral("导出失败：%1").arg(err));
    return;
  }
  QMessageBox::information(this, ExpTitle(expId_),
                           QStringLiteral("已导出：%1").arg(path));
}

void ControlExpDialog::onPickSc08() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("导入 SC-08 位姿估计"),
      QStringLiteral("sc08_estimation.csv"),
      QStringLiteral("SC-08 交付物 (*.csv *.json);;所有文件 (*)"));
  if (path.isEmpty()) return;
  QString err;
  if (!importSc08(path, &err)) {
    QMessageBox::warning(this, ExpTitle(expId_), err);
    return;
  }
  sc08PathEdit_->setText(path);
}

void ControlExpDialog::onAutoTune() {
  if (running_) return;
  if (expId_ != "SC-09") return;
  // 只跑识别阶段：跑完自动填 Kp/Ki/Kd，不追加验证阶段（验证请点「开始」）
  runs_.clear();
  runTable_->setRowCount(0);
  scoreLabel_->clear();
  queue_.clear();
  Phase probe;
  probe.label = QStringLiteral("① 开环阶跃识别（只整定）");
  probe.params = paramsFromUi();
  probe.params.law = keship::ControlLaw::kOpenLoop;
  probe.params.baseThrustN = probeBaseSpin_->value();
  probe.params.openLoopDiffN = probeDiffSpin_->value();
  probe.seconds = std::min(simSecSpin_->value(), 25.0);
  queue_.push_back(probe);
  phaseIndex_ = 0;
  tuneOnly_ = true;
  beginPhase(probe);
}

// ══════════════════════════════════════════════════════════════════════════
// SC-08 导入（L7）：只认真实字段，读不到就报错，不猜、不造假
// ══════════════════════════════════════════════════════════════════════════
bool ControlExpDialog::importSc08(const QString &_path, QString *_err) {
  auto fail = [&](const QString &_msg) {
    if (_err) *_err = _msg;
    std::cout << "SC-06: 导入 SC-08 失败 —— " << _msg.toStdString() << std::endl;
    return false;
  };

  QFile f(_path);
  if (!f.open(QIODevice::ReadOnly)) {
    return fail(QStringLiteral("无法打开文件：%1").arg(_path));
  }
  const QByteArray raw = f.readAll();
  f.close();

  std::vector<std::pair<double, double>> drift;   // (t, 估计−真值 °)

  if (QFileInfo(_path).suffix().compare("json", Qt::CaseInsensitive) == 0) {
    // keship.exp.v1 交付物：先校验 schema + expId，再读 extra 序列
    keship::ExpDeliverable d;
    std::string err;
    if (!keship::ReadExpDeliverable(_path.toStdString(), d, &err)) {
      return fail(QStringLiteral("不是合法的 keship.exp.v1 交付物：%1")
                      .arg(QString::fromStdString(err)));
    }
    if (!keship::ValidExpDeliverable(d, "SC-08", &err)) {
      return fail(QStringLiteral("expId 校验失败（本实验只接受 SC-08）：%1")
                      .arg(QString::fromStdString(err)));
    }
    const QJsonArray samples = d.extra.value("samples").toArray();
    if (samples.isEmpty()) {
      // 如实报错：把**实际存在的键**列出来，绝不猜键名编数据
      QStringList ek = d.extra.keys();
      QStringList mk = d.metrics.keys();
      return fail(QStringLiteral(
          "SC-08 交付物里没有位姿序列（期望 extra.samples = "
          "[{t, est_yaw, ref_yaw}]，与 SC-08 现有 CSV 表头同名）。\n"
          "实际 extra 键：[%1]\n实际 metrics 键：[%2]")
          .arg(ek.join(QStringLiteral(", ")))
          .arg(mk.join(QStringLiteral(", "))));
    }
    for (const auto &v : samples) {
      const QJsonObject o = v.toObject();
      const bool hasSnake = o.contains("est_yaw") && o.contains("ref_yaw");
      const bool hasCamel = o.contains("estYaw") && o.contains("refYaw");
      if (!hasSnake && !hasCamel) {
        return fail(QStringLiteral(
            "extra.samples 的元素缺少 est_yaw / ref_yaw（实际键：[%1]）")
            .arg(QStringList(o.keys()).join(QStringLiteral(", "))));
      }
      const double t = o.value("t").toDouble();
      const double est = hasSnake ? o.value("est_yaw").toDouble() : o.value("estYaw").toDouble();
      const double ref = hasSnake ? o.value("ref_yaw").toDouble() : o.value("refYaw").toDouble();
      drift.emplace_back(t, keship::WrapDeg180(est - ref));
    }
  } else {
    // SC-08 现有导出（SensorEstimationDialog「导出 CSV」）：
    //   表头 t_s,est_roll,est_pitch,est_yaw,ref_roll,ref_pitch,ref_yaw,gx,gy,gz
    const QStringList lines = QString::fromUtf8(raw).split('\n', Qt::SkipEmptyParts);
    if (lines.size() < 3) return fail(QStringLiteral("CSV 行数不足（需表头 + ≥2 行数据）"));
    const QStringList head = lines.front().trimmed().split(',');
    const int iT = head.indexOf(QStringLiteral("t_s"));
    const int iEst = head.indexOf(QStringLiteral("est_yaw"));
    const int iRef = head.indexOf(QStringLiteral("ref_yaw"));
    if (iT < 0 || iEst < 0 || iRef < 0) {
      return fail(QStringLiteral(
          "CSV 表头缺少 t_s / est_yaw / ref_yaw（实际表头：%1）").arg(lines.front()));
    }
    for (int i = 1; i < lines.size(); ++i) {
      const QStringList c = lines[i].trimmed().split(',');
      if (c.size() <= std::max({iT, iEst, iRef})) continue;
      const double t = c[iT].toDouble();
      const double est = c[iEst].toDouble();
      const double ref = c[iRef].toDouble();
      drift.emplace_back(t, keship::WrapDeg180(est - ref));
    }
  }

  if (drift.size() < 2) {
    return fail(QStringLiteral("可用数据点不足 2 个（实际 %1）").arg(drift.size()));
  }
  std::sort(drift.begin(), drift.end(),
            [](const std::pair<double, double> &_a, const std::pair<double, double> &_b) {
              return _a.first < _b.first;
            });
  double dmin = drift.front().second, dmax = drift.front().second;
  for (const auto &p : drift) {
    dmin = std::min(dmin, p.second);
    dmax = std::max(dmax, p.second);
  }

  sc08Drift_ = drift;
  sc08File_ = _path;
  useSc08_ = true;
  applySc08DriftToCore();

  sc08InfoLabel_->setText(
      QStringLiteral("已导入 %1 点（t=%2–%3 s，漂移 %4°…%5°）。"
                     "\n航向反馈改用 SC-08 估计值；**位置仍用真值**（SC-08 不导出位置）。")
          .arg(drift.size())
          .arg(drift.front().first, 0, 'f', 1)
          .arg(drift.back().first, 0, 'f', 1)
          .arg(dmin, 0, 'f', 2)
          .arg(dmax, 0, 'f', 2));
  std::cout << "SC-06: 已导入 SC-08 漂移 " << drift.size() << " 点，范围 ["
            << dmin << ", " << dmax << "]°" << std::endl;
  return true;
}

void ControlExpDialog::applySc08DriftToCore() {
  if (!core_) return;
  if (useSc08_ && sc08Drift_.size() >= 2) core_->setYawEstimateDrift(sc08Drift_);
  else core_->setYawEstimateDrift({});
}

// ══════════════════════════════════════════════════════════════════════════
// 交付物
// ══════════════════════════════════════════════════════════════════════════
QString ControlExpDialog::defaultFileName(const QString &_expId) {
  if (_expId == "SC-01") return QStringLiteral("sc01_openloop_closedloop.json");
  if (_expId == "SC-02") return QStringLiteral("sc02_pid_param_effects.json");
  if (_expId == "SC-03") return QStringLiteral("sc03_disturbance.json");
  if (_expId == "SC-04") return QStringLiteral("sc04_heading_control.json");
  if (_expId == "SC-05") return QStringLiteral("sc05_speed_control.json");
  if (_expId == "SC-06") return QStringLiteral("sc06_los_tracking.json");
  if (_expId == "SC-07") return QStringLiteral("sc07_dynamic_positioning.json");
  if (_expId == "SC-09") return QStringLiteral("sc09_pid_autotune.json");
  if (_expId == "SC-10") return QStringLiteral("sc10_waypoint_following.json");
  if (_expId == "SC-11") return QStringLiteral("sc11_colregs.json");
  if (_expId == "SC-12") return QStringLiteral("sc12_formation.json");
  return QStringLiteral("sc_experiment.json");
}

bool ControlExpDialog::buildDeliverable(keship::ExpDeliverable &_d,
                                        std::string *_err) const {
  if (runs_.empty()) {
    if (_err) *_err = "尚无运行结果";
    return false;
  }
  _d.schema = keship::kExpSchemaV1;
  _d.expId = expId_.toStdString();
  _d.createdAt = keship::NowIso8601();

  _d.object["type"] = "ksm";
  _d.object["name"] = modelCombo_->currentText();
  _d.object["world"] = QString::fromStdString(worldName_);

  const auto &p = current_.params;
  _d.condition["disturbance"] = p.disturbance;
  _d.condition["windMs"] = p.windMs;
  _d.condition["currentMs"] = p.currentMs;
  _d.condition["currentDirDeg"] = p.currentDirDeg;
  _d.condition["useEstimatedPose"] = useSc08_;
  _d.condition["simSeconds"] = p.simSeconds;
  _d.condition["samplePeriodS"] = periodSpin_->value();

  _d.params["thrusterLeft"] = thrLEdit_->text();
  _d.params["thrusterRight"] = thrREdit_->text();
  _d.params["controlLaw"] = QString::fromUtf8(keship::ControlLawKey(p.law));
  _d.params["kp"] = p.kp;
  _d.params["ki"] = p.ki;
  _d.params["kd"] = p.kd;
  _d.params["baseThrustN"] = p.baseThrustN;
  _d.params["diffLimitN"] = p.diffLimitN;
  _d.params["targetHeadingDeg"] = p.targetHeadingDeg;
  _d.params["targetSpeedMs"] = p.targetSpeedMs;
  _d.params["targetXM"] = p.targetXM;
  _d.params["targetYM"] = p.targetYM;
  _d.params["lookaheadM"] = p.lookaheadM;
  _d.params["obstacleXM"] = p.obstacleXM;
  _d.params["obstacleYM"] = p.obstacleYM;
  _d.params["obstacleRadiusM"] = p.obstacleRadiusM;
  _d.params["formationOffsetXM"] = p.formationOffsetXM;
  _d.params["formationOffsetYM"] = p.formationOffsetYM;
  _d.params["openLoopDiffN"] = p.openLoopDiffN;
  _d.params["posGainPerS"] = p.posGainPerS;
  _d.params["arriveRadiusM"] = p.arriveRadiusM;
  if (!current_.wps.empty()) {
    QJsonArray wps;
    for (const auto &w : current_.wps) {
      QJsonObject o;
      o["x"] = w.first;
      o["y"] = w.second;
      wps.append(o);
    }
    _d.params["waypoints"] = wps;
  }

  // metrics：ControlMetrics **全部字段**（键名逐字见报告 §3）
  const auto &m = runs_.back().m;
  for (const auto &f : kMetricFields) {
    _d.metrics[QString::fromUtf8(f.key)] = SafeDouble(m.*(f.member));
  }
  _d.metrics["valid"] = m.valid;
  _d.metrics["points"] = static_cast<int>(phaseTrail_.size());
  _d.metrics["phases"] = static_cast<int>(runs_.size());
  _d.metrics["elapsedS"] = runs_.back().elapsedS;
  _d.metrics["yawCourseOffsetDeg"] = core_->yawCourseOffsetDeg();

  // TaskScoring（SC-06/07/10/12 才有意义）
  {
    keship::TaskMetrics tm;
    tm.waypointsReached = core_->waypointsReached();
    tm.totalWaypoints = core_->waypointsTotal();
    tm.maxLateralDev = std::max(0.0, m.maxLateralM);
    tm.rmsDrift = std::max(0.0, m.dpHoldErrM);
    tm.meanPosError = std::max(0.0, m.dpHoldErrM);
    tm.meanHeadingErr = std::max(0.0, m.headingSteadyErrDeg);
    tm.meanThrust = std::max(0.0, m.meanThrustN);
    tm.maxThrust = std::max(1.0, p.baseThrustN + p.diffLimitN);
    tm.elapsedSec = runs_.back().elapsedS;
    tm.timeLimit = p.simSeconds;
    keship::ScoreResult sr;
    if (expId_ == "SC-10") sr = keship::TaskScoring::computeWaypoint(tm);
    else if (expId_ == "SC-06") sr = keship::TaskScoring::computeTracking(tm);
    else if (expId_ == "SC-07" || expId_ == "SC-12") sr = keship::TaskScoring::computeDp(tm);
    if (sr.valid) {
      _d.metrics["scoreTotal"] = sr.total;
      QJsonObject indiv;
      for (const auto &kv : sr.individual) indiv[QString::fromStdString(kv.first)] = kv.second;
      _d.extra["score"] = indiv;
    }
  }

  // extra.series：时程（上限 2000 点，超了等间隔抽稀）
  {
    const size_t n = phaseTrail_.size();
    const size_t cap = 2000;
    const size_t stride = (n + cap - 1) / cap > 1 ? (n + cap - 1) / cap : 1;
    QJsonArray t, yaw, lat, tl, tr, spd;
    for (size_t i = 0; i < n; i += stride) {
      t.append(phaseTrail_[i].t);
      yaw.append(phaseTrail_[i].yawDeg);
      lat.append(phaseTrail_[i].lateralErrM);
      tl.append(phaseTrail_[i].thrustL);
      tr.append(phaseTrail_[i].thrustR);
      spd.append(phaseTrail_[i].speedMs);
    }
    QJsonObject series;
    series["t"] = t;
    series["yaw"] = yaw;
    series["lateral"] = lat;
    series["thrustL"] = tl;
    series["thrustR"] = tr;
    series["speed"] = spd;
    series["phase"] = current_.label;
    series["sourcePoints"] = static_cast<int>(n);
    series["stride"] = static_cast<int>(stride);
    _d.extra["series"] = series;
  }

  // extra.runs：多阶段逐轮指标（SC-01 对照 / SC-02 扫描 / SC-09 识别+验证）
  {
    QJsonArray arr;
    for (const auto &r : runs_) {
      QJsonObject o;
      o["label"] = r.label;
      if (!std::isnan(r.sweptValue)) o["sweptValue"] = r.sweptValue;
      o["kp"] = r.params.kp;
      o["ki"] = r.params.ki;
      o["kd"] = r.params.kd;
      o["controlLaw"] = QString::fromUtf8(keship::ControlLawKey(r.params.law));
      o["elapsedS"] = r.elapsedS;
      for (const auto &f : kMetricFields)
        o[QString::fromUtf8(f.key)] = SafeDouble(r.m.*(f.member));
      arr.append(o);
    }
    _d.extra["runs"] = arr;
  }

  // extra.sc08：L7 溯源（只有 SC-06 且确实导入过才有）
  if (expId_ == "SC-06") {
    QJsonObject o;
    o["imported"] = useSc08_;
    if (useSc08_) {
      o["file"] = QFileInfo(sc08File_).fileName();
      o["points"] = static_cast<int>(sc08Drift_.size());
      o["tBeginS"] = sc08Drift_.front().first;
      o["tEndS"] = sc08Drift_.back().first;
      double dmin = sc08Drift_.front().second, dmax = dmin;
      for (const auto &v : sc08Drift_) {
        dmin = std::min(dmin, v.second);
        dmax = std::max(dmax, v.second);
      }
      o["driftMinDeg"] = dmin;
      o["driftMaxDeg"] = dmax;
      o["note"] = QStringLiteral("航向反馈用 SC-08 估计（真值+漂移）；位置仍为真值");
    } else {
      o["note"] = QStringLiteral("未导入 SC-08 —— 航向反馈用位姿真值（非 L7 模式）");
    }
    _d.extra["sc08"] = o;
  }

  return true;
}

bool ControlExpDialog::exportTo(const QString &_path) {
  keship::ExpDeliverable d;
  std::string err;
  if (!buildDeliverable(d, &err)) {
    std::cout << "SC: 导出失败 —— " << err << std::endl;
    return false;
  }
  if (!keship::WriteExpDeliverable(_path.toStdString(), d, &err)) {
    std::cout << "SC: 写文件失败 —— " << err << std::endl;
    return false;
  }
  std::cout << "SC: 已导出交付物 " << _path.toStdString()
            << "（expId=" << expId_.toStdString() << "）" << std::endl;
  return true;
}
