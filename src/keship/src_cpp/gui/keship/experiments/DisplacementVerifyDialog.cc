/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase C: 排水量验证实验对话框实现 */

#include "DisplacementVerifyDialog.hh"

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
  setupUI(); initPoseSubscription();

  poseTimer_ = new QTimer(this);
  connect(poseTimer_, &QTimer::timeout, this, &DisplacementVerifyDialog::onUpdatePose);
  poseTimer_->start(33);

  onTypeChanged();

  // 静水波浪参数：PMS + gain=0，WavefieldPublisher 会自动重发
  {
    gz::msgs::Param msg; auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(0.0);  (*p)["gain"] = v;
    auto pub = transportNode_.Advertise<gz::msgs::Param>("/keship/wavefield/parameters_set");
    pub.Publish(msg);
  }

  std::cout << "Phase C: 排水量验证对话框已创建" << std::endl;
}

DisplacementVerifyDialog::~DisplacementVerifyDialog() {
  { std::lock_guard<std::mutex> lock(poseState_->mutex); poseState_->alive = false; }
  if (!poseTopic_.empty()) transportNode_.Unsubscribe(poseTopic_);
  poseTimer_->stop(); doDeleteHull();
  delete generator_;

  // 恢复波浪：PMS + gain=1.0，WavefieldPublisher 会自动重发
  {
    gz::msgs::Param msg; auto *p = msg.mutable_params();
    gz::msgs::Any v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_STRING);
    v.set_string_value("PMS"); (*p)["model"] = v;
    v.set_type(gz::msgs::Any_ValueType::Any_ValueType_DOUBLE);
    v.set_double_value(1.0);  (*p)["gain"] = v;
    auto pub = transportNode_.Advertise<gz::msgs::Param>("/keship/wavefield/parameters_set");
    pub.Publish(msg);
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

void DisplacementVerifyDialog::onPayloadChanged(double _kg) {
  currentPayload_ = _kg;
  if (spawned_ && !payloadTopic_.empty()) {
    gz::msgs::Double msg; msg.set_data(_kg);
    payloadPub_.Publish(msg);
  }
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
    double vMeas = generator_->volumeAtDraft(d);
    double vTh = generator_->volumeAtDraft(d); // same function, for display
    volLabel_->setText(QString::number(vMeas, 'f', 3) + " m³");
    thVolLabel_->setText(QString::number(vTh, 'f', 3) + " m³");
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
  std::string visPath = tempDir_ + "hull_vis.obj";
  std::string buoyPath = tempDir_ + "hull_buoy.obj";
  hull.writeVisualOBJ(visPath);
  hull.writeBuoyancyOBJ(buoyPath);

  double actualVol = hull.hydroProps().volume;
  double m = params_.rho * actualVol;
  double ixx = m/12.0*(B*B + D*D);
  double iyy = m/12.0*(L*L + D*D);
  double izz = m/12.0*(L*L + B*B);

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
      << "      <enable_hydro>false</enable_hydro>\n"
      << "      <fluid_density>1000</fluid_density>\n"
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

bool DisplacementVerifyDialog::doSpawnHull() {
  doDeleteHull();
  dataTable_->setRowCount(0); recordedData_.clear(); chartWidget_->clearData();

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
  payloadSlider_->setValue(0); payloadSpin_->setValue(0);
  currentPayload_ = 0; spawned_ = true;
  std::cout << "Phase C: 模型已生成, payload topic=" << payloadTopic_ << std::endl;
  return true;
}

void DisplacementVerifyDialog::onSpawn() {
  if (!doSpawnHull()) QMessageBox::warning(this, "错误", "生成失败");
}

void DisplacementVerifyDialog::onRecord() {
  if (!spawned_) { QMessageBox::information(this, "提示", "请先生成模型"); return; }
  double d = currentDraft_;
  double vMeas = generator_ ? generator_->volumeAtDraft(d) : 0;
  double vTh = generator_ ? generator_->volumeAtDraft(d) : 0;
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
