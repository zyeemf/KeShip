/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 传感器编辑器对话框实现 — 多实例 + 位姿编辑 + 3D 预览 */
/// ZhangYu 创建 2026.5.16

#include "SensorEditorDialog.hh"
#include "../shared/UiTheme.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QMessageBox>

#include <cmath>

SensorEditorDialog::SensorEditorDialog(
    const QString &_modelName,
    const QString &_linkName,
    const Buoyancy3DModelInfo &_modelInfo,
    QWidget *_parent)
    : QDialog(_parent),
      modelName_(_modelName),
      linkName_(_linkName)
{
  setWindowTitle("传感器配置 — " + modelName_);
  setModal(false);
  resize(780, 500);

  auto *mainLayout = new QVBoxLayout(this);

  auto *contentLayout = new QHBoxLayout();

  /// 左侧: 传感器列表
  auto *leftPanel = new QVBoxLayout();
  listWidget_ = new QListWidget;
  listWidget_->setMinimumWidth(160);
  leftPanel->addWidget(listWidget_);

  auto *addDelLayout = new QHBoxLayout();
  auto *addBtn = new QPushButton("+ 添加");
  UiTheme::setButtonIcon(addBtn, UiTheme::Icon::kAdd);
  auto *delBtn = new QPushButton("删除");
  UiTheme::setButtonIcon(delBtn, UiTheme::Icon::kDelete);
  addDelLayout->addWidget(addBtn);
  addDelLayout->addWidget(delBtn);
  leftPanel->addLayout(addDelLayout);

  /// 参数面板
  rightPanel_ = new QGroupBox("传感器参数");
  auto *formLayout = new QFormLayout(rightPanel_);

  typeLabel_ = new QLabel;
  formLayout->addRow("类型:", typeLabel_);

  nameEdit_ = new QLineEdit;
  formLayout->addRow("名称:", nameEdit_);

  /// ★ 2026-09-26「Camera 规格用户自选」：仅 Camera 类型时可见（updateCameraRows 控制）
  /// 档位直接取自 ModelManager.hh 的表 ⇒ 与 ClampCameraSpec() 的吸附**同源**，
  /// 不会出现"GUI 能选但夹紧会改"的不一致。
  camResCombo_ = new QComboBox;
  for (int i = 0; i < keship::kCameraResolutionCount; ++i)
    camResCombo_->addItem(QString("%1×%2")
        .arg(keship::kCameraResolutions[i][0])
        .arg(keship::kCameraResolutions[i][1]));
  formLayout->addRow("分辨率:", camResCombo_);

  camRateCombo_ = new QComboBox;
  for (int i = 0; i < keship::kCameraRateCount; ++i)
    camRateCombo_->addItem(QString("%1 Hz").arg(keship::kCameraRates[i]));
  formLayout->addRow("帧率:", camRateCombo_);

  camCostLabel_ = new QLabel;
  formLayout->addRow("预计成本:", camCostLabel_);

  formLayout_ = formLayout;

  const char *poseLabels[] = {"X:", "Y:", "Z:", "Roll:", "Pitch:", "Yaw:"};
  const char *poseSuffix[] = {" m", " m", " m", "°", "°", "°"};
  for (int i = 0; i < 6; i++)
  {
    poseSpin_[i] = new QDoubleSpinBox;
    poseSpin_[i]->setRange(-1000, 1000);
    poseSpin_[i]->setDecimals(3);
    poseSpin_[i]->setSingleStep(0.1);
    poseSpin_[i]->setSuffix(poseSuffix[i]);
    formLayout->addRow(poseLabels[i], poseSpin_[i]);
  }

  saveBtn_ = new QPushButton("保存修改");
  UiTheme::setButtonIcon(saveBtn_, UiTheme::Icon::kSave);
  formLayout->addRow(saveBtn_);
  leftPanel->addWidget(rightPanel_);
  contentLayout->addLayout(leftPanel);
  rightPanel_->hide();  // 初始无选中，隐藏

  /// 右侧: 3D 预览
  auto *viewGroup = new QGroupBox("3D 预览");
  auto *viewLayout = new QVBoxLayout(viewGroup);
  view3d_ = new Buoyancy3DView(viewGroup);
  view3d_->setModel(_modelInfo);
  viewLayout->addWidget(view3d_);
  contentLayout->addWidget(viewGroup, 2);

  mainLayout->addLayout(contentLayout);

  /// 底部按钮
  auto *bottomLayout = new QHBoxLayout();
  auto *applyBtn = new QPushButton("应用并重建模型");
  UiTheme::setButtonIcon(applyBtn, UiTheme::Icon::kOk);
  auto *cancelBtn = new QPushButton("取消");
  UiTheme::setButtonIcon(cancelBtn, UiTheme::Icon::kCancel);
  bottomLayout->addStretch();
  bottomLayout->addWidget(applyBtn);
  bottomLayout->addWidget(cancelBtn);
  mainLayout->addLayout(bottomLayout);

  /// 信号连接
  connect(listWidget_, &QListWidget::currentRowChanged,
          this, [this](int _row) { selectedIdx_ = _row; onSelectionChanged(); });
  connect(addBtn, &QPushButton::clicked, this, &SensorEditorDialog::onAddSensor);
  connect(delBtn, &QPushButton::clicked, this, &SensorEditorDialog::onDeleteSensor);
  connect(saveBtn_, &QPushButton::clicked, this, &SensorEditorDialog::onSaveInstance);
  connect(applyBtn, &QPushButton::clicked, this, &SensorEditorDialog::onApplyAndRebuild);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

  /// spinbox 变化 → 实时更新 3D 预览
  for (int i = 0; i < 6; i++)
    connect(poseSpin_[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &SensorEditorDialog::onParamChanged);

  /// ★ 2026-09-26：规格变化 → 重算"预计成本"
  connect(camResCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { onCameraSpecChanged(); });
  connect(camRateCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, [this](int) { onCameraSpecChanged(); });
  updateCameraRows();   /// 初始无选中 ⇒ 隐藏
}

//////////////////////////////////////////////////////////////////////////////////
/// ★ 2026-09-26「Camera 规格用户自选」：规格三行只在选中 **Camera** 时出现。
/// QFormLayout 的行标题（label）要单独取出来一起隐藏，否则会留下孤零零的"分辨率:"。
void SensorEditorDialog::updateCameraRows()
{
  const bool isCam = (selectedIdx_ >= 0 && selectedIdx_ < (int)config_.size()
                      && config_[selectedIdx_].type == keship::SensorType::Camera);
  camResCombo_->setVisible(isCam);
  camRateCombo_->setVisible(isCam);
  camCostLabel_->setVisible(isCam);
  if (formLayout_)
  {
    for (QWidget *w : {static_cast<QWidget *>(camResCombo_),
                       static_cast<QWidget *>(camRateCombo_),
                       static_cast<QWidget *>(camCostLabel_)})
      if (auto *lbl = formLayout_->labelForField(w))
        lbl->setVisible(isCam);
  }
  if (isCam)
    onCameraSpecChanged();
}

//////////////////////////////////////////////////////////////////////////////////
/// 重算并显示"预计成本"：`×N` 相对默认档（320×240@10）。
/// ⚠ 显示的是 `ClampCameraSpec()` 夹紧后的**实际生效值** —— 若被夹紧，
///   直接回显"实际生效 WxH @R Hz"，用户立刻知道自己的选择会被改成什么。
void SensorEditorDialog::onCameraSpecChanged()
{
  if (!camResCombo_ || !camRateCombo_ || !camCostLabel_)
    return;
  const int ri = std::max(0, camResCombo_->currentIndex());
  const int fi = std::max(0, camRateCombo_->currentIndex());
  if (ri >= keship::kCameraResolutionCount || fi >= keship::kCameraRateCount)
    return;

  const int reqW = keship::kCameraResolutions[ri][0];
  const int reqH = keship::kCameraResolutions[ri][1];
  const double reqR = keship::kCameraRates[fi];

  bool clamped = false;
  const keship::CameraSpec spec =
      keship::ClampCameraSpec(reqW, reqH, reqR, &clamped);

  const double base = double(keship::kCameraDefaultWidth)
                    * keship::kCameraDefaultHeight * keship::kCameraDefaultRate;
  const double ratio = double(spec.width) * spec.height * spec.rate / base;

  QString txt = QString("×%1（默认 ×1.0）").arg(ratio, 0, 'f', 2);
  if (clamped)
    txt = QString("×%1 ⚠ 已按合法区间修正 → %2×%3 @ %4 Hz")
              .arg(ratio, 0, 'f', 2).arg(spec.width).arg(spec.height)
              .arg(spec.rate, 0, 'f', 1);
  /// ★ 2026-09-26：成本**只提示、不干预**（张老师纠正：仿真应保住用户设定）。
  /// 说清"影响的是什么、不影响什么"，避免用户以为会算错。
  if (ratio > 2.0)
    txt += QString("　⚠ 多模型时实时率(RTF)会下降 —— 只影响出结果的快慢，**不影响仿真结果**");
  camCostLabel_->setText(txt);
  camCostLabel_->setStyleSheet(ratio > 2.0 ? "color:#ef6c00;" : "");
}

//////////////////////////////////////////////////////////////////////////////////
Sensor3DInfo SensorEditorDialog::currentSensor3DInfo() const
{
  Sensor3DInfo si;
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size())
    return si;
  const auto &inst = config_[selectedIdx_];
  si.type = (int)inst.type;
  si.poseX = poseSpin_[0]->value();
  si.poseY = poseSpin_[1]->value();
  si.poseZ = poseSpin_[2]->value();
  si.poseRoll  = poseSpin_[3]->value() * M_PI / 180.0;
  si.posePitch = poseSpin_[4]->value() * M_PI / 180.0;
  si.poseYaw   = poseSpin_[5]->value() * M_PI / 180.0;
  return si;
}

//////////////////////////////////////////////////////////////////////////////////
QString SensorEditorDialog::autoName(keship::SensorType _type) const
{
  QString prefix;
  switch (_type)
  {
    case keship::SensorType::Camera:       prefix = "camera_"; break;
    case keship::SensorType::Lidar:        prefix = "lidar_"; break;
    case keship::SensorType::IMU:          prefix = "imu_"; break;
    case keship::SensorType::NavSat:       prefix = "bds_"; break;
    case keship::SensorType::Magnetometer: prefix = "mag_"; break;
    default: return "sensor";
  }
  int n = 0;
  for (const auto &s : config_)
    if (s.type == _type) n++;
  return prefix + QString::number(n + 1);
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::onAddSensor()
{
  /// 弹出类型选择
  QStringList types;
  types << "Camera (相机)" << "Lidar (激光雷达)" << "IMU (惯性单元)"
        << "NavSat (BDS)" << "Magnetometer (磁力计)";
  bool ok = false;
  QString sel = QInputDialog::getItem(this, "选择传感器类型",
      "类型:", types, 0, false, &ok);
  if (!ok || sel.isEmpty()) return;

  keship::SensorType st;
  if (sel.startsWith("Camera"))       st = keship::SensorType::Camera;
  else if (sel.startsWith("Lidar"))   st = keship::SensorType::Lidar;
  else if (sel.startsWith("IMU"))     st = keship::SensorType::IMU;
  else if (sel.startsWith("NavSat"))  st = keship::SensorType::NavSat;
  else                                st = keship::SensorType::Magnetometer;

  InternalSensor inst;
  inst.type = st;
  inst.name = autoName(st);

  /// 出厂默认 pose
  double defPose[6] = {0, 0, 0, 0, 0, 0};
  if (st == keship::SensorType::Camera || st == keship::SensorType::Lidar)
    defPose[2] = 1.0; // 默认上方 1m
  for (int i = 0; i < 6; i++) inst.pose[i] = defPose[i];

  config_.push_back(inst);
  updateList();
  listWidget_->setCurrentRow((int)config_.size() - 1);
  update3DView();
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::onDeleteSensor()
{
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size()) return;
  config_.erase(config_.begin() + selectedIdx_);
  selectedIdx_ = -1;
  updateList();
  if (!config_.empty())
    listWidget_->setCurrentRow(0);  // 自动选中第一个，触发 onSelectionChanged 显示面板
  else
  {
    updateParamPanel();  // 无传感器时隐藏面板
    update3DView();
  }
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::onSelectionChanged()
{
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size())
  {
    updateParamPanel();
    if (view3d_) view3d_->setSelectedSensorIdx(-1);
    return;
  }
  isLoading_ = true;
  const auto &inst = config_[selectedIdx_];
  const char *typeStr = keship::sensorTypeName(inst.type);
  typeLabel_->setText(QString::fromUtf8(typeStr));
  nameEdit_->setText(inst.name);
  nameEdit_->setEnabled(true);
  for (int i = 0; i < 6; i++)
  {
    poseSpin_[i]->setValue(inst.pose[i]);
    poseSpin_[i]->setEnabled(true);
  }
  saveBtn_->setEnabled(true);

  /// ★ 2026-09-26：填充 Camera 规格（0 = 未指定 ⇒ 定位到默认档）
  {
    bool cl = false;
    const keship::CameraSpec sp = keship::ClampCameraSpec(
        inst.camWidth, inst.camHeight, inst.camRate, &cl);
    for (int i = 0; i < keship::kCameraResolutionCount; ++i)
      if (keship::kCameraResolutions[i][0] == sp.width &&
          keship::kCameraResolutions[i][1] == sp.height)
        camResCombo_->setCurrentIndex(i);
    for (int i = 0; i < keship::kCameraRateCount; ++i)
      if (std::fabs(keship::kCameraRates[i] - sp.rate) < 1e-9)
        camRateCombo_->setCurrentIndex(i);
  }
  updateCameraRows();

  rightPanel_->show();
  if (view3d_) view3d_->setSelectedSensorIdx(selectedIdx_);
  isLoading_ = false;
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::onParamChanged()
{
  if (isLoading_) return;
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size()) return;

  if (view3d_)
    view3d_->updateSensor(selectedIdx_, currentSensor3DInfo());
}

//////////////////////////////////////////////////////////////////////////////////
bool SensorEditorDialog::onSaveInstance()
{
  if (selectedIdx_ < 0 || selectedIdx_ >= (int)config_.size()) return false;

  QString newName = nameEdit_->text().trimmed();
  if (newName.isEmpty())
  {
    QMessageBox::warning(this, "名称无效", "传感器名称不能为空");
    return false;
  }

  /// 检查是否与其他实例重名（忽略自身）
  for (int i = 0; i < (int)config_.size(); i++)
  {
    if (i == selectedIdx_) continue;
    if (config_[i].name == newName)
    {
      QMessageBox::warning(this, "名称冲突",
          QString("传感器名称 \"%1\" 已被使用，请使用其他名称").arg(newName));
      return false;
    }
  }

  auto &inst = config_[selectedIdx_];
  inst.name = newName;
  for (int i = 0; i < 6; i++)
    inst.pose[i] = poseSpin_[i]->value();

  /// ★ 2026-09-26：收集 Camera 规格（写**夹紧后的实际生效值**，
  ///   与 SDF 生成 / ksm 序列化用的是同一个 ClampCameraSpec ⇒ 三处必然一致）
  if (inst.type == keship::SensorType::Camera &&
      camResCombo_ && camRateCombo_)
  {
    const int ri = std::max(0, camResCombo_->currentIndex());
    const int fi = std::max(0, camRateCombo_->currentIndex());
    const keship::CameraSpec sp = keship::ClampCameraSpec(
        keship::kCameraResolutions[ri][0], keship::kCameraResolutions[ri][1],
        keship::kCameraRates[fi]);
    inst.camWidth  = sp.width;
    inst.camHeight = sp.height;
    inst.camRate   = sp.rate;
  }
  int savedIdx = selectedIdx_;
  updateList();
  listWidget_->setCurrentRow(savedIdx);
  update3DView();
  return true;
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::onApplyAndRebuild()
{
  /// 先保存当前选中传感器的面板修改，再关闭
  if (selectedIdx_ >= 0 && selectedIdx_ < (int)config_.size())
  {
    if (!onSaveInstance())
      return;
  }
  accept();
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::updateList()
{
  listWidget_->clear();
  const char *typeIcons[] = {"[IMU]", "[BDS]", "[MAG]", "[CAM]", "[LDR]"};
  for (const auto &inst : config_)
  {
    int idx = (int)inst.type;
    QString label = QString("%1 %2").arg(
        typeIcons[idx >= 0 && idx < 5 ? idx : 0],
        inst.name);
    listWidget_->addItem(label);
  }
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::updateParamPanel()
{
  bool valid = (selectedIdx_ >= 0 && selectedIdx_ < (int)config_.size());
  typeLabel_->setText(valid ? QString::fromUtf8(
      keship::sensorTypeName(config_[selectedIdx_].type)) : "");
  nameEdit_->setText(valid ? config_[selectedIdx_].name : "");
  for (int i = 0; i < 6; i++)
    poseSpin_[i]->setValue(valid ? config_[selectedIdx_].pose[i] : 0.0);
  typeLabel_->setEnabled(valid);
  nameEdit_->setEnabled(valid);
  saveBtn_->setEnabled(valid);
  for (int i = 0; i < 6; i++)
    poseSpin_[i]->setEnabled(valid);
  updateCameraRows();   /// ★ 2026-09-26：无选中/非 Camera 时隐藏规格三行
  rightPanel_->setVisible(valid);
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::update3DView()
{
  if (!view3d_) return;

  std::vector<Sensor3DInfo> infos;
  for (size_t i = 0; i < config_.size(); i++)
  {
    if (static_cast<int>(i) == selectedIdx_)
    {
      /// 当前选中项：使用 spinbox 预览值
      infos.push_back(currentSensor3DInfo());
    }
    else
    {
      /// 其他项：使用已保存的 config 值（config 存度，需转弧度给 3D 视图）
      Sensor3DInfo si;
      const auto &inst = config_[i];
      si.type = (int)inst.type;
      si.poseX = inst.pose[0];
      si.poseY = inst.pose[1];
      si.poseZ = inst.pose[2];
      si.poseRoll  = inst.pose[3] * M_PI / 180.0;
      si.posePitch = inst.pose[4] * M_PI / 180.0;
      si.poseYaw   = inst.pose[5] * M_PI / 180.0;
      infos.push_back(si);
    }
  }
  view3d_->syncSensors(infos);
  view3d_->setSelectedSensorIdx(selectedIdx_);
}

//////////////////////////////////////////////////////////////////////////////////
keship::SensorConfig SensorEditorDialog::getConfig() const
{
  keship::SensorConfig cfg;
  cfg.linkName = linkName_.toStdString();
  for (const auto &s : config_)
  {
    keship::SensorInstance inst;
    inst.type = s.type;
    inst.name = s.name.toStdString();
    inst.pose[0] = s.pose[0];
    inst.pose[1] = s.pose[1];
    inst.pose[2] = s.pose[2];
    inst.pose[3] = s.pose[3] * M_PI / 180.0;  // deg → rad
    inst.pose[4] = s.pose[4] * M_PI / 180.0;
    inst.pose[5] = s.pose[5] * M_PI / 180.0;
    /// ★ 2026-09-26「Camera 规格用户自选」
    inst.camWidth  = s.camWidth;
    inst.camHeight = s.camHeight;
    inst.camRate   = s.camRate;
    cfg.instances.push_back(inst);
  }
  return cfg;
}

//////////////////////////////////////////////////////////////////////////////////
void SensorEditorDialog::setConfig(const keship::SensorConfig &_config)
{
  config_.clear();
  linkName_ = QString::fromStdString(_config.linkName);
  for (const auto &inst : _config.instances)
  {
    InternalSensor s;
    s.type = inst.type;
    s.name = QString::fromStdString(inst.name);
    s.pose[0] = inst.pose[0];
    s.pose[1] = inst.pose[1];
    s.pose[2] = inst.pose[2];
    s.pose[3] = inst.pose[3] * 180.0 / M_PI;  // rad → deg
    s.pose[4] = inst.pose[4] * 180.0 / M_PI;
    s.pose[5] = inst.pose[5] * 180.0 / M_PI;
    /// ★ 2026-09-26「Camera 规格用户自选」（0 = 未指定 ⇒ 默认档）
    s.camWidth  = inst.camWidth;
    s.camHeight = inst.camHeight;
    s.camRate   = inst.camRate;
    config_.push_back(s);
  }
  selectedIdx_ = -1;
  updateList();
  updateParamPanel();
  if (!config_.empty())
    listWidget_->setCurrentRow(0);
  update3DView();
}
