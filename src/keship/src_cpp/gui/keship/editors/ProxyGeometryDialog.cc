/*
 * Copyright (C) 2024–2026 KeShip Team
 */
/* SD-06 代理几何体建模与简化 对话框实现 */
/// KeShip Team 创建 2026.10.15

#include "ProxyGeometryDialog.hh"
#include "../shared/UiTheme.hh"
#include "../shared/Buoyancy3DView.hh"

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
#include <QMessageBox>
#include <sstream>
#include <iomanip>
#include <cmath>

#include "keship/utils/KeshipPaths.hh"

namespace
{
/// 对比表的指标行名（与 ProxyGeometryDialog::kRows 一一对应）
const char *kRowNames[ProxyGeometryDialog::kRows] = {
    "顶点数",
    "三角面数",
    "体积 V (m³)",
    "质量 m (kg)",
    "排水浮力 F_B (kN)",
    "Ixx (kg·m²)",
    "Iyy (kg·m²)",
    "Izz (kg·m²)",
    "体积偏差 ΔV",
};

constexpr double kGravity = 9.81;   ///< 重力加速度 m/s²（浮力 = ρ_w·g·V）

QLabel *makeHeader(const QString &_text, QWidget *_parent,
                   Qt::Alignment _align = Qt::AlignCenter)
{
  auto *lb = new QLabel(_text, _parent);
  lb->setStyleSheet("font-weight: bold; color: #2c3e50; padding: 2px 6px;");
  lb->setAlignment(_align);
  return lb;
}

QLabel *makeCell(QWidget *_parent)
{
  auto *lb = new QLabel("—", _parent);
  lb->setStyleSheet("padding: 2px 6px; color: #333333;");
  lb->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  lb->setTextInteractionFlags(Qt::TextSelectableByMouse);
  return lb;
}
} // namespace

//---------------------------------------------------------------------
ProxyGeometryDialog::ProxyGeometryDialog(QWidget *_parent)
  : QDialog(_parent)
{
  setWindowTitle("SD-06 代理几何体建模与简化");
  resize(820, 900);
  setMinimumSize(660, 720);

  auto *mainLayout = new QVBoxLayout(this);
  mainLayout->setSpacing(6);

  // ===== 模型文件 =====
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

  // ===== 材料 / 水域密度 =====
  auto *matGroup = new QGroupBox("材料与水域", this);
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
  densitySpin_->setToolTip("代理几何体的材料密度（用于质量与惯量）");

  waterDensitySpin_ = new QDoubleSpinBox(matGroup);
  waterDensitySpin_->setRange(1.0, 2000.0);
  waterDensitySpin_->setValue(1025.0);
  waterDensitySpin_->setDecimals(0);
  waterDensitySpin_->setSuffix(" kg/m³");
  waterDensitySpin_->setToolTip("水域密度（用于排水浮力 F_B = ρ·g·V）");

  matLayout->addWidget(new QLabel("材料:", matGroup));
  matLayout->addWidget(materialCombo_, 2);
  matLayout->addWidget(densitySpin_, 2);
  matLayout->addWidget(new QLabel("水域:", matGroup));
  matLayout->addWidget(waterDensitySpin_, 2);
  mainLayout->addWidget(matGroup);

  // ===== 计算按钮 =====
  auto *btnRow = new QHBoxLayout;
  compareBtn_ = new QPushButton("计算三档对比", this);
  compareBtn_->setEnabled(false);
  compareBtn_->setStyleSheet(
      "QPushButton { background-color: #4a90d9; color: white; "
      "font-weight: bold; padding: 6px 16px; border-radius: 4px; } "
      "QPushButton:disabled { background-color: #c0c0c0; }");
  compareBtn_->setMinimumHeight(30);
  btnRow->addWidget(compareBtn_);
  btnRow->addStretch();
  mainLayout->addLayout(btnRow);

  // ===== 三档对比表 =====
  compareGroup_ = new QGroupBox("三档代理几何体对比", this);
  auto *grid = new QGridLayout(compareGroup_);
  grid->setHorizontalSpacing(10);
  grid->setVerticalSpacing(2);
  grid->addWidget(makeHeader("指标", compareGroup_,
                                Qt::AlignLeft | Qt::AlignVCenter), 0, 0);
  grid->addWidget(makeHeader("结构网格 (Mesh)", compareGroup_), 0, 1);
  grid->addWidget(makeHeader("凸包 (ConvexHull)", compareGroup_), 0, 2);
  grid->addWidget(makeHeader("无碰撞 (None)", compareGroup_),  0, 3);
  for (int i = 0; i < kRows; ++i)
  {
    auto *name = new QLabel(kRowNames[i], compareGroup_);
    name->setStyleSheet("padding: 2px 6px; color: #555555;");
    grid->addWidget(name, i + 1, 0);
    cellMesh_[i] = makeCell(compareGroup_);
    cellHull_[i] = makeCell(compareGroup_);
    cellNone_[i] = makeCell(compareGroup_);
    grid->addWidget(cellMesh_[i], i + 1, 1);
    grid->addWidget(cellHull_[i], i + 1, 2);
    grid->addWidget(cellNone_[i], i + 1, 3);
  }
  grid->setColumnStretch(0, 3);
  grid->setColumnStretch(1, 2);
  grid->setColumnStretch(2, 2);
  grid->setColumnStretch(3, 2);
  mainLayout->addWidget(compareGroup_);

  statusLabel_ = new QLabel("", this);
  statusLabel_->setWordWrap(true);
  statusLabel_->setStyleSheet("color: #666666; font-size: 11px;");
  statusLabel_->setTextFormat(Qt::PlainText);
  mainLayout->addWidget(statusLabel_);

  // ===== 3D 预览 =====
  auto *prevGroup = new QGroupBox("3D 预览", this);
  auto *prevLayout = new QVBoxLayout(prevGroup);
  auto *prevRow = new QHBoxLayout;
  prevRow->addWidget(new QLabel("预览档位:", prevGroup));
  previewModeCombo_ = new QComboBox(prevGroup);
  previewModeCombo_->addItem("结构网格 (Mesh)");
  previewModeCombo_->addItem("凸包 (ConvexHull)");
  previewModeCombo_->addItem("无碰撞 (None)");
  prevRow->addWidget(previewModeCombo_, 1);
  prevRow->addStretch();
  prevLayout->addLayout(prevRow);

  previewHint_ = new QLabel("", prevGroup);
  previewHint_->setStyleSheet("color: #666666; font-size: 11px;");
  prevLayout->addWidget(previewHint_);

  preview_ = new Buoyancy3DView(prevGroup);
  preview_->setMinimumHeight(220);
  preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  preview_->setShowCoG(false);
  prevLayout->addWidget(preview_, 1);
  mainLayout->addWidget(prevGroup, 1);

  // ===== 信号 =====
  connect(browseBtn_, &QPushButton::clicked,
          this, &ProxyGeometryDialog::onBrowseFile);
  connect(materialCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ProxyGeometryDialog::onDensityPreset);
  connect(compareBtn_, &QPushButton::clicked,
          this, &ProxyGeometryDialog::onCompare);
  connect(previewModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
          this, &ProxyGeometryDialog::onPreviewModeChanged);

  clearResult();
}

ProxyGeometryDialog::~ProxyGeometryDialog() = default;

//---------------------------------------------------------------------
std::string ProxyGeometryDialog::fmt(double _v, int _dec)
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(_dec) << _v;
  return oss.str();
}

//---------------------------------------------------------------------
void ProxyGeometryDialog::onBrowseFile()
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
void ProxyGeometryDialog::loadMeshFile(const std::string &_path)
{
  srcPath_ = _path;

  // 与 KSM 加载一致：先规范化，保证几何/预览与 KSM 同坐标系
  std::string normalized = keship::NormalizeMeshToSDFCache(_path);
  if (normalized.empty())
    normalized = _path;
  meshPath_ = normalized;
  fileEdit_->setText(QString::fromStdString(_path));

  loadedMesh_ = keship::LoadMeshFromFile(normalized);
  if (loadedMesh_.vertices.empty() || loadedMesh_.triangles.empty())
  {
    meshInfo_->setText("⚠ 加载失败或文件格式不支持");
    meshInfo_->setStyleSheet("color: #b00020; font-size: 11px;");
    compareBtn_->setEnabled(false);
    clearResult();
    return;
  }

  int nV = static_cast<int>(loadedMesh_.vertices.size());
  int nT = static_cast<int>(loadedMesh_.triangles.size());

  double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9, minZ = 1e9, maxZ = -1e9;
  for (const auto &v : loadedMesh_.vertices)
  {
    minX = std::min(minX, v.X()); maxX = std::max(maxX, v.X());
    minY = std::min(minY, v.Y()); maxY = std::max(maxY, v.Y());
    minZ = std::min(minZ, v.Z()); maxZ = std::max(maxZ, v.Z());
  }

  std::ostringstream oss;
  oss << "网格信息: 顶点 " << nV << "  |  三角面 " << nT
      << "  |  包围盒 " << fmt(maxX - minX, 3) << " × "
      << fmt(maxY - minY, 3) << " × " << fmt(maxZ - minZ, 3) << " m";
  meshInfo_->setText(QString::fromStdString(oss.str()));
  meshInfo_->setStyleSheet("color: #333333; font-size: 11px;");

  compareBtn_->setEnabled(true);
  clearResult();

  // 预览默认显示结构网格
  previewModeCombo_->setCurrentIndex(0);
  refreshPreview();
}

//---------------------------------------------------------------------
void ProxyGeometryDialog::onDensityPreset(int _idx)
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
void ProxyGeometryDialog::onPreviewModeChanged(int _idx)
{
  (void)_idx;
  refreshPreview();
}

//---------------------------------------------------------------------
void ProxyGeometryDialog::refreshPreview()
{
  if (!preview_)
    return;
  const int mode = previewModeCombo_ ? previewModeCombo_->currentIndex() : 0;

  Buoyancy3DModelInfo info;
  info.isBox = false;
  if (meshPath_.empty())
  {
    // 尚未加载模型 → 空预览（极小盒，避免误显示默认圆柱）
    info.isBox = true;
    info.boxDx = info.boxDy = info.boxDz = 0.001;
  }
  else if (mode == 1 && !hullPath_.empty())
  {
    info.meshPath = hullPath_;
  }
  else if (mode == 2)
  {
    // 无碰撞：不生成代理几何体 → 用极小盒表示"空"
    info.isBox = true;
    info.boxDx = info.boxDy = info.boxDz = 0.001;
  }
  else
  {
    info.meshPath = meshPath_;
  }
  preview_->setModel(info);
  preview_->setShowCoG(false);
  preview_->update();

  if (previewHint_)
  {
    const char *hint = (meshPath_.empty())
        ? "未加载模型：请先选择网格文件。"
        : (mode == 1 ? "预览：凸包 —— 简化后的代理几何体（三角面大幅减少）。"
        : (mode == 2 ? "预览：无碰撞档不生成代理几何体，故视图为空。"
                     : "预览：结构网格 —— 全细节原始网格。"));
    previewHint_->setText(hint);
  }
}

//---------------------------------------------------------------------
void ProxyGeometryDialog::clearResult()
{
  for (int i = 0; i < kRows; ++i)
  {
    for (QLabel *lb : {cellMesh_[i], cellHull_[i], cellNone_[i]})
      if (lb) { lb->setText("—"); lb->setStyleSheet("padding: 2px 6px; color: #333333;"); }
  }
  if (statusLabel_)
    statusLabel_->setText("加载模型后点击「计算三档对比」。");
}

//---------------------------------------------------------------------
void ProxyGeometryDialog::onCompare()
{
  if (loadedMesh_.triangles.empty())
  {
    QMessageBox::warning(this, "无数据", "请先加载模型文件");
    return;
  }

  const double rho  = densitySpin_->value();
  const double rhoW = waterDensitySpin_->value();
  lastDensity_  = rho;
  waterDensity_ = rhoW;

  // --- 两档可算的代理：结构网格(Direct) 与 凸包(ConvexHull) ---
  auto rMesh = keship::ComputeInertiaFromMesh(
      loadedMesh_, rho, keship::InertiaCalcMethod::Direct);
  auto rHull = keship::ComputeInertiaFromMesh(
      loadedMesh_, rho, keship::InertiaCalcMethod::ConvexHull);

  const bool okMesh = (rMesh.error == keship::InertiaCalcError::Ok);
  const bool okHull = (rHull.error == keship::InertiaCalcError::Ok);

  // --- 顶点 / 面数 ---
  const int nVmesh = static_cast<int>(loadedMesh_.vertices.size());
  const int nTmesh = static_cast<int>(loadedMesh_.triangles.size());
  const int nVhull = rHull.hullVertexCount;
  const int nThull = rHull.hullFaceCount;

  // --- 体积 ---
  const double vMesh = okMesh ? rMesh.volume : 0.0;
  const double vHull = okHull ? rHull.volume : 0.0;

  // --- 浮力（排水浮力 F_B = ρ_w·g·V，kN）---
  auto buoy = [&](double _v) { return rhoW * kGravity * _v / 1000.0; };

  // --- 填表 ---
  auto setCell = [](QLabel *_lb, const std::string &_text,
                    const char *_color = "#333333")
  {
    if (!_lb) return;
    _lb->setText(QString::fromStdString(_text));
    _lb->setStyleSheet(QString("padding: 2px 6px; color: %1;").arg(_color));
  };

  const std::string dash = "—";

  // 行 0/1：顶点数 / 三角面数
  setCell(cellMesh_[0], std::to_string(nVmesh));
  setCell(cellHull_[0], okHull ? std::to_string(nVhull) : dash);
  setCell(cellNone_[0], "0");
  setCell(cellMesh_[1], std::to_string(nTmesh));
  setCell(cellHull_[1], okHull ? std::to_string(nThull) : dash);
  setCell(cellNone_[1], "0");

  // 行 2：体积
  setCell(cellMesh_[2], okMesh ? fmt(vMesh, 4) : std::string("—（网格非水密）"),
          okMesh ? "#333333" : "#b00020");
  setCell(cellHull_[2], okHull ? fmt(vHull, 4) : dash);
  setCell(cellNone_[2], "0");

  // 行 3：质量
  setCell(cellMesh_[3], okMesh ? fmt(rMesh.params.mass, 2) : dash);
  setCell(cellHull_[3], okHull ? fmt(rHull.params.mass, 2) : dash);
  setCell(cellNone_[3], "0");

  // 行 4：排水浮力
  setCell(cellMesh_[4], okMesh ? fmt(buoy(vMesh), 3) : dash);
  setCell(cellHull_[4], okHull ? fmt(buoy(vHull), 3) : dash);
  setCell(cellNone_[4], "0");

  // 行 5-7：惯量
  auto inertiaCell = [&](QLabel *_lb, const keship::InertiaCalcResult &_r,
                         bool _ok, double _v)
  {
    setCell(_lb, _ok ? fmt(_v, 3) : dash);
  };
  inertiaCell(cellMesh_[5], rMesh, okMesh, rMesh.params.ixx);
  inertiaCell(cellHull_[5], rHull, okHull, rHull.params.ixx);
  setCell(cellNone_[5], "0");
  inertiaCell(cellMesh_[6], rMesh, okMesh, rMesh.params.iyy);
  inertiaCell(cellHull_[6], rHull, okHull, rHull.params.iyy);
  setCell(cellNone_[6], "0");
  inertiaCell(cellMesh_[7], rMesh, okMesh, rMesh.params.izz);
  inertiaCell(cellHull_[7], rHull, okHull, rHull.params.izz);
  setCell(cellNone_[7], "0");

  // 行 8：体积偏差 ΔV（相对全细节网格）
  std::string dVhull = dash, dVnone = dash;
  if (okMesh && vMesh > 1e-12)
  {
    if (okHull)
    {
      const double d = (vHull - vMesh) / vMesh * 100.0;
      dVhull = (d >= 0 ? "+" : "") + fmt(d, 2) + "%";
      setCell(cellHull_[8], dVhull, (std::fabs(d) > 1.0) ? "#b00020" : "#1b7f3b");
    }
    dVnone = "-100.00%";
    setCell(cellMesh_[8], "基准", "#555555");
    setCell(cellNone_[8], dVnone, "#b00020");
  }
  else
  {
    setCell(cellMesh_[8], dash);
    setCell(cellHull_[8], dash);
    setCell(cellNone_[8], dash);
  }

  // --- 生成凸包缓存（供预览）---
  hullPath_ = keship::GenerateConvexHullCache(srcPath_);
  refreshPreview();

  // --- 状态摘要 ---
  std::ostringstream st;
  if (okHull && okMesh && vMesh > 1e-12)
  {
    const double dV = (vHull - vMesh) / vMesh * 100.0;
    const double triCut = nTmesh > 0
        ? (1.0 - static_cast<double>(nThull) / nTmesh) * 100.0 : 0.0;
    st << "凸包把三角面从 " << nTmesh << " 降到 " << nThull
       << "（-" << fmt(triCut, 1) << "%）；排水浮力偏差 "
       << (dV >= 0 ? "+" : "") << fmt(dV, 2) << "%"
       << "（凸包对凹形船体通常高估排水体积）。";
  }
  else if (okHull)
  {
    st << "结构网格非水密，Direct 档无法算体积/惯量（已标红）；凸包档可用。";
  }
  else
  {
    st << "计算失败，请检查模型文件。";
  }
  statusLabel_->setText(QString::fromStdString(st.str()));

  if (!hullPath_.empty())
    previewModeCombo_->setToolTip("凸包缓存: " + QString::fromStdString(hullPath_));
}
