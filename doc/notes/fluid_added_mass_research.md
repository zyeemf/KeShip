# `<fluid_added_mass>` 技术调研报告

> 日期：2026-07-28
> 范围：SDF 解析 → ECM 传播 → DART Bridge 注入 → 官方插件 → VRX 实现 → Issue #1923 → SDF 规范

---

## 一、SDF 解析层（`sdformat` → `gz::math::Inertiald`）

### 1.1 数据来源

`sdformat/src/Link.cc` 在 `Load()` 中解析 `<inertial>` 子元素。当存在 `<fluid_added_mass>` 时，依次读取 21 个上三角元素，构造对称 6×6 `gz::math::Matrix6d`，存入 `Inertiald::SetFluidAddedMass()`。

### 1.2 21 元素完整映射

矩阵排列（行/列顺序：x, y, z, p, q, r）：

```
μ = ⎡ xx  xy  xz  xp  xq  xr ⎤   单位：
    ⎢ xy  yy  yz  yp  yq  yr ⎥   线性-线性 (xx~zz):   kg
    ⎢ xz  yz  zz  zp  zq  zr ⎥   线性-旋转 (xp~zr):   kg·m
    ⎢ xp  yp  zp  pp  pq  pr ⎥   旋转-旋转 (pp~rr):   kg·m²
    ⎢ xq  yq  zq  pq  qq  qr ⎥
    ⎣ xr  yr  zr  pr  qr  rr ⎦
```

解析代码（`sdformat/src/Link.cc`）：
```cpp
auto addedMassElem = inertialElem->GetElement("fluid_added_mass");
gz::math::Matrix6d addedMass;
addedMass(0, 0) = addedMassElem->Get<double>("xx", 0.0).first;
addedMass(0, 1) = addedMassElem->Get<double>("xy", 0.0).first;
// ... 读取所有上三角元素 ...
addedMass(1, 0) = addedMass(0, 1);   // 对称填充下三角
addedMass(2, 0) = addedMass(0, 2);
// ...
if (!this->dataPtr->inertial.SetFluidAddedMass(addedMass)) {
    errors.push_back({ErrorCode::LINK_INERTIA_INVALID, "..."});
}
```

所有元素可选，默认 0。矩阵必须对称，否则返回错误。

### 1.3 KSM 参数映射示例

假设用户代码中的 M_A 使用 **1-indexed**（Fossen 标准：1=x, 2=y, 3=z, 4=k, 5=m, 6=n）：

| 用户参数 (1-indexed) | 物理含义 | SDF 元素 | 值 |
|---------------------|---------|---------|----|
| M_A(1,1) = 360 | X_u_dot (surge) | `<xx>360</xx>` | kg |
| M_A(2,2) = 270 | Y_v_dot (sway) | `<yy>270</yy>` | kg |
| M_A(6,6) = 480 | N_r_dot (yaw) | `<rr>480</rr>` | kg·m² |
| M_A(1,6) = M_A(6,1) = -405 | X_r_dot (surge-yaw 耦合) | `<xr>-405</xr>` | kg·m |

完整 SDF 片段：
```xml
<inertial>
  <mass>300</mass>
  <pose>0 0 0 0 0 0</pose>
  <inertia>
    <ixx>100</ixx> <iyy>200</iyy> <izz>600</izz>
    <ixy>0</ixy> <ixz>0</ixz> <iyz>0</iyz>
  </inertia>
  <fluid_added_mass>
    <xx>360</xx>
    <yy>270</yy>
    <rr>480</rr>
    <xr>-405</xr>
  </fluid_added_mass>
</inertial>
```

> 注意：`<fluid_added_mass>` 使用**正号**物理约定。旧式插件参数 `<xDotU>` 使用负的 Fossen 符号约定。

---

## 二、ECM 传播层（`gz-sim` Link 组件）

### 2.1 数据路径

```
SDF <fluid_added_mass>
  → sdformat Link::Load() 解析
    → gz::math::Inertiald::SetFluidAddedMass()
      → 写入 ECM 的 components::Inertial 组件
        → Link::WorldFluidAddedMassMatrix(_ecm) 读取
```

### 2.2 `WorldFluidAddedMassMatrix()` 实现

位于 `<gz/sim/Link.hh>`（或对应的 .cc），从 ECM 中该 link 实体的 `Inertial` 组件取出 `FluidAddedMass()`，返回 `std::optional<Matrix6d>`。如果从未设置，返回 `std::nullopt`。

### 2.3 对 URDF 用户的注意事项

URDF 标准没有 `<fluid_added_mass>` 元素。如果使用 URDF 描述模型，需要通过 `<gazebo>` 扩展标签注入：

```xml
<gazebo reference="base_link">
  <fluid_added_mass>
    <xx>360</xx>
    <yy>270</yy>
    <rr>480</rr>
    <xr>-405</xr>
  </fluid_added_mass>
</gazebo>
```

参见 GitHub Issue `sdformat#1467`：URDF → SDF 转换时 `<fluid_added_mass>` 的支持尚在讨论中。

---

## 三、DART Bridge 注入层（`gz-physics8`）

### 3.1 关键文件

`dartsim/src/AddedMassFeatures.cc` — `SetLinkAddedMass()`

### 3.2 块重排（SDF → Featherstone 约定）

Gazebo/SDF 和 DART 使用不同的 6×6 空间矩阵块排列：

```
SDF 顺序：    [M_rotational,  M_coupled  ]        (左上角=旋转，右下角=平动)
              [M_coupled^T,   M_translational]

DART 顺序：   [M_translational, M_coupled^T]       (左上角=平动，右下角=旋转)
              [M_coupled,       M_rotational]
```

注入代码执行 4 个 3×3 块的置换：
```cpp
math::Matrix6d featherstoneMatrix;
featherstoneMatrix.SetSubmatrix(math::Matrix6d::TOP_LEFT,       // ← 原 BOTTOM_RIGHT
    sdfInertia.FluidAddedMass().value().Submatrix(math::Matrix6d::BOTTOM_RIGHT));
featherstoneMatrix.SetSubmatrix(math::Matrix6d::TOP_RIGHT,      // ← 原 BOTTOM_LEFT
    sdfInertia.FluidAddedMass().value().Submatrix(math::Matrix6d::BOTTOM_LEFT));
featherstoneMatrix.SetSubmatrix(math::Matrix6d::BOTTOM_LEFT,    // ← 原 TOP_RIGHT
    sdfInertia.FluidAddedMass().value().Submatrix(math::Matrix6d::TOP_RIGHT));
featherstoneMatrix.SetSubmatrix(math::Matrix6d::BOTTOM_RIGHT,   // ← 原 TOP_LEFT
    sdfInertia.FluidAddedMass().value().Submatrix(math::Matrix6d::TOP_LEFT));
```

### 3.3 空间张量注入

```cpp
// 禁用 DART 内置重力（需要用 F=ma 手动施加）
bn->setGravityMode(false);

// 把重排后的 M_A 加到 DART 的空间张量中
newInertia.setSpatialTensor(
    newInertia.getSpatialTensor() + math::eigen3::convert(featherstoneMatrix));
bn->setInertia(newInertia);
```

**效果**：DART 的动力学方程变为：
```
(I_RB + I_A) · ν̇_n = τ_gravity + τ_external
```

M_A 在等号**左边**，与 ν̇_n 同时求解——隐式积分，无条件稳定。

### 3.4 `setGravityMode(false)` 的含义

DART 不再自动对 link 施加重力。重力必须通过外力（wrench）手动施加。这通常是 Buoyancy 插件的职责——在施加浮力的同时，也需要施加 `m*g` 的重力。

在 `Base.hh` 中有保护逻辑：
```cpp
if (linkInfo->inertial.has_value() &&
    linkInfo->inertial->FluidAddedMass().has_value()) {
    return;  // 不允许外部修改重力模式
}
```

### 3.5 `SDFFeatures.cc` 中的双阶段创建

`ConstructSdfLink()` 分两步创建 DART 节点：

1. **初始创建**：只设置质量、MOI、COM（不含 M_A）
2. **后续注入**：检测到 `FluidAddedMass()` 后调用 `SetLinkAddedMass()`

```cpp
// 阶段 1
bodyProperties.mInertia.setMass(sdfInertia.MassMatrix().Mass());
bodyProperties.mInertia.setMoment(math::eigen3::convert(sdfInertia.Moi()));
bodyProperties.mInertia.setLocalCOM(math::eigen3::convert(sdfInertia.Pose().Pos()));

// 创建节点
auto result = modelInfo.model->createJointAndBodyNodePair<FreeJoint>(...);

// 阶段 2（仅在存在 fluid_added_mass 时）
if (sdfInertia.FluidAddedMass().has_value()) {
    amf->SetLinkAddedMass(linkIdentity, sdfInertia.FluidAddedMass().value());
}
```

### 3.6 完整传播路径总结

```
SDF <inertial><fluid_added_mass>
  → sdformat::Link::Load() → Inertiald::SetFluidAddedMass()
    → ECM::components::Inertial (含 M_A)
      → gz-physics8 SDFFeatures::ConstructSdfLink()
        → DartBodyNode::Properties 设置质量/MOI/COM
        → AddedMassFeatures::SetLinkAddedMass()
          → 块重排 SDF→Featherstone
          → newInertia.setSpatialTensor(body + M_A)
          → bn->setInertia(newInertia)
          → bn->setGravityMode(false)
            → DART 隐式求解 (I_RB+M_A)·ν̇ = τ
```

---

## 四、官方 Hydrodynamics 插件分析（`gz-sim8`）

### 4.1 文件

`src/systems/hydrodynamics/Hydrodynamics.cc` + `HydrodynamicsUtils.hh`

### 4.2 `Configure()` 阶段

**a) 读取阻尼系数**（SNAME 命名约定）：
```cpp
for (auto i = 0; i < 6; i++)
    for (auto j = 0; j < 6; j++) {
        std::string prefix = {snameConventionMoment[i]};  // x,y,z,k,m,n
        prefix += snameConventionVel[j];                   // U,V,W,P,Q,R
        stabilityLinearTerms[i*6+j] = SdfParamDouble(_sdf, prefix, 0);
        // quadDerivs[216], quadAbsDerivs[216] 同理
    }
```
例如 `<xUabsU>` → 二次方阻力（surge），`<yV>` → 线性阻力（sway）。

**b) 读取插件式 M_A 参数**（已弃用）：
```cpp
this->dataPtr->Ma = Eigen::Matrix<double, 6, 6>::Zero();
for (auto i = 0; i < 6; i++)
    for (auto j = 0; j < 6; j++) {
        std::string prefix = {snameConventionMoment[i]};
        prefix += "Dot" + snameConventionVel[j];  // xDotU, yDotV, ...
        this->dataPtr->Ma(i, j) = SdfParamDouble(_sdf, prefix, 0);
    }
```

**c) 检测 `<fluid_added_mass>` 并防守双重计数**：
```cpp
gz::sim::Link link(this->dataPtr->linkEntity);
auto fluidMa = link.WorldFluidAddedMassMatrix(_ecm);
if (fluidMa.has_value()) {
    this->dataPtr->hasFluidAddedMass = true;
    // 缓存 M_A 用于 Coriolis 流修正
    this->dataPtr->fluidAddedMass = ...;

    // 双重计数防护
    if (fluidMa.has_value() && addedMassSpecified && !disableAddedMass) {
        gzerr << "Link has <fluid_added_mass> AND plugin added mass parameters. "
              << "This will double-count added mass forces. Set "
              << "<disable_added_mass>true</disable_added_mass>." << std::endl;
    }
} else if (!disableAddedMass && addedMassSpecified) {
    gzwarn << "Plugin-based added mass has known numerical instabilities. "
           << "If using DART, migrate to <fluid_added_mass>. "
           << "Other engines (Bullet, MuJoCo) only support plugin-based." << std::endl;
}
```

### 4.3 `PreUpdate()` 阶段 — 核心计算

**a) 状态读取**：
```cpp
auto linearVelocity = _ecm.Component<components::WorldLinearVelocity>(...);
auto rotationalVelocity = baseLink.WorldAngularVelocity(_ecm);
auto pose = baseLink.WorldPose(_ecm);
// 海流校正（若有）
auto localLinearVelocity = pose->Rot().Inverse() * (linearVelocity->Data() - currentVector);
auto localRotationalVelocity = pose->Rot().Inverse() * *rotationalVelocity;

state << localLinearVel.X(), ..., localRotationalVel.Z();
```

**b) 加速度估计**（仅用于插件式 M_A，非 DART 原生路径）：
```cpp
if (this->dataPtr->firstIteration) {
    this->dataPtr->prevState = state;
    this->dataPtr->firstIteration = false;
}
auto dt = static_cast<double>(_info.dt.count()) / 1e9;
stateDot = (state - this->dataPtr->prevState) / dt;
this->dataPtr->prevState = state;
```

**c) 合力计算**（分三支）：

| 分量 | 条件 | 公式 |
|------|------|------|
| 插件 M_A·ν̇ | `!disableAddedMass` | `-Ma * stateDot` |
| 插件 Coriolis | `!disableCoriolis` | `-Cmat(ν) * state` |
| Damping | 始终 | `Dmat(ν) * state` |

```cpp
if (!this->dataPtr->disableAddedMass)
    kTotalWrench += -this->dataPtr->Ma * stateDot;

if (!this->dataPtr->disableCoriolis) {
    Cmat = hydro::buildCoriolisMatrix(this->dataPtr->Ma, state);
    kTotalWrench += -Cmat * state;
}

Dmat = hydro::buildDampingMatrix(linear, quadAbs, quad, state);
kTotalWrench += Dmat * state;
```

**d) Coriolis 流修正**（当同时有 `<fluid_added_mass>` 和海流时）：
```cpp
if (this->dataPtr->hasFluidAddedMass) {
    auto Ca_abs = buildFullCoriolisMatrix(fluidAddedMass, absState);  // 绝对速度
    auto Ca_rel = buildFullCoriolisMatrix(fluidAddedMass, state);     // 相对速度
    kTotalWrench += Ca_rel * state - Ca_abs * absState;
}
```
DART 隐式计算 `C_A(v)·v`，但 Fossen 要求 `C_A(v_r)·v_r`。修正力使得净效果为 `-C_A(v_r)·v_r`。

**e) 施加 wrench**：
```cpp
math::Vector3d totalForce(-kTotalWrench(0), -kTotalWrench(1), -kTotalWrench(2));
math::Vector3d totalTorque(-kTotalWrench(3), -kTotalWrench(4), -kTotalWrench(5));
baseLink.AddWorldWrench(_ecm, pose->Rot() * totalForce, pose->Rot() * totalTorque);
```

### 4.4 各物理引擎推荐配置表

来自 `theory_hydrodynamics.html`（gz-sim10 文档）：

| 物理引擎 | M_A 方法 | 阻尼方法 | 稳定性 |
|---------|---------|---------|--------|
| **DART** | `<fluid_added_mass>` SDF 注入 | Hydrodynamics 插件 | **无条件稳定**，全 6×6 矩阵，隐式 Coriolis |
| Bullet-Featherstone | 插件 `<xDotU>` 参数 | Hydrodynamics 插件 | 条件稳定（无原生 M_A 支持） |
| Bullet (classic) | 插件 `<xDotU>` 参数 | Hydrodynamics 插件 | 条件稳定 |
| MuJoCo | 插件 `<xDotU>` 参数 | Hydrodynamics 插件 | 条件稳定 |

---

## 五、VRX SimpleHydrodynamics 分析

### 5.1 文件

`vrx_gz/src/SimpleHydrodynamics.cc`（VRX main 分支，适配 Gazebo Harmonic）

### 5.2 配置

```cpp
this->dataPtr->link.EnableVelocityChecks(_ecm);
this->dataPtr->link.EnableAccelerationChecks(_ecm);
// 读取参数（含单位默认值）
this->dataPtr->paramXdotU = _sdf->Get<double>("xDotU", 5  ).first;
this->dataPtr->paramYdotV = _sdf->Get<double>("yDotV", 5  ).first;
// ... zDotW=0.1, kDotP=0.1, mDotQ=0.1, nDotR=1
this->dataPtr->paramXu    = _sdf->Get<double>("xU",   20 ).first;
this->dataPtr->paramXuu   = _sdf->Get<double>("xUU",   0 ).first;
// ...
```

### 5.3 PreUpdate 中的加速度来源

VRX 不使用 `(ν_n - ν_{n-1})/dt` 数值差分，而是直接读取物理引擎计算的加速度：

```cpp
auto worldAngularAccel = this->dataPtr->link.WorldAngularAcceleration(_ecm);
auto worldLinearAccel = this->dataPtr->link.WorldLinearAcceleration(_ecm);

auto comPose = this->dataPtr->link.WorldInertialPose(_ecm);
auto localAngularAccel = comPose->Rot().Inverse() * (*worldAngularAccel);
auto localLinearAccel  = comPose->Rot().Inverse() * (*worldLinearAccel);

stateDot << localLinearAccel.X(), localLinearAccel.Y(), localLinearAccel.Z(),
            localAngularAccel.X(), localAngularAccel.Y(), localAngularAccel.Z();
```

`WorldAngularAcceleration` / `WorldLinearAcceleration` 由物理引擎在 Update 阶段**计算完成后**写入 ECM。PreUpdate 读取到的是**上一时间步**的加速度——与 UUV Simulator 和官方 Hydrodynamics 插件同样的一步滞后问题。

### 5.4 力计算

```cpp
// M_A 力
const Eigen::VectorXd kAmassVec = -1.0 * this->dataPtr->Ma * stateDot;

// Coriolis - 仅 3 个非零项（用于水面艇 3-DOF 近似）
Cmat(0, 5) = this->dataPtr->paramYdotV * localLinearVel.Y();
Cmat(1, 5) = this->dataPtr->paramXdotU * localLinearVel.X();
Cmat(5, 0) = this->dataPtr->paramYdotV * localLinearVel.Y();
Cmat(5, 1) = this->dataPtr->paramXdotU * localLinearVel.X();
// 注意：Cmat 未加入合力！→ 死代码

// 阻尼（对角二次方）
Dmat(0, 0) = paramXu + paramXuu * std::abs(localLinearVel.X());
// ... 同理 y, z, k, m, n

// 只加了 M_A + 阻尼 → 无 Coriolis
const Eigen::VectorXd kForceSum = kAmassVec + kDvec;
```

**关键发现**：Coriolis 矩阵虽然计算了，但**从未加到 `kForceSum` 中**。这是 VRX 和官方插件的重大差异。

### 5.5 VRX 方案为什么不发散

| 因素 | VRX (WAM-V) | KSM | 影响 |
|------|-------------|-----|------|
| M_A 对角值 | xDotU=5 (小) | xx=360 (大) | 大 M_A 放大滞后误差 |
| M_A 耦合项 | 无 | M_A(1,6)=-405 | 耦合导致谱半径剧增 |
| DOF | 3-DOF 水面 (surge/sway/yaw) | 6-DOF 水下 | 偏航恢复力来自浮力 |
| Coriolis | 未施加 | 需施加 | 减少正反馈回路 |
| 加速度源 | 物理引擎输出 ν̇ | 同左 | 步滞后不影响小 M_A |

VRX 的使用场景（小 M_A、对角项、水面强恢复力）掩盖了一步滞后导致的发散问题。KSM 场景（大 M_A、强耦合、全 6-DOF）会完全暴露此问题。

---

## 六、GitHub Issue #1923 关键证据

### 6.1 基本信息

- **Issue**：`gazebosim/gz-sim#1923` — "Unbounded velocity growth observed with hydrodynamics"
- **状态**：已关闭
- **相关 PR**：`#1592`（引入 `<fluid_added_mass>`）、`#1888`（修复阻尼参数）、`#2493`（弃用插件 M_A）

### 6.2 问题陈述

> There appears to be a bug, which I believe to be related to the hydrodynamics plugin, which results in a model world velocity run-away. I've experienced this with my personal model, and it is reproducible in the AUV example provided by `auv_controls.sdf`.

> I believe M_A (added mass matrix) should be positive definite, symmetric. Yet the terms that exist in `auv_controls.sdf` result in a decidedly negative definite matrix.

### 6.3 开发者确认

> "The fluid_added_mass present in the inertial (which leverages DART's ability to handle 6x6 spatial inertia tensors) should be favored. It leads to a simpler and more robust solution. The only downturn is the reliance on DART itself (it's not supported by other physics engines)."

> "The added mass effect of the hydrodynamics plugin, IMHO, could be deprecated. The current implementation, as known, is brittle. Doing `a[k] = -Mrb^-1 * Ma * a[k-1]` leads to instability when Ma > Mrb."

### 6.4 社区结论

- 插件式 M_A：**条件稳定**，当 M_A 的某些分量超过对应的刚体惯量时发散
- DART `<fluid_added_mass>`：**无条件稳定**，隐式求解无反馈环路
- 宜弃用插件式 M_A 参数（`<xDotU>` 等），迁移至 SDF 注入

---

## 七、SDF `<fluid_added_mass>` 规范完整参考

### 7.1 元素清单

来源：`sdformat/sdf/1.10/inertial.sdf` 和 `docs/added_mass/proposal.md`。

| 元素 | 矩阵位置 | 单位 | 物理含义 |
|------|---------|------|---------|
| `<xx>` | (0,0) | kg | X 方向平动 → X 方向力 |
| `<xy>` | (0,1) | kg | Y 方向平动 → X 方向力 |
| `<xz>` | (0,2) | kg | Z 方向平动 → X 方向力 |
| `<xp>` | (0,3) | kg·m | 绕 X 轴旋转 → X 方向力 |
| `<xq>` | (0,4) | kg·m | 绕 Y 轴旋转 → X 方向力 |
| `<xr>` | (0,5) | kg·m | 绕 Z 轴旋转 → X 方向力 |
| `<yy>` | (1,1) | kg | Y 方向平动 → Y 方向力 |
| `<yz>` | (1,2) | kg | Z 方向平动 → Y 方向力 |
| `<yp>` | (1,3) | kg·m | 绕 X 轴旋转 → Y 方向力 |
| `<yq>` | (1,4) | kg·m | 绕 Y 轴旋转 → Y 方向力 |
| `<yr>` | (1,5) | kg·m | 绕 Z 轴旋转 → Y 方向力 |
| `<zz>` | (2,2) | kg | Z 方向平动 → Z 方向力 |
| `<zp>` | (2,3) | kg·m | 绕 X 轴旋转 → Z 方向力 |
| `<zq>` | (2,4) | kg·m | 绕 Y 轴旋转 → Z 方向力 |
| `<zr>` | (2,5) | kg·m | 绕 Z 轴旋转 → Z 方向力 |
| `<pp>` | (3,3) | kg·m² | 绕 X 轴旋转 → 绕 X 轴力矩 |
| `<pq>` | (3,4) | kg·m² | 绕 Y 轴旋转 → 绕 X 轴力矩 |
| `<pr>` | (3,5) | kg·m² | 绕 Z 轴旋转 → 绕 X 轴力矩 |
| `<qq>` | (4,4) | kg·m² | 绕 Y 轴旋转 → 绕 Y 轴力矩 |
| `<qr>` | (4,5) | kg·m² | 绕 Z 轴旋转 → 绕 Y 轴力矩 |
| `<rr>` | (5,5) | kg·m² | 绕 Z 轴旋转 → 绕 Z 轴力矩 |

### 7.2 与 Fossen 论文符号对应

Fossen (2011) eq. 6.39 的 6×6 附加质量矩阵：

```
M_A = -⎡ X_u̇  X_v̇  X_ẇ  X_ṗ  X_q̇  X_ṙ ⎤
        ⎢ Y_u̇  Y_v̇  Y_ẇ  Y_ṗ  Y_q̇  Y_ṙ ⎥
        ⎢ Z_u̇  Z_v̇  Z_ẇ  Z_ṗ  Z_q̇  Z_ṙ ⎥
        ⎢ K_u̇  K_v̇  K_ẇ  K_ṗ  K_q̇  K_ṙ ⎥
        ⎢ M_u̇  M_v̇  M_ẇ  M_ṗ  M_q̇  M_ṙ ⎥
        ⎣ N_u̇  N_v̇  N_ẇ  N_ṗ  N_q̇  N_ṙ ⎦
```

Fossen 使用**负号**约定（流体静力学的标准）。SDF `<fluid_added_mass>` 使用**正号**物理约定（与 DART 一致）。两者的关系是：

```
M_A_SDF = -M_A_Fossen
```

### 7.3 与旧式插件参数的关系

| 旧插件参数 (Fossen 负号) | SDF `<fluid_added_mass>` (正号) |
|--------------------------|-------------------------------|
| `<xDotU>-4.876` | `<xx>4.876</xx>` |
| `<yDotV>-126.325` | `<yy>126.325</yy>` |
| `<zDotW>-126.325` | `<zz>126.325</zz>` |
| `<kDotP>-33.46` | `<pp>33.46</pp>` |
| `<mDotQ>-33.46` | `<qq>33.46</qq>` |
| `<nDotR>-33.46` | `<rr>33.46</rr>` |

### 7.4 完整示例（官方 AUV）

来自 `examples/worlds/auv_controls.sdf`（gz-sim10 文档推荐配置）：

```xml
<!-- 旧方式（已弃用）：插件参数 + 负号
<link_name>base_link</link_name>
<xDotU>-4.876161</xDotU>
<yDotV>-126.324739</yDotV>
... -->

<!-- 新方式（推荐）：SDF 注入 + 正号 -->
<link name="base_link">
  <inertial>
    <mass>147.8671</mass>
    <fluid_added_mass>
      <xx>4.876161</xx>
      <yy>126.324739</yy>
      <zz>126.324739</zz>
      <qq>33.46</qq>
      <rr>33.46</rr>
    </fluid_added_mass>
  </inertial>
</link>

<!-- Hydrodynamics 插件仅处理阻尼 -->
<plugin filename="gz-sim-hydrodynamics-system"
        name="gz::sim::systems::Hydrodynamics">
  <link_name>base_link</link_name>
  <disable_added_mass>true</disable_added_mass>
  <xUabsU>-6.2282</xUabsU>
  <yVabsV>-601.27</yVabsV>
  <zWabsW>-601.27</zWabsW>
  <kPabsP>-0.1916</kPabsP>
  <mQabsQ>-632.698957</mQabsQ>
  <nRabsR>-632.698957</nRabsR>
</plugin>
```
