/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SP-04 数学船型稳性对比 — 对话框实现 */

#include "HullStabilityDialog.hh"
#include "../shared/UiTheme.hh"
#include "keship/utils/LinearDamping.hh"
#include "MeshUtils.hh"   // 与 ShipHullGenerator.hh 同目录（src_cpp/utils），非 include/keship/utils
#include "../shared/UiTheme.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QPainter>
#include <QMessageBox>
#include <QFrame>
/// ★ 2026-09-21 新增：压载位置输入改 QLineEdit + 「移动压载」确认按钮 + 滑条防抖
#include <QLineEdit>
#include <QDoubleValidator>
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
#include "keship/utils/KeshipPaths.hh"
#include <gz/msgs/empty.pb.h>
#include <gz/common/Util.hh>
#include <gz/msgs/param.pb.h>
#include <gz/msgs/any.pb.h>

using keship::ShipHullSpec;
using keship::HydroProperties;

// ═══════════════════════════════════════════════════════════
// HullChartWidget
// ═══════════════════════════════════════════════════════════

HullChartWidget::HullChartWidget(QWidget *_parent) : QWidget(_parent)
{ setMinimumSize(300, 200); }

void HullChartWidget::setData(const std::vector<HullDataPoint> &_pts,
                               double _expGM, double _thGM)
{
  pts_ = _pts; expGM_ = _expGM; thGM_ = _thGM;
  xMax_ = 100.0; yMin_ = 0.0; yMax_ = 0.05;
  for (const auto &p : pts_) {
    if (p.wDy > xMax_) xMax_ = p.wDy;
    if (p.tanPhi < yMin_) yMin_ = p.tanPhi;
    if (p.tanPhi > yMax_) yMax_ = p.tanPhi;
  }
  xMax_ = std::ceil(xMax_ * 1.2 / 10.0) * 10.0;
  double yR = yMax_ - yMin_;
  yMin_ = std::floor(yMin_ * 1000.0) / 1000.0;
  yMax_ = std::ceil((yMax_ + yR * 0.3) * 1000.0) / 1000.0;
  if (xMax_ < 20.0) xMax_ = 20.0;
  if (yMax_ - yMin_ < 0.005) yMax_ = yMin_ + 0.005;
  update();
}

void HullChartWidget::clearData()
{ pts_.clear(); expGM_ = 0; thGM_ = 0; xMax_ = 100; yMax_ = 0.05; update(); }

void HullChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  int w = width(), h = height();
  if (w < 60 || h < 60) return;
  int ml = 65, mr = 30, mt = 20, mb = 45, pw = w - ml - mr, ph = h - mt - mb;
  auto toX = [&](double v) { return ml + (int)((v-xMin_)/(xMax_-xMin_)*pw); };
  auto toY = [&](double v) { return mt+ph-(int)((v-yMin_)/(yMax_-yMin_)*ph); };

  p.fillRect(rect(), Qt::white);
  p.setPen(QPen(QColor(220,220,220), 1, Qt::DashLine));
  for (int i = 0; i <= 4; ++i) {
    p.drawLine(toX(xMax_*i/4.0), mt, toX(xMax_*i/4.0), mt+ph);
    double yv = yMin_ + (yMax_-yMin_)*i/4.0;
    p.drawLine(ml, toY(yv), w-mr, toY(yv));
  }
  if (pts_.size() >= 2) {
    double sx=0,sy=0,sxy=0,sx2=0,n=pts_.size();
    for(auto &pt:pts_){sx+=pt.wDy;sy+=pt.tanPhi;sxy+=pt.wDy*pt.tanPhi;sx2+=pt.wDy*pt.wDy;}
    double d=n*sx2-sx*sx;
    if(std::abs(d)>1e-12){
      double sl=(n*sxy-sx*sy)/d, ic=(sy-sl*sx)/n;
      p.setPen(QPen(QColor(200,40,40),2));
      p.drawLine(toX(0),toY(ic),toX(xMax_),toY(sl*xMax_+ic));
    }
  }
  p.setPen(QPen(QColor(40,80,200),2));
  p.setBrush(QColor(60,120,220));
  for(auto &pt:pts_) p.drawEllipse(QPoint(toX(pt.wDy),toY(pt.tanPhi)),5,5);
  p.setPen(Qt::black); p.drawLine(ml,mt,ml,mt+ph); p.drawLine(ml,mt+ph,w-mr,mt+ph);
  QFont f; f.setPointSize(9); p.setFont(f);
  for(int i=0;i<=4;++i)p.drawText(QRect(toX(xMax_*i/4.0)-25,mt+ph+2,50,18),Qt::AlignHCenter,QString::number(xMax_*i/4.0,'f',0));
  p.drawText(QRect(ml,mt+ph+22,pw,20),Qt::AlignHCenter,"w·Δy (Nm)");
  for(int i=0;i<=4;++i){
    double yv = yMin_ + (yMax_-yMin_)*i/4.0;
    p.drawText(QRect(2,toY(yv)-8,ml-6,16),Qt::AlignRight|Qt::AlignVCenter,QString::number(yv,'f',4));
  }
  p.save(); p.translate(14,mt+ph/2); p.rotate(-90); p.drawText(QRect(-50,-10,100,20),Qt::AlignHCenter,"tanφ"); p.restore();

  int lx=w-mr-170,ly=mt+6;
  p.setPen(QPen(QColor(40,80,200),2)); p.setBrush(QColor(60,120,220));
  p.drawEllipse(QPoint(lx+6,ly+5),5,5); p.setPen(Qt::black); p.setBrush(Qt::NoBrush);
  p.drawText(QRect(lx+14,ly,140,14),"数据点");
  ly+=18; p.setPen(QPen(QColor(200,40,40),2));
  p.drawLine(lx,ly+5,lx+12,ly+5); p.setPen(Qt::black);
  p.drawText(QRect(lx+14,ly,140,14),"线性拟合");
  ly+=18;
  p.drawText(QRect(lx,ly,160,14),expGM_>0?QString("GM实验 = %1 m").arg(expGM_,0,'f',4):QString("GM实验: 数据不足"));
  ly+=16; p.drawText(QRect(lx,ly,160,14),QString("GM理论 = %1 m").arg(thGM_,0,'f',4));
}

// ═══════════════════════════════════════════════════════════
// HullStabilityDialog
// ═══════════════════════════════════════════════════════════

static ShipHullSpec mapHullSpec(int _idx, double L, double B, double D) {
  switch (_idx) {
    case 0: return ShipHullSpec::WigleyClassic(L, B, D);
    case 1: return ShipHullSpec::WigleyFlat(L, B, D);
    case 2: return ShipHullSpec::WigleySharp(L, B, D);
    case 3: return ShipHullSpec::WigleyFine(L, B, D);
    case 4: return ShipHullSpec::LewisU(L, B, D);
    case 5: return ShipHullSpec::LewisV(L, B, D);
    default: return ShipHullSpec::WigleyClassic(L, B, D);
  }
}

HullStabilityDialog::HullStabilityDialog(
    gz::transport::Node &_transportNode, const std::string &_worldName,
    QWidget *_parent)
    : QDialog(_parent), transportNode_(_transportNode), worldName_(_worldName)
{
  setWindowTitle("SP-04 数学船型稳性对比");
  setModal(false); resize(960, 720);
  poseState_ = std::make_shared<HullPoseState>();

  /// 临时目录
  keship::EnsureKeshipDataDirs();
  tempDir_ = keship::KeshipSp03Dir();

  setupUI(); initPoseSubscription();
  poseTimer_ = new QTimer(this);
  connect(poseTimer_, &QTimer::timeout, this, &HullStabilityDialog::onUpdatePose);
  poseTimer_->start(33);
  onHullTypeChanged();

  // 静水波浪参数：PMS + gain=0，WavefieldPublisher 会自动重发
  {
    gz::msgs::Param msg;
    auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(0.0);  (*p)["gain"] = v;
    auto pub = _transportNode.Advertise<gz::msgs::Param>("/keship/wavefield/parameters_set");
    pub.Publish(msg);
    std::cout << "SP-04: 已发布静水波浪参数" << std::endl;
  }

  std::cout << "SP-04: 数学船型稳性对话框已创建" << std::endl;
}

HullStabilityDialog::~HullStabilityDialog() {
  { std::lock_guard<std::mutex> lock(poseState_->mutex); poseState_->alive = false; }
  if (!poseTopic_.empty()) transportNode_.Unsubscribe(poseTopic_);
  poseTimer_->stop(); doDeleteAll();

  // 恢复波浪：PMS + gain=1.0，WavefieldPublisher 会自动重发
  {
    gz::msgs::Param msg;
    auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(1.0);  (*p)["gain"] = v;
    auto pub = transportNode_.Advertise<gz::msgs::Param>("/keship/wavefield/parameters_set");
    pub.Publish(msg);
    std::cout << "SP-04: 已恢复波浪参数" << std::endl;
  }
}

void HullStabilityDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  auto *desc = new QLabel("<b>实验目的：</b>对比不同数学船型的水线面形状与稳性差异。<br>"
    "<b>操作：</b>①选择船型 → ②点击[生成] → ③移动压载 → ④记录数据。", this);
  desc->setWordWrap(true); mainLayout->addWidget(desc);

  auto *topRow = new QHBoxLayout();
  auto *paramGroup = new QGroupBox("船型参数", this);
  auto *paramForm = new QFormLayout(paramGroup);

  hullTypeCombo_ = new QComboBox(this);
  hullTypeCombo_->addItems({"Wigley 经典", "Wigley 平底", "Wigley 尖舭", "Wigley 瘦削", "Lewis U", "Lewis V"});
  paramForm->addRow("船型:", hullTypeCombo_);

  lengthSpin_ = new QDoubleSpinBox(this);
  lengthSpin_->setRange(2,20); lengthSpin_->setValue(6); lengthSpin_->setSuffix(" m"); lengthSpin_->setDecimals(1);
  paramForm->addRow("L:", lengthSpin_);
  breadthSpin_ = new QDoubleSpinBox(this);
  breadthSpin_->setRange(0.5,5); breadthSpin_->setValue(1.5); breadthSpin_->setSuffix(" m"); breadthSpin_->setDecimals(2);
  paramForm->addRow("B:", breadthSpin_);
  depthSpin_ = new QDoubleSpinBox(this);
  depthSpin_->setRange(0.3,3); depthSpin_->setValue(1.0); depthSpin_->setSuffix(" m"); depthSpin_->setDecimals(2);
  paramForm->addRow("D:", depthSpin_);

  auto *hydroFrame = new QFrame(this); hydroFrame->setFrameStyle(QFrame::StyledPanel);
  auto *hydroForm = new QFormLayout(hydroFrame);
  awLabel_ = new QLabel; cwpLabel_ = new QLabel; ciLabel_ = new QLabel;
  volLabel_ = new QLabel; kbLabel_ = new QLabel; bmLabel_ = new QLabel; gmLabel_ = new QLabel;
  hydroForm->addRow("Aw:", awLabel_); hydroForm->addRow("Cwp:", cwpLabel_); hydroForm->addRow("Ci:", ciLabel_);
  hydroForm->addRow("Vol:", volLabel_); hydroForm->addRow("KB:", kbLabel_); hydroForm->addRow("BM:", bmLabel_);
  hydroForm->addRow("GM(th):", gmLabel_);
  paramForm->addRow(hydroFrame);

  spawnBtn_ = new QPushButton("生成船型 + 压载块", this);
  spawnBtn_->setMinimumHeight(30); paramForm->addRow(spawnBtn_);
  topRow->addWidget(paramGroup);

  auto *ctlGroup = new QGroupBox("压载控制", this);
  auto *ctlForm = new QFormLayout(ctlGroup);
  weightSlider_ = new QSlider(Qt::Horizontal, this);
  weightSlider_->setRange(0, 500); weightSlider_->setValue(250);
  sliderValueLabel_ = new QLabel("0.000 m", this); sliderValueLabel_->setStyleSheet("font-weight:bold;");
  /// ★ 2026-09-21：压载位置输入 **由 QDoubleSpinBox 改为 QLineEdit**（与 SP-03 同一处改造）。
  /// 原因：QDoubleSpinBox 自动格式化成固定小数位（decimals=3 → "0.000"），
  /// "在小数点后插入一个 1" → "0.1000" 被判非法 → **按键被吞**（SP-03 实测同现象）。
  weightEdit_ = new QLineEdit("0", this);
  auto *wv = new QDoubleValidator(-0.25, 0.25, 6, this);
  wv->setNotation(QDoubleValidator::StandardNotation);
  weightEdit_->setValidator(wv);
  weightEdit_->setMaximumWidth(90);
  weightEdit_->setPlaceholderText("0.1");
  weightEdit_->setToolTip("压载横向位置（m），范围 −0.25 ~ 0.25；输入后点『移动压载』才生效");

  applyBtn_ = new QPushButton("移动压载", this);
  applyBtn_->setToolTip("把压载移动到左侧输入框指定的位置（只有点这里才会真正移动）");

  auto *wRow = new QHBoxLayout();
  wRow->addWidget(weightSlider_, 3);
  wRow->addWidget(weightEdit_, 1);
  wRow->addWidget(new QLabel("m", this));
  wRow->addWidget(applyBtn_);
  ctlForm->addRow("压载 y:", sliderValueLabel_); ctlForm->addRow(wRow);

  auto *applyHint = new QLabel("输入后点『移动压载』生效；也可直接拖动滑条（松手 0.3 s 后移动）", this);
  applyHint->setStyleSheet("color:#666; font-size:11px;"); applyHint->setWordWrap(true);
  ctlForm->addRow(applyHint);

  auto *sep = new QFrame(this); sep->setFrameStyle(QFrame::HLine); ctlForm->addRow(sep);
  heelLabel_ = new QLabel("— °", this);
  heelLabel_->setStyleSheet("font-size:18px; font-weight:bold;"); ctlForm->addRow("横倾角 φ:", heelLabel_);
  deltaYLabel_ = new QLabel; wDyLabel_ = new QLabel;
  ctlForm->addRow("Δy:", deltaYLabel_); ctlForm->addRow("w·Δy:", wDyLabel_);
  ctlGroup->setMinimumWidth(300); topRow->addWidget(ctlGroup);   ///< 2026-09-21：250 太挤
  mainLayout->addLayout(topRow);

  auto *btnRow = new QHBoxLayout();
  recordBtn_ = new QPushButton("记录数据", this);
  resetBtn_ = new QPushButton("压载归零", this);
  auto *delBtn = new QPushButton("删除选中", this);
  UiTheme::setButtonIcon(delBtn, UiTheme::Icon::kDelete);
  clearBtn_ = new QPushButton("清除全部", this);
  btnRow->addWidget(recordBtn_); btnRow->addWidget(resetBtn_);
  btnRow->addWidget(delBtn); btnRow->addWidget(clearBtn_); btnRow->addStretch();
  for (QPushButton *b : {recordBtn_, resetBtn_, delBtn, clearBtn_}) b->setMinimumHeight(30);
  mainLayout->addLayout(btnRow);

  dataTable_ = new QTableWidget(0, 5, this);
  dataTable_->setHorizontalHeaderLabels({"y(m)","Δy(m)","φ(°)","tanφ","w·Δy(Nm)"});
  dataTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  dataTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  dataTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  dataTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  dataTable_->setMinimumHeight(100); mainLayout->addWidget(dataTable_, 1);
  chartWidget_ = new HullChartWidget(this); mainLayout->addWidget(chartWidget_, 2);

  connect(spawnBtn_, &QPushButton::clicked, this, &HullStabilityDialog::onSpawn);
  connect(weightSlider_, &QSlider::valueChanged, this, &HullStabilityDialog::onSliderChanged);
  connect(recordBtn_, &QPushButton::clicked, this, &HullStabilityDialog::onRecord);
  connect(resetBtn_, &QPushButton::clicked, this, &HullStabilityDialog::onResetWeight);
  connect(clearBtn_, &QPushButton::clicked, this, &HullStabilityDialog::onClearAll);
  connect(delBtn, &QPushButton::clicked, this, &HullStabilityDialog::onDeleteSelected);
  connect(hullTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &HullStabilityDialog::onHullTypeChanged);
  connect(lengthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onHullTypeChanged(); });
  connect(breadthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onHullTypeChanged(); });
  connect(depthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onHullTypeChanged(); });

  /// ★ 2026-09-21：删掉原「滑条↔spinbox 双向同步、各自直接 moveWeight」的两条连接
  /// （与 SP-03 同因：moveWeight 走 `/set_pose/blocking`，同步阻塞 RPC 最长 2 s，
  ///  逐键 / 逐格触发会冻住 UI）。现在只有滑条一条连接，且内部防抖 300 ms。
  connect(applyBtn_, &QPushButton::clicked, this, &HullStabilityDialog::onApplyWeight);
  moveDebounce_ = new QTimer(this);
  moveDebounce_->setSingleShot(true);
  connect(moveDebounce_, &QTimer::timeout, this, &HullStabilityDialog::onMoveDebounced);
}

void HullStabilityDialog::initPoseSubscription() {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  auto state = poseState_; std::string name = hullName_;
  transportNode_.Subscribe(poseTopic_,
    std::function<void(const gz::msgs::Pose_V&)>(
      [state, name](const gz::msgs::Pose_V &msg) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return; state->latest = msg; state->found = false;
        for (int i = 0; i < msg.pose_size(); ++i) {
          if (msg.pose(i).name() == name) {
            state->hullZ = msg.pose(i).position().z();
            auto &ori = msg.pose(i).orientation();
            gz::math::Quaterniond q(ori.w(),ori.x(),ori.y(),ori.z());
            state->heelDeg = q.Roll()*180.0/M_PI; state->found = true; break;
          }
        }
      }));
}

void HullStabilityDialog::onHullTypeChanged() {
  double L = lengthSpin_->value(), B = breadthSpin_->value(), D = depthSpin_->value();
  params_ = mapHullSpec(hullTypeCombo_->currentIndex(), L, B, D);
  keship::ShipHullGenerator hull(params_);
  hydro_ = hull.hydroProps();
  updateHydroDisplay();
}

void HullStabilityDialog::updateHydroDisplay() {
  /// ★ 2026-09-20 修复（缺陷 ⑩）：本面板**全部 7 个值**原用历史口径，显示给学生的都是错的 ——
  /// `hydro_.Aw/Ix` 取 **z=0（甲板，该船型最宽处）** 水线面；`hydro_.volume/KB` 按**全深度**积分
  /// （故 `volume` 是"全船体积"、`KB` 是"距甲板深度"）；`GM = KB + BM − KG` **坐标系混用**。
  /// 现改用**设计吃水处**的新字段（由 `computeVolumeCurve()` 末尾算出）：
  /// `AwD/IxD/volumeD/KBup/BMd/GMd`。**`hydro_.volume` 仍是全船体积**
  /// （4 处 `m = rho*volume` 依赖它，不能改 —— 见 §26）。详见 `plans/headless_ctl_plan.md` §26。
  const double LB = params_.L * params_.B;
  const double LB3_12 = params_.L * params_.B * params_.B * params_.B / 12.0;
  awLabel_->setText(QString::number(hydro_.AwD,'f',3)+" m²");
  cwpLabel_->setText(QString::number(LB > 1e-9 ? hydro_.AwD / LB : 0.0,'f',3));
  ciLabel_->setText(QString::number(LB3_12 > 1e-9 ? hydro_.IxD / LB3_12 : 0.0,'f',3));
  volLabel_->setText(QString::number(hydro_.volumeD,'f',3)+" m³");
  kbLabel_->setText(QString::number(hydro_.KBup,'f',3)+" m");   ///< 距**龙骨**高度
  bmLabel_->setText(QString::number(hydro_.BMd,'f',4)+" m");
  double GM = hydro_.GMd;
  gmLabel_->setText(QString::number(GM,'f',4)+" m");
  gmLabel_->setStyleSheet(
      QString("font-weight:bold;color:%1;")
          .arg(UiTheme::statusColor(GM > 0.01 ? UiTheme::Status::kOk
                                              : UiTheme::Status::kFail)));
}

void HullStabilityDialog::updateStatusDisplay() {
  heelLabel_->setText(QString::number(currentHeelDeg_,'f',2)+" °");
  if (quiescent_&&std::abs(currentHeelDeg_)<5.0)
    heelLabel_->setStyleSheet(QString("font-size:18px;font-weight:bold;color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kOk)));
  else if (quiescent_)
    heelLabel_->setStyleSheet(QString("font-size:18px;font-weight:bold;color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kWarn)));
  else
    heelLabel_->setStyleSheet(QString("font-size:18px;font-weight:bold;color:%1;").arg(UiTheme::statusColor(UiTheme::Status::kDisabled)));
  double wDy = kWeightForce*std::abs(weightY_);
  deltaYLabel_->setText(QString::number(weightY_,'f',3)+" m");
  wDyLabel_->setText(QString::number(wDy,'f',1)+" Nm");
}

namespace {
/// 流体密度 / 重力 —— **必须与下面 SDF 的 <fluid_density> 同源**
/// （P3-2 教训：同一个量写两处，改一处必漏一处）
constexpr double kFluidDensity = 1000.0;
constexpr double kGravity = 9.81;
}  // namespace

std::string HullStabilityDialog::makeHullSDF() const {
  double L = params_.L, B = params_.B, D = params_.D;

  // 构造视觉和浮力网格（ShipHullGenerator v2 — Gazebo Z_UP 直接生成）
  ShipHullSpec visSpec = params_;
  visSpec.visNx = 40; visSpec.visNz = 20;
  visSpec.buoyNx = 20; visSpec.buoyNz = 10;
  keship::ShipHullGenerator hull(visSpec);

  /// ★ 文件名带**网格内容指纹**（2026-09-19 修，与 SP-01 同一处错）：
  /// gz 的 gz::common::MeshManager **按文件路径**缓存网格，同路径换内容仍返回旧网格
  /// → 改船型参数后"生成"显示的仍是第一艘船。详见 ShipHullGenerator::meshTag() 注释。
  const std::string meshTag = hull.meshTag();
  std::string visPath = tempDir_ + "hull_vis_" + meshTag + ".obj";
  std::string buoyPath = tempDir_ + "hull_buoy_" + meshTag + ".obj";
  hull.writeVisualOBJ(visPath);
  hull.writeBuoyancyOBJ(buoyPath);

  // 质量 = 船体密度 × 实际排水体积
  double actualVol = hull.hydroProps().volume;
  double m = params_.rho * actualVol;
  double ixx = m/12.0*(B*B + D*D);
  double iyy = m/12.0*(L*L + D*D);
  double izz = m/12.0*(L*L + B*B);

  /// ── 线性阻尼（2026-09-19 新增，与 SP-01 的 7.5 ① 同一修法）──────────────
  /// 本实验要求"等横倾角稳定"才允许记录（判据 |Δheel|<0.05°/帧 × 15 帧，见 cc:563-564），
  /// 而**网格船体走不了插件的 pose_based 分支**（该分支需 hasCylinderGeometry_）→
  /// 原先 `<enable_hydro>false</enable_hydro>` 等于零阻尼 → 横摇 60 s 不衰减 →
  /// 判据永不满足、"记录数据"永远点不了。
  /// 详见 doc/plans/headless_ctl_plan.md §14。
  double A_wp = L * B * 0.8;   ///< 兜底（体积曲线不可用时按水线面系数 0.8 估）
  {
    const auto &dc = hull.hydroProps().draftCurve;
    const auto &vc = hull.hydroProps().volumeCurve;
    if (dc.size() >= 2 && vc.size() == dc.size())
    {
      const size_t k = dc.size() / 2;
      if (k > 0 && k + 1 < dc.size())
        A_wp = (vc[k+1] - vc[k-1]) / (dc[k+1] - dc[k-1]);
      else
        A_wp = (vc[1] - vc[0]) / (dc[1] - dc[0]);
      if (!(A_wp > 1e-6)) A_wp = L * B * 0.8;
    }
  }
  const keship::LinearDampingCoeffs damp = keship::MakeLinearDamping(
      A_wp, L, B, m, ixx, iyy, 0.15, 0.10, kFluidDensity, kGravity);

  std::ostringstream sdf;
  // SDF <pose> 为占位符，真正 spawn 位置由 EntityFactory 请求设定（对齐工作代码）
  sdf << "<?xml version=\"1.0\"?>\n<sdf version=\"1.6\">\n"
      << "  <model name=\"" << hullName_ << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      // 均质船体重心在几何中心 z=-D/2（相对 link 原点）
      << "      <inertial>\n"
      << "        <pose>0 0 " << (-D/2.0) << " 0 0 0</pose>\n"
      << "        <mass>" << m << "</mass>\n"
      << "        <inertia>\n"
      << "          <ixx>" << ixx << "</ixx><ixy>0</ixy><ixz>0</ixz>\n"
      << "          <iyy>" << iyy << "</iyy><iyz>0</iyz>\n"
      << "          <izz>" << izz << "</izz>\n"
      << "        </inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"visual\">\n"
      << "        <geometry><mesh><uri>file://" << visPath << "</uri></mesh></geometry>\n"
      << "        <material><ambient>0.6 0.5 0.4 1</ambient><diffuse>0.7 0.6 0.5 1</diffuse></material>\n"
      << "      </visual>\n"
      << "      <collision name=\"collision\">\n"
      << "        <geometry><mesh><uri>file://" << visPath << "</uri></mesh></geometry>\n"
      << "      </collision>\n"
      << "    </link>\n"
      << "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\" name=\"keship::KSPlugin_HydroBuoyancy\">\n"
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
      << "      <buoyancy name=\"hull\">\n"
      << "        <geometry>\n"
      << "          <mesh><uri>" << buoyPath << "</uri></mesh>\n"
      << "        </geometry>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "  </model>\n</sdf>\n";
  return sdf.str();
}

std::string HullStabilityDialog::makeWeightSDF(double _y) const {
  double wh = 0.2;
  // 对齐 SP-03: deckZ = 甲板高度 + 压载半高 + 间隙
  // SP-03: H/2 + wh/2 + 0.02 (驳船 centered box, 甲板在 H/2)
  // SP-04: 0 + wh/2 + 0.02   (船体 mesh 甲板在 z=0)
  double deckZ = wh/2.0 + 0.02;
  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n<sdf version=\"1.6\">\n"
      << "  <model name=\"" << weightName_ << "\">\n"
      << "    <pose>0 " << _y << " " << deckZ << " 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
      << "      <inertial><mass>"<<kWeightMass<<"</mass>\n"
      << "        <inertia><ixx>1.0</ixx><ixy>0</ixy><ixz>0</ixz>"
      << "<iyy>1.0</iyy><iyz>0</iyz><izz>1.0</izz></inertia>\n"
      << "      </inertial>\n"
      << "      <visual name=\"vis\"><geometry><box><size>0.3 0.2 0.2</size></box></geometry>\n"
      << "        <material><ambient>1.0 0.15 0.15 1.0</ambient><diffuse>1.0 0.2 0.2 1.0</diffuse></material>\n"
      << "      </visual>\n"
      << "      <collision name=\"col\"><geometry><box><size>0.3 0.2 0.2</size></box></geometry></collision>\n"
      << "    </link>\n"
      << "  </model>\n</sdf>\n";
  return sdf.str();
}

bool HullStabilityDialog::resolveEntityID(const std::string &_name, uint32_t &_id) {
  gz::msgs::Empty req; gz::msgs::Scene rep; bool r = false;
  if (!transportNode_.Request("/world/"+worldName_+"/scene/info", req, 5000, rep, r) || !r) return false;
  for (int i = 0; i < rep.model_size(); ++i)
    if (rep.model(i).name() == _name) { _id = rep.model(i).id(); return true; }
  return false;
}

bool HullStabilityDialog::moveWeight(double _y) {
  if (weightEntityId_ == 0) return false;
  double wh = 0.2;
  // 对齐 SP-03: deckZ = 甲板世界Z + 压载半高 + 间隙
  double deckZ = currentHullZ_ + wh/2.0 + 0.02;
  gz::msgs::Pose req; req.set_id(weightEntityId_);
  req.mutable_position()->set_x(0); req.mutable_position()->set_y(_y); req.mutable_position()->set_z(deckZ);
  gz::math::Quaterniond q(0,0,0);
  req.mutable_orientation()->set_w(q.W()); req.mutable_orientation()->set_x(q.X());
  req.mutable_orientation()->set_y(q.Y()); req.mutable_orientation()->set_z(q.Z());
  gz::msgs::Boolean rep; bool r = false;
  bool ok = transportNode_.Request("/world/"+worldName_+"/set_pose/blocking", req, 2000, rep, r);
  return ok && r && rep.data();
}

bool HullStabilityDialog::doDeleteAll() {
  for (auto &[name, eid] : std::vector<std::pair<std::string, uint32_t*>>{{weightName_, &weightEntityId_}, {hullName_, &hullEntityId_}}) {
    if (*eid == 0) continue;
    gz::msgs::Entity req; req.set_name(name); req.set_type(gz::msgs::Entity::MODEL);
    gz::msgs::Boolean rep; bool r = false;
    transportNode_.Request("/world/"+worldName_+"/remove", req, 5000, rep, r);
    for (int retry = 0; retry < 20; ++retry) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      uint32_t id = 0; if (!resolveEntityID(name, id)) break;
    }
    *eid = 0;
  }
  spawned_ = false; return true;
}

bool HullStabilityDialog::doSpawnAll() {
  doDeleteAll();
  /// 回收上一轮的内容寻址网格（2026-09-19，缺陷 D 的配套，同 SP-01）。
  /// 旧模型已由 doDeleteAll() 删除、KeshipSp03Dir() 由本对话框独占 → 可安全清理；
  /// 必须在 makeHullSDF() 之前，否则会删掉本轮刚写出的网格。
  keship::RemoveStaleMeshFiles(tempDir_, "hull_vis");
  keship::RemoveStaleMeshFiles(tempDir_, "hull_buoy");
  dataTable_->setRowCount(0); recordedData_.clear(); chartWidget_->clearData();

  std::string sdf = makeHullSDF();
  gz::msgs::EntityFactory req; req.set_name(hullName_);
	  req.set_allow_renaming(false);
	  req.set_sdf(sdf);
	  auto *pose = req.mutable_pose();
	  pose->mutable_position()->set_x(0);
	  pose->mutable_position()->set_y(0);
	  pose->mutable_position()->set_z(params_.D / 2.0 + 0.1);
  gz::msgs::Boolean rep; bool r = false;
  if (!transportNode_.Request("/world/"+worldName_+"/create/blocking", req, 20000, rep, r) || !r) return false;
  for (int retry = 0; retry < 20; ++retry) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (resolveEntityID(hullName_, hullEntityId_)) break;
  }

  sdf = makeWeightSDF(0);
  req.set_name(weightName_); req.set_sdf(sdf);
  transportNode_.Request("/world/"+worldName_+"/create/blocking", req, 20000, rep, r);
  for (int retry = 0; retry < 20; ++retry) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (resolveEntityID(weightName_, weightEntityId_)) break;
  }

  spawnedHydro_ = hydro_;
  weightY_ = 0;
  if (moveDebounce_) moveDebounce_->stop();
  weightSlider_->blockSignals(true); weightSlider_->setValue(250); weightSlider_->blockSignals(false);
  weightEdit_->blockSignals(true); weightEdit_->setText("0"); weightEdit_->blockSignals(false);
  sliderValueLabel_->setText("0.000 m"); quiescent_ = false; settleCounter_ = 0; spawned_ = true;
  std::cout << "SP-04: 船型+压载块已生成" << std::endl;
  return true;
}

void HullStabilityDialog::onSpawn() { if (!doSpawnAll()) QMessageBox::warning(this,"错误","生成失败"); updateHydroDisplay(); updateStatusDisplay(); }
void HullStabilityDialog::onSliderChanged(int v) {
  double y = (v/500.0-0.5)*0.5;
  /// 纯 UI 更新：输入框与读数立即跟随（不下发 RPC）
  weightY_ = y;
  weightEdit_->blockSignals(true); weightEdit_->setText(QString::number(y,'f',3)); weightEdit_->blockSignals(false);
  sliderValueLabel_->setText(QString::number(y,'f',3)+" m");
  if (spawned_) {
    /// ★ 先置"未稳定"：防抖窗口内若点了"记录"，此时压载尚未移动 → 让 onRecord 拒绝
    quiescent_ = false; settleCounter_ = 0; pendingY_ = y;
    moveDebounce_->start(300);      ///< 停手 300 ms 才真正下发
  }
  updateStatusDisplay();
}

/// ★ 2026-09-21 新增：移动压载的统一入口（与 SP-03 同一套写法）
void HullStabilityDialog::commitMove(double _y) {
  weightY_ = _y;
  sliderValueLabel_->setText(QString::number(_y,'f',3)+" m");
  if (spawned_) { quiescent_ = false; settleCounter_ = 0; moveWeight(_y); }
  updateStatusDisplay();
}

void HullStabilityDialog::onMoveDebounced() { commitMove(pendingY_); }

/// ★ 2026-09-21 新增：「移动压载」确认按钮 —— 只有点它，输入才真正下发
void HullStabilityDialog::onApplyWeight() {
  const QString raw = weightEdit_->text().trimmed();
  bool ok = false;
  double y = raw.toDouble(&ok);
  if (!ok) {
    QMessageBox::warning(this, "输入无效",
        QString("无法识别 \"%1\"。请输入一个数值，范围 −0.25 ~ 0.25 m。").arg(raw));
    return;
  }
  if (y < -0.25 || y > 0.25) {
    y = std::clamp(y, -0.25, 0.25);
    QMessageBox::information(this, "超出量程",
        QString("压载位置超出 ±0.25 m，已按 %1 m 处理。").arg(QString::number(y,'f',3)));
  }
  if (moveDebounce_) moveDebounce_->stop();   ///< 与滑条防抖互斥
  pendingY_ = y;
  const int v = std::clamp((int)((y/0.5+0.5)*500.0), 0, 500);
  weightSlider_->blockSignals(true); weightSlider_->setValue(v); weightSlider_->blockSignals(false);
  weightEdit_->blockSignals(true); weightEdit_->setText(QString::number(y,'f',3)); weightEdit_->blockSignals(false);
  commitMove(y);
}

void HullStabilityDialog::onRecord() {
  if (!spawned_) { QMessageBox::information(this,"提示","请先生成船型"); return; }
  if (!quiescent_) { QMessageBox::information(this,"提示","请等待横倾角稳定"); return; }
  /// ★ 2026-09-20 修复：`tanφ` 必须与 `w·Δy` **口径一致**（都取幅值）。
  /// 原实现 `tanP` 保留符号、而 `wDy` 用 `std::abs(dY)`（恒 ≥0）
  /// ⇒ 压载移到**负侧**时记录点变成 (+w·Δy, −|tanφ|)，与正侧点符号自相矛盾
  /// ⇒ `computeLinearFit` 的斜率被破坏、`GM实验 = 1/(disp·slope)` 全废。
  /// （与 SP-03 `IncliningExperimentDialog.cc` 同一处缺陷、同一修法；图表 x 轴同样硬编码从 0 起。）
  double dY = weightY_, tanP = std::abs(std::tan(currentHeelDeg_*M_PI/180.0)), wDy = kWeightForce*std::abs(dY);
  recordedData_.push_back({weightY_, dY, currentHeelDeg_, tanP, wDy});
  int row = dataTable_->rowCount(); dataTable_->insertRow(row);
  dataTable_->setItem(row,0,new QTableWidgetItem(QString::number(weightY_,'f',3)));
  dataTable_->setItem(row,1,new QTableWidgetItem(QString::number(dY,'f',3)));
  dataTable_->setItem(row,2,new QTableWidgetItem(QString::number(currentHeelDeg_,'f',2)));
  dataTable_->setItem(row,3,new QTableWidgetItem(QString::number(tanP,'f',4)));
  dataTable_->setItem(row,4,new QTableWidgetItem(QString::number(wDy,'f',1)));
  updateChart();
}

void HullStabilityDialog::onResetWeight() {
  if (moveDebounce_) moveDebounce_->stop();
  commitMove(0);   ///< 统一入口：记录 y、重置判稳、下发、刷新
  weightSlider_->blockSignals(true); weightSlider_->setValue(250); weightSlider_->blockSignals(false);
  weightEdit_->blockSignals(true); weightEdit_->setText("0"); weightEdit_->blockSignals(false);
  updateStatusDisplay();
}

void HullStabilityDialog::onDeleteSelected() {
  auto sel = dataTable_->selectionModel()->selectedRows(); if (sel.isEmpty()) return;
  std::sort(sel.begin(), sel.end(), [](auto &a, auto &b) { return a.row() > b.row(); });
  for (auto &i : sel) dataTable_->removeRow(i.row());
  recordedData_.clear();
  for (int r = 0; r < dataTable_->rowCount(); ++r) {
    auto *i0=dataTable_->item(r,0),*i1=dataTable_->item(r,1),*i2=dataTable_->item(r,2);
    auto *i3=dataTable_->item(r,3),*i4=dataTable_->item(r,4);
    if (!i0||!i1||!i2||!i3||!i4) continue;
    recordedData_.push_back({i0->text().toDouble(),i1->text().toDouble(),
      i2->text().toDouble(),i3->text().toDouble(),i4->text().toDouble()});
  }
  updateChart();
}

void HullStabilityDialog::onClearAll() { dataTable_->setRowCount(0); recordedData_.clear(); chartWidget_->clearData(); }
void HullStabilityDialog::onUpdatePose() {
  double heel, hullZ; bool found;
  { std::lock_guard<std::mutex> lock(poseState_->mutex);
    heel = poseState_->heelDeg; hullZ = poseState_->hullZ; found = poseState_->found; }
  currentHeelDeg_ = heel;
  currentHullZ_ = hullZ;
  if (spawned_ && found) {
    if (std::abs(heel-lastHeelDeg_) < 0.05) settleCounter_++; else settleCounter_ = 0;
    if (settleCounter_ >= 15) quiescent_ = true;
    lastHeelDeg_ = heel;
  }
  updateStatusDisplay();
}

void HullStabilityDialog::updateChart() {
  /// ★ 2026-09-20 修复（缺陷 ⑩）：
  /// ① `gmTh` 原为 `KB + BM − D/2`（KB 是"距甲板深度"却当"距龙骨高度"用 + KG 粗估）
  ///    → 改用设计吃水处的 `GMd`（显示值从 0.0678 → 0.1825，见 §26/§31.4）；
  /// ② `disp` 原用 `hydroProps().volume`（**全船体积** 3.9969）→ Δ = 39240 N
  ///    比真实船重 20605 N **大 1.903 倍** → `GM实验` 只有真值的 **52.6%**
  ///    → 改用**设计吃水处排水体积** `volumeD`（= 船体自身排水量，与真实船重一致）。
  /// 实测（§31.4）：修前 GUI 显示 `GM理论=0.0679`、`GM实验=0.0966`（比值 1.42，互相矛盾）；
  /// 修后应为 `0.1825` 与 `≈0.178`（比值 ≈0.98，自洽）。
  double gmTh = spawnedHydro_.GMd;
  if (recordedData_.size() < 2) { chartWidget_->setData(recordedData_, 0, gmTh); return; }
  double sl=0,ic=0; computeLinearFit(recordedData_, sl, ic);
  double disp = kFluidDensity * kGravity * spawnedHydro_.volumeD;
  double gmExp = (std::abs(sl) > 1e-12) ? 1.0/(disp*sl) : 0;
  chartWidget_->setData(recordedData_, gmExp, gmTh);
}

void HullStabilityDialog::computeLinearFit(const std::vector<HullDataPoint> &_pts,
                                            double &_sl, double &_ic) const {
  size_t n = _pts.size(); if (n<2){_sl=0;_ic=0;return;}
  double sx=0,sy=0,sxy=0,sx2=0;
  for(auto &p:_pts){sx+=p.wDy;sy+=p.tanPhi;sxy+=p.wDy*p.tanPhi;sx2+=p.wDy*p.wDy;}
  double d=n*sx2-sx*sx; if(std::abs(d)<1e-12){_sl=0;_ic=0;return;}
  _sl = (n*sxy-sx*sy)/d; _ic = (sy-_sl*sx)/n;
}
