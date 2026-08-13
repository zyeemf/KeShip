/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* 惯性计算工具对话框实现 */
/// ZhangYu 创建 2026.7.18

#include "InertiaCalculatorDialog.hh"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QLabel>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QApplication>
#include <QClipboard>
#include <QMessageBox>
#include <sstream>
#include <iomanip>
#include <cmath>

#include "../shared/Buoyancy3DView.hh"
#include "keship/utils/KeshipPaths.hh"

//---------------------------------------------------------------------
InertiaCalculatorDialog::InertiaCalculatorDialog(QWidget *_parent)
  : QDialog(_parent)
{
  setWindowTitle("惯性计算工具");
  resize(600, 720);
  setMinimumSize(500, 600);

  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(6);

  // ===== 文件选择 =====
  auto *fileGroup = new QGroupBox("模型文件", this);
  auto *fileLayout = new QVBoxLayout(fileGroup);

  auto *fileRow = new QHBoxLayout;
  fileEdit_ = new QLineEdit(fileGroup);
  fileEdit_->setReadOnly(true);
  fileEdit_->setPlaceholderText("选择 .dae / .stl / .obj / .gltf 文件");
  browseBtn_ = new QPushButton("浏览", fileGroup);
  browseBtn_->setFixedWidth(60);
  fileRow->addWidget(fileEdit_, 1);
  fileRow->addWidget(browseBtn_);
  fileLayout->addLayout(fileRow);

  meshInfo_ = new QLabel("网格信息: 未加载", fileGroup);
  meshInfo_->setStyleSheet("color: #666666; font-size: 11px;");
  fileLayout->addWidget(meshInfo_);

  mainLayout->addWidget(fileGroup);

  // ===== 材料密度 =====
  auto *matGroup = new QGroupBox("材料密度", this);
  auto *matLayout = new QHBoxLayout(matGroup);

  materialCombo_ = new QComboBox(matGroup);
  materialCombo_->addItem("钢 Steel",      7850.0);
  materialCombo_->addItem("铝 Aluminum",   2700.0);
  materialCombo_->addItem("FRP 玻璃钢",    1800.0);
  materialCombo_->addItem("水 Water",      1000.0);
  materialCombo_->addItem("自定义 Custom", 0.0);
  materialCombo_->setCurrentIndex(3);

  densitySpin_ = new QDoubleSpinBox(matGroup);
  densitySpin_->setRange(1.0, 20000.0);
  densitySpin_->setValue(1000.0);
  densitySpin_->setDecimals(0);
  densitySpin_->setSuffix(" kg/m³");
  densitySpin_->setEnabled(false);

  matLayout->addWidget(materialCombo_, 1);
  matLayout->addWidget(densitySpin_, 1);

  mainLayout->addWidget(matGroup);

  // ===== 计算方法 =====
  auto *methodGroup = new QGroupBox("计算方法", this);
  auto *methodLayout = new QHBoxLayout(methodGroup);
  methodCombo_ = new QComboBox(methodGroup);
  methodCombo_->addItem("直接计算 (Direct)",
      static_cast<int>(keship::InertiaCalcMethod::Direct));
  methodCombo_->addItem("凸包计算 (Convex Hull)",
      static_cast<int>(keship::InertiaCalcMethod::ConvexHull));
  methodCombo_->addItem("凸分解合并 (V-HACD)",
      static_cast<int>(keship::InertiaCalcMethod::VHACD));
  methodCombo_->setCurrentIndex(1);
  methodLayout->addWidget(methodCombo_, 1);
  mainLayout->addWidget(methodGroup);

  // ===== 3D 预览 =====
  auto *prevGroup = new QGroupBox("3D 预览", this);
  auto *prevLayout = new QVBoxLayout(prevGroup);
  preview_ = new Buoyancy3DView(prevGroup);
  preview_->setMinimumHeight(200);
  preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  prevLayout->addWidget(preview_);
  mainLayout->addWidget(prevGroup, 1);

  // ===== 操作按钮 =====
  auto *btnLayout = new QHBoxLayout;
  calcBtn_ = new QPushButton("计算惯性", this);
  calcBtn_->setEnabled(false);
  calcBtn_->setStyleSheet(
      "QPushButton { background-color: #4a90d9; color: white; "
      "font-weight: bold; padding: 6px 16px; border-radius: 4px; } "
      "QPushButton:disabled { background-color: #c0c0c0; }");
  copyBtn_ = new QPushButton("复制 SDF 片段", this);
  copyBtn_->setEnabled(false);

  btnLayout->addWidget(calcBtn_);
  btnLayout->addWidget(copyBtn_);
  btnLayout->addStretch();
  mainLayout->addLayout(btnLayout);

  // ===== 计算结果 =====
  resultGroup_ = new QGroupBox("计算结果", this);
  auto *resLayout = new QVBoxLayout(resultGroup_);

  volumeLabel_ = new QLabel("体积: ---", resultGroup_);
  massLabel_   = new QLabel("质量: ---", resultGroup_);
  cogLabel_    = new QLabel("重心偏移: ---", resultGroup_);
  tensorLabel_ = new QLabel("惯量张量: ---", resultGroup_);
  statusLabel_ = new QLabel("", resultGroup_);

  for (auto *lb : {volumeLabel_, massLabel_, cogLabel_, tensorLabel_})
    lb->setTextFormat(Qt::PlainText);

  resLayout->addWidget(volumeLabel_);
  resLayout->addWidget(massLabel_);
  resLayout->addWidget(cogLabel_);
  resLayout->addWidget(tensorLabel_);
  resLayout->addWidget(statusLabel_);

  mainLayout->addWidget(resultGroup_);

  // ===== 信号绑定 =====
  connect(browseBtn_, &QPushButton::clicked,
          this, &InertiaCalculatorDialog::onBrowseFile);
  connect(materialCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &InertiaCalculatorDialog::onDensityPreset);
  connect(methodCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &InertiaCalculatorDialog::onMethodChanged);
  connect(calcBtn_, &QPushButton::clicked,
          this, &InertiaCalculatorDialog::onCalculate);
  connect(copyBtn_, &QPushButton::clicked,
          this, &InertiaCalculatorDialog::onCopySDF);
}

InertiaCalculatorDialog::~InertiaCalculatorDialog() = default;

//---------------------------------------------------------------------
void InertiaCalculatorDialog::onBrowseFile()
{
  QString filter = "3D 模型 (*.dae *.stl *.obj *.gltf *.glb);;"
                   "DAE (*.dae);;STL (*.stl);;OBJ (*.obj);;"
                   "glTF (*.gltf *.glb);;所有文件 (*)";
  QString path = QFileDialog::getOpenFileName(this, "选择模型文件",
      QString::fromStdString(keship::KeshipAllModelsDir()), filter);
  if (path.isEmpty())
    return;

  loadMeshFile(path.toStdString());
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::loadMeshFile(const std::string &_path)
{
  meshPath_ = _path;
  fileEdit_->setText(QString::fromStdString(_path));

  loadedMesh_ = keship::LoadMeshFromFile(_path);
  if (loadedMesh_.vertices.empty() || loadedMesh_.triangles.empty())
  {
    meshInfo_->setText("⚠ 加载失败或文件格式不支持");
    calcBtn_->setEnabled(false);
    clearResult();
    return;
  }

  int nV = (int)loadedMesh_.vertices.size();
  int nT = (int)loadedMesh_.triangles.size();

  double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9, minZ = 1e9, maxZ = -1e9;
  for (const auto &v : loadedMesh_.vertices)
  {
    if (v.X() < minX) minX = v.X();
    if (v.X() > maxX) maxX = v.X();
    if (v.Y() < minY) minY = v.Y();
    if (v.Y() > maxY) maxY = v.Y();
    if (v.Z() < minZ) minZ = v.Z();
    if (v.Z() > maxZ) maxZ = v.Z();
  }

  std::ostringstream oss;
  oss << "顶点 " << nV << "  |  三角面 " << nT
      << "  |  包围盒  " << sciStr(maxX - minX) << " × "
      << sciStr(maxY - minY) << " × " << sciStr(maxZ - minZ);
  meshInfo_->setText(QString::fromStdString(oss.str()));
  meshInfo_->setStyleSheet("color: #333333; font-size: 11px;");

  calcBtn_->setEnabled(true);
  clearResult();

  Buoyancy3DModelInfo info;
  info.isBox = false;
  info.meshPath = _path;
  preview_->setModel(info);
  preview_->setShowCoG(false);
  preview_->update();
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::onDensityPreset(int _idx)
{
  double dv = materialCombo_->itemData(_idx).toDouble();
  if (dv > 0.0)
  {
    densitySpin_->setValue(dv);
    densitySpin_->setEnabled(false);
  }
  else
  {
    densitySpin_->setEnabled(true);
    densitySpin_->setFocus();
  }
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::onMethodChanged(int _idx)
{
  (void)_idx;
  // 计算前清除上次结果
  clearResult();
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::onCalculate()
{
  if (loadedMesh_.triangles.empty())
  {
    QMessageBox::warning(this, "无数据", "请先加载模型文件");
    return;
  }

  double density = densitySpin_->value();
  lastDensity_ = density;

  auto method = static_cast<keship::InertiaCalcMethod>(
      methodCombo_->currentData().toInt());
  auto result = keship::ComputeInertiaFromMesh(loadedMesh_, density, method);
  lastResult_ = result;

  if (result.error != keship::InertiaCalcError::Ok)
  {
    showResult(result);
    copyBtn_->setEnabled(false);
    return;
  }

  copyBtn_->setEnabled(true);
  showResult(result);

  preview_->setCoGPosition(
      static_cast<float>(result.params.cogX),
      static_cast<float>(result.params.cogY),
      static_cast<float>(result.params.cogZ));
  preview_->setShowCoG(true);
  preview_->update();
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::showResult(const keship::InertiaCalcResult &_r)
{
  const auto &p = _r.params;

  volumeLabel_->setText(
      QString("体积: %1 m³").arg(QString::fromStdString(sciStr(_r.volume))));
  massLabel_->setText(
      QString("质量: %1 kg").arg(QString::fromStdString(sciStr(p.mass))));

  cogLabel_->setText(QString("重心偏移:  cogX %1 m    cogY %2 m    cogZ %3 m")
      .arg(QString::number(p.cogX, 'f', 4))
      .arg(QString::number(p.cogY, 'f', 4))
      .arg(QString::number(p.cogZ, 'f', 4)));

  tensorLabel_->setText(QString(
      "惯量张量 (kg·m²):\n"
      "  Ixx %1    Ixy %2    Ixz %3\n"
      "  Iyy %4    Iyz %5    Izz %6")
      .arg(sciStr(p.ixx).c_str()).arg(sciStr(p.ixy).c_str())
      .arg(sciStr(p.ixz).c_str())
      .arg(sciStr(p.iyy).c_str()).arg(sciStr(p.iyz).c_str())
      .arg(sciStr(p.izz).c_str()));

  std::string methodName;
  switch (_r.method)
  {
    case keship::InertiaCalcMethod::Direct:     methodName = "直接计算"; break;
    case keship::InertiaCalcMethod::ConvexHull: methodName = "凸包计算"; break;
    case keship::InertiaCalcMethod::VHACD:      methodName = "V-HACD";  break;
  }

  std::ostringstream status;
  status << "方法: " << methodName;
  if (_r.hullVertexCount > 0)
    status << "  |  凸包顶点 " << _r.hullVertexCount
           << "  三角面 " << _r.hullFaceCount;
  status << "  |  ";

  if (_r.error == keship::InertiaCalcError::Ok)
  {
    bool ok = (p.ixx + p.iyy) > p.izz
           && (p.ixx + p.izz) > p.iyy
           && (p.iyy + p.izz) > p.ixx;
    if (ok)
      status << "计算通过，惯量张量满足三角不等式";
    else
      status << "惯量张量不满足三角不等式，请检查网格质量";
    statusLabel_->setStyleSheet("color: #228822; font-size: 12px;");
  }
  else
  {
    status << _r.message;
    statusLabel_->setStyleSheet("color: #cc2222; font-size: 12px;");
  }
  statusLabel_->setText(QString::fromStdString(status.str()));
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::clearResult()
{
  lastResult_ = {};
  copyBtn_->setEnabled(false);

  volumeLabel_->setText("体积: ---");
  massLabel_->setText("质量: ---");
  cogLabel_->setText("重心偏移: ---");
  tensorLabel_->setText("惯量张量: ---");
  statusLabel_->setText("");
}

//---------------------------------------------------------------------
void InertiaCalculatorDialog::onCopySDF()
{
  if (lastResult_.error != keship::InertiaCalcError::Ok)
    return;

  std::string sdf = keship::FormatInertiaSDF(lastResult_.params);
  QApplication::clipboard()->setText(QString::fromStdString(sdf));

  QMessageBox::information(this, "已复制",
      "SDF <inertial> 片段已复制到剪贴板，可粘贴到模型 SDF 中。\n"
      "也可手动填入 ModelSettingsDialog 的对应字段。");
}

//---------------------------------------------------------------------
std::string InertiaCalculatorDialog::sciStr(double _v) const
{
  double av = std::abs(_v);
  std::ostringstream ss;

  if (av == 0.0)
    return "0";
  if (av < 0.01 || av >= 1e6)
  {
    ss << std::scientific << std::setprecision(4) << _v;
    return ss.str();
  }
  ss << std::fixed << std::setprecision(4) << _v;
  return ss.str();
}
