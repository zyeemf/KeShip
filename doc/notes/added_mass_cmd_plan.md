# 水动力重构方案 — FossenModel 化简 + 阻尼-only [实施完成]

> 日期：2026-07-29
> 关键字：方案 A、M_A 始终在 DART、阻尼-only、现状保持、不修改内核
> 状态：**已实施**（见 §八）

---

## 一、决策

| 项目 | 内容 |
|------|------|
| 方案 | **方案 A**：保持 M_A 在 DART `<fluid_added_mass>` 中始终存在，不运行时切换 |
| 空中情况 | **暂不考虑**（不实现 air cancellation，不依赖加速度读取） |
| 内核修改 | **不修改** gz-sim / gz-physics（全部在 keship 包内实现） |
| 数值稳定性 | λ((M_RB+M_A)⁻¹·M_A) = 0.664 < 1，无条件收敛（已验证） |

## 二、改动范围

只在 keship 包内，不涉及 gazebo 内核。

| 文件 | 改动 |
|------|------|
| `FossenModel.hh` | 删除 M_A/C_A 相关成员/函数，仅保留阻尼 |
| `FossenModel.cc` | 同上 |
| `KSPlugin_HydroBuoyancy.hh` | 删除加速度诊断、M_A 存储 |
| `KSPlugin_HydroBuoyancy.cc` | PreUpdate 中删除水动力 M_A 计算 + 加速度读取 |
| `ModelManager.cc` | 保持 `InjectFluidAddedMass` 不变（M_A 继续注入 SDF） |

## 三、详细改动

### 3.1 FossenModel.hh

**删除**：
- `setAddedMassCoeffs()`, `setAddedMassMatrix()`, `buildAddedMassMatrix()`
- `computeAddedMassForce()`, `computeCoriolisForce()`
- `computeAirCorrection()`
- `computeHydroForces(nu, nuDot, tau)`（三参数版本）
- 成员 `M_A_`, `addedMassCoeffs_`, `mass_`, `zg_`, `Ixx_`, `Iyy_`, `Izz_`, `Ixz_`
- 头部注释中关于 M_A/C_A 分工的说明

**保留**：
- `computeDamping(nu, tau)`
- `setDampingTerms()`, `setDampingCoeffs()`
- `dampingTerms_`, `dampingCoeffs_`
- `valid()`（简化为 `!dampingTerms_.empty()`）

**新接口**：

```cpp
class FossenModel {
public:
  FossenModel();

  void setDampingTerms(const std::vector<DampingTerm> &_terms);
  void setDampingCoeffs(const std::array<double, 8> &_coeffs);

  /// 计算阻尼：τ = -D(ν)·ν
  void computeDamping(const Eigen::Vector<double, 6> &_nu,
                      Eigen::Vector<double, 6> &_tau) const;

  bool valid() const;
};
```

### 3.2 FossenModel.cc

删除全部 M_A/C_A 相关实现，仅保留：

```cpp
void FossenModel::computeDamping(
    const Eigen::Vector<double, 6> &_nu,
    Eigen::Vector<double, 6> &_tau) const
{
  _tau.setZero();
  for (const auto &term : dampingTerms_)
  {
    double val = term.coefficient;
    for (const auto &f : term.factors)
    {
      double absv = std::abs(_nu[f.dof]);
      double contrib = (f.order == 0) ? absv : _nu[f.dof];
      for (int i = 1; i < f.order; ++i)
        contrib *= absv;
      val *= contrib;
    }
    _tau(term.target) -= val;
  }
}
```

### 3.3 KSPlugin_HydroBuoyancy.hh

- `Implementation::hydroDiag` 中删除 `tauMA[6]`, `tauCA[6]`, `accelAvail`
- 删除 `Implementation::M_A_`（不再需要）
- Configure 中 Fossen 初始化时**不再读取 M_A**

### 3.4 KSPlugin_HydroBuoyancy.cc — PreUpdate

**删除以下逻辑**（第 7 段水动力，约第 450-540 行）：

- 读取 `WorldLinearAcceleration` / `WorldAngularAcceleration`
- 调用 `computeHydroForces(nu, nuDot, tauBody)` / `computeAddedMassForce` / `computeCoriolisForce`
- 诊断 `hydroDiag.tauMA` / `hydroDiag.tauCA` 的写入
- Configure 中对加速度 `EnableAccelerationChecks` 的启用

**保留**：

```cpp
// ── 阻尼（水中） ──
if (inWater && !aboveWater && this->enableHydro_ &&
    this->dataPtr->fossen && this->dataPtr->fossen->valid())
{
  auto linVelWorld = this->linkObj_.WorldLinearVelocity(_ecm);
  auto angVelWorld = this->linkObj_.WorldAngularVelocity(_ecm);
  if (linVelWorld && angVelWorld)
  {
    gz::math::Quaterniond R_bw = modelPose.Rot().Inverse();
    nu << (R_bw * (*linVelWorld)).X(), ...
          ...;
    Eigen::Vector<double, 6> tauDamp;
    this->dataPtr->fossen->computeDamping(nu, tauDamp);
    totalForce += modelPose.Rot() * gz::math::Vector3d(tauDamp(0), ...);
    totalTorque += ...;
  }
}
```

阻尼仅**水中**施加。空中不施加阻尼（DART 自身重力 + M_A 惯量，不外加其他力）。

Configure 中 Fossen 初始化**不再读取 `<added_mass>` SDF 元素**（M_A 已在 `InjectFluidAddedMass` 中注入 SDF `<fluid_added_mass>`，不依赖插件）。

### 3.5 ModelManager.cc

不变。`InjectFluidAddedMass` 保持现有实现。

## 四、现状总结

```
M_A 在 DART <fluid_added_mass> 中始终存在（ModelManager 注入 SDF）
  ↓
DART 空间惯量 = M_RB + M_A（始终）
DART 隐式积分（M_RB + M_A）·ν̇ = ΣF

水中：
  插件：τ_plugin = τ_buoyancy + τ_damping
  DART：(M_RB + M_A)·ν̇ = τ_gravity + τ_buoyancy + τ_damping
  → 正确的水中动力学

空中（暂不处理）：
  插件：τ_plugin = 0（无 buoyancy，无 damping）
  DART：(M_RB + M_A)·ν̇ = τ_gravity
  → 自由落体受 M_A 影响，加速度 ≠ g
  → 但 λ<1 不发散

未来如需处理空中：加回 air cancellation τ_cancel = +M_A·ν̇ + C_A·ν
  → 这需要重新读取 WorldLinearAcceleration
  → 需要新增加速度使能
```

## 五、与旧方案对比

| 维度 | 旧方案（07-28） | 新方案 |
|------|----------------|--------|
| FossenModel 复杂度 | M_A + C_A + D 三套计算 | 仅 D 阻尼 |
| AL 依赖 | 是（需加速度） | **否** |
| 空中抵消 | 是（`computeAirCorrection`） | **否**（暂不处理） |
| 内核改动 | 无 | **无** |
| 代码量 | 全量 | 减 ~60% |

## 六、构建

```bash
cd ~/keship_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select keship --merge-install
```

不依赖 gz-sim/gz-physics 重新编译。

## 八、实施记录（2026-07-29）

### 2026-07-29 修复: 圆柱体水动力系数修正（方案 A）

**根因三连**:
1. `cylinderDefaults` 使用船型系数（heave=0.8M、surge=0.1M），
   竖直圆柱体所需相反（径向 > 轴向）
2. UI 圆柱体 spawn 路径中 `InjectFluidAddedMass` 调用在重构时被移除，
   `<fluid_added_mass>` 未注入 DART → 零附加质量
3. `aboveWater` 条件（linkZ > fluidLevel + 0.5m）在大物体部分出水时
   完全切断阻尼 → 能量重新注入 → 持续振荡

**改动**:
- `HydrodynamicsEditorDialog.cc` — `cylinderDefaults` 重写为基于面积比的
  圆柱体物理系数（径向 6.1×M、轴向 1.2×M、附加质量用 ρ_fluid×V）
- `ModelManager.cc` — 删除 `MakeBuoyancyPluginSDF` 中冗余 `<added_mass>`
  块；恢复 `SpawnModelWithBuoyancy` 中 `InjectFluidAddedMass` 调用
- `KSPlugin_HydroBuoyancy.cc` — 去除 `!aboveWater` 条件，阻尼始终在
  水区生效；`inWater` 标志已足够保护完全离水情况

### 实际改动 vs 计划差异

| 项 | 计划 | 实际 |
|---|------|------|
| `ValidateInertia` | 删除 | **保留**（GUI `ModelSettingsDialog` 依赖） |
| `FossenModel::computeDamping` | 计划内联实现 | 委托 `computeDampingForces`（私有方法未删除） |
| `setDampingCoeffs` 公式 | `add(n, n, 1, _coeffs[n])` | 同上（去掉 `×mass_`） |
| `KSPlugin_HydroBuoyancy` 诊断 | 仅保留 tauD | 同计划 |
| `ModelManager.cc` | 不变 | 不变（`InjectFluidAddedMass` 继续注入 SDF `<fluid_added_mass>`） |

### 构建验证

```bash
cd ~/keship_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select keship --merge-install  # ✅ 通过
```

### 测试结果

日志 `hydro_detail.csv` 中仅 tauD 分量输出，数据正常，符合阻尼-only 预期。

## 七、相关文件

| 文件 | 用途 |
|------|------|
| `archive/ma_dart_removal_fix.md` | 历史文档（M_A 注入 DART + 空中抵消的原始方案，含数值稳定性分析） |
| 本文件 | 当前重构方案 |
