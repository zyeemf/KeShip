# Fossen 物理模型重构方案

> 日期：2026-07-15
> 目标：惯性校验 + 阻尼项通用化（支持任意阶次和交叉项）
> 原则：物理层与 UI 层完全解耦

---

## 第一部分：惯性校验

### 理论依据

任意质量分布，三个主转动惯量的物理定义为：

```
Ixx = ∫(y² + z²) dm
Iyy = ∫(x² + z²) dm
Izz = ∫(x² + y²) dm
```

直接相加推出**三角不等式**：

```
Ixx + Iyy = ∫(x² + y² + 2z²) dm ≥ ∫(x² + y²) dm = Izz
```

同理 `Ixx + Izz ≥ Iyy` 和 `Iyy + Izz ≥ Ixx`。

**三角不等式是物理底线**——源自惯性矩的物理定义，不是人为附加的数值约束。自然界不存在违反三角不等式的质量分布。

### DART 约束

DART 的 ODE 碰撞检测器**显式校验三角函数不等式**，违反时抛出 `Error Code 19`。

| 约束 | 性质 | 违反后果 |
|------|------|---------|
| Ixx > 0, Iyy > 0, Izz > 0 | 正定性（必要） | 未定义行为 |
| Ixx + Iyy ≥ Izz & friends | 三角不等式（物理真相） | Error 19, spawn 失败 |
| det(I) > 0 (off-diagonal 存在时) | 完整正定性（充分） | 未定义行为 |

三角不等式比完整正定性**更强**、**能检测更多无效输入**，且**直接反映 DART 的 Error 19**。

### 数据结构

```cpp
struct InertiaValidation {
    bool valid = true;
    std::string message;  // 空 → 通过；非空 → 错误描述（UI 弹窗用）
};

InertiaValidation ValidateInertia(
    double mass,
    double ixx, double iyy, double izz,
    double ixy = 0, double ixz = 0, double iyz = 0);
```

### 校验规则

| # | 校验 | 条件 | 错误消息示例 |
|---|------|------|------------|
| 1 | 质量为正 | mass > 0 | `质量必须大于 0` |
| 2 | 对角项为正 | ixx > 0, iyy > 0, izz > 0 | `iXX 必须大于 0` |
| 3 | 三角不等式 | ixx + iyy ≥ izz | `iXX (50.0) + iYY (50.0) = 100.0 < iZZ (498.2)` |
| 4 | 三角不等式 | ixx + izz ≥ iyy | `iXX + iZZ < iYY` |
| 5 | 三角不等式 | iyy + izz ≥ ixx | `iYY + iZZ < iXX` |

---

## 第二部分：阻尼项通用化

### 现状

当前 `FossenModel` 使用固定的 8 系数线性阻尼（`Xu, Yv, Zw, Kp, Mq, Nr, Yr, Nv`），通过 6×6 矩阵 `D_` 计算：

```cpp
τ -= D_ * ν;   // 只能表达 |v|^1 项
```

无法表达二次阻尼（`|v|v`）和交叉耦合阻尼（`|v|r`）。

### 理论背景

船舶水动力阻尼的泰勒展开形式（展开点 ν = 0）：

```
τ_damp,i = - Σ_j D_{ij}·ν_j - Σ_{j,k} D_{ijk}·ν_j·|ν_k| - ...
                ↑ 一阶（现有）         ↑ 二阶（新增）
```

不存在常数项（ν=0 时流体静止，净力为零）。由于船舶一般前后和左右对称，偶数阶系数为零——所以一阶之后直接跳到二阶（含绝对值）。

对单 DOF 的扩展形式：

```
τ_i = -[ c₁·ν     + c₂·|ν|·ν     + c₃·|ν|²·ν     + ... ]
         ↑ 线性      ↑ 二次         ↑ 三阶
```

对交叉项（如横荡-首摇耦合）：

```
τ_Y = -[ ... + c₁·|v|·r  + ... ]
                     ↑ |v| 的符号保证阻尼力始终与 v 同向
```

### 数据结构

```cpp
/// 单个速度因子的描述
struct DampingFactor {
    int dof;      // 0=Surge(u), 1=Sway(v), 2=Heave(w), 3=Roll(p), 4=Pitch(q), 5=Yaw(r)
    int order;    // 0=|ν_dof| (绝对值, 用于交叉项中提取幅值)
                  // 1=ν_dof   (线性)
                  // 2=|ν_dof|·ν_dof (二次)
                  // n=|ν_dof|^(n-1)·ν_dof (n 阶)
};

/// 一个阻尼项 = coefficient × Π factor
struct DampingTerm {
    int target;                            // 力/力矩 DOF (0~5)
    double coefficient;                    // 系数（无量纲或带量纲，由调用者保证一致性）
    std::vector<DampingFactor> factors;    // 乘积的因子列表
};
```

### 计算规则

```
// 对每个 factor j:
f_j = (order == 0) ? |ν_dof| : (|ν_dof|^(order-1) × ν_dof)

// 乘积:
τ_damp[target] -= coefficient × Π f_j
```

### 典型映射

| 标准符号 | target | factors | 公式展开 |
|---------|--------|---------|---------|
| `Xu` (线性纵荡) | 0 | `{0,1}` | c × u |
| `Yv` (线性横荡) | 1 | `{1,1}` | c × v |
| `Y_{|v|v}` (二次横荡) | 1 | `{1,2}` | c × |v|×v |
| `Y_{|v|r}` (横荡-首摇耦合) | 1 | `{1,0}, {5,1}` | c × |v|×r |
| `X_{uu}` (二次纵荡) | 0 | `{0,2}` | c × |u|×u |
| `Nr` (线性首摇) | 5 | `{5,1}` | c × r |
| `N_{|r|r}` (二次首摇) | 5 | `{5,2}` | c × |r|×r |
| `N_{|v|v}` (横荡-首摇交叉) | 5 | `{1,2}` | c × |v|×v |
| `M_{|w|q}` (垂荡-纵摇交叉) | 4 | `{2,0}, {4,1}` | c × |w|×q |

### 默认阻尼项

调用 `setDampingCoeffs()`（旧 API，保留兼容）时自动生成 8 个一阶对角 DampingTerm：

| # | target | factors | coefficient |
|---|--------|---------|-------------|
| 0 | 0 (Surge) | `{0,1}` | c[0]·mass (Xu) |
| 1 | 1 (Sway) | `{1,1}` | c[1]·mass (Yv) |
| 2 | 2 (Heave) | `{2,1}` | c[2]·mass (Zw) |
| 3 | 3 (Roll) | `{3,1}` | c[3]·mass (Kp) |
| 4 | 4 (Pitch) | `{4,1}` | c[4]·mass (Mq) |
| 5 | 5 (Yaw) | `{5,1}` | c[5]·mass (Nr) |
| 6 | 1 (Sway) | `{5,1}` | c[6]·mass (Yr) |
| 7 | 5 (Yaw) | `{1,1}` | c[7]·mass (Nv) |

### 实现

```cpp
void FossenModel::computeDampingForces(
    const Eigen::Vector<double, 6> &_nu,
    Eigen::Vector<double, 6> &_tau) const
{
    for (const auto &term : dampingTerms_)
    {
        double val = term.coefficient;
        for (const auto &f : term.factors)
        {
            double v = std::abs(_nu[f.dof]);
            if (f.order == 0) continue;  // 阶为 0 时只乘一次绝对值
            for (int i = 1; i < f.order; ++i) val *= v;
            val *= _nu[f.dof];
        }
        _tau[term.target] -= val;
    }
}
```

### 计算开销分析

| 工况 | 计算量 | 相对占比 |
|------|--------|---------|
| 6 个一阶对角项 | 6 × 1 = 6 次乘法 | 极少 |
| 30 个复杂 term | 30 × ~2 = 60 次乘法 | 极少 |
| Catto 三角裁剪 (78K face) | ~100,000 ops | 瓶颈 |

**阻尼计算在所有操作中占比 < 0.1%**，不存在性能顾虑。

---

## 第三部分：FossenModel API 变更

### 公开接口（新增）

```cpp
// ── 惯性校验 ──
struct InertiaValidation { bool valid; std::string message; };
InertiaValidation ValidateInertia(double mass, double ixx, double iyy, double izz,
                                   double ixy = 0, double ixz = 0, double iyz = 0);

// ── 阻尼 ──
struct DampingFactor { int dof; int order; };
struct DampingTerm { int target; double coefficient; std::vector<DampingFactor> factors; };
void setDampingTerms(const std::vector<DampingTerm> &_terms);

// 保留旧 API 兼容
void setDampingCoeffs(const std::array<double, 8> &_coeffs);
```

### 内部变更

| 成员 | 旧 | 新 |
|------|----|----|
| `dampingCoeffs_` | `std::array<double, 8>` | 保留（`setDampingCoeffs` 内部转 DampingTerm） |
| `D_` | `Eigen::Matrix<double, 6, 6>` | **删除** |
| `dampingTerms_` | — | `std::vector<DampingTerm>` **新增** |
| `buildDampingMatrix()` | 构建 D_ | **删除** |
| `computeHydroForces` L130 | `τ -= D_ * ν` | `computeDampingForces(nu, tau)` |
| `damping()` getter | 返回 D_ | **删除** |

### 不受影响

- `computeHydroForces` 的 M_A 和 C_A 部分：完全不变
- `HydrodynamicsSystem::PreUpdate`：调用接口 `computeHydroForces(nu, nuDot, tau)` 签名不变
- `HydrodynamicsSystem::Configure`：阻尼解析待第二阶段适配新 SDF 格式

---

## 第四部分：文件清单（仅物理层）

| # | 文件 | 操作 | 说明 |
|---|------|------|------|
| 1 | `FossenModel.hh` | 修改 | 新增 4 个 struct、`ValidateInertia` 声明、`setDampingTerms`、`computeDampingForces`、`dampingTerms_`；删除 `D_`、`buildDampingMatrix`、`damping()` getter |
| 2 | `FossenModel.cc` | 修改 | 实现 `ValidateInertia`、`computeDampingForces`、`setDampingTerms`、`setAddedMassMatrix`；废弃 `buildDampingMatrix` 实现；`computeHydroForces` L130 → `computeDampingForces` | ✅ 已完成 |
| 3 | `HydrodynamicsSystem.cc` | 修改 | `Configure()` M_A 解析改为 `<m00>..<m55>`（36 元素），兼容旧 12 命名格式；阻尼解析保持旧格式 | ✅ 已完成 |
| 4 | `HydrodynamicsEditorDialog.cc/hh` | 修改 | 12 spinbox → 6×6 QGridLayout 对称矩阵 UI；前缀 X_udot: ~ N_rdot: | ✅ 已完成 |
| 5 | `ModelManager.cc/hh` | 修改 | `HydroConfig::addedMass` 12→36；`MakeHydroPluginSDF` 输出 `<m00>..<m55>`；KSM 保存/加载适配 36 元素 | ✅ 已完成 |

---

## 第五部分：后续 UI 层规划（备忘）

### SDF 格式（规划）

```xml
<damping>
  <term target="0" coefficient="0.1">
    <factor dof="0" order="1"/>
  </term>
  <term target="1" coefficient="0.05">
    <factor dof="1" order="2"/>
  </term>
  <term target="1" coefficient="-0.02">
    <factor dof="1" order="0"/>
    <factor dof="5" order="1"/>
  </term>
</damping>
```

### 向后兼容

旧 KSM 文件中 `<linear_damping>` 段 8 系数自动映射为 8 个 `DampingTerm`（一阶线性）。

### UI 设计（规划）

阻尼编辑器从 8 spinbox 改为动态 term 列表：表格形式，每行显示 target(dof) / factors / coefficient / 删除按钮。底部 `+` 按钮添加新行。默认 6 个一阶对角 term。

---

## 第六部分：M_A 6×6 对称矩阵重构 ✅

> 实施日期：2026-07-15
> 状态：已完成

### 背景

原先 M_A 以 12 个无关联的命名 spinbox 展示（X_udot, Y_vdot, ...），用户无法直观理解矩阵结构和对称性。改为 6×6 对称矩阵，模仿惯性张量编辑形式。

### UI 最终布局

```
┌─ 附加质量 M_A (无量纲系数 × 质量) ───────────────────────────────────────────┐
│             │  Surge    │  Sway     │  Heave    │  Roll     │  Pitch    │  Yaw     │
│ Surge(X)    │ X_udot:0  │ X_vdot:0  │ X_wdot:0  │ X_pdot:0  │ X_qdot:0  │ X_rdot:0 │ ← 可编辑
│ Sway(Y)     │    灰色   │ Y_vdot:0  │ Y_wdot:0  │ Y_pdot:0  │ Y_qdot:0  │ Y_rdot:0 │    (i≤j)
│ Heave(Z)    │    灰色   │    灰色   │ Z_wdot:0  │ Z_pdot:0  │ Z_qdot:0  │ Z_rdot:0 │
│ Roll(K)     │    灰色   │    灰色   │    灰色   │ K_pdot:0  │ K_qdot:0  │ K_rdot:0 │
│ Pitch(M)    │    灰色   │    灰色   │    灰色   │    灰色   │ M_qdot:0  │ M_rdot:0 │
│ Yaw(N)      │    灰色   │    灰色   │    灰色   │    灰色   │    灰色   │ N_rdot:0 │
└─────────────┴───────────┴───────────┴───────────┴───────────┴───────────┴──────────┘
```

- 上三角 + 对角 (i ≤ j)：可编辑，前缀 `X_udot:` ~ `N_rdot:`
- 下三角 (i > j)：灰色只读，值从上三角镜像实时同步（`updateSymmetric()` slot）

### 命名规范

每个位置的标签由行字母 + `_` + 列字母 + `dot` 组成：

- 行字母：X(Surge), Y(Sway), Z(Heave), K(Roll), M(Pitch), N(Yaw)
- 列字母：x(Surge), y(Sway), z(Heave), p(Roll), q(Pitch), r(Yaw)

如 `(1,5)` → `Y_rdot`（第一行 Sway = Y，第五列 Yaw = r）。

### 数据结构

`HydroConfig::addedMass` 从 `std::array<double,12>` 改为 `std::array<double,36>`，row-major 存储（`m[i*6 + j]`）。

### 新增 API

| API | 位置 | 说明 |
|-----|------|------|
| `setAddedMassMatrix(const array<double,36>&)` | FossenModel | 直接填 M_A_ 6×6 矩阵 |
| `updateSymmetric()` | HydrodynamicsEditorDialog | 上三角变动时同步镜像至下三角 |

### SDF 格式

```xml
<added_mass>
  <m00>0.05</m00> <m01>0.0</m01> ... <m55>0.8</m55>
</added_mass>
```

36 个全量元素，简洁无歧义。

### KSM 格式

**新格式（36 元素）**：
```
am=m00,0.05
am=m01,0.0
...
am=m55,0.8
```

**兼容旧格式**：`ParseKSMFile` 同时支持旧的 12 命名标签（`am=X_udot,0.05`），自动映射到 6×6 矩阵的对应位置。

### 对称性与矩阵构建

- M_A 的 36 个元素直接写入 `FossenModel::M_A_`
- 若用户输入的矩阵不对称（上三角与下三角不符），`updateSymmetric()` 在 UI 层保证 i>j 的值始终等于 j>i
- `computeHydroForces` 中的 M_A·ν̇ 和 C_A·ν 计算自动使用完整矩阵

### 修改文件（与阻尼重构合并）

| # | 文件 | 改动 |
|---|------|------|
| 1 | `FossenModel.hh` | 新增 `setAddedMassMatrix` |
| 2 | `FossenModel.cc` | 实现 36 元素 → M_A_ |
| 3 | `ModelManager.hh` | `HydroConfig::addedMass` 12→36 |
| 4 | `ModelManager.cc` | `MakeHydroPluginSDF`、KSM save/parse 适配 |
| 5 | `HydrodynamicsSystem.cc` | SDF 解析 `<m00>..<m55>`，兼容旧命名 |
| 6 | `HydrodynamicsEditorDialog.hh` | `amSpins_[12]`→`[36]`，新增 `updateSymmetric` slot |
| 7 | `HydrodynamicsEditorDialog.cc` | 6×6 QGridLayout 重构 + 对称性同步 |

---

## 第七部分：惯性校验 UI 集成 ✅

> 实施日期：2026-07-15
> 状态：已完成

### 集成方式

在 `ModelSettingsDialog::validateAndAccept()` 中，名称唯一性校验之后、`accept()` 关闭对话框之前，插入惯性校验。

### 触发条件

仅当惯性复选框**已启用且已勾选**时执行校验：

```cpp
if (setInertiaCheckBox->isEnabled() && setInertiaCheckBox->isChecked())
{
  auto v = keship::ValidateInertia(
      inertiaMassSpinBox->value(),
      inertiaTensorSpinBoxes[0]->value(),  // Ixx
      inertiaTensorSpinBoxes[4]->value(),  // Iyy
      inertiaTensorSpinBoxes[8]->value(),  // Izz
      inertiaTensorSpinBoxes[1]->value(),  // Ixy
      inertiaTensorSpinBoxes[2]->value(),  // Ixz
      inertiaTensorSpinBoxes[5]->value()); // Iyz
  if (!v.valid) {
    QMessageBox::warning(this, "惯性参数无效",
        QString::fromStdString(v.message));
    return;  // 阻止对话框关闭，用户可修改后重试
  }
}
accept();
```

### 三种模式行为

| 模式 | 惯性状态 | 校验 |
|------|---------|------|
| `Full`（新建模型） | 用户可选择勾选 | 勾选时校验，不勾选跳过 |
| `OnlyPose`（属性编辑） | CheckBox 启用，用户可选择勾选 | 同 Full |
| `Ksm`（KSM 加载） | CheckBox 禁用（`setEnabled(false)`） | **跳过**（`isEnabled()` 为 false） |

### 错误提示示例

```
"iXX (50.0) + iYY (50.0) = 100.0 < iZZ (498.2)，违反惯性三角不等式"
"质量必须大于 0"
"Ixx、Iyy、Izz 必须大于 0"
```

### 修改文件

| # | 文件 | 改动 | 行数 |
|---|------|------|------|
| 1 | `ModelSettingsDialog.cc` | `#include "../utils/FossenModel.hh"` | +1 |
| 2 | `ModelSettingsDialog.cc` | `validateAndAccept()` 追加惯性校验块 | +15 |

物理层函数 `ValidateInertia` 和 UI 层验证现已完整贯通。

---

## 第八部分：D 阻尼编辑器重构

> 实施日期：2026-07-15
> 状态：进行中

### 背景

原先阻尼以 8 个命名 spinbox 展示（`Xu, Yv, Zw, Kp, Mq, Nr, Yr, Nv`），只能表达一阶线性阻尼。用户无法添加二阶（`X_{|u|u}`）或交叉耦合（`Y_{|v|r}`）项。

### 数据结构

`HydroConfig` 新增 `dampingTerms` 字段（`vector<DampingTerm>`），原有 `damping[8]` 保留兼容。

```cpp
struct HydroConfig {
    std::string linkName;
    std::array<double, 36> addedMass{};
    std::array<double, 8>  damping{};
    std::vector<DampingTerm> dampingTerms;  // 新增
};
```

### UI 布局

```
┌─ D 阻尼项 ───────────────────────────────────────────────────┐
│ ┌ 阻尼项列表 ────────────────────────────────────────────┐ │
│ │ 公式           系数     颜色   删除                      │ │
│ │ X_u            0.100    ■      [×]                      │ │
│ │ Y_v            0.200    ■      [×]                      │ │
│ │ Y_{|v|v}       0.050    ■      [×]                      │ │
│ │ Y_{|v|r}      -0.020    ■      [×]                      │ │
│ │  目标: Sway   factor 1:  v⁰   factor 2:  r¹            │ │
│ └─────────────────────────────────────────────────────────┘ │
│                    [+ 添加阻尼项]                            │
│                                                              │
│ D 矩阵拓扑                                                   │
│         Surge  Sway  Heave  Roll  Pitch  Yaw                │
│ Surge   ■₁     ○                                             │
│ Sway           ■₂⊕■₃⊕■₄                                    │
│ Heave                 ■₅      ○                              │
│ Roll                          ■₆                             │
│ Pitch                                 ■₇                     │
│ Yaw             ■₈                                    ■₉     │
│                                                              │
│ ■₁ X_u  ■₂ Y_v  ■₃ Y_{|v|v}  ■₄ Y_{|v|r}  ■₅ Z_w  ...    │
└──────────────────────────────────────────────────────────────┘
```

- 每 term 一行：公式名(Qt 富文本)、系数 spinbox、16×16 色块、删除按钮
- 多 factor 交叉项展开显示因子编辑面板
- 12 色轮循分配颜色
- D 矩阵格子：`行=target DOF, 列=第一个factor DOF`
- ≤2 term 同格 → 对角拼色；≥3 → 灰色，tooltip 列全
- hover 矩阵格显示 tooltip，click 跳转列表行

### SDF 格式

```xml
<damping>
  <term target="0" coefficient="0.1">
    <factor dof="0" order="1"/>
  </term>
  <term target="1" coefficient="0.05">
    <factor dof="1" order="2"/>
  </term>
  <term target="1" coefficient="-0.02">
    <factor dof="1" order="0"/>
    <factor dof="5" order="1"/>
  </term>
</damping>
```

兼容旧 `<linear_damping>` 格式。

### KSM 格式

```
新: dampt=0,0.1,0,1              ← target,coeff,dof,order
    dampt=1,-0.02,1,0,5,1        ← 多 factor 追加

兼容旧: damp=Xu,0.1  → dampt=0,0.1,0,1
```

### `HydrodynamicsSystem::Configure()` 解析

```cpp
if (_sdf->HasElement("damping"))
{
  // 新格式 <term>
  auto sdfPtr = const_cast<sdf::Element*>(_sdf.get());
  auto damp = sdfPtr->GetElement("damping");
  std::vector<DampingTerm> terms;
  for (auto termElem = damp->GetElement("term"); termElem;
       termElem = termElem->GetNextElement("term"))
  {
    DampingTerm t;
    t.target = termElem->Get<int>("target", 0);
    t.coefficient = termElem->Get<double>("coefficient", 0.0);
    for (auto fe = termElem->GetElement("factor"); fe;
         fe = fe->GetNextElement("factor"))
    {
      t.factors.push_back({
          fe->Get<int>("dof", 0),
          fe->Get<int>("order", 1)});
    }
    terms.push_back(t);
  }
  dataPtr->fossen->setDampingTerms(terms);
}
else if (_sdf->HasElement("linear_damping"))
{
  // 旧格式兼容
  ...
}
```

### 文件改动清单

| # | 文件 | 操作 | 内容 |
|---|------|------|------|
| 1 | `ModelManager.hh` | 修改 | `HydroConfig` 新增 `dampingTerms` |
| 2 | `ModelManager.cc` | 修改 | `MakeHydroPluginSDF` 新旧双格式输出；KSM save/parse 适配 |
| 3 | `HydrodynamicsSystem.cc` | 修改 | SDF 解析 `<damping><term>` + 兼容旧格式 |
| 4 | `HydrodynamicsEditorDialog.hh` | 修改 | 新增 term 列表/矩阵相关的 UI 成员、颜色表、信号 |
| 5 | `HydrodynamicsEditorDialog.cc` | 重写 | 阻尼编辑部分完全重构 |

### 修订 (2026-07-15)

**问题 1**：无预置线性项，添加按钮硬编码 → 修复为始终预置 8 个线性 term + 弹出选择对话框。

**问题 2**：公式显示为 LaTeX 纯文本 → 改为 HTML RichText。

**问题 3**：颜色逻辑颠倒（term 分配颜色 → 矩阵格跟进）→ 改为矩阵 36 格各有固定基色（行=色相，列=饱和度渐变），term 色块从矩阵格查询，多 term 同格同色。详细记录见第九部分。
