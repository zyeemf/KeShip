/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* Phase 10: 任务设定对话框 — waypoint / tracking 轨迹编辑器 / dp */

#include "TaskSetDialog.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <iostream>
#include <cmath>

#include "TaskTomlParser.hh"
#include "keship/utils/KeshipPaths.hh"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double deg2rad(double d) { return d * M_PI / 180.0; }
static double rad2deg(double r) { return r * 180.0 / M_PI; }

TaskSetDialog::TaskSetDialog(gz::transport::Node &_transportNode,
                               const std::string &_worldName,
                               QWidget *_parent)
    : QDialog(_parent), node_(_transportNode), worldName_(_worldName) {
  setWindowTitle("任务设定");
  setModal(false);
  resize(800, 700);
  setAttribute(Qt::WA_DeleteOnClose);
  setupUI();
}

// ═══════════════════════════════════════════════════════════
// UI
// ═══════════════════════════════════════════════════════════

void TaskSetDialog::setupUI() {
  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(3);
  mainLayout->setContentsMargins(6, 4, 6, 4);

  // ---- 第一行：基本设置 + 任务类型 ----
  auto *topRow = new QHBoxLayout();
  topRow->setSpacing(6);
  auto *basicGroup = new QGroupBox("基本设置", this);
  auto *basicForm = new QFormLayout(basicGroup);
  basicForm->setSpacing(2); basicForm->setContentsMargins(4, 2, 4, 2);
  nameEdit_ = new QLineEdit("新任务", this);
  basicForm->addRow("任务名称:", nameEdit_);
  timeSpin_ = new QSpinBox(this);
  timeSpin_->setRange(10, 3600); timeSpin_->setValue(300); timeSpin_->setSuffix(" s");
  basicForm->addRow("时间限制:", timeSpin_);
  topRow->addWidget(basicGroup);

  auto *typeGroup = new QGroupBox("任务类型", this);
  auto *typeForm = new QFormLayout(typeGroup);
  typeForm->setSpacing(1); typeForm->setContentsMargins(4, 2, 4, 2);
  wpRadio_ = new QRadioButton("航路点 (waypoint)", this); wpRadio_->setChecked(true);
  trRadio_ = new QRadioButton("轨迹跟踪 (tracking)", this);
  dpRadio_ = new QRadioButton("动力定位 (dp)", this);
  gotoRadio_ = new QRadioButton("定点航行 (goto)", this);
  typeForm->addRow(wpRadio_); typeForm->addRow(trRadio_); typeForm->addRow(dpRadio_); typeForm->addRow(gotoRadio_);
  topRow->addWidget(typeGroup);
  mainLayout->addLayout(topRow);

  // ---- waypoint 面板 ----
  wpPanel_ = new QWidget(this);
  auto *wpLayout = new QHBoxLayout(wpPanel_);
  wpLayout->setContentsMargins(2, 0, 2, 0);
  radiusSpin_ = new QDoubleSpinBox(this);
  radiusSpin_->setRange(0.5, 20); radiusSpin_->setValue(2.0); radiusSpin_->setSuffix(" m");
  radiusSpin_->setToolTip("距航路点多远即算\"抵达\"");
  wpLayout->addWidget(new QLabel("抵达半径:", this));
  wpLayout->addWidget(radiusSpin_);
  wpLayout->addStretch();

  // ---- DP 面板 ----
  dpPanel_ = new QWidget(this);
  auto *dpLayout = new QHBoxLayout(dpPanel_);
  dpLayout->setContentsMargins(2, 0, 2, 0);
  holdXSpin_ = new QDoubleSpinBox(this);
  holdXSpin_->setRange(-1000, 1000); holdXSpin_->setValue(0); holdXSpin_->setSuffix(" m");
  holdYSpin_ = new QDoubleSpinBox(this);
  holdYSpin_->setRange(-1000, 1000); holdYSpin_->setValue(0); holdYSpin_->setSuffix(" m");
  holdHdgSpin_ = new QDoubleSpinBox(this);
  holdHdgSpin_->setRange(-999, 360); holdHdgSpin_->setValue(-999); holdHdgSpin_->setSuffix(" °");
  holdHdgSpin_->setSpecialValueText("无视");
  tolSpin_ = new QDoubleSpinBox(this);
  tolSpin_->setRange(0.5, 50); tolSpin_->setValue(3.0); tolSpin_->setSuffix(" m");
  dpLayout->addWidget(new QLabel("X:", this)); dpLayout->addWidget(holdXSpin_);
  dpLayout->addWidget(new QLabel("Y:", this)); dpLayout->addWidget(holdYSpin_);
  dpLayout->addWidget(new QLabel("航向:", this)); dpLayout->addWidget(holdHdgSpin_);
  dpLayout->addWidget(new QLabel("容许半径:", this)); dpLayout->addWidget(tolSpin_);
  dpLayout->addStretch();

  // ---- goto 面板 ----
  gotoPanel_ = new QWidget(this);
  auto *gotoLayout = new QHBoxLayout(gotoPanel_);
  gotoLayout->setContentsMargins(2, 0, 2, 0);
  gotoXSpin_ = new QDoubleSpinBox(this);
  gotoXSpin_->setRange(-500, 500); gotoXSpin_->setPrefix("X: "); gotoXSpin_->setSuffix(" m");
  gotoYSpin_ = new QDoubleSpinBox(this);
  gotoYSpin_->setRange(-500, 500); gotoYSpin_->setPrefix("Y: "); gotoYSpin_->setSuffix(" m");
  gotoHdgSpin_ = new QDoubleSpinBox(this);
  gotoHdgSpin_->setRange(-1, 360); gotoHdgSpin_->setValue(-1); gotoHdgSpin_->setPrefix("航向: "); gotoHdgSpin_->setSuffix(" °");
  gotoHdgSpin_->setSpecialValueText("无视");
  gotoRSpin_ = new QDoubleSpinBox(this);
  gotoRSpin_->setRange(0.5, 50); gotoRSpin_->setValue(3.0); gotoRSpin_->setPrefix("半径: "); gotoRSpin_->setSuffix(" m");
  gotoLayout->addWidget(gotoXSpin_); gotoLayout->addWidget(gotoYSpin_);
  gotoLayout->addWidget(gotoHdgSpin_); gotoLayout->addWidget(gotoRSpin_);
  gotoLayout->addStretch();

  // ---- 地图（必须在 setupTrackingPanel 之前创建，后者会连接 mapWidget_ 信号）----
  mapWidget_ = new MapWidget(this);

  // ---- tracking 面板 ----
  setupTrackingPanel();

  // ---- 参数面板（直接 layout + visible 切换，避免 QStackedWidget 按最大子控件预留空间）----
  mainLayout->addWidget(wpPanel_);
  mainLayout->addWidget(dpPanel_);
  mainLayout->addWidget(gotoPanel_);
  mainLayout->addWidget(trPanel_);
  dpPanel_->setVisible(false);
  gotoPanel_->setVisible(false);
  trPanel_->setVisible(false);

  // ---- 地图（主导区域，大比例） ----
  mainLayout->addWidget(mapWidget_, 5);

  // ---- 评分权重（紧凑行） ----
  weightPanel_ = new QWidget(this);
  auto *wLayout = new QHBoxLayout(weightPanel_);
  wLayout->setContentsMargins(2, 0, 2, 0);
  wLayout->addWidget(new QLabel("评分权重:", this));
  w1Label_ = new QLabel("进度", this);
  w1Spin_ = new QDoubleSpinBox(this);
  w1Spin_->setRange(0.0, 1.0); w1Spin_->setSingleStep(0.05); w1Spin_->setValue(0.35); w1Spin_->setDecimals(2);
  w2Label_ = new QLabel("精度", this);
  w2Spin_ = new QDoubleSpinBox(this);
  w2Spin_->setRange(0.0, 1.0); w2Spin_->setSingleStep(0.05); w2Spin_->setValue(0.40); w2Spin_->setDecimals(2);
  w3Label_ = new QLabel("时间", this);
  w3Spin_ = new QDoubleSpinBox(this);
  w3Spin_->setRange(0.0, 1.0); w3Spin_->setSingleStep(0.05); w3Spin_->setValue(0.25); w3Spin_->setDecimals(2);
  wLayout->addWidget(w1Label_); wLayout->addWidget(w1Spin_);
  wLayout->addWidget(w2Label_); wLayout->addWidget(w2Spin_);
  wLayout->addWidget(w3Label_); wLayout->addWidget(w3Spin_);
  wLayout->addStretch();
  mainLayout->addWidget(weightPanel_);

  // ---- 按钮 ----
  auto *btnRow = new QHBoxLayout();
  btnRow->setContentsMargins(2, 0, 2, 0);
  auto *saveBtn = new QPushButton("保存任务 (TOML)", this); saveBtn->setMinimumHeight(30);
  auto *closeBtn = new QPushButton("关闭", this); closeBtn->setMinimumHeight(30);
  btnRow->addStretch(); btnRow->addWidget(saveBtn); btnRow->addWidget(closeBtn);
  btnRow->addStretch();
  mainLayout->addLayout(btnRow);

  // ---- 信号 ----
  connect(wpRadio_, &QRadioButton::clicked, this, &TaskSetDialog::onTaskTypeChanged);
  connect(trRadio_, &QRadioButton::clicked, this, &TaskSetDialog::onTaskTypeChanged);
  connect(dpRadio_, &QRadioButton::clicked, this, &TaskSetDialog::onTaskTypeChanged);
  connect(gotoRadio_, &QRadioButton::clicked, this, &TaskSetDialog::onTaskTypeChanged);
  connect(saveBtn, &QPushButton::clicked, this, &TaskSetDialog::onSave);
  connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
  connect(mapWidget_, &MapWidget::waypointsChanged, this, [this]() {
    setDirty(true);
    // goto 模式：地图点击 → 更新目标坐标
    if (gotoRadio_->isChecked()) {
      auto &wpts = mapWidget_->waypoints();
      if (!wpts.empty()) {
        gotoXSpin_->setValue(wpts.back().x);
        gotoYSpin_->setValue(wpts.back().y);
        mapWidget_->setGotoTarget(wpts.back().x, wpts.back().y, gotoRSpin_->value());
        mapWidget_->clearWaypoints();  // 只保留目标标记，不保留 waypoint
      }
    }
  });
  connect(nameEdit_, &QLineEdit::textChanged, this, [this]() { setDirty(true); });
  connect(timeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) { setDirty(true); });
}

// ═══════════════════════════════════════════════════════════
// Tracking 面板构建
// ═══════════════════════════════════════════════════════════

void TaskSetDialog::setupTrackingPanel() {
  trPanel_ = new QWidget(this);
  auto *tl = new QVBoxLayout(trPanel_);
  tl->setContentsMargins(4, 2, 4, 2);

  // 形状选择行
  auto *row1 = new QHBoxLayout();
  row1->addWidget(new QLabel("轨迹形状:", this));
  shapeCombo_ = new QComboBox(this);
  shapeCombo_->addItems({"直线", "正弦曲线", "圆", "椭圆", "自定义(手绘)"});
  row1->addWidget(shapeCombo_);

  genBtn_ = new QPushButton("生成轨迹", this);
  genBtn_->setMinimumHeight(28);
  row1->addWidget(genBtn_);

  trajLenLabel_ = new QLabel("", this);
  row1->addWidget(trajLenLabel_);
  row1->addStretch();
  tl->addLayout(row1);

  // 形状参数面板（直接 layout + visible，避免 QStackedWidget 按最大子控件预留空间）
  shapePages_.resize(5);
  for (int i = 0; i < 5; ++i) {
    shapePages_[i] = new QWidget(this);
    setupShapePanel(i, shapePages_[i]);
    shapePages_[i]->setVisible(i == 0);  // 仅第 0 个（直线）初始可见
    tl->addWidget(shapePages_[i]);
  }

  // 信号
  connect(shapeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &TaskSetDialog::onShapeChanged);
  connect(genBtn_, &QPushButton::clicked, this, &TaskSetDialog::onGenerateTrajectory);
  connect(mapWidget_, &MapWidget::refPointPlaced, this, &TaskSetDialog::onRefPointPlaced);
  connect(mapWidget_, &MapWidget::freeDrawChanged, this, &TaskSetDialog::onFreeDrawChanged);
}

void TaskSetDialog::setupShapePanel(int _idx, QWidget *_parent) {
  auto *form = new QFormLayout(_parent);
  form->setSpacing(2);
  form->setContentsMargins(0, 0, 0, 0);

  switch (_idx) {
  case 0: {  // 直线
    lineX1_ = new QDoubleSpinBox(this); lineX1_->setRange(-500, 500); lineX1_->setPrefix("X: "); lineX1_->setSuffix(" m");
    lineY1_ = new QDoubleSpinBox(this); lineY1_->setRange(-500, 500); lineY1_->setPrefix("Y: "); lineY1_->setSuffix(" m");
    lineX2_ = new QDoubleSpinBox(this); lineX2_->setRange(-500, 500); lineX2_->setValue(50); lineX2_->setPrefix("X: "); lineX2_->setSuffix(" m");
    lineY2_ = new QDoubleSpinBox(this); lineY2_->setRange(-500, 500); lineY2_->setPrefix("Y: "); lineY2_->setSuffix(" m");
    lineN_ = new QSpinBox(this); lineN_->setRange(5, 500); lineN_->setValue(50);

    auto *r1 = new QHBoxLayout(); r1->setSpacing(4); r1->addWidget(lineX1_); r1->addWidget(lineY1_);
    form->addRow("起点:", r1);
    auto *r2 = new QHBoxLayout(); r2->setSpacing(4); r2->addWidget(lineX2_); r2->addWidget(lineY2_);
    form->addRow("终点:", r2);
    form->addRow("采样点数:", lineN_);
    break;
  }
  case 1: {  // 正弦
    sineX1_ = new QDoubleSpinBox(this); sineX1_->setRange(-500, 500); sineX1_->setPrefix("X: "); sineX1_->setSuffix(" m");
    sineY1_ = new QDoubleSpinBox(this); sineY1_->setRange(-500, 500); sineY1_->setPrefix("Y: "); sineY1_->setSuffix(" m");
    sineX2_ = new QDoubleSpinBox(this); sineX2_->setRange(-500, 500); sineX2_->setValue(50); sineX2_->setPrefix("X: "); sineX2_->setSuffix(" m");
    sineY2_ = new QDoubleSpinBox(this); sineY2_->setRange(-500, 500); sineY2_->setPrefix("Y: "); sineY2_->setSuffix(" m");
    sineAmp_ = new QDoubleSpinBox(this); sineAmp_->setRange(0.5, 100); sineAmp_->setValue(5); sineAmp_->setSuffix(" m");
    sineCycles_ = new QSpinBox(this); sineCycles_->setRange(1, 20); sineCycles_->setValue(3);
    sineN_ = new QSpinBox(this); sineN_->setRange(10, 1000); sineN_->setValue(100);

    auto *r1 = new QHBoxLayout(); r1->setSpacing(4); r1->addWidget(sineX1_); r1->addWidget(sineY1_);
    form->addRow("起点:", r1);
    auto *r2 = new QHBoxLayout(); r2->setSpacing(4); r2->addWidget(sineX2_); r2->addWidget(sineY2_);
    form->addRow("终点:", r2);
    auto *r3 = new QHBoxLayout(); r3->setSpacing(4); r3->addWidget(sineAmp_); r3->addWidget(sineCycles_);
    form->addRow("振幅/周期:", r3);
    form->addRow("采样点数:", sineN_);
    break;
  }
  case 2: {  // 圆
    circCX_ = new QDoubleSpinBox(this); circCX_->setRange(-500, 500); circCX_->setPrefix("X: "); circCX_->setSuffix(" m");
    circCY_ = new QDoubleSpinBox(this); circCY_->setRange(-500, 500); circCY_->setPrefix("Y: "); circCY_->setSuffix(" m");
    circR_ = new QDoubleSpinBox(this); circR_->setRange(1, 200); circR_->setValue(20); circR_->setSuffix(" m");
    circStart_ = new QDoubleSpinBox(this); circStart_->setRange(0, 360); circStart_->setPrefix("起: "); circStart_->setSuffix(" °");
    circEnd_ = new QDoubleSpinBox(this); circEnd_->setRange(0, 360); circEnd_->setValue(360); circEnd_->setPrefix("止: "); circEnd_->setSuffix(" °");
    circDir_ = new QComboBox(this); circDir_->addItems({"逆时针", "顺时针"});
    circN_ = new QSpinBox(this); circN_->setRange(10, 1000); circN_->setValue(100);

    auto *r1 = new QHBoxLayout(); r1->setSpacing(4); r1->addWidget(circCX_); r1->addWidget(circCY_);
    form->addRow("圆心:", r1);
    auto *r2 = new QHBoxLayout(); r2->setSpacing(4); r2->addWidget(circR_); r2->addWidget(circDir_);
    form->addRow("半径/方向:", r2);
    auto *r3 = new QHBoxLayout(); r3->setSpacing(4); r3->addWidget(circStart_); r3->addWidget(circEnd_);
    form->addRow("角度:", r3);
    form->addRow("采样点数:", circN_);
    break;
  }
  case 3: {  // 椭圆
    ellCX_ = new QDoubleSpinBox(this); ellCX_->setRange(-500, 500); ellCX_->setPrefix("X: "); ellCX_->setSuffix(" m");
    ellCY_ = new QDoubleSpinBox(this); ellCY_->setRange(-500, 500); ellCY_->setPrefix("Y: "); ellCY_->setSuffix(" m");
    ellA_ = new QDoubleSpinBox(this); ellA_->setRange(1, 200); ellA_->setValue(30); ellA_->setPrefix("a: "); ellA_->setSuffix(" m");
    ellB_ = new QDoubleSpinBox(this); ellB_->setRange(1, 200); ellB_->setValue(15); ellB_->setPrefix("b: "); ellB_->setSuffix(" m");
    ellRot_ = new QDoubleSpinBox(this); ellRot_->setRange(0, 360); ellRot_->setPrefix("旋转: "); ellRot_->setSuffix(" °");
    ellStart_ = new QDoubleSpinBox(this); ellStart_->setRange(0, 360); ellStart_->setPrefix("起: "); ellStart_->setSuffix(" °");
    ellEnd_ = new QDoubleSpinBox(this); ellEnd_->setRange(0, 360); ellEnd_->setValue(360); ellEnd_->setPrefix("止: "); ellEnd_->setSuffix(" °");
    ellN_ = new QSpinBox(this); ellN_->setRange(10, 1000); ellN_->setValue(100);

    auto *r1 = new QHBoxLayout(); r1->setSpacing(4); r1->addWidget(ellCX_); r1->addWidget(ellCY_);
    form->addRow("中心:", r1);
    auto *r2 = new QHBoxLayout(); r2->setSpacing(4); r2->addWidget(ellA_); r2->addWidget(ellB_);
    form->addRow("长/短半轴:", r2);
    auto *r3 = new QHBoxLayout(); r3->setSpacing(4); r3->addWidget(ellRot_); r3->addWidget(ellStart_); r3->addWidget(ellEnd_);
    form->addRow("旋转/角度:", r3);
    form->addRow("采样点数:", ellN_);
    break;
  }
  case 4: {  // 自定义
    freeInterval_ = new QDoubleSpinBox(this);
    freeInterval_->setRange(0.2, 20); freeInterval_->setValue(2.0); freeInterval_->setSuffix(" m");
    freeInterval_->setToolTip("拖拽时每隔多少米采样一个点");
    form->addRow("采样间距:", freeInterval_);
    break;
  }
  }
}

// ═══════════════════════════════════════════════════════════
// 模式切换
// ═══════════════════════════════════════════════════════════

void TaskSetDialog::onTaskTypeChanged() {
  // 防止重复点击同一 radio 导致数据丢失
  int curType = wpRadio_->isChecked() ? 0 : (trRadio_->isChecked() ? 1 : (dpRadio_->isChecked() ? 2 : 3));
  if (curType == lastTaskType_) return;
  lastTaskType_ = curType;

  wpPanel_->setVisible(wpRadio_->isChecked());
  trPanel_->setVisible(trRadio_->isChecked());
  dpPanel_->setVisible(dpRadio_->isChecked());
  gotoPanel_->setVisible(gotoRadio_->isChecked());

  // 公共重置：所有 MapWidget 状态清零，各分支再按需设置
  mapWidget_->clearWaypoints();
  mapWidget_->clearRefPoints();
  mapWidget_->clearDpTarget();
  mapWidget_->setTrackingMode(false);
  mapWidget_->setTrackingShape(false);

  if (wpRadio_->isChecked()) {
    mapWidget_->setEnabled(true);
    mapWidget_->setHintText("左键添加航路点  拖拽移动  右键删除  双击设置航向");
    w1Label_->setText("进度"); w1Spin_->setValue(0.35);
    w2Label_->setText("精度"); w2Spin_->setValue(0.40);
    w3Label_->setText("时间"); w3Spin_->setValue(0.25);
    w3Label_->show(); w3Spin_->show();
    genBtn_->hide();
  } else if (trRadio_->isChecked()) {
    mapWidget_->setTrackingMode(true);
    mapWidget_->setEnabled(true);
    w1Label_->setText("偏差"); w1Spin_->setValue(0.70);
    w2Label_->setText("时间"); w2Spin_->setValue(0.30);
    w3Label_->hide(); w3Spin_->hide();
    genBtn_->show();
    onShapeChanged(shapeCombo_->currentIndex());
  } else if (dpRadio_->isChecked()) {
    mapWidget_->setEnabled(false);
    mapWidget_->setHintText("动力定位：用下方面板设置保持位置");
    w1Label_->setText("漂移"); w1Spin_->setValue(0.50);
    w2Label_->setText("航向"); w2Spin_->setValue(0.20);
    w3Label_->setText("能耗"); w3Spin_->setValue(0.30);
    w3Label_->show(); w3Spin_->show();
    genBtn_->hide();
  } else {  // goto
    mapWidget_->setEnabled(true);
    mapWidget_->setHintText("← 在地图上点击放置目标点");
    w1Label_->setText("精度"); w1Spin_->setValue(0.70);
    w2Label_->setText("时间"); w2Spin_->setValue(0.30);
    w3Label_->hide(); w3Spin_->hide();
    genBtn_->hide();
  }
  setDirty(true);
}

// ═══════════════════════════════════════════════════════════
// 轨迹形状切换
// ═══════════════════════════════════════════════════════════

void TaskSetDialog::onShapeChanged(int _idx) {
  for (int i = 0; i < (int)shapePages_.size(); ++i)
    shapePages_[i]->setVisible(i == _idx);
  mapWidget_->clearWaypoints();
  mapWidget_->clearRefPoints();
  trajLenLabel_->setText("");

  if (_idx == 4) {
    // 自定义：拖拽画线
    mapWidget_->setTrackingShape(false);
    mapWidget_->setFreeDrawInterval(freeInterval_->value());
    mapWidget_->setHintText("← 按住左键在地图上拖拽绘制轨迹");
    genBtn_->hide();
    // 重设间距
    connect(freeInterval_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double v) { mapWidget_->setFreeDrawInterval(v); },
            Qt::UniqueConnection);
  } else {
    // 预设形状：点击地图设参考点
    mapWidget_->setTrackingShape(true);
    genBtn_->show();
    if (_idx == 0 || _idx == 1)
      mapWidget_->setRefPointCount(2);  // 直线/正弦：起终点
    else
      mapWidget_->setRefPointCount(1);  // 圆/椭圆：圆心（半径从参数面板取）
    mapWidget_->setHintText("← 在地图上点击放置参考点，然后点\"生成轨迹\"");
  }
  setDirty(true);
}

// ═══════════════════════════════════════════════════════════
// 参考点回调
// ═══════════════════════════════════════════════════════════

void TaskSetDialog::onRefPointPlaced(int _idx, double _x, double _y) {
  int shape = shapeCombo_->currentIndex();
  if (shape == 0) {  // 直线
    if (_idx == 0) { lineX1_->setValue(_x); lineY1_->setValue(_y); }
    else { lineX2_->setValue(_x); lineY2_->setValue(_y); }
  } else if (shape == 1) {  // 正弦
    if (_idx == 0) { sineX1_->setValue(_x); sineY1_->setValue(_y); }
    else { sineX2_->setValue(_x); sineY2_->setValue(_y); }
  } else if (shape == 2) {  // 圆
    circCX_->setValue(_x); circCY_->setValue(_y);
  } else if (shape == 3) {  // 椭圆
    ellCX_->setValue(_x); ellCY_->setValue(_y);
  }
  setDirty(true);
}

void TaskSetDialog::onFreeDrawChanged() {
  // 更新轨迹长度显示
  auto &wpts = mapWidget_->waypoints();
  if (wpts.size() < 2) return;
  double total = 0;
  for (size_t i = 1; i < wpts.size(); ++i)
    total += std::hypot(wpts[i].x - wpts[i-1].x, wpts[i].y - wpts[i-1].y);
  trajLenLabel_->setText(QString("轨迹长: %1 m (%2 点)").arg(total, 0, 'f', 1).arg(wpts.size()));
  setDirty(true);
}

// ═══════════════════════════════════════════════════════════
// 轨迹生成
// ═══════════════════════════════════════════════════════════

void TaskSetDialog::onGenerateTrajectory() {
  switch (shapeCombo_->currentIndex()) {
    case 0: generateLinePath(); break;
    case 1: generateSinePath(); break;
    case 2: generateCirclePath(); break;
    case 3: generateEllipsePath(); break;
  }
  onFreeDrawChanged();
}

void TaskSetDialog::generateLinePath() {
  double x1 = lineX1_->value(), y1 = lineY1_->value();
  double x2 = lineX2_->value(), y2 = lineY2_->value();
  int n = lineN_->value();

  std::vector<Waypoint> wpts;
  for (int i = 0; i <= n; ++i) {
    double t = (double)i / n;
    wpts.push_back({x1 + t * (x2 - x1), y1 + t * (y2 - y1), -1.0});
  }
  mapWidget_->setWaypoints(wpts);
  mapWidget_->clearRefPoints();
  setDirty(true);
}

void TaskSetDialog::generateSinePath() {
  double x1 = sineX1_->value(), y1 = sineY1_->value();
  double x2 = sineX2_->value(), y2 = sineY2_->value();
  double amp = sineAmp_->value();
  int cycles = sineCycles_->value();
  int n = sineN_->value();

  double dx = x2 - x1, dy = y2 - y1;
  double len = std::hypot(dx, dy);
  if (len < 0.01) return;
  double px = -dy / len, py = dx / len;  // 垂直单位向量

  std::vector<Waypoint> wpts;
  for (int i = 0; i <= n; ++i) {
    double t = (double)i / n;
    double x = x1 + t * dx + amp * std::sin(2.0 * M_PI * cycles * t) * px;
    double y = y1 + t * dy + amp * std::sin(2.0 * M_PI * cycles * t) * py;
    wpts.push_back({x, y, -1.0});
  }
  mapWidget_->setWaypoints(wpts);
  mapWidget_->clearRefPoints();
  setDirty(true);
}

void TaskSetDialog::generateCirclePath() {
  double cx = circCX_->value(), cy = circCY_->value();
  double r = circR_->value();
  double sa = circStart_->value(), ea = circEnd_->value();
  bool cw = (circDir_->currentIndex() == 1);
  int n = circN_->value();

  std::vector<Waypoint> wpts;
  for (int i = 0; i <= n; ++i) {
    double t = (double)i / n;
    double ang;
    if (cw) {
      // 顺时针：角度递减（如 0°→360°→...→90°）
      double sweep = (ea > sa) ? (sa + 360.0 - ea) : (sa - ea);
      ang = sa - t * sweep;
    } else {
      // 逆时针：角度递增
      double sweep = (ea > sa) ? (ea - sa) : (ea + 360.0 - sa);
      ang = sa + t * sweep;
    }
    double rad = deg2rad(ang);
    wpts.push_back({cx + r * std::cos(rad), cy + r * std::sin(rad), -1.0});
  }
  mapWidget_->setWaypoints(wpts);
  mapWidget_->clearRefPoints();
  setDirty(true);
}

void TaskSetDialog::generateEllipsePath() {
  double cx = ellCX_->value(), cy = ellCY_->value();
  double a = ellA_->value(), b = ellB_->value();
  double phi = deg2rad(ellRot_->value());
  double sa = ellStart_->value(), ea = ellEnd_->value();
  int n = ellN_->value();

  double cp = std::cos(phi), sp = std::sin(phi);
  std::vector<Waypoint> wpts;
  for (int i = 0; i <= n; ++i) {
    double t = (double)i / n;
    double ang = deg2rad(sa + t * (ea - sa));
    double xp = a * std::cos(ang), yp = b * std::sin(ang);
    wpts.push_back({cx + xp * cp - yp * sp, cy + xp * sp + yp * cp, -1.0});
  }
  mapWidget_->setWaypoints(wpts);
  mapWidget_->clearRefPoints();
  setDirty(true);
}

// ═══════════════════════════════════════════════════════════
// 保存
// ═══════════════════════════════════════════════════════════

bool TaskSetDialog::onSave() {
  if (nameEdit_->text().isEmpty()) {
    QMessageBox::warning(this, "错误", "请输入任务名称");
    return false;
  }
  if ((wpRadio_->isChecked() || trRadio_->isChecked())
      && mapWidget_->waypoints().size() < 2) {
    QMessageBox::warning(this, "错误", "航路点/跟踪任务至少需要 2 个点");
    return false;
  }

  QString path = QFileDialog::getSaveFileName(this, "保存任务",
      QString::fromStdString(keship::KeshipTaskDir()) + "/新任务.toml", "TOML (*.toml)");
  if (path.isEmpty()) return false;
  if (!path.endsWith(".toml")) path += ".toml";

  keship::TaskConfig cfg;
  cfg.name = nameEdit_->text().toStdString();
  cfg.timeLimit = timeSpin_->value();

  if (wpRadio_->isChecked()) {
    cfg.type = "waypoint";
    cfg.arrivalRadius = radiusSpin_->value();
  } else if (trRadio_->isChecked()) {
    cfg.type = "tracking";
    cfg.arrivalRadius = radiusSpin_->value();
  } else if (dpRadio_->isChecked()) {
    cfg.type = "dp";
    cfg.holdX = holdXSpin_->value();
    cfg.holdY = holdYSpin_->value();
    cfg.holdHdg = holdHdgSpin_->value();
    cfg.tolerance = tolSpin_->value();
  } else {
    cfg.type = "goto";
    cfg.targetX = gotoXSpin_->value();
    cfg.targetY = gotoYSpin_->value();
    cfg.targetHeading = gotoHdgSpin_->value();
    cfg.gotoRadius = gotoRSpin_->value();
  }

  for (auto &w : mapWidget_->waypoints())
    cfg.waypoints.push_back({w.x, w.y, w.heading});

  std::string err;
  if (!keship::TaskTomlParser::save(path.toStdString(), cfg, &err)) {
    QMessageBox::warning(this, "错误", QString::fromStdString(err));
    return false;
  }

  std::cout << "Phase 10: 任务已保存 → " << path.toStdString() << std::endl;
  setDirty(false);
  emit taskSaved(path);
  return true;
}

// ═══════════════════════════════════════════════════════════

bool TaskSetDialog::isDirty() const { return dirty_; }
void TaskSetDialog::setDirty(bool _d) { dirty_ = _d; }

void TaskSetDialog::closeEvent(QCloseEvent *_event) {
  if (isDirty()) {
    auto btn = QMessageBox::question(
        this, "未保存的更改",
        "任务尚未保存，是否保存后关闭？",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (btn == QMessageBox::Save) {
      if (onSave()) _event->accept(); else _event->ignore();
    } else if (btn == QMessageBox::Discard) {
      _event->accept();
    } else {
      _event->ignore();
    }
  } else {
    _event->accept();
  }
}
