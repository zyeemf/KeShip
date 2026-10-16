/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-04 初稳性倾斜实验对话框实现 */
/// 2026.5.28

#include "IncliningExperimentDialog.hh"
#include "WavefieldPublisher.hh"
#include "../shared/UiTheme.hh"
#include "keship/utils/LinearDamping.hh"
#include "../shared/UiTheme.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QMessageBox>
#include <QFrame>
/// ★ 2026-09-21 新增：压载位置输入由 QDoubleSpinBox 改为 QLineEdit（自由编辑），
/// 并新增「移动压载」确认按钮 + 滑条防抖定时器。
#include <QLineEdit>
#include <QDoubleValidator>
#include <QTimer>
#include <QLabel>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <functional>
#include <thread>
#include <chrono>

#include <gz/math/Quaternion.hh>
#include <gz/msgs/entity_factory.pb.h>
#include <gz/msgs/entity.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/pose.pb.h>
#include <gz/msgs/scene.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/param.pb.h>
#include <gz/msgs/any.pb.h>

// ═══════════════════════════════════════════════════════════
// IncliningChartWidget
// ═══════════════════════════════════════════════════════════

IncliningChartWidget::IncliningChartWidget(QWidget *_parent)
    : QWidget(_parent)
{
  setMinimumSize(300, 200);
}

void IncliningChartWidget::setData(
    const std::vector<IncliningDataPoint> &_pts,
    double _expGM, double _thGM)
{
  pts_ = _pts;
  expGM_ = _expGM;
  thGM_ = _thGM;

  /// 动态范围（含负值适应）
  xMax_ = 100.0;
  yMin_ = 0.0;
  yMax_ = 0.05;
  for (const auto &p : pts_)
  {
    if (p.wDy > xMax_) xMax_ = p.wDy;
    if (p.tanPhi < yMin_) yMin_ = p.tanPhi;
    if (p.tanPhi > yMax_) yMax_ = p.tanPhi;
  }
  xMax_ = std::ceil(xMax_ * 1.2 / 10.0) * 10.0;
  double yRange = yMax_ - yMin_;
  yMin_ = std::floor(yMin_ * 1000.0) / 1000.0;
  yMax_ = std::ceil((yMax_ + yRange * 0.3) * 1000.0) / 1000.0;
  if (xMax_ < 20.0) xMax_ = 20.0;
  if (yMax_ - yMin_ < 0.005) { yMax_ = yMin_ + 0.005; }

  update();
}

void IncliningChartWidget::clearData()
{
  pts_.clear();
  expGM_ = 0.0;
  thGM_ = 0.0;
  xMin_ = 0.0;
  xMax_ = 100.0;
  yMin_ = 0.0;
  yMax_ = 0.05;
  update();
}

void IncliningChartWidget::paintEvent(QPaintEvent *_event)
{
  Q_UNUSED(_event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  int w = width();
  int h = height();
  if (w < 60 || h < 60) return;

  int ml = 65, mr = 30, mt = 20, mb = 45;
  int pw = w - ml - mr;
  int ph = h - mt - mb;

  auto toX = [&](double _v) -> int {
    return ml + (int)((_v - xMin_) / (xMax_ - xMin_) * pw);
  };
  auto toY = [&](double _v) -> int {
    return mt + ph - (int)((_v - yMin_) / (yMax_ - yMin_) * ph);
  };

  /// 背景
  painter.fillRect(rect(), Qt::white);

  /// 网格
  painter.setPen(QPen(QColor(220, 220, 220), 1, Qt::DashLine));
  for (int i = 0; i <= 4; ++i)
  {
    double xv = xMax_ * i / 4.0;
    int x = toX(xv);
    painter.drawLine(x, mt, x, mt + ph);
    double yv = yMin_ + (yMax_ - yMin_) * i / 4.0;
    int y = toY(yv);
    painter.drawLine(ml, y, w - mr, y);
  }

  /// 拟合线
  if (pts_.size() >= 2)
  {
    double slope = 0.0, intercept = 0.0;
    double sx = 0, sy = 0, sxy = 0, sx2 = 0;
    size_t n = pts_.size();
    for (const auto &p : pts_)
    {
      sx += p.wDy;
      sy += p.tanPhi;
      sxy += p.wDy * p.tanPhi;
      sx2 += p.wDy * p.wDy;
    }
    double denom = n * sx2 - sx * sx;
    if (std::abs(denom) > 1e-12)
    {
      slope = (n * sxy - sx * sy) / denom;
      intercept = (sy - slope * sx) / n;
      double x1 = 0.0, x2 = xMax_;
      double y1 = slope * x1 + intercept, y2 = slope * x2 + intercept;
      painter.setPen(QPen(QColor(200, 40, 40), 2));
      painter.drawLine(toX(x1), toY(y1), toX(x2), toY(y2));
    }
  }

  /// 数据点
  painter.setPen(QPen(QColor(40, 80, 200), 2));
  painter.setBrush(QColor(60, 120, 220));
  for (const auto &p : pts_)
  {
    painter.drawEllipse(QPoint(toX(p.wDy), toY(p.tanPhi)), 5, 5);
  }

  /// 坐标轴
  painter.setPen(QPen(Qt::black, 1));
  painter.drawLine(ml, mt, ml, mt + ph);
  painter.drawLine(ml, mt + ph, w - mr, mt + ph);

  /// 轴标签
  QFont axisFont;
  axisFont.setPointSize(9);
  painter.setFont(axisFont);
  painter.setPen(Qt::black);

  for (int i = 0; i <= 4; ++i)
  {
    double xv = xMax_ * i / 4.0;
    painter.drawText(QRect(toX(xv) - 25, mt + ph + 2, 50, 18),
                     Qt::AlignHCenter, QString::number(xv, 'f', 0));
  }
  painter.drawText(QRect(ml, mt + ph + 22, pw, 20),
                   Qt::AlignHCenter, "w·Δy (Nm)");

  for (int i = 0; i <= 4; ++i)
  {
    double yv = yMin_ + (yMax_ - yMin_) * i / 4.0;
    painter.drawText(QRect(2, toY(yv) - 8, ml - 6, 16),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(yv, 'f', 4));
  }
  painter.save();
  painter.translate(14, mt + ph / 2);
  painter.rotate(-90);
  painter.drawText(QRect(-50, -10, 100, 20), Qt::AlignHCenter, "tanφ");
  painter.restore();

  /// 图例
  int lx = w - mr - 170;
  int ly = mt + 6;

  painter.setPen(QPen(QColor(40, 80, 200), 2));
  painter.setBrush(QColor(60, 120, 220));
  painter.drawEllipse(QPoint(lx + 6, ly + 5), 5, 5);
  painter.setPen(Qt::black);
  painter.setBrush(Qt::NoBrush);
  painter.drawText(QRect(lx + 14, ly, 140, 14), "数据点");

  ly += 18;
  painter.setPen(QPen(QColor(200, 40, 40), 2));
  painter.drawLine(lx, ly + 5, lx + 12, ly + 5);
  painter.setPen(Qt::black);
  painter.drawText(QRect(lx + 14, ly, 140, 14), "线性拟合");

  ly += 18;
  if (expGM_ > 0.0)
    painter.drawText(QRect(lx, ly, 160, 14),
        QString("GM实验 = %1 m").arg(expGM_, 0, 'f', 4));
  else
    painter.drawText(QRect(lx, ly, 160, 14), "GM实验: 数据不足");

  ly += 16;
  if (thGM_ != 0.0)
    painter.drawText(QRect(lx, ly, 160, 14),
        QString("GM理论 = %1 m").arg(thGM_, 0, 'f', 4));
}

// ═══════════════════════════════════════════════════════════
// IncliningExperimentDialog
// ═══════════════════════════════════════════════════════════

IncliningExperimentDialog::IncliningExperimentDialog(
    gz::transport::Node &_transportNode,
    const std::string &_worldName,
    QWidget *_parent)
    : QDialog(_parent),
      transportNode_(_transportNode),
      worldName_(_worldName)
{
  setWindowTitle("HS-04 初稳性倾斜实验");
  setModal(false);
  resize(960, 720);

  poseState_ = std::make_shared<IncliningPoseState>();

  setupUI();
  initPoseSubscription();

  poseTimer_ = new QTimer(this);
  connect(poseTimer_, &QTimer::timeout,
          this, &IncliningExperimentDialog::onUpdatePose);
  poseTimer_->start(33);

  updateTheoryDisplay();

  // 静水波浪参数：PMS + gain=0（经进程内唯一发布器下发）
  {
    gz::msgs::Param msg; auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(0.0);  (*p)["gain"] = v;
    keship::WavefieldPublisher(_transportNode).Publish(msg);
  }

  std::cout << "HS-04: 初稳性倾斜实验对话框已创建" << std::endl;
}

IncliningExperimentDialog::~IncliningExperimentDialog()
{
  {
    std::lock_guard<std::mutex> lock(poseState_->mutex);
    poseState_->alive = false;
  }
  if (!poseTopic_.empty())
    transportNode_.Unsubscribe(poseTopic_);
  poseTimer_->stop();
  doDeleteBoth();

  // 恢复波浪：PMS + gain=1.0（经进程内唯一发布器下发）
  {
    gz::msgs::Param msg; auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(1.0);  (*p)["gain"] = v;
    keship::WavefieldPublisher(transportNode_).Publish(msg);
  }
}

void IncliningExperimentDialog::setupUI()
{
  auto *mainLayout = new QVBoxLayout(this);

  auto *descLabel = new QLabel(
      "<b>实验目的：</b>通过移动压载测量横倾角，反算初稳性高度 GM。<br>"
      "<b>原理：</b>GM = w·Δy / (Δ·tanφ)<br>"
      "<b>操作：</b>①点击[生成驳船] → ②拖动滑块移动压载 → "
      "③等待横倾角稳定 → ④记录数据 → ⑤重复多组 → 观察拟合线。",
      this);
  descLabel->setWordWrap(true);
  mainLayout->addWidget(descLabel);

  /// 中上：参数 + 控制
  auto *topRow = new QHBoxLayout();

  /// 左侧：驳船参数
  auto *paramGroup = new QGroupBox("驳船参数", this);
  auto *paramForm = new QFormLayout(paramGroup);

  lengthSpin_ = new QDoubleSpinBox(this);
  lengthSpin_->setRange(2.0, 20.0);
  lengthSpin_->setValue(6.0);
  lengthSpin_->setDecimals(2);
  lengthSpin_->setSuffix(" m");
  lengthSpin_->setSingleStep(0.5);
  paramForm->addRow("长度 L:", lengthSpin_);

  breadthSpin_ = new QDoubleSpinBox(this);
  breadthSpin_->setRange(0.5, 5.0);
  breadthSpin_->setValue(1.5);
  breadthSpin_->setDecimals(2);
  breadthSpin_->setSuffix(" m");
  breadthSpin_->setSingleStep(0.1);
  paramForm->addRow("宽度 B:", breadthSpin_);

  heightSpin_ = new QDoubleSpinBox(this);
  heightSpin_->setRange(0.3, 3.0);
  heightSpin_->setValue(1.0);
  heightSpin_->setDecimals(2);
  heightSpin_->setSuffix(" m");
  heightSpin_->setSingleStep(0.1);
  paramForm->addRow("型深 H:", heightSpin_);

  densitySpin_ = new QDoubleSpinBox(this);
  densitySpin_->setRange(100, 900);
  densitySpin_->setValue(500);
  densitySpin_->setDecimals(0);
  densitySpin_->setSuffix(" kg/m³");
  densitySpin_->setSingleStep(50);
  paramForm->addRow("密度 ρ:", densitySpin_);

  /// 理论值
  auto *theoryFrame = new QFrame(this);
  theoryFrame->setFrameStyle(QFrame::StyledPanel);
  auto *theoryForm = new QFormLayout(theoryFrame);
  draftLabel_ = new QLabel("— m", this);
  theoryForm->addRow("吃水 d:", draftLabel_);
  gmTheoryLabel_ = new QLabel("— m", this);
  gmTheoryLabel_->setStyleSheet("font-weight: bold;");
  theoryForm->addRow("GM (理论):", gmTheoryLabel_);
  paramForm->addRow(theoryFrame);

  spawnBtn_ = new QPushButton("生成驳船 + 压载块", this);
  spawnBtn_->setMinimumHeight(30);
  paramForm->addRow(spawnBtn_);

  topRow->addWidget(paramGroup);

  /// 右侧：压载控制
  auto *ctlGroup = new QGroupBox("压载控制", this);
  auto *ctlForm = new QFormLayout(ctlGroup);

  weightSlider_ = new QSlider(Qt::Horizontal, this);
  weightSlider_->setRange(0, 500);
  weightSlider_->setValue(250);
  sliderValueLabel_ = new QLabel("0.000 m", this);
  sliderValueLabel_->setStyleSheet("font-weight: bold;");

  /// ★ 2026-09-21：压载位置输入 **由 QDoubleSpinBox 改为 QLineEdit**。
  /// 为什么换掉 QDoubleSpinBox：
  ///   它把值**自动格式化成固定小数位**（`setDecimals(3)` → 显示 "0.000"），
  ///   于是"在小数点后插入一个 1"必然产生 4 位小数 → 被内部 validate 判非法 → **按键被吞**。
  ///   张老师实测："想把 0.000 改成 0.1，无法直接写一个 1"。
  ///   改 QLineEdit 后保留用户原文（初始 "0"），配 QDoubleValidator 控范围即可自由编辑。
  ///   小数位上限放宽到 6（>3）也是为此：允许 "0.1000" 这类中间文本存在。
  weightEdit_ = new QLineEdit("0", this);
  auto *wValidator = new QDoubleValidator(-0.25, 0.25, 6, this);
  wValidator->setNotation(QDoubleValidator::StandardNotation);
  weightEdit_->setValidator(wValidator);
  weightEdit_->setMaximumWidth(90);
  weightEdit_->setToolTip(
      "压载横向位置（m），范围 −0.25 ~ 0.25；输入后点『移动压载』才生效");
  weightEdit_->setPlaceholderText("0.1");

  applyBtn_ = new QPushButton("移动压载", this);
  applyBtn_->setToolTip("把压载移动到左侧输入框指定的位置（只有点这里才会真正移动）");

  auto *weightRow = new QHBoxLayout();
  weightRow->addWidget(weightSlider_, 3);
  weightRow->addWidget(weightEdit_, 1);
  weightRow->addWidget(new QLabel("m", this));
  weightRow->addWidget(applyBtn_);
  ctlForm->addRow("压载位置 y:", sliderValueLabel_);
  ctlForm->addRow(weightRow);

  auto *applyHint = new QLabel("输入后点『移动压载』生效；也可直接拖动滑条（松手 0.3 s 后移动）",
                               this);
  applyHint->setStyleSheet("color: #666; font-size: 11px;");
  applyHint->setWordWrap(true);
  ctlForm->addRow(applyHint);

  auto *sep = new QFrame(this);
  sep->setFrameStyle(QFrame::HLine);
  ctlForm->addRow(sep);

  heelLabel_ = new QLabel("— °", this);
  heelLabel_->setStyleSheet("font-size: 18px; font-weight: bold;");
  ctlForm->addRow("横倾角 φ:", heelLabel_);

  deltaYLabel_ = new QLabel("— m", this);
  ctlForm->addRow("偏移距 Δy:", deltaYLabel_);

  wDyLabel_ = new QLabel("— Nm", this);
  ctlForm->addRow("横倾力矩 w·Δy:", wDyLabel_);

  ctlGroup->setMinimumWidth(300);   ///< 2026-09-21：加了输入框 + 「移动压载」按钮，250 太挤
  topRow->addWidget(ctlGroup);
  mainLayout->addLayout(topRow);

  /// 按钮行
  auto *btnRow = new QHBoxLayout();
  recordBtn_ = new QPushButton("记录数据", this);
  resetBtn_ = new QPushButton("压载归零", this);
  auto *deleteBtn = new QPushButton("删除选中", this);
  UiTheme::setButtonIcon(deleteBtn, UiTheme::Icon::kDelete);
  clearBtn_ = new QPushButton("清除全部", this);
  btnRow->addWidget(recordBtn_);
  btnRow->addWidget(resetBtn_);
  btnRow->addWidget(deleteBtn);
  btnRow->addWidget(clearBtn_);
  for (QPushButton *b : {recordBtn_, resetBtn_, deleteBtn, clearBtn_}) b->setMinimumHeight(30);
  btnRow->addStretch();
  mainLayout->addLayout(btnRow);

  /// 数据表
  dataTable_ = new QTableWidget(0, 5, this);
  dataTable_->setHorizontalHeaderLabels(
      {"y(m)", "Δy(m)", "φ(°)", "tanφ", "w·Δy(Nm)"});
  dataTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  dataTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  dataTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  dataTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  dataTable_->setMinimumHeight(100);
  mainLayout->addWidget(dataTable_, 1);

  /// 散点图
  chartWidget_ = new IncliningChartWidget(this);
  mainLayout->addWidget(chartWidget_, 2);

  /// 信号连接
  connect(spawnBtn_, &QPushButton::clicked,
          this, &IncliningExperimentDialog::onSpawn);
  connect(weightSlider_, &QSlider::valueChanged,
          this, &IncliningExperimentDialog::onSliderChanged);
  /// ★ 2026-09-21 新增：确认按钮 —— 输入压载位置后**点它才真正移动**
  connect(applyBtn_, &QPushButton::clicked,
          this, &IncliningExperimentDialog::onApplyWeight);
  connect(recordBtn_, &QPushButton::clicked,
          this, &IncliningExperimentDialog::onRecord);
  connect(resetBtn_, &QPushButton::clicked,
          this, &IncliningExperimentDialog::onResetWeight);
  connect(clearBtn_, &QPushButton::clicked,
          this, &IncliningExperimentDialog::onClearAll);
  connect(deleteBtn, &QPushButton::clicked,
          this, &IncliningExperimentDialog::onDeleteSelected);

  /// ★ 2026-09-21：删掉原「spinbox ↔ 滑条双向同步、各自直接 moveWeight」的两条连接。
  /// 现在**只有一条**滑条连接（onSliderChanged，内部防抖 300 ms 后才真正下发）；
  /// 输入框**不再**触发任何下发 —— 必须由用户点「移动压载」（onApplyWeight）确认。
  /// 原因：moveWeight 走 `/world/<w>/set_pose/**blocking**`（同步阻塞 RPC，超时 2000 ms，
  /// 见 moveWeight 实现）→ 逐键 / 逐格触发会把 UI 线程冻住，
  /// 表现即"输入框删不掉、输不进去"（张老师 2026-09-21 实测）。
  ///
  /// ★ 防抖定时器（滑条专用）：拖动时只更新显示，停手 300 ms 才下发
  moveDebounce_ = new QTimer(this);
  moveDebounce_->setSingleShot(true);
  connect(moveDebounce_, &QTimer::timeout,
          this, &IncliningExperimentDialog::onMoveDebounced);

  connect(lengthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { updateTheoryDisplay(); });
  connect(breadthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { updateTheoryDisplay(); });
  connect(heightSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { updateTheoryDisplay(); });
  connect(densitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { updateTheoryDisplay(); });
}

void IncliningExperimentDialog::initPoseSubscription()
{
  poseTopic_ = "/world/" + worldName_ + "/pose/info";

  auto state = poseState_;
  std::string bargeName = bargeName_;

  std::function<void(const gz::msgs::Pose_V &)> cb =
      [state, bargeName](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return;
        state->latest = _msg;
        state->found = false;
        for (int i = 0; i < _msg.pose_size(); ++i)
        {
          if (_msg.pose(i).name() == bargeName)
          {
            const auto &ori = _msg.pose(i).orientation();
            gz::math::Quaterniond q(ori.w(), ori.x(), ori.y(), ori.z());
            double roll = q.Roll();
            state->heelDeg = roll * 180.0 / M_PI;
            state->found = true;
            break;
          }
        }
      };

  transportNode_.Subscribe(poseTopic_, cb);
}

void IncliningExperimentDialog::updateTheoryDisplay()
{
  double L = lengthSpin_->value();
  double B = breadthSpin_->value();
  double H = heightSpin_->value();
  double rho = densitySpin_->value();

  double d = computeTheoryDraft(rho, H);
  double gm = computeTheoryGM(L, B, H, rho);

  draftLabel_->setText(QString::number(d, 'f', 3) + " m");
  gmTheoryLabel_->setText(QString::number(gm, 'f', 4) + " m");

  if (gm > 0.01)
    gmTheoryLabel_->setStyleSheet(QString("font-weight: bold; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
  else if (gm < -0.01)
    gmTheoryLabel_->setStyleSheet(QString("font-weight: bold; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kFail)));
  else
    gmTheoryLabel_->setStyleSheet(QString("font-weight: bold; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));
}

void IncliningExperimentDialog::updateStatusDisplay()
{
  heelLabel_->setText(QString::number(currentHeelDeg_, 'f', 2) + " °");

  if (quiescent_ && std::abs(currentHeelDeg_) < 5.0)
    heelLabel_->setStyleSheet(
        QString("font-size:18px; font-weight:bold; color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
  else if (quiescent_)
    heelLabel_->setStyleSheet(
        QString("font-size:18px; font-weight:bold; color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));
  else
    heelLabel_->setStyleSheet(
        QString("font-size:18px; font-weight:bold; color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kDisabled)));

  double deltaY = weightY_;
  double wDy = kWeightForce * std::abs(deltaY);
  deltaYLabel_->setText(QString::number(deltaY, 'f', 3) + " m");
  wDyLabel_->setText(QString::number(wDy, 'f', 1) + " Nm");
}

namespace {
/// 流体密度 / 重力 —— **必须与下面 SDF 的 <fluid_density> 同源**
/// （P3-2 教训：同一个量写两处，改一处必漏一处）
constexpr double kFluidDensity = 1000.0;
constexpr double kGravity = 9.81;
}  // namespace

std::string IncliningExperimentDialog::makeBargeSDF(
    double _L, double _B, double _H, double _density) const
{
  double mass = _density * _L * _B * _H;
  double ixx = mass / 12.0 * (_B * _B + _H * _H);
  double iyy = mass / 12.0 * (_L * _L + _H * _H);
  double izz = mass / 12.0 * (_L * _L + _B * _B);

  /// ── 线性阻尼（2026-09-19 新增，与 HS-02 的 7.5 ① 同一修法）──────────────
  /// 本实验要求"等待横倾角稳定"才能记录（判据 |Δheel|<0.05°/帧 × 15 帧，见 cc:973-979），
  /// 而**长方体走不了插件的 pose_based 分支**（该分支需 hasCylinderGeometry_）→
  /// 原先 `<enable_hydro>false</enable_hydro>` 等于零阻尼 → 横摇 60 s 不衰减 →
  /// 判据永不满足、"记录数据"永远点不了。
  /// 详见 doc/plans/headless_ctl_plan.md §14。
  /// 长方体水线面恒为矩形 → A_wp = L·B **精确**（且 A_wp·B²/12 = L·B³/12 正是其水线面惯性矩）
  const double A_wp = _L * _B;
  const keship::LinearDampingCoeffs damp = keship::MakeLinearDamping(
      A_wp, _L, _B, mass, ixx, iyy, 0.15, 0.10, kFluidDensity, kGravity);

  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.6\">\n"
      << "  <model name=\"" << bargeName_ << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <inertial>\n"
      << "        <mass>" << mass << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>" << ixx << "</ixx>\n"
      << "          <ixy>0</ixy>\n"
      << "          <ixz>0</ixz>\n"
      << "          <iyy>" << iyy << "</iyy>\n"
      << "          <iyz>0</iyz>\n"
      << "          <izz>" << izz << "</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << _L << " " << _B << " " << _H << "</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "        <material>\n"
      << "          <ambient>0.7 0.5 0.3 1.0</ambient>\n"
      << "          <diffuse>0.7 0.5 0.3 1.0</diffuse>\n"
      << "        </material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << _L << " " << _B << " " << _H << "</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
      << " name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <link_name>base_link</link_name>\n"
      << "      <enable_buoyancy>true</enable_buoyancy>\n"
      << "      <enable_hydro>true</enable_hydro>\n"
      << "      <linear_damping>\n"
      << "        <Zw>" << damp.Zw << "</Zw>\n"
      << "        <Kp>" << damp.Kp << "</Kp>\n"
      << "        <Mq>" << damp.Mq << "</Mq>\n"
      << "      </linear_damping>\n"
      << "      <fluid_density>" << kFluidDensity << "</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <buoyancy name=\"barge_hull\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << _L << " " << _B << " " << _H << "</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "  </model>\n"
      << "</sdf>\n";
  return sdf.str();
}

std::string IncliningExperimentDialog::makeWeightSDF(double _y) const
{
  double wh = 0.2;  // weight height
  double deckZ = heightSpin_->value() / 2.0 + wh / 2.0 + 0.02;

  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.6\">\n"
      << "  <model name=\"" << weightName_ << "\">\n"
      << "    <pose>0 " << _y << " " << deckZ << " 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <inertial>\n"
      << "        <mass>" << kWeightMass << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>1.0</ixx>\n"
      << "          <ixy>0</ixy>\n"
      << "          <ixz>0</ixz>\n"
      << "          <iyy>1.0</iyy>\n"
      << "          <iyz>0</iyz>\n"
      << "          <izz>1.0</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>0.3 0.2 0.2</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "        <material>\n"
      << "          <ambient>1.0 0.15 0.15 1.0</ambient>\n"
      << "          <diffuse>1.0 0.2 0.2 1.0</diffuse>\n"
      << "        </material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>0.3 0.2 0.2</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "  </model>\n"
      << "</sdf>\n";
  return sdf.str();
}

bool IncliningExperimentDialog::resolveEntityID(
    const std::string &_name, uint32_t &_id)
{
  std::string service = "/world/" + worldName_ + "/scene/info";
  gz::msgs::Empty req;
  gz::msgs::Scene rep;
  bool result = false;

  bool ok = transportNode_.Request(service, req, 5000, rep, result);
  if (!ok || !result) return false;

  for (int i = 0; i < rep.model_size(); ++i)
  {
    if (rep.model(i).name() == _name)
    {
      _id = rep.model(i).id();
      return true;
    }
  }
  return false;
}

bool IncliningExperimentDialog::moveWeight(double _y)
{
  if (weightEntityId_ == 0) return false;

  double wh = 0.2;
  double deckZ = heightSpin_->value() / 2.0 + wh / 2.0 + 0.02;

  gz::msgs::Pose req;
  req.set_id(weightEntityId_);
  req.mutable_position()->set_x(0);
  req.mutable_position()->set_y(_y);
  req.mutable_position()->set_z(deckZ);
  gz::math::Quaterniond q(0, 0, 0);
  req.mutable_orientation()->set_w(q.W());
  req.mutable_orientation()->set_x(q.X());
  req.mutable_orientation()->set_y(q.Y());
  req.mutable_orientation()->set_z(q.Z());

  gz::msgs::Boolean rep;
  bool result = false;
  std::string service = "/world/" + worldName_ + "/set_pose/blocking";
  bool ok = transportNode_.Request(service, req, 2000, rep, result);

  return ok && result && rep.data();
}

bool IncliningExperimentDialog::doDeleteBoth()
{
  /// 先删压载块（在驳船上），再删驳船
  if (weightEntityId_ != 0)
  {
    gz::msgs::Entity req;
    req.set_name(weightName_);
    req.set_type(gz::msgs::Entity::MODEL);
    gz::msgs::Boolean rep;
    bool r = false;
    transportNode_.Request(
        "/world/" + worldName_ + "/remove", req, 5000, rep, r);
    for (int retry = 0; retry < 20; ++retry)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      uint32_t id = 0;
      if (!resolveEntityID(weightName_, id))
        break;
    }
    weightEntityId_ = 0;
  }

  if (bargeEntityId_ != 0)
  {
    gz::msgs::Entity req;
    req.set_name(bargeName_);
    req.set_type(gz::msgs::Entity::MODEL);
    gz::msgs::Boolean rep;
    bool r = false;
    transportNode_.Request(
        "/world/" + worldName_ + "/remove", req, 5000, rep, r);
    for (int retry = 0; retry < 20; ++retry)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      uint32_t id = 0;
      if (!resolveEntityID(bargeName_, id))
        break;
    }
    bargeEntityId_ = 0;
  }

  spawned_ = false;
  return true;
}

bool IncliningExperimentDialog::doSpawnBoth()
{
  doDeleteBoth();

  /// 清空旧数据（防止不同参数的駁船数据混杂）
  dataTable_->setRowCount(0);
  recordedData_.clear();
  chartWidget_->clearData();

  double L = lengthSpin_->value();
  double B = breadthSpin_->value();
  double H = heightSpin_->value();
  double rho = densitySpin_->value();

  /// spawn 驳船
  {
    std::string sdfStr = makeBargeSDF(L, B, H, rho);
    gz::msgs::EntityFactory req;
    req.set_name(bargeName_);
    req.set_allow_renaming(false);
    req.set_sdf(sdfStr);
    gz::msgs::Boolean rep;
    bool result = false;
    bool ok = transportNode_.Request(
        "/world/" + worldName_ + "/create/blocking", req, 20000, rep, result);
    if (!ok || !result)
    {
      std::cerr << "HS-04: 驳船生成失败" << std::endl;
      return false;
    }

    /// 解析 entity ID
    for (int retry = 0; retry < 20; ++retry)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (resolveEntityID(bargeName_, bargeEntityId_)) break;
    }
    if (bargeEntityId_ == 0)
    {
      std::cerr << "HS-04: 无法解析驳船 entity ID" << std::endl;
      return false;
    }
  }

  /// spawn 压载块
  {
    std::string sdfStr = makeWeightSDF(0.0);
    gz::msgs::EntityFactory req;
    req.set_name(weightName_);
    req.set_allow_renaming(false);
    req.set_sdf(sdfStr);
    gz::msgs::Boolean rep;
    bool result = false;
    transportNode_.Request(
        "/world/" + worldName_ + "/create/blocking", req, 20000, rep, result);

    for (int retry = 0; retry < 20; ++retry)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (resolveEntityID(weightName_, weightEntityId_)) break;
    }
  }

  /// 重置状态
  weightY_ = 0.0;
  if (moveDebounce_) moveDebounce_->stop();
  weightSlider_->blockSignals(true);
  weightSlider_->setValue(250);
  weightSlider_->blockSignals(false);
  weightEdit_->blockSignals(true);
  weightEdit_->setText("0");
  weightEdit_->blockSignals(false);
  sliderValueLabel_->setText("0.000 m");
  quiescent_ = false;
  settleCounter_ = 0;
  spawned_ = true;

  std::cout << "HS-04: 驳船 + 压载块已生成 — L=" << L
            << ", B=" << B << ", H=" << H << ", ρ=" << rho << std::endl;
  return true;
}

void IncliningExperimentDialog::onSpawn()
{
  if (!doSpawnBoth())
  {
    QMessageBox::warning(this, "错误", "驳船生成失败，请检查参数。");
    return;
  }
  updateTheoryDisplay();
  updateStatusDisplay();
}

void IncliningExperimentDialog::onSliderChanged(int _value)
{
  double y = (_value / 500.0 - 0.5) * 0.5;  // -0.25 to +0.25

  /// 纯 UI 更新：输入框与读数**立即**跟随（不下发任何 RPC）
  weightY_ = y;
  weightEdit_->blockSignals(true);
  weightEdit_->setText(QString::number(y, 'f', 3));
  weightEdit_->blockSignals(false);
  sliderValueLabel_->setText(QString::number(y, 'f', 3) + " m");

  if (spawned_)
  {
    /// ★ 必须**先**置"未稳定"：防抖窗口内若用户点了"记录"，此时压载尚未真正移动，
    ///   置 false 可让 onRecord 拒绝，避免记录到"y 已改、船未动"的错误数据。
    quiescent_ = false;
    settleCounter_ = 0;
    pendingY_ = y;
    moveDebounce_->start(300);   ///< 停手 300 ms 才真正下发（避免逐格阻塞 RPC）
  }

  updateStatusDisplay();
}

/// ★ 2026-09-21 新增：移动压载的**统一入口**
/// 所有路径（确认按钮 / 滑条防抖 / 压载归零）都走这里，保证"y / 判稳 / 下发 / 面板"四者一致。
void IncliningExperimentDialog::commitMove(double _y)
{
  weightY_ = _y;
  sliderValueLabel_->setText(QString::number(_y, 'f', 3) + " m");

  if (spawned_)
  {
    /// 移动后必须重新判稳，才允许记录
    quiescent_ = false;
    settleCounter_ = 0;
    moveWeight(_y);
  }
  updateStatusDisplay();
}

void IncliningExperimentDialog::onMoveDebounced()
{
  commitMove(pendingY_);
}

/// ★ 2026-09-21 新增：「移动压载」确认按钮
/// 只有点它，输入框里的位置才会真正下发到仿真（输入过程零 RPC，故不再卡顿）。
void IncliningExperimentDialog::onApplyWeight()
{
  const QString raw = weightEdit_->text().trimmed();
  bool ok = false;
  double y = raw.toDouble(&ok);
  if (!ok)
  {
    QMessageBox::warning(this, "输入无效",
        QString("无法识别 \"%1\"。请输入一个数值，范围 −0.25 ~ 0.25 m。").arg(raw));
    return;
  }
  if (y < -0.25 || y > 0.25)
  {
    y = std::clamp(y, -0.25, 0.25);
    QMessageBox::information(this, "超出量程",
        QString("压载位置超出 ±0.25 m，已按 %1 m 处理。")
            .arg(QString::number(y, 'f', 3)));
  }

  /// 与滑条防抖互斥：确认按钮是显式动作，立即生效
  if (moveDebounce_) moveDebounce_->stop();
  pendingY_ = y;

  /// 同步滑条位置（blockSignals 防回环触发 onSliderChanged）
  const int v = std::clamp(static_cast<int>((y / 0.5 + 0.5) * 500.0), 0, 500);
  weightSlider_->blockSignals(true);
  weightSlider_->setValue(v);
  weightSlider_->blockSignals(false);

  /// 规范化显示（"0.1000" → "0.100"），便于阅读
  weightEdit_->blockSignals(true);
  weightEdit_->setText(QString::number(y, 'f', 3));
  weightEdit_->blockSignals(false);

  commitMove(y);
}

void IncliningExperimentDialog::onRecord()
{
  if (!spawned_)
  {
    QMessageBox::information(this, "提示", "请先生成驳船和压载块。");
    return;
  }
  if (!quiescent_)
  {
    QMessageBox::information(this, "提示", "请等待横倾角稳定后再记录。");
    return;
  }

  double deltaY = weightY_;
  /// ★ 2026-09-20 修复：`tanφ` 必须与 `w·Δy` **口径一致**（都取幅值）。
  /// 原实现 `tanφ` 保留符号、而 `w·Δy` 用 `std::abs(deltaY)`（恒 ≥0）
  /// ⇒ 压载移到**负侧**时记录点变成 (+w·Δy, −|tanφ|)，与正侧点符号自相矛盾
  /// ⇒ `computeLinearFit` 的斜率被破坏、`GM实验 = 1/(disp·slope)` 全废。
  /// 实测（无头，`data/test/sp03_inclining.py`）：只用正侧 → gmExp=0.1226（真值 ✓）；
  /// **含负侧 → gmExp=1.75~2.24**（真值的 14~18 倍）✗。
  /// 为什么取幅值而不是把 w·Δy 改成有符号：**图表的 x 轴硬编码从 0 开始**
  /// （`IncliningChartWidget::clearData()` 里 `xMin_ = 0.0`，且 setData 只跟踪 `xMax_`）
  /// ⇒ 作者的设计意图就是"幅值"；改成有符号还得动图表坐标轴（见 modules/gui.md 第 5 条的魔数换算）。
  /// 表格里 `Δy(m)` 与 `φ(°)` **仍保留符号**，学生照样能看出倾斜方向。
  double tanPhi = std::abs(std::tan(currentHeelDeg_ * M_PI / 180.0));
  double wDy = kWeightForce * std::abs(deltaY);

  IncliningDataPoint pt{weightY_, deltaY, currentHeelDeg_, tanPhi, wDy};
  recordedData_.push_back(pt);

  int row = dataTable_->rowCount();
  dataTable_->insertRow(row);
  dataTable_->setItem(row, 0,
      new QTableWidgetItem(QString::number(weightY_, 'f', 3)));
  dataTable_->setItem(row, 1,
      new QTableWidgetItem(QString::number(deltaY, 'f', 3)));
  dataTable_->setItem(row, 2,
      new QTableWidgetItem(QString::number(currentHeelDeg_, 'f', 2)));
  dataTable_->setItem(row, 3,
      new QTableWidgetItem(QString::number(tanPhi, 'f', 4)));
  dataTable_->setItem(row, 4,
      new QTableWidgetItem(QString::number(wDy, 'f', 1)));

  updateChart();

  std::cout << "HS-04: 记录数据 — y=" << weightY_
            << ", φ=" << currentHeelDeg_
            << "°, tanφ=" << tanPhi << std::endl;
}

void IncliningExperimentDialog::onResetWeight()
{
  if (moveDebounce_) moveDebounce_->stop();
  commitMove(0.0);   ///< 统一入口：记录 y、重置判稳、下发、刷新面板

  weightSlider_->blockSignals(true);
  weightSlider_->setValue(250);
  weightSlider_->blockSignals(false);
  weightEdit_->blockSignals(true);
  weightEdit_->setText("0");
  weightEdit_->blockSignals(false);

  updateStatusDisplay();
}

void IncliningExperimentDialog::onDeleteSelected()
{
  auto selected = dataTable_->selectionModel()->selectedRows();
  if (selected.isEmpty()) return;

  std::sort(selected.begin(), selected.end(),
            [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
  for (const auto &idx : selected)
    dataTable_->removeRow(idx.row());

  /// 从剩余行重建 recordedData_
  recordedData_.clear();
  for (int r = 0; r < dataTable_->rowCount(); ++r)
  {
    double y = dataTable_->item(r, 0)->text().toDouble();
    double dy = dataTable_->item(r, 1)->text().toDouble();
    double phi = dataTable_->item(r, 2)->text().toDouble();
    double tanPhi = dataTable_->item(r, 3)->text().toDouble();
    double wDy = dataTable_->item(r, 4)->text().toDouble();
    recordedData_.push_back({y, dy, phi, tanPhi, wDy});
  }
  updateChart();
  std::cout << "HS-04: 已删除 " << selected.size() << " 行" << std::endl;
}

void IncliningExperimentDialog::onClearAll()
{
  dataTable_->setRowCount(0);
  recordedData_.clear();
  chartWidget_->clearData();
  std::cout << "HS-04: 已清除全部数据" << std::endl;
}

void IncliningExperimentDialog::onUpdatePose()
{
  double heel;
  bool found;
  {
    std::lock_guard<std::mutex> lock(poseState_->mutex);
    heel = poseState_->heelDeg;
    found = poseState_->found;
  }

  currentHeelDeg_ = heel;

  if (spawned_ && found)
  {
    /// 稳定检测
    if (std::abs(heel - lastHeelDeg_) < 0.05)
      settleCounter_++;
    else
      settleCounter_ = 0;

    if (settleCounter_ >= 15)
      quiescent_ = true;

    lastHeelDeg_ = heel;
  }

  updateStatusDisplay();
}

void IncliningExperimentDialog::updateChart()
{
  if (recordedData_.size() < 2)
  {
    chartWidget_->setData(recordedData_, 0.0,
        computeTheoryGM(lengthSpin_->value(), breadthSpin_->value(),
                         heightSpin_->value(), densitySpin_->value()));
    return;
  }

  double slope = 0.0, intercept = 0.0;
  computeLinearFit(recordedData_, slope, intercept);

  double L = lengthSpin_->value();
  double B = breadthSpin_->value();
  double d = computeTheoryDraft(densitySpin_->value(), heightSpin_->value());
  double displacement = kFluidDensity * kGravity * L * B * d;
  double gmExp = (std::abs(slope) > 1e-12) ? 1.0 / (displacement * slope) : 0.0;
  double gmTh = computeTheoryGM(L, B, heightSpin_->value(),
                                 densitySpin_->value());

  chartWidget_->setData(recordedData_, gmExp, gmTh);
}

double IncliningExperimentDialog::computeTheoryGM(
    double _L, double _B, double _H, double _density) const
{
  Q_UNUSED(_L);
  double d = computeTheoryDraft(_density, _H);
  if (d <= 0.0 || d >= _H) return 0.0;
  double KB = d / 2.0;
  double BM = _B * _B / (12.0 * d);
  double KG = _H / 2.0;
  return KB + BM - KG;
}

double IncliningExperimentDialog::computeTheoryDraft(
    double _density, double _H) const
{
  return (_density / kFluidDensity) * _H;
}

void IncliningExperimentDialog::computeLinearFit(
    const std::vector<IncliningDataPoint> &_pts,
    double &_slope, double &_intercept) const
{
  size_t n = _pts.size();
  if (n < 2) { _slope = 0.0; _intercept = 0.0; return; }

  double sx = 0.0, sy = 0.0, sxy = 0.0, sx2 = 0.0;
  for (const auto &p : _pts)
  {
    sx += p.wDy;
    sy += p.tanPhi;
    sxy += p.wDy * p.tanPhi;
    sx2 += p.wDy * p.wDy;
  }
  double denom = n * sx2 - sx * sx;
  if (std::abs(denom) < 1e-12)
    { _slope = 0.0; _intercept = 0.0; return; }

  _slope = (n * sxy - sx * sy) / denom;
  _intercept = (sy - _slope * sx) / n;
}
