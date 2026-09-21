/*
 * Copyright (C) 2024–2026 KeShip Team
 */
#include "HydrodynamicsEditorDialog.hh"
#include "../shared/UiTheme.hh"
#include "../shared/UiTheme.hh"

#include "../shared/FlowLayout.hh"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QGroupBox>
#include <QLabel>
#include <QComboBox>
#include <QGridLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>
#include <QMessageBox>
#include <QScrollArea>
#include <iostream>
#include <cmath>

static const char *kDofNames[6]   = {"Surge","Sway","Heave","Roll","Pitch","Yaw"};
static const char *kDofLetter[6]  = {"X","Y","Z","K","M","N"};
static const char *kDofLetter2[6] = {"u","v","w","p","q","r"};

static const double kHue[6]   = {0.0, 210.0, 120.0, 30.0, 280.0, 180.0};
static const int kSatCol[6]    = {255, 210, 170, 130, 90, 50};

static QColor cellColor(int _r, int _c)
{
  return QColor::fromHsvF(kHue[_r] / 360.0,
      kSatCol[_c] / 255.0, 0.65);
}
static QColor cellColorBg(int _r, int _c)
{
  return QColor::fromHsvF(kHue[_r] / 360.0,
      kSatCol[_c] / 510.0, 0.95);
}

static const char *amLabel(int _i, int _j)
{
  static char buf[16];
  snprintf(buf, sizeof(buf), "%s_%sdot", kDofLetter[_i], kDofLetter2[_j]);
  return buf;
}

// ─── D 矩阵预览组件 ─────────────────────────────────────

class DMatrixPreview : public QWidget
{
public:
  DMatrixPreview(QWidget *_p = nullptr) : QWidget(_p) {
    setMinimumSize(240, 180);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }

  void setTerms(const std::vector<keship::DampingTerm> *_t) { terms_ = _t; update(); }
  void setClickCallback(std::function<void(int,int)> _cb) { clickCb_ = _cb; }

protected:
  void paintEvent(QPaintEvent *) override
  {
    int w = width(), h = height();
    int cellW = (w - mL) / 6, cellH = (h - mT) / 6;
    if (cellW < 20 || cellH < 16) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(Qt::black);
    p.setFont(QFont("sans-serif", 7));

    for (int j = 0; j < 6; ++j)
      p.drawText(QRect(mL + j * cellW, 0, cellW, mT), Qt::AlignCenter, kDofNames[j]);
    for (int i = 0; i < 6; ++i)
      p.drawText(QRect(0, mT + i * cellH, mL, cellH), Qt::AlignCenter, kDofLetter[i]);

    for (int i = 0; i < 6; ++i)
      for (int j = 0; j < 6; ++j)
        cells_[i][j].clear();

    if (terms_)
      for (size_t ti = 0; ti < terms_->size(); ++ti) {
        const auto &t = (*terms_)[ti];
        int r = t.target;
        int c = (t.factors.empty() ? r : t.factors[0].dof);
        if (r >= 0 && r < 6 && c >= 0 && c < 6)
          cells_[r][c].push_back((int)ti);
      }

    for (int i = 0; i < 6; ++i)
    {
      for (int j = 0; j < 6; ++j)
      {
        int rx = mL + j * cellW + 1, ry = mT + i * cellH + 1;
        int cw = cellW - 2, ch = cellH - 2;

        if (cells_[i][j].empty())
          p.fillRect(rx, ry, cw, ch, cellColorBg(i, j));
        else
          p.fillRect(rx, ry, cw, ch, cellColor(i, j));

        p.setPen(QPen(QColor(220, 220, 220), 1));
        p.drawRect(rx, ry, cw, ch);
      }
    }
  }

  void mouseMoveEvent(QMouseEvent *_e) override
  {
    int cellW = (width() - mL) / 6, cellH = (height() - mT) / 6;
    int c = (_e->pos().x() - mL) / cellW;
    int r = (_e->pos().y() - mT) / cellH;
    if (r < 0 || r > 5 || c < 0 || c > 5 || cells_[r][c].empty()) return;

    QString tip;
    for (int id : cells_[r][c]) {
      if (!tip.isEmpty()) tip += "\n";
      tip += QString("■ ") + formatTermName(id);
    }
    QToolTip::showText(_e->globalPos(), tip, this);
  }

  void mouseReleaseEvent(QMouseEvent *_e) override
  {
    int cellW = (width() - mL) / 6, cellH = (height() - mT) / 6;
    int c = (_e->pos().x() - mL) / cellW;
    int r = (_e->pos().y() - mT) / cellH;
    if (r >= 0 && r < 6 && c >= 0 && c < 6 && !cells_[r][c].empty() && clickCb_)
      clickCb_(r, c);
  }

private:
  const std::vector<keship::DampingTerm> *terms_ = nullptr;
  std::function<void(int,int)> clickCb_;
  std::vector<int> cells_[6][6];
  int mL = 50, mT = 20;

  QString formatTermName(int _idx) const
  {
    if (!terms_ || _idx < 0 || _idx >= (int)terms_->size()) return "";
    const auto &t = (*terms_)[_idx];
    if (t.factors.empty()) return "?";
    QString s = QString("%1").arg(kDofLetter[t.target]) + "_";
    for (size_t fi = 0; fi < t.factors.size(); ++fi) {
      if (fi > 0) s += "";
      auto &f = t.factors[fi];
      if (t.factors.size() == 1 && f.order == 1)
        s += kDofLetter2[f.dof];
      else if (f.order == 0)
        s += QString("|%1|").arg(kDofLetter2[f.dof]);
      else
        s += QString("|%1|%1").arg(kDofLetter2[f.dof]);
    }
    return s;
  }
};

// ─── HydrodynamicsEditorDialog ──────────────────────────

HydrodynamicsEditorDialog::HydrodynamicsEditorDialog(
    const std::string &_modelName,
    const std::string &_linkName,
    QWidget *parent)
    : QDialog(parent), modelName_(_modelName), linkName_(_linkName)
{
  setWindowTitle(QString("流体动力编辑器 — %1").arg(QString::fromStdString(modelName_)));
  setModal(false);
  resize(680, 720);

  auto *mainLayout = new QVBoxLayout(this);
  const double maxVal = 9e15;
  const int dec = 4;

  auto *linkLayout = new QHBoxLayout();
  linkLayout->addWidget(new QLabel("Link:", this));
  auto *linkCombo = new QComboBox(this);
  linkCombo->addItem(QString::fromStdString(linkName_));
  linkCombo->setEnabled(false);
  linkLayout->addWidget(linkCombo);
  linkLayout->addStretch();
  mainLayout->addLayout(linkLayout);

  auto *amGroup = new QGroupBox("附加质量 M_A (无量纲系数 × 质量)", this);
  auto *amOuter = new QVBoxLayout(amGroup);
  auto *gridWidget = new QWidget(this);
  auto *grid = new QGridLayout(gridWidget);
  grid->setSpacing(3);

  grid->addWidget(new QLabel(""), 0, 0);
  for (int j = 0; j < 6; ++j)
    grid->addWidget(new QLabel(kDofNames[j]), 0, j + 1);

  for (int i = 0; i < 6; ++i)
  {
    grid->addWidget(new QLabel(QString("%1(%2)").arg(kDofNames[i]).arg(kDofLetter[i])), i + 1, 0);
    for (int j = 0; j < 6; ++j)
    {
      int idx = i * 6 + j;
      amSpins_[idx] = new QDoubleSpinBox(this);
      amSpins_[idx]->setRange(-maxVal, maxVal);
      amSpins_[idx]->setDecimals(dec);
      amSpins_[idx]->setValue(0.0);
      amSpins_[idx]->setPrefix(QString(amLabel(i, j)) + ": ");
      amSpins_[idx]->setSingleStep(0.05);

      if (i <= j)
        connect(amSpins_[idx], QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &HydrodynamicsEditorDialog::updateSymmetric);
      else
      {
        amSpins_[idx]->setEnabled(false);
        amSpins_[idx]->setStyleSheet(QString("color: %1; background-color: #f0f0f0;").arg(UiTheme::statusColor(UiTheme::Status::kDisabled)));
      }
      grid->addWidget(amSpins_[idx], i + 1, j + 1);
    }
  }
  amOuter->addWidget(gridWidget);
  mainLayout->addWidget(amGroup);

  /// ── D 阻尼项 ──
  auto *dampGroup = new QGroupBox("阻尼项 D", this);
  auto *dampLayout = new QVBoxLayout(dampGroup);

  termListWidget_ = new QWidget(this);
  auto *scrollArea = new QScrollArea(this);
  scrollArea->setWidget(termListWidget_);
  scrollArea->setWidgetResizable(true);
  scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  scrollArea->setFrameShape(QFrame::NoFrame);
  dampLayout->addWidget(scrollArea);

  auto *addBtn = new QPushButton("+ 添加阻尼项", this);
  UiTheme::setButtonIcon(addBtn, UiTheme::Icon::kAdd);
  connect(addBtn, &QPushButton::clicked, this, &HydrodynamicsEditorDialog::addDampingTerm);
  dampLayout->addWidget(addBtn);

  dMatrix_ = new DMatrixPreview(this);
  dMatrix_->setClickCallback([this](int r, int c) { onMatrixCellClicked(r, c); });
  dampLayout->addWidget(dMatrix_);

  mainLayout->addWidget(dampGroup);

  auto *bottomLayout = new QHBoxLayout();
  auto *applyBtn = new QPushButton("应用并重建模型", this);
  UiTheme::setButtonIcon(applyBtn, UiTheme::Icon::kOk);
  auto *cancelBtn = new QPushButton("取消", this);
  UiTheme::setButtonIcon(cancelBtn, UiTheme::Icon::kCancel);
  bottomLayout->addStretch();
  bottomLayout->addWidget(applyBtn);
  bottomLayout->addWidget(cancelBtn);
  mainLayout->addLayout(bottomLayout);

  connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
  connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

HydrodynamicsEditorDialog::~HydrodynamicsEditorDialog() = default;

void HydrodynamicsEditorDialog::updateSymmetric()
{
  for (int i = 0; i < 6; ++i)
    for (int j = 0; j < i; ++j)
      amSpins_[i * 6 + j]->setValue(amSpins_[j * 6 + i]->value());
}

keship::HydroConfig HydrodynamicsEditorDialog::getConfig() const
{
  keship::HydroConfig cfg;
  cfg.linkName = linkName_;
  for (int i = 0; i < 36; i++) cfg.addedMass[i] = amSpins_[i]->value();
  cfg.dampingTerms = terms_;
  for (int i = 0; i < 8; i++) cfg.damping[i] = 0.0;
  return cfg;
}

void HydrodynamicsEditorDialog::setConfig(const keship::HydroConfig &_config)
{
  linkName_ = _config.linkName;
  for (int i = 0; i < 36; i++) amSpins_[i]->setValue(_config.addedMass[i]);
  setupTermsFromConfig(_config);
}

void HydrodynamicsEditorDialog::setupTermsFromConfig(const keship::HydroConfig &_cfg)
{
  terms_.clear();

  if (!_cfg.dampingTerms.empty())
  {
    terms_ = _cfg.dampingTerms;
  }
  else
  {
    const char *dNames[] = {"Xu","Yv","Zw","Kp","Mq","Nr","Yr","Nv"};
    int oldToNew[8][2] = {{0,0},{1,1},{2,2},{3,3},{4,4},{5,5},{1,5},{5,1}};
    for (int i = 0; i < 8; i++)
      terms_.push_back({oldToNew[i][0], _cfg.damping[i], {{oldToNew[i][1], 1}}});
  }

  rebuildTermList();
}

void HydrodynamicsEditorDialog::rebuildTermList()
{
  for (auto &tw : termWidgets_) {
    if (tw.row) tw.row->deleteLater();
  }
  termWidgets_.clear();

  delete termListWidget_->layout();
  auto *listLayout = new FlowLayout(termListWidget_, 4, 6, 4);
  listLayout->setContentsMargins(0, 0, 0, 0);

  for (size_t i = 0; i < terms_.size(); ++i)
  {
    auto *row = new QWidget(termListWidget_);
    auto *hl = new QHBoxLayout(row);
    hl->setContentsMargins(2, 2, 2, 2);

    auto *formula = new QLabel(QString::fromStdString(formatTermFormula(i)), row);
    formula->setTextFormat(Qt::RichText);
    hl->addWidget(formula);

    auto *coeff = new QDoubleSpinBox(row);
    coeff->setRange(-9e15, 9e15);
    coeff->setDecimals(4);
    coeff->setValue(terms_[i].coefficient);
    coeff->setSingleStep(0.05);
    hl->addWidget(coeff);

    auto *colorLabel = new QLabel(row);
    colorLabel->setFixedSize(16, 16);
    {
      int cr = terms_[i].target;
      int cc = terms_[i].factors.empty() ? cr : terms_[i].factors[0].dof;
      colorLabel->setStyleSheet(QString("background-color: %1; border: 1px solid #888;")
          .arg(cellColor(cr, cc).name()));
    }
    hl->addWidget(colorLabel);

    auto *delBtn = new QPushButton("×", row);
    delBtn->setFixedWidth(28);
    int idx = (int)i;
    connect(delBtn, &QPushButton::clicked, this, [this, idx]() { deleteDampingTerm(idx); });
    hl->addWidget(delBtn);

    hl->addStretch();
    listLayout->addWidget(row);

    TermWidget tw = {row, formula, coeff, colorLabel, nullptr};
    termWidgets_.push_back(tw);

    connect(coeff, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this, idx](double _v) {
      if (idx < (int)terms_.size()) terms_[idx].coefficient = _v;
      updateDMatrix();
    });
  }

  updateDMatrix();

  termListWidget_->updateGeometry();
}

std::string HydrodynamicsEditorDialog::formatTermFormula(int _idx) const
{
  if (_idx < 0 || _idx >= (int)terms_.size()) return "?";
  const auto &t = terms_[_idx];
  if (t.factors.empty()) return "?";

  std::string s = std::string("<i>") + kDofLetter[t.target] + "</i><sub>";
  for (size_t fi = 0; fi < t.factors.size(); ++fi)
  {
    if (fi > 0) s += "";
    const auto &f = t.factors[fi];
    if (t.factors.size() == 1 && f.order == 1)
      s += kDofLetter2[f.dof];
    else if (f.order == 0)
      s += std::string("|") + kDofLetter2[f.dof] + "|";
    else
      s += std::string("|") + kDofLetter2[f.dof] + "|" + kDofLetter2[f.dof];
  }
  s += "</sub>";
  return s;
}

void HydrodynamicsEditorDialog::updateDMatrix()
{
  dMatrix_->setTerms(&terms_);
}

void HydrodynamicsEditorDialog::addDampingTerm()
{
  QDialog dlg(this);
  dlg.setWindowTitle("新增阻尼项");
  auto *dl = new QFormLayout(&dlg);

  auto *tCombo = new QComboBox(&dlg);
  for (int i = 0; i < 6; ++i)
    tCombo->addItem(QString("%1(%2)").arg(kDofNames[i]).arg(kDofLetter[i]));
  dl->addRow("目标 DOF:", tCombo);

  auto *sCombo = new QComboBox(&dlg);
  for (int i = 0; i < 6; ++i)
    sCombo->addItem(QString("%1(%2)").arg(kDofNames[i]).arg(kDofLetter2[i]));
  dl->addRow("源 DOF:", sCombo);

  auto *oCombo = new QComboBox(&dlg);
  oCombo->addItem("0: |ν| (绝对值)", 0);
  oCombo->addItem("1: ν (线性)", 1);
  oCombo->addItem("2: |ν|ν (二次)", 2);
  oCombo->addItem("3: |ν|²ν (三阶)", 3);
  oCombo->setCurrentIndex(1);
  dl->addRow("阶次:", oCombo);

  auto *cSpin = new QDoubleSpinBox(&dlg);
  cSpin->setRange(-9e15, 9e15);
  cSpin->setDecimals(4);
  cSpin->setValue(0.0);
  dl->addRow("系数:", cSpin);

  auto *btnLayout = new QHBoxLayout();
  auto *okBtn = new QPushButton("确定", &dlg);
  UiTheme::setButtonIcon(okBtn, UiTheme::Icon::kOk);
  auto *cancelBtn = new QPushButton("取消", &dlg);
  UiTheme::setButtonIcon(cancelBtn, UiTheme::Icon::kCancel);
  btnLayout->addStretch();
  btnLayout->addWidget(okBtn);
  btnLayout->addWidget(cancelBtn);
  dl->addRow(btnLayout);

  connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
  connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);

  if (dlg.exec() != QDialog::Accepted) return;

  int target = tCombo->currentIndex();
  int dof = sCombo->currentIndex();
  int order = oCombo->currentData().toInt();
  double coeff = cSpin->value();

  for (const auto &t : terms_)
    if (t.target == target && t.factors.size() == 1
        && t.factors[0].dof == dof && t.factors[0].order == order)
    {
      QMessageBox::warning(this, "重复添加",
          QString("阻尼项 %1_%2 (order=%3) 已存在")
          .arg(kDofLetter[target]).arg(kDofLetter2[dof]).arg(order));
      return;
    }

  terms_.push_back({target, coeff, {{dof, order}}});
  rebuildTermList();
}

void HydrodynamicsEditorDialog::deleteDampingTerm(int _idx)
{
  if (_idx < 0 || _idx >= (int)terms_.size()) return;
  terms_.erase(terms_.begin() + _idx);
  rebuildTermList();
}

void HydrodynamicsEditorDialog::onTermChanged()
{
  updateDMatrix();
}

void HydrodynamicsEditorDialog::onMatrixCellClicked(int _row, int _col)
{
  for (size_t i = 0; i < terms_.size(); ++i)
  {
    const auto &t = terms_[i];
    int r = t.target;
    int c = t.factors.empty() ? r : t.factors[0].dof;
    if (r == _row && c == _col)
    {
      if (i < termWidgets_.size() && termWidgets_[i].coeff)
        termWidgets_[i].coeff->setFocus();
      return;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
std::array<double, 36> HydrodynamicsEditorDialog::cylinderAddedMass(
    double _mass, double _radius, double _length, double _rho)
{
  /// M_A 由排开水的质量 ρV 决定，与物体质量 _mass 无关；
  /// 保留 _mass 参数仅为与 cylinderDefaults 签名保持一致。
  (void)_mass;

  const double kPi = 3.14159265358979323846;
  double volume = kPi * _radius * _radius * _length;
  double fluidMass = _rho * volume;

  std::array<double, 36> am{};
  auto set = [&](int i, int j, double v) { am[i * 6 + j] = v; };

  /// 平移（单位 kg）：
  ///   径向（surge/sway）：1.0·ρV —— 无限长圆柱二维势流 C_a = 1.0（精确值）
  ///   轴向（heave）：0.02·ρV —— 细长体轴向附加质量小（有限长修正量级）
  set(0,0, 1.0 * fluidMass);    // Surge
  set(1,1, 1.0 * fluidMass);    // Sway
  set(2,2, 0.02 * fluidMass);   // Heave

  /// 转动（单位 kg·m²，与 DART fluid_added_mass / 惯性矩阵同单位）：
  ///   绕横轴（roll/pitch）：0.25·ρV·L²/12 —— 细长圆柱绕中点横轴
  ///     附加质量矩（Newman 细长体理论，L/r≫1 时系数 ≈ 0.2~0.3）
  ///   绕轴自转（yaw）：0.02·ρV·r² —— 旋转体绕对称轴无位移流场，
  ///     附加惯量矩很小，取小值
  set(3,3, 0.25 * fluidMass * _length * _length / 12.0);  // Roll
  set(4,4, 0.25 * fluidMass * _length * _length / 12.0);  // Pitch
  set(5,5, 0.02 * fluidMass * _radius * _radius);         // Yaw

  return am;
}

///////////////////////////////////////////////////////////////////////////////
keship::HydroConfig HydrodynamicsEditorDialog::cylinderDefaults(
    double _mass, double _radius, double _length)
{
  const double kPi = 3.14159265358979323846;
  const double rho = 1000.0;   // 流体密度（与插件 fluid_density 一致）
  const double g = 9.81;
  double volume = kPi * _radius * _radius * _length;
  double fluidMass = rho * volume;

  /// 轴向投影面积 = π*r²（直立水线面积，垂荡刚度用）
  double axialArea = kPi * _radius * _radius;

  /// 平衡浸没长度：浮力平衡 → 排水质量 = 物体质量
  double subLen = _mass / (rho * axialArea);

  /// ── 线性阻尼（物理参数，见 notes/cylinder_hydro_physics.md）──
  /// 垂荡：ζ=0.2（浮体垂荡经验区间 0.1~0.3），水线面积取直立 πr²
  ///   B_heave = 2·ζ·√(ρ·g·A_wp·m)
  double Zw_lin = 2.0 * 0.2 * std::sqrt(rho * g * axialArea * _mass);
  /// 水平漂移（surge/sway）：无静水恢复力，线性摩擦阻尼小
  ///   （时间常数 τ = m/B ≈ 20s），压差阻力由二次项主导
  double Xu_lin = 0.05 * _mass;
  /// 横摇/纵摇：ζ≈0.18（船舶/浮体横摇经验区间 0.03~0.2）
  double linRollPitch = 0.15 * _mass * _length * _length / 12.0;
  /// 绕轴自转（yaw）：对称旋转体表面切向流无压差阻力，真实粘性衰减
  ///   极慢；保留小值（τ≈2s）保证仿真收敛
  double linYaw = 0.05 * _mass * _length * _length / 12.0;

  /// Morison 二次阻力系数（高雷诺数压差阻力主导）
  const double cdNormal = 1.1;    // 圆柱法向绕流阻力系数
  const double cdAxial  = 0.82;   // 平头端面阻力系数
  double qSurgeSway = 0.5 * rho * cdNormal * (2.0 * _radius * subLen);
  double qHeave     = 0.5 * rho * cdAxial * axialArea;
  double r4 = _radius * _radius * _radius * _radius;
  double qRollPitch = 0.5 * rho * cdNormal * kPi * r4 * subLen;
  double qYaw = 0.5 * rho * cdNormal
                * (2.0 * kPi * _radius * subLen)
                * (_radius * _radius);

  keship::HydroConfig cfg;
  cfg.addedMass = cylinderAddedMass(_mass, _radius, _length, rho);

  /// 线性阻尼数组（绝对系数，与 dampingTerms 数值一致；KSM 兼容字段）
  cfg.damping = {
      Xu_lin, Xu_lin, Zw_lin,
      linRollPitch, linRollPitch, linYaw,
      0.0, 0.0
  };
  cfg.dampingTerms = {
      /// 线性项（order=1）
      {0, Xu_lin, {{0,1}}},
      {1, Xu_lin, {{1,1}}},
      {2, Zw_lin, {{2,1}}},
      {3, linRollPitch, {{3,1}}},
      {4, linRollPitch, {{4,1}}},
      {5, linYaw, {{5,1}}},
      /// 二次阻力项（order=2，Morison，主导耗散）
      {0, qSurgeSway, {{0,2}}},
      {1, qSurgeSway, {{1,2}}},
      {2, qHeave, {{2,2}}},
      {3, qRollPitch, {{3,2}}},
      {4, qRollPitch, {{4,2}}},
      {5, qYaw, {{5,2}}},
  };
  return cfg;
}
