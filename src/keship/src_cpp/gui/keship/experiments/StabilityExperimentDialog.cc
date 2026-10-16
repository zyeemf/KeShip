/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* HS-03 漂浮圆柱体初稳性实验对话框实现 */
/// 2026.5.27 重构

#include "StabilityExperimentDialog.hh"
#include "WavefieldPublisher.hh"
#include "../shared/UiTheme.hh"
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
#include <gz/msgs/param.pb.h>
#include <gz/msgs/any.pb.h>

// ═══════════════════════════════════════════════════════════
// StabilityChartWidget
// ═══════════════════════════════════════════════════════════

StabilityChartWidget::StabilityChartWidget(QWidget *_parent)
    : QWidget(_parent)
{
  setMinimumSize(300, 200);
}

void StabilityChartWidget::setData(
    const std::vector<std::pair<double, double>> &_stable,
    const std::vector<std::pair<double, double>> &_unstable)
{
  stablePts_ = _stable;
  unstablePts_ = _unstable;

  /// 动态扩展纵轴范围
  dlMax_ = 2.5;
  for (const auto &p : stablePts_)
    if (p.second > dlMax_) dlMax_ = p.second;
  for (const auto &p : unstablePts_)
    if (p.second > dlMax_) dlMax_ = p.second;
  dlMax_ = std::ceil(dlMax_ * 1.2);

  update();
}

void StabilityChartWidget::clearData()
{
  stablePts_.clear();
  unstablePts_.clear();
  dlMax_ = 2.5;
  update();
}

void StabilityChartWidget::paintEvent(QPaintEvent *_event)
{
  Q_UNUSED(_event);

  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  int w = width();
  int h = height();
  if (w < 60 || h < 60) return;

  int ml = 60, mr = 30, mt = 20, mb = 40;
  int pw = w - ml - mr;
  int ph = h - mt - mb;

  auto toX = [&](double _s) -> int {
    return ml + (int)((_s - sMin_) / (sMax_ - sMin_) * pw);
  };
  auto toY = [&](double _dl) -> int {
    return mt + ph - (int)((_dl - dlMin_) / (dlMax_ - dlMin_) * ph);
  };

  /// 背景
  painter.fillRect(rect(), Qt::white);

  /// 网格线
  painter.setPen(QPen(QColor(220, 220, 220), 1));
  for (int i = 0; i <= 4; ++i)
  {
    double sv = 0.25 * i;
    int x = toX(sv);
    painter.drawLine(x, mt, x, mt + ph);
  }
  for (int i = 0; i <= 4; ++i)
  {
    double dlv = dlMax_ * i / 4.0;
    int y = toY(dlv);
    painter.drawLine(ml, y, w - mr, y);
  }

  /// 稳定区 / 倾倒区 半透明填充
  {
    QPainterPath stablePath, unstablePath;
    bool first = true;
    for (int i = 0; i < pw; ++i)
    {
      double s = sMin_ + (sMax_ - sMin_) * i / (double)pw;
      if (s <= 0.01 || s >= 0.99) continue;
      double dlCrit = std::sqrt(8.0 * s * (1.0 - s));
      int x = toX(s);
      int y = toY(dlCrit);
      if (first) {
        stablePath.moveTo(x, y);
        unstablePath.moveTo(x, y);
        first = false;
      } else {
        stablePath.lineTo(x, y);
        unstablePath.lineTo(x, y);
      }
    }
    stablePath.lineTo(toX(0.99), mt);
    stablePath.lineTo(toX(0.01), mt);
    stablePath.closeSubpath();
    painter.fillPath(stablePath, QColor(60, 180, 80, 35));

    unstablePath.lineTo(toX(0.99), mt + ph);
    unstablePath.lineTo(toX(0.01), mt + ph);
    unstablePath.closeSubpath();
    painter.fillPath(unstablePath, QColor(200, 60, 60, 30));
  }

  /// 理论临界曲线
  painter.setPen(QPen(QColor(40, 80, 200), 2));
  for (int i = 0; i < pw; ++i)
  {
    double s = sMin_ + (sMax_ - sMin_) * i / (double)pw;
    if (s <= 0.0 || s >= 1.0) continue;
    double dlCrit = std::sqrt(8.0 * s * (1.0 - s));
    int y = toY(dlCrit);
    if (i == 0)
      painter.drawLine(toX(s), y, toX(s), y);
    else
      painter.drawLine(toX(s - (sMax_ - sMin_) / pw), toY(std::sqrt(8.0 * (s - (sMax_ - sMin_) / pw) * (1.0 - (s - (sMax_ - sMin_) / pw)))), toX(s), y);
  }

  /// 区域标注
  QFont labelFont;
  labelFont.setPointSize(9);
  labelFont.setBold(true);
  painter.setFont(labelFont);
  /// 稳定区标注（绿色半透明区域内）
  painter.setPen(QPen(QColor(30, 100, 30), 1));
  painter.fillRect(QRect(toX(0.60), toY(1.9) - 12, 14, 14), QColor(60, 180, 80, 80));
  painter.drawText(QRect(toX(0.60) + 18, toY(1.9) - 12, 100, 16), Qt::AlignLeft | Qt::AlignVCenter, "竖直稳定区");

  /// 倾倒区标注（红色半透明区域内）
  painter.setPen(QPen(QColor(140, 40, 40), 1));
  painter.fillRect(QRect(toX(0.60), toY(0.3) - 2, 14, 14), QColor(200, 60, 60, 80));
  painter.drawText(QRect(toX(0.60) + 18, toY(0.3) - 2, 80, 16), Qt::AlignLeft | Qt::AlignVCenter, "倾倒区");

  /// 数据点
  /// 稳定点（绿色圆）
  painter.setPen(QPen(QColor(40, 160, 40), 2));
  painter.setBrush(QColor(120, 220, 120));
  for (const auto &p : stablePts_)
  {
    int cx = toX(p.first);
    int cy = toY(p.second);
    painter.drawEllipse(QPoint(cx, cy), 6, 6);
  }

  /// 不稳定点（红色叉）
  painter.setPen(QPen(QColor(200, 40, 40), 2));
  painter.setBrush(Qt::NoBrush);
  for (const auto &p : unstablePts_)
  {
    int cx = toX(p.first);
    int cy = toY(p.second);
    painter.drawLine(cx - 5, cy - 5, cx + 5, cy + 5);
    painter.drawLine(cx + 5, cy - 5, cx - 5, cy + 5);
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

  /// X 轴
  for (int i = 0; i <= 4; ++i)
  {
    double sv = 0.25 * i;
    QString label = QString::number(sv, 'f', 2);
    painter.drawText(QRect(toX(sv) - 20, mt + ph + 2, 40, 18),
                     Qt::AlignHCenter, label);
  }
  painter.drawText(QRect(ml, mt + ph + 18, pw, 20),
                   Qt::AlignHCenter, "密度比 s = ρ/ρw");

  /// Y 轴
  for (int i = 0; i <= 4; ++i)
  {
    double dlv = dlMax_ * i / 4.0;
    QString label = QString::number(dlv, 'f', 1);
    painter.drawText(QRect(2, toY(dlv) - 8, ml - 6, 16),
                     Qt::AlignRight | Qt::AlignVCenter, label);
  }
  painter.save();
  painter.translate(14, mt + ph / 2);
  painter.rotate(-90);
  painter.drawText(QRect(-50, -10, 100, 20), Qt::AlignHCenter, "D/L");
  painter.restore();

  /// 图例
  int lx = w - mr - 160;
  int ly = mt + 6;
  painter.setPen(QPen(QColor(40, 80, 200), 2));
  painter.drawLine(lx, ly + 5, lx + 25, ly + 5);
  painter.setPen(Qt::black);
  painter.drawText(QRect(lx + 29, ly, 120, 14), "理论临界曲线");

  ly += 18;
  painter.setPen(QPen(QColor(40, 160, 40), 2));
  painter.setBrush(QColor(120, 220, 120));
  painter.drawEllipse(QPoint(lx + 12, ly + 5), 5, 5);
  painter.setPen(Qt::black);
  painter.setBrush(Qt::NoBrush);
  painter.drawText(QRect(lx + 29, ly, 120, 14), "竖直稳定");

  ly += 18;
  painter.setPen(QPen(QColor(200, 40, 40), 2));
  painter.drawLine(lx + 6, ly, lx + 18, ly + 10);
  painter.drawLine(lx + 18, ly, lx + 6, ly + 10);
  painter.drawText(QRect(lx + 29, ly, 120, 14), "倾倒");
}

// ═══════════════════════════════════════════════════════════
// StabilityExperimentDialog
// ═══════════════════════════════════════════════════════════

StabilityExperimentDialog::StabilityExperimentDialog(
    gz::transport::Node &_transportNode,
    const std::string &_worldName,
    QWidget *_parent)
    : QDialog(_parent),
      transportNode_(_transportNode),
      worldName_(_worldName),
      cylinderName_("SP01_cylinder")
{
  setWindowTitle("HS-03 漂浮圆柱体初稳性");
  setModal(false);
  resize(920, 720);

  poseState_ = std::make_shared<StabilityPoseState>();

  setupUI();
  initPoseSubscription();

  poseTimer_ = new QTimer(this);
  connect(poseTimer_, &QTimer::timeout,
          this, &StabilityExperimentDialog::onUpdatePose);
  poseTimer_->start(33);

  /// 初始理论显示
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

  std::cout << "HS-03: 初稳性实验对话框已创建" << std::endl;
}

StabilityExperimentDialog::~StabilityExperimentDialog()
{
  {
    std::lock_guard<std::mutex> lock(poseState_->mutex);
    poseState_->alive = false;
  }
  if (!poseTopic_.empty())
    transportNode_.Unsubscribe(poseTopic_);
  poseTimer_->stop();
  doDeleteCylinder();

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

void StabilityExperimentDialog::setupUI()
{
  auto *mainLayout = new QVBoxLayout(this);

  /// 说明
  auto *descLabel = new QLabel(
      "<b>实验目的：</b>理解初稳性高度 GM = KB + BM − KG。<br>"
      "<b>操作：</b>设置圆柱体参数 → 选择扰动强度 → 点击\"生成圆柱体\""
      " → 观察横倾角变化 → 记录数据。",
      this);
  descLabel->setWordWrap(true);
  mainLayout->addWidget(descLabel);

  /// 中上部分：参数 + 状态
  auto *topRow = new QHBoxLayout();

  /// 左侧：参数面板
  auto *paramGroup = new QGroupBox("圆柱体参数", this);
  auto *paramForm = new QFormLayout(paramGroup);

  diameterSpin_ = new QDoubleSpinBox(this);
  diameterSpin_->setRange(0.3, 10.0);
  diameterSpin_->setValue(2.0);
  diameterSpin_->setDecimals(2);
  diameterSpin_->setSuffix(" m");
  diameterSpin_->setSingleStep(0.2);
  paramForm->addRow("直径 D:", diameterSpin_);

  lengthSpin_ = new QDoubleSpinBox(this);
  lengthSpin_->setRange(0.5, 20.0);
  lengthSpin_->setValue(5.0);
  lengthSpin_->setDecimals(2);
  lengthSpin_->setSuffix(" m");
  lengthSpin_->setSingleStep(0.5);
  paramForm->addRow("长度 L:", lengthSpin_);

  densitySpin_ = new QDoubleSpinBox(this);
  densitySpin_->setRange(50, 950);
  densitySpin_->setValue(500);
  densitySpin_->setDecimals(0);
  densitySpin_->setSuffix(" kg/m³");
  densitySpin_->setSingleStep(50);
  paramForm->addRow("密度 ρ:", densitySpin_);

  /// 扰动
  auto *perturbLayout = new QHBoxLayout();
  lightPerturbBtn_ = new QRadioButton("轻推 3°", this);
  mediumPerturbBtn_ = new QRadioButton("中推 5°", this);
  heavyPerturbBtn_ = new QRadioButton("重推 10°", this);
  mediumPerturbBtn_->setChecked(true);
  perturbLayout->addWidget(lightPerturbBtn_);
  perturbLayout->addWidget(mediumPerturbBtn_);
  perturbLayout->addWidget(heavyPerturbBtn_);
  paramForm->addRow("扰动强度:", perturbLayout);

  spawnBtn_ = new QPushButton("生成圆柱体", this);
  spawnBtn_->setMinimumHeight(30);
  paramForm->addRow(spawnBtn_);

  /// 理论值
  auto *theoryFrame = new QFrame(this);
  theoryFrame->setFrameStyle(QFrame::StyledPanel);
  auto *theoryForm = new QFormLayout(theoryFrame);
  sLabel_ = new QLabel("—", this);
  theoryForm->addRow("s = ρ/ρw:", sLabel_);
  dlLabel_ = new QLabel("—", this);
  theoryForm->addRow("D/L:", dlLabel_);
  gmLabel_ = new QLabel("—", this);
  theoryForm->addRow("GM (理论):", gmLabel_);
  dlCritLabel_ = new QLabel("—", this);
  theoryForm->addRow("(D/L)临界:", dlCritLabel_);
  paramForm->addRow(theoryFrame);

  topRow->addWidget(paramGroup);

  /// 右侧：实时状态
  auto *statusGroup = new QGroupBox("实时状态", this);
  auto *statusForm = new QFormLayout(statusGroup);

  heelLabel_ = new QLabel("— °", this);
  heelLabel_->setStyleSheet("font-size: 16px; font-weight: bold;");
  statusForm->addRow("横倾角 φ:", heelLabel_);

  draftLabel_ = new QLabel("— m", this);
  statusForm->addRow("吃水 d:", draftLabel_);

  statusLabel_ = new QLabel("等待生成圆柱体", this);
  statusLabel_->setStyleSheet("font-size: 14px;");
  statusForm->addRow("判定:", statusLabel_);

  statusGroup->setMinimumWidth(220);
  topRow->addWidget(statusGroup);
  mainLayout->addLayout(topRow);

  /// 操作按钮
  auto *btnRow = new QHBoxLayout();
  recordBtn_ = new QPushButton("记录数据", this);
  auto *delSelBtn = new QPushButton("删除选中", this);
  UiTheme::setButtonIcon(delSelBtn, UiTheme::Icon::kDelete);
  clearBtn_ = new QPushButton("清除全部", this);
  btnRow->addWidget(recordBtn_);
  btnRow->addWidget(delSelBtn);
  btnRow->addWidget(clearBtn_);
  for (QPushButton *b : {recordBtn_, delSelBtn, clearBtn_}) b->setMinimumHeight(30);
  btnRow->addStretch();
  mainLayout->addLayout(btnRow);

  /// 数据表
  dataTable_ = new QTableWidget(0, 7, this);
  dataTable_->setHorizontalHeaderLabels(
      {"D(m)", "L(m)", "ρ(kg/m³)", "s", "D/L", "GM理论(m)", "稳定?"});
  dataTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  dataTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  dataTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  dataTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  dataTable_->setMinimumHeight(100);
  mainLayout->addWidget(dataTable_, 1);

  /// 稳定域图
  chartWidget_ = new StabilityChartWidget(this);
  mainLayout->addWidget(chartWidget_, 2);

  /// 信号连接
  connect(spawnBtn_, &QPushButton::clicked,
          this, &StabilityExperimentDialog::onSpawn);
  connect(recordBtn_, &QPushButton::clicked,
          this, &StabilityExperimentDialog::onRecord);
  connect(delSelBtn, &QPushButton::clicked,
          this, &StabilityExperimentDialog::onDeleteSelected);
  connect(clearBtn_, &QPushButton::clicked,
          this, &StabilityExperimentDialog::onClearAll);
  connect(diameterSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &StabilityExperimentDialog::onParamChanged);
  connect(lengthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &StabilityExperimentDialog::onParamChanged);
  connect(densitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, &StabilityExperimentDialog::onParamChanged);
}

void StabilityExperimentDialog::initPoseSubscription()
{
  poseTopic_ = "/world/" + worldName_ + "/pose/info";

  auto state = poseState_;
  std::string cylName = cylinderName_;

  std::function<void(const gz::msgs::Pose_V &)> cb =
      [state, cylName](const gz::msgs::Pose_V &_msg) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return;
        state->latest = _msg;
        state->found = false;
        for (int i = 0; i < _msg.pose_size(); ++i)
        {
          if (_msg.pose(i).name() == cylName)
          {
            const auto &ori = _msg.pose(i).orientation();
            gz::math::Quaterniond q(ori.w(), ori.x(), ori.y(), ori.z());
            double roll = q.Roll();
            state->heelDeg = roll * 180.0 / M_PI;
            state->draft = -_msg.pose(i).position().z();
            if (state->draft < 0.0) state->draft = 0.0;
            state->found = true;
            break;
          }
        }
      };

  transportNode_.Subscribe(poseTopic_, cb);
}

void StabilityExperimentDialog::updateTheoryDisplay()
{
  double D = diameterSpin_->value();
  double L = lengthSpin_->value();
  double rho = densitySpin_->value();
  double s = rho / kFluidDensity;
  double DL = (L > 0.0) ? D / L : 0.0;
  double gm = computeGM(D, L, s);
  double dlCrit = std::sqrt(std::max(0.0, 8.0 * s * (1.0 - s)));

  sLabel_->setText(QString::number(s, 'f', 3));
  dlLabel_->setText(QString::number(DL, 'f', 3));
  gmLabel_->setText(QString::number(gm, 'f', 3) + " m");

  if (s <= 0.0 || s >= 1.0)
    dlCritLabel_->setText("— (s 需在 0~1 内)");
  else
    dlCritLabel_->setText(QString::number(dlCrit, 'f', 3));

  /// GM 正负着色
  if (gm > 0.01)
    gmLabel_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
  else if (gm < -0.01)
    gmLabel_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(UiTheme::statusColor(UiTheme::Status::kFail)));
  else
    gmLabel_->setStyleSheet(QString("color: %1; font-weight: bold;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));
}

void StabilityExperimentDialog::updateStatusDisplay()
{
  heelLabel_->setText(QString::number(currentHeelDeg_, 'f', 2) + " °");
  draftLabel_->setText(QString::number(currentDraft_, 'f', 3) + " m");

  /// 横倾角着色
  if (std::abs(currentHeelDeg_) < 3.0)
    heelLabel_->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
  else if (std::abs(currentHeelDeg_) > 45.0)
    heelLabel_->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kFail)));
  else
    heelLabel_->setStyleSheet(QString("font-size: 16px; font-weight: bold; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));

  /// 稳定判定
  switch (stabilityResult_)
  {
  case 1:
    statusLabel_->setText("● 竖直稳定");
    statusLabel_->setStyleSheet(QString("font-size: 14px; color: %1; font-weight: bold;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
    break;
  case 2:
    statusLabel_->setText("● 倾倒（水平稳定）");
    statusLabel_->setStyleSheet(QString("font-size: 14px; color: %1; font-weight: bold;").arg(UiTheme::statusColor(UiTheme::Status::kFail)));
    break;
  case 3:
    statusLabel_->setText("● 临界状态");
    statusLabel_->setStyleSheet(QString("font-size: 14px; color: %1; font-weight: bold;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));
    break;
  default:
    if (observing_)
    {
      statusLabel_->setText(
          QString("观测中...(%1/150)").arg(observeCounter_));
      statusLabel_->setStyleSheet(QString("font-size: 14px; color: %1;").arg(UiTheme::statusColor(UiTheme::Status::kDisabled)));
    }
    else
    {
      statusLabel_->setText("等待生成圆柱体");
      statusLabel_->setStyleSheet("font-size: 14px;");
    }
    break;
  }
}

double StabilityExperimentDialog::computeGM(double _d, double _l, double _s) const
{
  if (_l <= 0.0 || _s <= 0.0 || _s >= 1.0) return 0.0;
  double D2 = _d * _d;
  double L2 = _l * _l;
  return (_l / 2.0) * (D2 / (8.0 * _s * L2) + _s - 1.0);
}

std::string StabilityExperimentDialog::makeCylinderSDF(
    double _diameter, double _length, double _density, double _rollDeg) const
{
  double r = _diameter * 0.5;
  double volume = M_PI * r * r * _length;
  double mass = _density * volume;
  double ixx = (1.0 / 12.0) * mass * (3.0 * r * r + _length * _length);
  double izz = 0.5 * mass * r * r;
  double rollRad = _rollDeg * M_PI / 180.0;

  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.6\">\n"
      << "  <model name=\"" << cylinderName_ << "\">\n"
      << "    <pose>0 0 0 " << rollRad << " 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <inertial>\n"
      << "        <mass>" << mass << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>" << ixx << "</ixx>\n"
      << "          <ixy>0</ixy>\n"
      << "          <ixz>0</ixz>\n"
      << "          <iyy>" << ixx << "</iyy>\n"
      << "          <iyz>0</iyz>\n"
      << "          <izz>" << izz << "</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry>\n"
      << "          <cylinder>\n"
      << "            <radius>" << r << "</radius>\n"
      << "            <length>" << _length << "</length>\n"
      << "          </cylinder>\n"
      << "        </geometry>\n"
      << "        <material>\n"
      << "          <ambient>1.0 0.4 0.1 1.0</ambient>\n"
      << "          <diffuse>1.0 0.5 0.2 1.0</diffuse>\n"
      << "        </material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry>\n"
      << "          <cylinder>\n"
      << "            <radius>" << r << "</radius>\n"
      << "            <length>" << _length << "</length>\n"
      << "          </cylinder>\n"
      << "        </geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
      << " name=\"keship::KSPlugin_HydroBuoyancy\">\n"
      << "      <link_name>base_link</link_name>\n"
      << "      <enable_buoyancy>true</enable_buoyancy>\n"
      << "      <enable_hydro>true</enable_hydro>\n"
      << "      <damping_mode>pose_based</damping_mode>\n"
      << "      <fluid_density>1000</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <cylinder_radius>" << r << "</cylinder_radius>\n"
      << "      <cylinder_length>" << _length << "</cylinder_length>\n"
      << "      <buoyancy name=\"cylinder_hull\">\n"
      << "        <geometry>\n"
      << "          <cylinder>\n"
      << "            <radius>" << r << "</radius>\n"
      << "            <length>" << _length << "</length>\n"
      << "          </cylinder>\n"
      << "        </geometry>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "  </model>\n"
      << "</sdf>\n";
  return sdf.str();
}

bool StabilityExperimentDialog::doDeleteCylinder()
{
  /// 先检查是否存在
  {
    std::lock_guard<std::mutex> lock(poseState_->mutex);
    bool exists = false;
    for (int i = 0; i < poseState_->latest.pose_size(); ++i)
    {
      if (poseState_->latest.pose(i).name() == cylinderName_)
        { exists = true; break; }
    }
    if (!exists) return true;  // 不存在，无需删除
  }

  gz::msgs::Entity req;
  req.set_name(cylinderName_);
  req.set_type(gz::msgs::Entity::MODEL);

  gz::msgs::Boolean rep;
  bool result = false;
  bool ok = transportNode_.Request(
      "/world/" + worldName_ + "/remove", req, 5000, rep, result);

  if (!ok || !result)
  {
    std::cout << "HS-03: 删除旧圆柱体失败" << std::endl;
    return false;
  }

  /// 等待删除完成
  for (int retry = 0; retry < 20; ++retry)
  {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> lock(poseState_->mutex);
    bool found = false;
    for (int i = 0; i < poseState_->latest.pose_size(); ++i)
    {
      if (poseState_->latest.pose(i).name() == cylinderName_)
        { found = true; break; }
    }
    if (!found) return true;
  }
  return false;
}

bool StabilityExperimentDialog::doSpawnCylinder(
    double _diameter, double _length, double _density, double _rollDeg)
{
  std::string sdfStr = makeCylinderSDF(_diameter, _length, _density, _rollDeg);

  gz::msgs::EntityFactory req;
  req.set_name(cylinderName_);
  req.set_allow_renaming(false);
  req.set_sdf(sdfStr);

  gz::msgs::Boolean rep;
  bool result = false;
  bool ok = transportNode_.Request(
      "/world/" + worldName_ + "/create/blocking", req, 20000, rep, result);

  if (!ok || !result)
  {
    std::cout << "HS-03: 生成圆柱体失败" << std::endl;
    return false;
  }

  std::cout << "HS-03: 圆柱体已生成 — D=" << _diameter
            << ", L=" << _length << ", ρ=" << _density
            << ", roll=" << _rollDeg << "°" << std::endl;
  return true;
}

void StabilityExperimentDialog::onSpawn()
{
  double D = diameterSpin_->value();
  double L = lengthSpin_->value();
  double rho = densitySpin_->value();

  double rollDeg = 5.0;  // 默认中推
  if (lightPerturbBtn_->isChecked())    rollDeg = 3.0;
  else if (heavyPerturbBtn_->isChecked()) rollDeg = 10.0;

  /// 删除旧圆柱体
  doDeleteCylinder();

  /// 重置状态
  stabilityResult_ = 0;
  observeCounter_ = 0;
  observing_ = true;
  currentHeelDeg_ = 0.0;
  currentDraft_ = 0.0;

  /// 生成新圆柱体
  if (!doSpawnCylinder(D, L, rho, rollDeg))
  {
    QMessageBox::warning(this, "错误", "无法生成圆柱体，请检查参数。");
    observing_ = false;
    return;
  }

  updateStatusDisplay();
}

void StabilityExperimentDialog::onParamChanged()
{
  updateTheoryDisplay();
}

void StabilityExperimentDialog::onRecord()
{
  if (stabilityResult_ == 0 && !observing_)
  {
    QMessageBox::information(this, "提示",
        "请先生成圆柱体并等待稳定判定完成。");
    return;
  }

  double D = diameterSpin_->value();
  double L = lengthSpin_->value();
  double rho = densitySpin_->value();
  double s = rho / kFluidDensity;
  double DL = D / L;
  double gm = computeGM(D, L, s);

  QString stableStr;
  switch (stabilityResult_)
  {
  case 1: stableStr = "稳定"; break;
  case 2: stableStr = "倾倒"; break;
  case 3: stableStr = "临界"; break;
  default: stableStr = "未判定"; break;
  }

  int row = dataTable_->rowCount();
  dataTable_->insertRow(row);
  dataTable_->setItem(row, 0, new QTableWidgetItem(QString::number(D, 'f', 2)));
  dataTable_->setItem(row, 1, new QTableWidgetItem(QString::number(L, 'f', 2)));
  dataTable_->setItem(row, 2, new QTableWidgetItem(QString::number(rho, 'f', 0)));
  dataTable_->setItem(row, 3, new QTableWidgetItem(QString::number(s, 'f', 3)));
  dataTable_->setItem(row, 4, new QTableWidgetItem(QString::number(DL, 'f', 3)));
  dataTable_->setItem(row, 5, new QTableWidgetItem(QString::number(gm, 'f', 3)));
  dataTable_->setItem(row, 6, new QTableWidgetItem(stableStr));

  /// 更新稳定域图
  if (stabilityResult_ == 1)
    stablePts_.push_back({s, DL});
  else if (stabilityResult_ == 2)
    unstablePts_.push_back({s, DL});
  chartWidget_->setData(stablePts_, unstablePts_);

  std::cout << "HS-03: 记录数据 — D=" << D << ", L=" << L
            << ", ρ=" << rho << ", s=" << s << ", D/L=" << DL
            << ", GM=" << gm << ", " << stableStr.toStdString() << std::endl;
}

void StabilityExperimentDialog::onDeleteSelected()
{
  auto selected = dataTable_->selectionModel()->selectedRows();
  if (selected.isEmpty()) return;

  /// 从后往前删，避免索引变化
  std::sort(selected.begin(), selected.end(),
            [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
  for (const auto &idx : selected)
    dataTable_->removeRow(idx.row());

  /// 从剩余的表格行重建图表数据
  stablePts_.clear();
  unstablePts_.clear();
  for (int r = 0; r < dataTable_->rowCount(); ++r)
  {
    auto *itemS = dataTable_->item(r, 3);
    auto *itemDL = dataTable_->item(r, 4);
    auto *itemSt = dataTable_->item(r, 6);
    if (!itemS || !itemDL || !itemSt) continue;
    double s = itemS->text().toDouble();
    double dl = itemDL->text().toDouble();
    QString stableStr = itemSt->text();
    if (stableStr == "稳定")
      stablePts_.push_back({s, dl});
    else if (stableStr == "倾倒")
      unstablePts_.push_back({s, dl});
  }
  chartWidget_->setData(stablePts_, unstablePts_);
  std::cout << "HS-03: 已删除 " << selected.size() << " 行" << std::endl;
}

void StabilityExperimentDialog::onClearAll()
{
  dataTable_->setRowCount(0);
  stablePts_.clear();
  unstablePts_.clear();
  chartWidget_->clearData();
  stabilityResult_ = 0;
  observing_ = false;
  observeCounter_ = 0;

  std::cout << "HS-03: 已清除全部数据" << std::endl;
}

void StabilityExperimentDialog::onUpdatePose()
{
  double heel;
  double draft;
  bool found;
  {
    std::lock_guard<std::mutex> lock(poseState_->mutex);
    heel = poseState_->heelDeg;
    draft = poseState_->draft;
    found = poseState_->found;
  }

  currentHeelDeg_ = heel;
  currentDraft_ = draft;

  if (observing_ && found)
  {
    ++observeCounter_;

    /// 提前判定
    if (std::abs(heel) < 1.5 && observeCounter_ > 30)
    {
      stabilityResult_ = 1;  // 稳定
      observing_ = false;
    }
    else if (std::abs(heel) > 70.0 && observeCounter_ > 20)
    {
      stabilityResult_ = 2;  // 倾倒
      observing_ = false;
    }
    else if (observeCounter_ >= kObserveFrames)
    {
      /// 150 帧后判定
      if (std::abs(heel) < 3.0)
        stabilityResult_ = 1;
      else if (std::abs(heel) > 50.0)
        stabilityResult_ = 2;
      else
        stabilityResult_ = 3;
      observing_ = false;
    }
  }

  updateStatusDisplay();
}
