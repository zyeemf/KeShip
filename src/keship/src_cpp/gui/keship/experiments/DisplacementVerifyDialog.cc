/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase C: 排水量验证实验对话框实现 */

#include "DisplacementVerifyDialog.hh"
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
#include <thread>
#include <chrono>

#include <gz/math/Quaternion.hh>
#include <gz/msgs/entity_factory.pb.h>
#include <gz/msgs/boolean.pb.h>
#include <gz/msgs/double.pb.h>
#include <gz/msgs/param.pb.h>
#include <gz/msgs/any.pb.h>
#include <gz/msgs/pose.pb.h>
#include "keship/utils/KeshipPaths.hh"
#include "MeshUtils.hh"   // 与 ShipHullGenerator.hh 同目录（src_cpp/utils），非 include/keship/utils
#include <gz/msgs/scene.pb.h>
#include <gz/msgs/empty.pb.h>
#include <gz/msgs/entity.pb.h>
#include <gz/common/Util.hh>

using keship::ShipHullSpec;

static ShipHullSpec mapType(int _idx, double L, double B, double D, double rho) {
  switch (_idx) {
    case 0: return ShipHullSpec::WigleyClassic(L, B, D, rho);
    case 1: return ShipHullSpec::WigleyFlat(L, B, D, rho);
    case 2: return ShipHullSpec::WigleySharp(L, B, D, rho);
    case 3: return ShipHullSpec::WigleyFine(L, B, D, rho);
    case 4: return ShipHullSpec::LewisU(L, B, D, rho);
    case 5: return ShipHullSpec::LewisV(L, B, D, rho);
    default: return ShipHullSpec::WigleyClassic(L, B, D, rho);
  }
}

// ═══════════════════════════════════════════════════════════
// DispChartWidget
// ═══════════════════════════════════════════════════════════

DispChartWidget::DispChartWidget(QWidget *_parent) : QWidget(_parent) {
  setMinimumSize(300, 200);
}

void DispChartWidget::setTheory(const std::vector<double> &_drafts,
                                 const std::vector<double> &_volumes) {
  thDrafts_ = _drafts; thVolumes_ = _volumes;
  if (!thDrafts_.empty()) { dMin_ = 0; dMax_ = thDrafts_.back() * 1.05; }
  if (!thVolumes_.empty()) { vMin_ = 0; vMax_ = thVolumes_.back() * 1.2; }
  update();
}

void DispChartWidget::setData(const std::vector<DispDataPoint> &_pts) {
  pts_ = _pts; update();
}

void DispChartWidget::clearData() { pts_.clear(); update(); }

void DispChartWidget::paintEvent(QPaintEvent *) {
  QPainter p(this); p.setRenderHint(QPainter::Antialiasing);
  int w = width(), h = height();
  if (w < 60 || h < 60) return;
  p.fillRect(rect(), Qt::white);

  int ml = 65, mr = 30, mt = 20, mb = 45, pw = w - ml - mr, ph = h - mt - mb;
  auto tx = [&](double v) { return ml + (int)(v / dMax_ * pw); };
  auto ty = [&](double v) { return mt + ph - (int)(v / vMax_ * ph); };

  // 网格
  p.setPen(QPen(QColor(220,220,220), 1, Qt::DotLine));
  for (int i = 0; i <= 4; ++i) {
    p.drawLine(tx(dMax_*i/4), mt, tx(dMax_*i/4), mt+ph);
    p.drawLine(ml, ty(vMax_*i/4), w-mr, ty(vMax_*i/4));
  }

  // 理论曲线
  if (thDrafts_.size() >= 2) {
    p.setPen(QPen(QColor(200, 40, 40), 2));
    QPainterPath path;
    path.moveTo(tx(thDrafts_[0]), ty(thVolumes_[0]));
    for (size_t i = 1; i < thDrafts_.size(); ++i)
      path.lineTo(tx(thDrafts_[i]), ty(thVolumes_[i]));
    p.drawPath(path);
  }

  // 实测数据点
  p.setPen(QPen(QColor(40, 80, 200), 2));
  p.setBrush(QColor(60, 120, 220));
  for (auto &pt : pts_)
    p.drawEllipse(QPoint(tx(pt.draft), ty(pt.volMeas)), 6, 6);

  // 坐标轴
  p.setPen(Qt::black); p.setBrush(Qt::NoBrush);
  p.drawLine(ml, mt, ml, mt+ph);
  p.drawLine(ml, mt+ph, w-mr, mt+ph);

  QFont f; f.setPointSize(9); p.setFont(f);
  for (int i = 0; i <= 4; ++i)
    p.drawText(QRect(tx(dMax_*i/4)-25, mt+ph+2, 50, 18), Qt::AlignHCenter,
               QString::number(dMax_*i/4, 'f', 2));
  p.drawText(QRect(ml, mt+ph+22, pw, 20), Qt::AlignHCenter, "吃水 d (m)");
  for (int i = 0; i <= 4; ++i) {
    double yv = vMax_ * i / 4;
    p.drawText(QRect(2, ty(yv)-8, ml-6, 16), Qt::AlignRight|Qt::AlignVCenter,
               QString::number(yv, 'f', 3));
  }
  p.save(); p.translate(14, mt+ph/2); p.rotate(-90);
  p.drawText(QRect(-50, -10, 100, 20), Qt::AlignHCenter, "∇ (m³)"); p.restore();

  // 图例
  int lx = w - mr - 160, ly = mt + 4;
  p.setPen(QPen(QColor(200,40,40),2)); p.drawLine(lx,ly+5,lx+14,ly+5);
  p.setPen(Qt::black); p.drawText(QRect(lx+18,ly,100,14), "理论曲线");
  ly += 18;
  p.setPen(QPen(QColor(40,80,200),2)); p.setBrush(QColor(60,120,220));
  p.drawEllipse(QPoint(lx+7,ly+5),5,5);
  p.setPen(Qt::black); p.setBrush(Qt::NoBrush);
  p.drawText(QRect(lx+18,ly,100,14), "实测数据");
}

// ═══════════════════════════════════════════════════════════
// DisplacementVerifyDialog
// ═══════════════════════════════════════════════════════════

DisplacementVerifyDialog::DisplacementVerifyDialog(
    gz::transport::Node &_transportNode, const std::string &_worldName,
    QWidget *_parent)
    : QDialog(_parent), transportNode_(_transportNode), worldName_(_worldName) {
  setWindowTitle("排水量测量实验");
  setModal(false); resize(900, 720);
  setAttribute(Qt::WA_DeleteOnClose);

  keship::EnsureKeshipDataDirs();
  tempDir_ = keship::KeshipDispDir();

  poseState_ = std::make_shared<DispPoseState>();
  volState_ = std::make_shared<DispVolState>();
  setupUI(); initPoseSubscription(); initVolumeSubscription();

  poseTimer_ = new QTimer(this);
  connect(poseTimer_, &QTimer::timeout, this, &DisplacementVerifyDialog::onUpdatePose);
  poseTimer_->start(33);

  /// 配载防抖（2026-09-19）：改配载要重建模型，而滑条 valueChanged 会连续触发
  /// → 等用户停下 500 ms 再重建一次。见 DisplacementVerifyDialog.hh 与 §28.9。
  payloadDebounce_ = new QTimer(this);
  payloadDebounce_->setSingleShot(true);
  connect(payloadDebounce_, &QTimer::timeout, this,
          &DisplacementVerifyDialog::respawnForPayload);

  onTypeChanged();

  // 静水波浪参数：PMS + gain=0（经进程内唯一发布器下发）
  {
    gz::msgs::Param msg; auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(0.0);  (*p)["gain"] = v;
    keship::WavefieldPublisher(transportNode_).Publish(msg);
  }

  std::cout << "Phase C: 排水量验证对话框已创建" << std::endl;
}

DisplacementVerifyDialog::~DisplacementVerifyDialog() {
  { std::lock_guard<std::mutex> lock(poseState_->mutex); poseState_->alive = false; }
  { std::lock_guard<std::mutex> lock(volState_->mutex); volState_->alive = false; }
  if (!poseTopic_.empty()) transportNode_.Unsubscribe(poseTopic_);
  if (!subVolTopic_.empty()) transportNode_.Unsubscribe(subVolTopic_);
  poseTimer_->stop(); doDeleteHull();
  delete generator_;

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

void DisplacementVerifyDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  auto *desc = new QLabel("<b>排水量测量实验</b> — 加配载→测吃水→算排水量→对比理论曲线", this);
  desc->setWordWrap(true); mainLayout->addWidget(desc);

  auto *topRow = new QHBoxLayout();
  auto *paramGroup = new QGroupBox("船型参数", this);
  auto *paramForm = new QFormLayout(paramGroup);

  typeCombo_ = new QComboBox(this);
  typeCombo_->addItems({"Wigley 经典", "Wigley 平底", "Wigley 尖舭",
                         "Wigley 瘦削", "Lewis U", "Lewis V"});
  paramForm->addRow("船型:", typeCombo_);

  lengthSpin_ = new QDoubleSpinBox(this);
  lengthSpin_->setRange(2,20); lengthSpin_->setValue(6); lengthSpin_->setSuffix(" m"); lengthSpin_->setDecimals(1);
  paramForm->addRow("L:", lengthSpin_);
  breadthSpin_ = new QDoubleSpinBox(this);
  breadthSpin_->setRange(0.5,5); breadthSpin_->setValue(1.5); breadthSpin_->setSuffix(" m"); breadthSpin_->setDecimals(2);
  paramForm->addRow("B:", breadthSpin_);
  depthSpin_ = new QDoubleSpinBox(this);
  depthSpin_->setRange(0.3,3); depthSpin_->setValue(1.0); depthSpin_->setSuffix(" m"); depthSpin_->setDecimals(2);
  paramForm->addRow("D:", depthSpin_);
  densitySpin_ = new QDoubleSpinBox(this);
  densitySpin_->setRange(100,950); densitySpin_->setValue(500); densitySpin_->setSuffix(" kg/m³"); densitySpin_->setDecimals(0);
  paramForm->addRow("ρ:", densitySpin_);

  spawnBtn_ = new QPushButton("生成模型", this);
  spawnBtn_->setMinimumHeight(30); paramForm->addRow(spawnBtn_);
  topRow->addWidget(paramGroup);

  auto *ctlGroup = new QGroupBox("配载控制", this);
  auto *ctlForm = new QFormLayout(ctlGroup);
  payloadSlider_ = new QSlider(Qt::Horizontal, this);
  payloadSlider_->setRange(0, 5000);
  payloadSpin_ = new QDoubleSpinBox(this);
  payloadSpin_->setRange(0, 5000); payloadSpin_->setValue(0);
  payloadSpin_->setSuffix(" kg"); payloadSpin_->setDecimals(0);
  auto *wRow = new QHBoxLayout(); wRow->addWidget(payloadSlider_); wRow->addWidget(payloadSpin_);
  ctlForm->addRow("配载:", wRow);

  draftLabel_ = new QLabel("— m", this); draftLabel_->setStyleSheet("font-weight:bold;");
  volLabel_ = new QLabel("— m³", this); volLabel_->setStyleSheet("font-weight:bold;");
  thVolLabel_ = new QLabel("— m³", this);
  errLabel_ = new QLabel("—", this);
  ctlForm->addRow("吃水:", draftLabel_);
  ctlForm->addRow("实测∇:", volLabel_);
  ctlForm->addRow("理论∇:", thVolLabel_);
  ctlForm->addRow("误差:", errLabel_);
  topRow->addWidget(ctlGroup);
  mainLayout->addLayout(topRow);

  auto *btnRow = new QHBoxLayout();
  recordBtn_ = new QPushButton("记录数据", this);
  clearBtn_ = new QPushButton("清除全部", this);
  auto *delBtn = new QPushButton("删除选中", this);
  UiTheme::setButtonIcon(delBtn, UiTheme::Icon::kDelete);
  btnRow->addWidget(recordBtn_); btnRow->addWidget(delBtn);
  btnRow->addWidget(clearBtn_); btnRow->addStretch();
  for (QPushButton *b : {recordBtn_, delBtn, clearBtn_}) b->setMinimumHeight(30);
  mainLayout->addLayout(btnRow);

  dataTable_ = new QTableWidget(0, 5, this);
  dataTable_->setHorizontalHeaderLabels({"配载(kg)", "吃水(m)", "实测∇(m³)", "理论∇(m³)", "误差(%)"});
  dataTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
  dataTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  dataTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
  dataTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  dataTable_->setMinimumHeight(80); mainLayout->addWidget(dataTable_, 1);

  chartWidget_ = new DispChartWidget(this); mainLayout->addWidget(chartWidget_, 2);

  connect(spawnBtn_, &QPushButton::clicked, this, &DisplacementVerifyDialog::onSpawn);
  connect(recordBtn_, &QPushButton::clicked, this, &DisplacementVerifyDialog::onRecord);
  connect(clearBtn_, &QPushButton::clicked, this, &DisplacementVerifyDialog::onClearAll);
  connect(delBtn, &QPushButton::clicked, this, &DisplacementVerifyDialog::onDeleteSelected);
  connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &DisplacementVerifyDialog::onTypeChanged);
  connect(lengthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onTypeChanged(); });
  connect(breadthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onTypeChanged(); });
  connect(depthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onTypeChanged(); });
  connect(densitySpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this]() { onTypeChanged(); });

  connect(payloadSlider_, &QSlider::valueChanged, this, [this](int v) {
    payloadSpin_->blockSignals(true); payloadSpin_->setValue(v); payloadSpin_->blockSignals(false);
    onPayloadChanged(v);
  });
  connect(payloadSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
          this, [this](double v) {
    payloadSlider_->blockSignals(true); payloadSlider_->setValue((int)v); payloadSlider_->blockSignals(false);
    onPayloadChanged(v);
  });
}

void DisplacementVerifyDialog::onTypeChanged() {
  double L = lengthSpin_->value(), B = breadthSpin_->value();
  double D = depthSpin_->value(), rho = densitySpin_->value();
  params_ = mapType(typeCombo_->currentIndex(), L, B, D, rho);

  delete generator_;
  generator_ = new keship::ShipHullGenerator(params_);

  chartWidget_->setTheory(generator_->hydroProps().draftCurve,
                          generator_->hydroProps().volumeCurve);
}

void DisplacementVerifyDialog::initPoseSubscription() {
  poseTopic_ = "/world/" + worldName_ + "/pose/info";
  auto state = poseState_; std::string name = hullName_;
  transportNode_.Subscribe(poseTopic_,
    std::function<void(const gz::msgs::Pose_V&)>(
      [state, name](const gz::msgs::Pose_V &msg) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return; state->found = false;
        for (int i = 0; i < msg.pose_size(); ++i) {
          if (msg.pose(i).name() == name) {
            state->hullZ = msg.pose(i).position().z();
            state->found = true; break;
          }
        }
      }));
}

void DisplacementVerifyDialog::initVolumeSubscription() {
  /// 实测 ∇ 的真实来源：浮力插件逐帧算出的浸水体积（需 SDF 开 <publish_subvolume>）
  subVolTopic_ = "/keship/sp01/subvol/" + hullName_;
  auto state = volState_;
  transportNode_.Subscribe(subVolTopic_,
    std::function<void(const gz::msgs::Double&)>(
      [state](const gz::msgs::Double &msg) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->alive) return;
        state->subVol = msg.data();
        state->hasSample = true;
      }));
}

void DisplacementVerifyDialog::onPayloadChanged(double _kg) {
  currentPayload_ = _kg;
  if (spawned_ && !payloadTopic_.empty()) {
    /// 话题照发（不增加代价）；但**它目前对动力学无效**（见 makeHullSDF 注释）。
    gz::msgs::Double msg; msg.set_data(_kg);
    payloadPub_.Publish(msg);
    /// ★ 2026-09-19 修复：真正生效的是"按新质量**重建模型**"。
    /// 防抖 500 ms —— 滑条连续拖动只在停下后重建一次（见 .hh 注释）。
    payloadDebounce_->start(500);
  }
}

void DisplacementVerifyDialog::respawnForPayload() {
  if (!spawned_) return;
  /// `_keepRecords = true`：**保留**已记录的数据表与当前配载。
  /// 若走 doSpawnHull() 的默认行为，会清空 dataTable_ 并把配载归零 —— 学生记录就没了。
  if (!doSpawnHull(true)) {
    QMessageBox::warning(this, "错误", "按新配载重建模型失败");
    return;
  }
  updateStatusDisplay();
}

void DisplacementVerifyDialog::onUpdatePose() {
  double hullZ; bool found;
  { std::lock_guard<std::mutex> lock(poseState_->mutex);
    hullZ = poseState_->hullZ; found = poseState_->found; }
  if (spawned_ && found) {
    currentDraft_ = std::max(0.0, params_.D - hullZ);
    updateStatusDisplay();
  }
}

void DisplacementVerifyDialog::updateStatusDisplay() {
  double d = currentDraft_;
  draftLabel_->setText(QString::number(d, 'f', 3) + " m");
  if (generator_) {
    /// 理论 ∇：生成器静水力曲线在当前吃水处插值
    double vTh = generator_->volumeAtDraft(d);
    thVolLabel_->setText(QString::number(vTh, 'f', 3) + " m³");

    /// 实测 ∇：浮力插件发布的**真实浸水体积**
    /// （2026-09-19 修正：原先这里也调用 volumeAtDraft(d)，等于把实测吃水代回理论曲线，
    ///  实测与理论同源 → 误差恒为 0，实验根本没有测量）
    double measVol = 0.0; bool hasMeas = false;
    { std::lock_guard<std::mutex> lock(volState_->mutex);
      measVol = volState_->subVol; hasMeas = volState_->hasSample; }

    if (!hasMeas) {
      /// 未收到样本时显示"—"，**绝不当 0 用** —— 否则又变回"假实测"
      volLabel_->setText("— m³");
      errLabel_->setText("—");
      errLabel_->setStyleSheet("");
      return;
    }

    double vMeas = measVol;
    volLabel_->setText(QString::number(vMeas, 'f', 3) + " m³");
    double err = vTh > 1e-6 ? std::abs(vMeas - vTh) / vTh * 100 : 0;
    errLabel_->setText(QString::number(err, 'f', 2) + "%");
    // 2026-09-18（逐个修改 · 试点）：原直接写 green / red 英文色名，与项目其他位置的
    // #448844 / #d9534f 不一致；改用 UiTheme 统一状态语义色（判定阈值 5% 不变）
    errLabel_->setStyleSheet(
        QString("color:%1;font-weight:bold;")
            .arg(err < 5.0 ? UiTheme::statusColor(UiTheme::Status::kOk)
                           : UiTheme::statusColor(UiTheme::Status::kFail)));
  }
}

std::string DisplacementVerifyDialog::makeSDF() const {
  double L = params_.L, B = params_.B, D = params_.D;

  keship::ShipHullGenerator hull(params_);
  /// ★ 文件名带**网格内容指纹**（2026-09-19 修）：
  /// gz 的 gz::common::MeshManager **按文件路径**缓存网格，同一路径换内容后仍返回旧网格
  /// （进程内、永不失效；GUI 进程常驻 → 永不刷新）。原先固定写 `hull_vis.obj` →
  /// 改参数后"生成模型"显示的仍是第一艘船（张老师实测：改船长无变化）。
  /// 加指纹后：几何一变 URI 就变 → 缓存必然未命中；几何相同则命中缓存是正确的。
  /// 代价：每次改参数会多留一对 OBJ（约 240 KB，不删以免影响后续 Rebuild）。
  const std::string meshTag = hull.meshTag();
  std::string visPath = tempDir_ + "hull_vis_" + meshTag + ".obj";
  std::string buoyPath = tempDir_ + "hull_buoy_" + meshTag + ".obj";
  hull.writeVisualOBJ(visPath);
  hull.writeBuoyancyOBJ(buoyPath);

  double actualVol = hull.hydroProps().volume;
  /// ★ 2026-09-19 修复：**把配载烘进质量**。
  /// 原做法是把配载发给 `KSPlugin_VariableMass`，但该插件只写 ECM 的 `Inertial` 组件，
  /// 而 gz **没有**把它传到物理引擎的通路（`gz::sim::Link` 只有读的 `WorldInertial`；
  /// `gz/physics7` 的公开 `Link` 接口全库无 `SetInertial`）→ **dartsim 一直用创建时的质量**。
  /// 实测：配载 0→1000 kg 时插件日志 `mass` 由 1998.4 变到 2998.4，而**吃水恒为 0.6542**；
  /// 改配载后把船按深再放开，它回到按**旧**质量算的平衡（距旧 0.0015 m / 距新 0.1773 m）。
  /// 详见 doc/plans/headless_ctl_plan.md §28。
  /// 无头验证（同一改法）：m = 1998 / 2498 / 2998 kg → 吃水 0.6542 / 0.7461 / 0.8337
  /// （理论 0.6524 / 0.7442 / 0.8315，偏差 ≤ 0.0022 m）。
  /// 注：ixx/iyy/izz 由 m 派生（沿用本文件既有的"均质箱体"口径）→ 惯量随配载一起变。
  double m = params_.rho * actualVol + currentPayload_;
  double ixx = m/12.0*(B*B + D*D);
  double iyy = m/12.0*(L*L + D*D);
  double izz = m/12.0*(L*L + B*B);

  /// ── 阻尼系数（2026-09-19 新增）：c = 2·ζ·√(k·m)，k 为静水恢复刚度 ──
  /// 为什么不用 pose_based：插件的 computePoseBasedDamping() 按**圆柱体端盖**积分
  /// （需 hasCylinderGeometry_），本实验是网格船体 → 该分支被跳过，阻尼为零 → 长时间垂荡。
  /// 故改走 fossen 的 <linear_damping>：
  ///   A_wp    = d∇/dT（取自生成器自身体积曲线，与船型一致，非矩形粗估）
  ///   垂荡 k   = ρ·g·A_wp                        → Zw = 2·ζ_h·√(k·m)
  ///   横摇 k   = ρ·g·(A_wp·B²/12), I_eff = ixx   → Kp = 2·ζ_r·√(k·ixx)
  ///   纵摇 k   = ρ·g·(A_wp·L²/12), I_eff = iyy   → Mq = 2·ζ_r·√(k·iyy)
  /// ζ 取插件 pose_based 同值（垂荡 0.15 / 转动 0.10），保持全项目一致。
  const double zetaHeave = 0.15;
  const double zetaRot   = 0.10;
  /// 水密度 / 重力改取类内单一来源（kFluidDensity / kGravity，见 .hh 末尾），
  /// 不再就地写 1000.0 / 9.81 —— 否则与 <fluid_density> 各存一份、改一处漏一处
  /// （2026-09-19 复核；数值不变，纯消除重复）

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

  /// 系数恒取 > 0：插件在 enable_hydro=true 而 fossen 无效时会**整体 return**，
  /// 连浮力都不施加（既有行为，见 KSPlugin_HydroBuoyancy::PreUpdate 开头）。
  /// 这里兜底，避免退化参数（如 m→0）把浮力一起弄没。
  const double Zw = std::max(1e-3, 2.0 * zetaHeave *
      std::sqrt(kFluidDensity * kGravity * A_wp * m));
  const double Kp = std::max(1e-3, 2.0 * zetaRot *
      std::sqrt(kFluidDensity * kGravity * (A_wp * B * B / 12.0) * ixx));
  const double Mq = std::max(1e-3, 2.0 * zetaRot *
      std::sqrt(kFluidDensity * kGravity * (A_wp * L * L / 12.0) * iyy));

  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n<sdf version=\"1.6\">\n"
      << "  <model name=\"" << hullName_ << "\">\n"
      << "    <pose>0 0 0 0 0 0</pose>\n"
      << "    <static>false</static>\n"
      << "    <link name=\"base_link\">\n"
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
      << "      <publish_subvolume>true</publish_subvolume>\n"
      << "      <linear_damping>\n"
      << "        <Zw>" << Zw << "</Zw>\n"
      << "        <Kp>" << Kp << "</Kp>\n"
      << "        <Mq>" << Mq << "</Mq>\n"
      << "      </linear_damping>\n"
      << "      <fluid_density>" << kFluidDensity << "</fluid_density>\n"
      << "      <fluid_level>0.0</fluid_level>\n"
      << "      <buoyancy name=\"hull\">\n"
      << "        <geometry>\n"
      << "          <mesh><uri>" << buoyPath << "</uri></mesh>\n"
      << "        </geometry>\n"
      << "      </buoyancy>\n"
      << "    </plugin>\n"
      << "    <plugin filename=\"libKSPlugin_VariableMass.so\" name=\"keship::KSPlugin_VariableMass\">\n"
      << "      <link_name>base_link</link_name>\n"
      << "      <external_force>\n"
      << "        <link_name>base_link</link_name>\n"
      << "      </external_force>\n"
      << "    </plugin>\n"
      << "  </model>\n</sdf>\n";
  return sdf.str();
}

bool DisplacementVerifyDialog::resolveEntityID(const std::string &_name, uint32_t &_id) {
  gz::msgs::Empty req; gz::msgs::Scene rep; bool r = false;
  if (!transportNode_.Request("/world/"+worldName_+"/scene/info", req, 5000, rep, r) || !r) return false;
  for (int i = 0; i < rep.model_size(); ++i)
    if (rep.model(i).name() == _name) { _id = rep.model(i).id(); return true; }
  return false;
}

bool DisplacementVerifyDialog::doDeleteHull() {
  if (hullEntityId_ == 0) return true;
  gz::msgs::Entity req; req.set_name(hullName_); req.set_type(gz::msgs::Entity::MODEL);
  gz::msgs::Boolean rep; bool r = false;
  transportNode_.Request("/world/"+worldName_+"/remove", req, 5000, rep, r);
  for (int retry = 0; retry < 20; ++retry) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    uint32_t id = 0; if (!resolveEntityID(hullName_, id)) break;
  }
  hullEntityId_ = 0; spawned_ = false; return true;
}

bool DisplacementVerifyDialog::doSpawnHull(bool _keepRecords) {
  doDeleteHull();
  /// 回收上一轮的内容寻址网格（2026-09-19，缺陷 D 的配套）。
  /// 网格名随几何内容变化（hull_vis_<tag>.obj）→ 每换一组参数就多留一对文件；
  /// 此刻旧模型已由 doDeleteHull() 删除、且 KeshipDispDir() 由本对话框独占
  /// → 可安全清理（前提见 MeshUtils.hh::RemoveStaleMeshFiles 注释）。
  /// 必须在 makeSDF() 之前调用，否则会删掉本轮刚写出的网格。
  keship::RemoveStaleMeshFiles(tempDir_, "hull_vis");
  keship::RemoveStaleMeshFiles(tempDir_, "hull_buoy");
  /// ★ 2026-09-19：改配载重建（_keepRecords=true）时**不清表** ——
  /// 学生的记录必须保留，否则"改配载 → 测吃水 → 记录"这条实验流程会把自己的数据抹掉。
  if (!_keepRecords) {
    dataTable_->setRowCount(0); recordedData_.clear(); chartWidget_->clearData();
  }

  std::string sdfStr = makeSDF();
  gz::msgs::EntityFactory req;
  req.set_name(hullName_); req.set_allow_renaming(false); req.set_sdf(sdfStr);
  auto *pose = req.mutable_pose();
  pose->mutable_position()->set_z(params_.D / 2.0 + 0.1);

  gz::msgs::Boolean rep; bool r = false;
  if (!transportNode_.Request("/world/"+worldName_+"/create/blocking", req, 20000, rep, r) || !r)
    return false;

  for (int retry = 0; retry < 20; ++retry) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (resolveEntityID(hullName_, hullEntityId_)) break;
  }
  if (hullEntityId_ == 0) return false;

  payloadTopic_ = "/keship/sp01/payload/" + hullName_;
  payloadPub_ = transportNode_.Advertise<gz::msgs::Double>(payloadTopic_);

  /// 配载量程随本船排水量自适应（2026-09-19）
  /// 原先写死 0~5000 kg：默认船型（L6/B1.5/D1.0/ρ500）船体质量仅约 2 t，
  /// 滑块上半程就超过储备浮力 → 船会沉没，实验数据失去意义；
  /// 而下半程的吃水变化（百 kg 量级 → 厘米级）又被垂荡淹没 → 表现为"改配载没反应"。
  /// 现取上限 = 0.5 × 船体质量：全程不沉，且吃水变化始终可察觉。
  if (generator_) {
    const double hullMass = params_.rho * generator_->hydroProps().volume;
    const double maxPayload = std::max(1.0, 0.5 * hullMass);
    payloadSpin_->setRange(0.0, maxPayload);
    payloadSlider_->setRange(0, static_cast<int>(maxPayload));
    std::cout << "Phase C: 配载量程自适应 = 0~" << maxPayload
              << " kg（船体质量 " << hullMass << " kg）" << std::endl;
  }
  /// ★ 2026-09-19：改配载重建（_keepRecords=true）时**不要把配载归零** ——
  /// 否则"按新配载重建"会退化成重建一艘空船（配载永远是 0）。
  /// 注意：下面两句会触发 valueChanged → onPayloadChanged()，但此刻 spawned_ 仍为 false，
  /// 故不会误启动防抖定时器（spawned_ = true 在之后才赋值）。
  if (!_keepRecords) {
    payloadSlider_->setValue(0); payloadSpin_->setValue(0);
    currentPayload_ = 0;
  }
  spawned_ = true;
  /// 换了模型：清掉上一艘的实测样本，避免拿旧船的浸水体积算新船的误差
  { std::lock_guard<std::mutex> lock(volState_->mutex);
    volState_->hasSample = false; volState_->subVol = 0.0; }
  std::cout << "Phase C: 模型已生成, payload topic=" << payloadTopic_ << std::endl;
  return true;
}

void DisplacementVerifyDialog::onSpawn() {
  if (!doSpawnHull()) QMessageBox::warning(this, "错误", "生成失败");
}

void DisplacementVerifyDialog::onRecord() {
  if (!spawned_) { QMessageBox::information(this, "提示", "请先生成模型"); return; }

  /// 实测 ∇ 必须来自真实浸水体积；没有样本就拒绝记录，
  /// 否则又会写入"误差 0"的假数据（2026-09-19 修正）
  double measVol = 0.0; bool hasMeas = false;
  { std::lock_guard<std::mutex> lock(volState_->mutex);
    measVol = volState_->subVol; hasMeas = volState_->hasSample; }
  if (!hasMeas) {
    QMessageBox::information(this, "提示",
        "尚未收到实测浸水体积（模型未入水或插件未发布），暂不能记录");
    return;
  }
  if (!generator_) { QMessageBox::information(this, "提示", "理论曲线未就绪"); return; }

  double d = currentDraft_;
  double vMeas = measVol;                        ///< 实测：真实浸水体积
  double vTh = generator_->volumeAtDraft(d);     ///< 理论：静水力曲线插值
  double err = vTh > 1e-6 ? std::abs(vMeas - vTh) / vTh * 100 : 0;
  recordedData_.push_back({currentPayload_, d, vMeas, vTh, err});

  int row = dataTable_->rowCount(); dataTable_->insertRow(row);
  dataTable_->setItem(row, 0, new QTableWidgetItem(QString::number(currentPayload_, 'f', 0)));
  dataTable_->setItem(row, 1, new QTableWidgetItem(QString::number(d, 'f', 3)));
  dataTable_->setItem(row, 2, new QTableWidgetItem(QString::number(vMeas, 'f', 3)));
  dataTable_->setItem(row, 3, new QTableWidgetItem(QString::number(vTh, 'f', 3)));
  dataTable_->setItem(row, 4, new QTableWidgetItem(QString::number(err, 'f', 2)));
  chartWidget_->setData(recordedData_);
}

void DisplacementVerifyDialog::onDeleteSelected() {
  auto sel = dataTable_->selectionModel()->selectedRows(); if (sel.isEmpty()) return;
  std::sort(sel.begin(), sel.end(), [](auto &a, auto &b) { return a.row() > b.row(); });
  for (auto &i : sel) dataTable_->removeRow(i.row());
  recordedData_.clear();
  for (int r = 0; r < dataTable_->rowCount(); ++r) {
    recordedData_.push_back({
      (double)dataTable_->item(r,0)->text().toDouble(),
      dataTable_->item(r,1)->text().toDouble(),
      dataTable_->item(r,2)->text().toDouble(),
      dataTable_->item(r,3)->text().toDouble(),
      dataTable_->item(r,4)->text().toDouble()});
  }
  chartWidget_->setData(recordedData_);
}

void DisplacementVerifyDialog::onClearAll() {
  dataTable_->setRowCount(0); recordedData_.clear(); chartWidget_->clearData();
}
