# 迁移方案：Plugin M_A → DART `<fluid_added_mass>`

## 问题背景

HydrodynamicsSystem 插件通过有限差分 `ν̇ = (ν_N − ν_{N−1})/dt` 估计加速度，
代入 Fossen 方程得到：

```
ν̇_N = −(M_RB⁻¹ · M_A) · ν̇_{N-1}
```

当 `M_RB⁻¹·M_A` 的任一特征值 > 1 时，系统无条件发散（`M_A > M_RB` 时必然发生）。
详见 Gazebo Sim Issue #1923, PR #2493。

## 解决方案

将 M_A 从插件中移除，改为 SDF `<fluid_added_mass>` 注入 link 的 `<inertial>`，
由 DART 物理引擎原生处理：

- DART 将 M_A 合并到空间惯量 `M_total = M_RB + M_A`
- 半隐式 Euler 积分器直接求解 `M_total · a = Σf —— C_total(ν)·ν`
- 隐式方法，无条件稳定，无特征值限制

## 改动原则

1. FossenModel 中**不删任何成员/函数**，仅改 `computeHydroForces` 函数体
2. 原代码通过 `#if 0 ... #endif` 保留供公式对比参考
3. 每个改动点加中文注释说明原因

## 改动文件清单

| 文件 | 改动 |
|------|------|
| `FossenModel.hh` | `computeHydroForces` 去掉 `_nuDot` 参数；加类注释块 |
| `FossenModel.cc` | `computeHydroForces` 只调 `computeDampingForces`；原代码 `#if 0` 保留 |
| `HydrodynamicsSystem.hh` | `prevNu`/`stepCount` 加注释说明废弃 |
| `HydrodynamicsSystem.cc` | Configure 中 `<added_mass>` 解析 `#if 0`；PreUpdate 删有限差分 |
| `ModelManager.hh` | +`InjectFluidAddedMass` 声明 |
| `ModelManager.cc` | +`InjectFluidAddedMass` 实现 + 7 处注入 + 改 `MakeHydroPluginSDF` |

## 详细改动

### FossenModel.hh

- `computeHydroForces` 签名：去掉 `_nuDot` 参数
- 头部注释块：解释 M_A/C_A 由 DART 处理的原因

### FossenModel.cc

```cpp
void FossenModel::computeHydroForces(
    const Eigen::Vector<double, 6> &_nu,
    Eigen::Vector<double, 6> &_tau) const
{
#if 0
  // Fossen 原始公式：τ = -M_A·ν̇ - C_A(ν)·ν - D(ν)·ν
  // 已被 DART <fluid_added_mass> 替代
  _tau = -(M_A_ * _nuDot);
  // ... ca_nu 计算 ...
  _tau -= ca_nu;
  computeDampingForces(_nu, _tau);
#else
  _tau.setZero();
  computeDampingForces(_nu, _tau);
#endif
}
```

### HydrodynamicsSystem.hh

`prevNu`/`stepCount` 保留并加注释。

### HydrodynamicsSystem.cc

- Configure：`#if 0` 包裹 `<added_mass>` 解析块
- PreUpdate：删除有限差分，保留 `dt` 仅用于诊断
- 调用改为 `computeHydroForces(nu, tauBody)`

### ModelManager.hh

新增 `InjectFluidAddedMass` 静态方法。

### ModelManager.cc

- 实现 `InjectFluidAddedMass`：21 元素上三角映射，`_addedMass` 已是物理值（UI 输入 kg），注入到 `</inertial>` 前
- 7 处 SDF 组装路径中在 `rfind("</link>")` 前注入
- `MakeHydroPluginSDF` 中删除 `<added_mass>` 输出
- SDF 版本 `1.6` → `1.10`

## 21 元素全映射（对称矩阵上三角，仅输出非零项）

| 矩阵位置 | XML 标签 | Fossen 12参数对应 | 默认值 |
|---------|---------|------------------|--------|
| M_A(0,0) | `<xx>` | X_udot | 0 |
| M_A(0,1) | `<xy>` | — | 0 |
| M_A(0,2) | `<xz>` | — | 0 |
| M_A(0,3) | `<xp>` | — | 0 |
| M_A(0,4) | `<xq>` | — | 0 |
| M_A(0,5) | `<xr>` | — | 0 |
| M_A(1,1) | `<yy>` | Y_vdot | 0 |
| M_A(1,2) | `<yz>` | — | 0 |
| M_A(1,3) | `<yp>` | Y_pdot | 0 |
| M_A(1,4) | `<yq>` | — | 0 |
| M_A(1,5) | `<yr>` | Y_rdot | 0 |
| M_A(2,2) | `<zz>` | Z_wdot | 0 |
| M_A(2,3) | `<zp>` | — | 0 |
| M_A(2,4) | `<zq>` | M_wdot | 0 |
| M_A(2,5) | `<zr>` | — | 0 |
| M_A(3,3) | `<pp>` | K_pdot | 0 |
| M_A(3,4) | `<pq>` | — | 0 |
| M_A(3,5) | `<pr>` | K_rdot | 0 |
| M_A(4,4) | `<qq>` | M_qdot | 0 |
| M_A(4,5) | `<qr>` | — | 0 |
| M_A(5,5) | `<rr>` | N_rdot | 0 |

所有值直接写入（`_addedMass` 已是物理值 kg 单位，无需乘 mass）。

## 修正记录 (2026-07-21)

### Bug: InjectFluidAddedMass 双重缩放

**问题**：`InjectFluidAddedMass` 中 `* _mass` 导致附加质量被乘了两次（`buildAddedMassMatrix` 已做过一次缩放），所有值放大 300 倍，有效 Z 轴惯量 108300 kg 导致加速度仅 g/360 ≈ 0.027 m/s²。

**修复**：删除 `* _mass`，函数签名移除 `_mass` 参数。

### 补全 21 元素映射

原代码只映射了 15 个上三角元素，缺少 `xq(0,4)、xr(0,5)、yq(1,4)、yr(1,5)、zq(2,4)、zr(2,5)` 六个线性-旋转耦合项。其中 `yr=Y_rdot` 和 `zq=M_wdot` 在 12 参数 Fossen 模型中可为非零。现已补全，输出时跳过零值。
