# Issue 2 根治方案 — DART 隐式 M_A + 空中抵消

> 更新 (2026-07-29)：重构方案见 [added_mass_cmd_plan.md](../added_mass_cmd_plan.md) — 化简为阻尼-only

> 日期：2026-07-28（第三次修订）
> 关键字：`<fluid_added_mass>`、Fossen、空中抵消、数值稳定性、`WorldLinearAcceleration`
> 最终决策：M_A 放回 DART（隐式积分），空中用外部 wrench 抵消 M_A/C_A

---

## 一、现象

| 场景 | 配置 | 测试结果 |
|------|------|---------|
| **无水域 + 原始代码**（M_A 在 DART + CoG 偏移） | `neworigin_damp.ksm` | ❌ 自由落体自转 |
| **无水域 + 空化 M_A 注入** | `neworigin_damp.ksm` | ✅ 自由落体平滑 |
| **有水域 + 空化 M_A 注入 + 显式 Fossen** | `neworigin_damp.ksm` | ❌ 瞬间飞到无穷远 |

前两个测试说明：**M_A 在 DART 是 CoG 偏移自转的根因**。
第二个+第三个测试说明：**显式 `τ = -M_A·ν̇` 数值发散**（λ_max=1.975 > 1）。

---

## 二、显式 vs 隐式：时间步错位（数值发散根因）

### 2.1 Gazebo 的读-写-解时序

每个物理步 n 的完整流程：

```
① PreUpdate（插件执行）
   读: ν̇_measured = DART **上一步**算完的 ν̇_{n-1}
   写: τ_plugin_n   → 写入 ExternalWorldWrenchCmd

② PhysicsStep（DART 执行）
   解: I_RB·ν̇_n = τ_gravity + τ_plugin_n   (用刚写的力)
   得: ν̇_n → 更新位置/速度
```

**关键：PreUpdate 永远读到的是上一步的加速度 ν̇_{n-1}，即将写入的力 τ_n 会被用于计算 ν̇_n。**

### 2.2 显式方案（M_A 在插件）— 半步滞后

```
τ_plugin_n = -M_A·ν̇_{n-1} - C_A·ν_n - D·ν_n
              ↑ 上一步的加速度！PreUpdate 只能读到这一步

DART 求解:
  I_RB·ν̇_n = τ_gravity - M_A·ν̇_{n-1} - C_A·ν_n - D·ν_n

整理:
  I_RB·ν̇_n + M_A·ν̇_{n-1} = τ_gravity - C_A·ν_n - D·ν_n
         ↑  ↑
         I_RB 用 ν̇_n，M_A 用 ν̇_{n-1} — 时间不一致！

递推关系:
  ν̇_n = -I_RB⁻¹·M_A·ν̇_{n-1} + (其他项)

  每步把上一步的 ν̇ 乘以 I_RB⁻¹·M_A（谱半径 1.98）后送入下一步
  → 指数发散
```

### 2.3 隐式方案（M_A 在 DART）— 同时求解

```
DART 的求解矩阵已包含 M_A：
  (I_RB + M_A)·ν̇_n = τ_gravity + τ_plugin_n

τ_plugin_n 不含 ν̇（只含阻尼 -D·ν），无循环依赖：

  (I_RB + M_A)·ν̇_n = τ_gravity - D·ν_n

  M_A 在等号左边，与 ν̇_n 同时求解，一步到位，没有递推
  → 无条件稳定
```

### 2.4 核心区别一句话

```
显式:  I_RB·ν̇_n + M_A·ν̇_{n-1} = g + ...    ✗ M_A 滞后一个时间步
隐式:  (I_RB + M_A)·ν̇_n         = g + ...    ✓ 同时求解
```

两个方程数学形式等价，但执行时 M_A 的时间点不同。**用上一步的加速度算这一步的 M_A 力，就是显式发散的根因。**

---

## 三、数值稳定性分析

### 3.1 关键耦合块

以 `neworigin_damp.ksm` 的 sway↔yaw 耦合为例：

```
M_A_sway_yaw = [ 360, -405 ]     (sway DOF 1, yaw DOF 5)
               [ -405,  480 ]

M_RB_sway_yaw = [ 300,   0 ]     (m=300, Izz=600)
                [   0, 600 ]
```

### 3.2 显式 Fossen 方案（失败方案）

DART 求解（无 M_A 在空间惯量中）：
```
M_RB · ν̇_{n+1} = τ_other + (-M_A · ν̇_n)
ν̇_{n+1} = -(M_RB^{-1} · M_A) · ν̇_n + ...
```

放大矩阵特征值：
```
λ(M_RB^{-1}·M_A) = {1.9753, 0.0247}
```

**|1.9753| > 1 → 发散** ✓ 解释了"瞬间飞到无穷远"

### 3.3 空中抵消方案（最终方案）

DART 包含 M_A 在空间惯量中，同时插件施加抵消力：
```
(I_RB + I_A) · ν̇_{n+1} = τ_other + (+M_A · ν̇_n + C_A·ν)
ν̇_{n+1} = (I_RB + I_A)^{-1} · M_A · ν̇_n + ...
```

放大矩阵特征值：
```
λ((I_RB + I_A)^{-1}·M_A) = {0.6641, 0.0241}
```

**均 < 1 → 指数收敛** ✓

---

## 四、方案对比

| 方案 | 稳定性 | CoG 偏移 | 能否按需开关 | 结论 |
|------|:------:|:--------:|:----------:|:----:|
| 纯显式 `-M_A·ν̇` | ❌ λ=1.975 | ✅ | ✅ 可空中关 | **不可行** |
| 注入 DART（不做任何事） | ✅ 隐式 | ❌ 虚假旋转 | ❌ 无法关 | **不可行** |
| **注入 DART + 空中抵消** | ✅ λ=0.664 | ✅ 抵消后收敛 | ✅ 抵消可空中开 | **可行** |

---

## 五、分工

```
M_A 注入 DART <fluid_added_mass>
  └─ DART 空间惯量 I_total = I_RB + I_A
  └─ DART 隐式积分 — 无条件稳定

水中 (inWater && !aboveWater)：
  ┌─ DART 隐式处理 M_A·ν̇ + C_A(ν)·ν
  └─ 插件仅施加阻尼：τ = -D·ν

空中 (!inWater || aboveWater)：
  ┌─ DART 仍会计算 M_A·ν̇ + C_A(ν)·ν
  └─ 插件施加空中抵消：τ_cancel = +M_A·ν̇ + C_A(ν)·ν
  └─ 收敛后净效果 = 纯刚体动力学：M_RB·ν̇ + C_RB(ν)·ν = τ_gravity
```

---

## 六、关键 API

| API | 来源 | 用途 |
|-----|------|------|
| `Link::EnableAccelerationChecks(ecm, true)` | `<gz/sim/Link.hh>` | 启用加速度组件输出 |
| `WorldLinearAcceleration(ecm)` → `optional<Vector3d>` | 同上 | 世界系线加速度 |
| `WorldAngularAcceleration(ecm)` → `optional<Vector3d>` | 同上 | 世界系角加速度 |
| `<fluid_added_mass>` SDF 元素 | DART | 注入 6×6 M_A 到空间惯性 |

---

## 七、改动文件清单

| # | 文件 | 改动 |
|---|------|------|
| 1 | `program/notes/ma_dart_removal_fix.md` | 更新本方案文档 |
| 2 | `program/keship_issues_log.md` | 更新时间轴条目 |
| 3 | **`ModelManager.cc:2218-2269`** | **恢复** `InjectFluidAddedMass` 的 `<fluid_added_mass>` 注入 |
| 4 | **`FossenModel.hh`** | `computeHydroForces` → 水中仅阻尼；新增 `computeAirCorrection` |
| 5 | **`FossenModel.cc`** | `computeHydroForces` → 仅阻尼；`computeAirCorrection` → `+M_A·ν̇ + C_A·ν` |
| 6 | **`KSPlugin_HydroBuoyancy.cc:417-470`** | PreUpdate 分两路：水中→阻尼；空中→抵消 |

---

## 八、测试验证

```bash
cd ~/keship_ws && source /opt/ros/jazzy/setup.bash
colcon build --packages-select keship --merge-install
```

| 测试用例 | 预期 |
|---------|------|
| `neworigin_damp.ksm` + 无水区 | 自由落体近平滑（λ=0.664 → 数步收敛） |
| `neworigin_damp.ksm` + 有水区 | 正常浮于水面，无发散 |
| `origin_damp.ksm` + 无水区 | 自由落体平滑（回归测试） |

---

## 九、回退方案

如需完全恢复旧方案：
1. `ModelManager.cc`：保持 M_A 注入（不变）
2. `FossenModel.cc`：`computeHydroForces` 仅阻尼；`computeAirCorrection` 设为空
3. `KSPlugin_HydroBuoyancy.cc`：去掉空中抵消分支
4. 限制条件：CoG 必须在 link 原点
