# KeShip 物理设计文档

> 6-DOF Fossen 水动力学模型 + Wavefield–Buoyancy 集成方案

---

## 第一部分：6-DOF Fossen 水动力学模型

> 参考: Fossen, T. I. (2021). *Handbook of Marine Craft Hydrodynamics and Motion Control, 2nd Edition*. Wiley.

### 坐标系

| 坐标系 | 符号 | 说明 |
|--------|------|------|
| NED（世界系） | `{n}` | North-East-Down，x=北 y=东 z=下，与 Gazebo 世界系对齐 |
| BODY（船体系） | `{b}` | 原点在船中，x=艏向 y=右舷 z=下 |

Gazebo 世界系 Z 轴向上为正，但 Fossen 的 NED 是 Z 向下为正。**处理方式**：直接从 Gazebo 读取的 Pose/Velocity 保持 Z-up 约定，计算时在插件内部做符号转换，Fossen 方程的输出（NED 下的力/力矩）再转回 Z-up 写入 ECM。

### 状态变量

```
η = [x, y, z,  φ, θ, ψ]ᵀ    — NED 下的位置和欧拉角
ν = [u, v, w,  p, q, r]ᵀ    — BODY 下的线速度和角速度
```

- `u`: surge（纵荡），`v`: sway（横荡），`w`: heave（垂荡）
- `p`: roll rate，`q`: pitch rate，`r`: yaw rate

### 运动方程

```
M · ν̇ + C(ν) · ν + D(ν) · ν + g(η) = τ

M   = M_RB + M_A         刚体惯量 + 附加质量      (6×6)
C   = C_RB + C_A         刚体科氏力 + 附加科氏力    (6×6)
D   = D_l + D_n(ν)       线性阻尼 + 非线性阻尼      (6×6)
g   = g(η)               回复力（重力/浮力）       (6×1)
τ   = τ_thrust + τ_env   外部力（推进器+环境）     (6×1)
```

### 各项详细分解

#### 1. 刚体惯量矩阵 M_RB

假设船体关于 xz 平面对称（大多数船舶满足），且 BODY 原点在重心正上方：

```
         [ m    0    0    0   m·zg   0   ]
         [ 0    m    0  -m·zg   0    0   ]
M_RB =   [ 0    0    m    0     0    0   ]
         [ 0  -m·zg  0   Ixx   0   Ixz  ]
         [ m·zg 0    0    0    Iyy   0   ]
         [ 0    0    0   Ixz   0   Izz  ]
```

- `m`：总质量（从 Gazebo Inertial 组件读取）
- `zg`：重心在 BODY 系中的 z 坐标（zg > 0 表示重心在原点下方）
- `Ixx, Iyy, Izz, Ixz`：转动惯量和惯性积（从 Gazebo Inertial 读取）

#### 2. 附加质量矩阵 M_A

低频假设下，M_A 为常数对称正定矩阵。利用 xz 平面对称性：

```
         [ X_u̇   0     0     0     0     0   ]
         [ 0    Y_v̇   0     Y_ṗ   0     Y_ṙ  ]
M_A =    [ 0     0    Z_ẇ   0     Z_q̇   0   ]
         [ 0    K_v̇   0     K_ṗ   0     K_ṙ  ]
         [ 0     0    M_ẇ   0     M_q̇   0   ]
         [ 0    N_v̇   0     N_ṗ   0     N_ṙ  ]
```

共 12 个独立系数。这些系数可通过以下方式获取：
- **经验公式**：基于船体主尺度的半经验估算
- **势流理论**：WAMIT/AQWA 等面板法工具计算
- **CFD**：OpenFOAM 数值计算
- **系统辨识**：实船或模型试验数据拟合

**第一阶段**：使用经验公式，以船体 L/B/D 和排水量估算。

#### 3. 科氏力矩阵 C(ν) = C_RB(ν) + C_A(ν)

刚体科氏力 C_RB(ν) 由 M_RB 和 ν 导出，附加质量科氏力 C_A(ν) 由 M_A 和 ν 导出：

```
C_A = [  0    0    0      0      -a3    a2  ]
       [  0    0    0     a3      0    -a1  ]
       [  0    0    0    -a2     a1     0   ]
       [  0   -a3   a2    0     -b3    b2  ]
       [ a3    0   -a1   b3      0    -b1  ]
       [-a2   a1    0    -b2     b1     0   ]
```

其中 a = M_A(1:3,1:3)·ν₁ + M_A(1:3,4:6)·ν₂, b = M_A(4:6,1:3)·ν₁ + M_A(4:6,4:6)·ν₂

#### 4. 阻尼矩阵 D(ν) = D_l + D_n(ν)

**线性阻尼 D_l**（6×6 对角或三对角，低频主导）：

```
D_l = diag{ Xu, Yv, Zw, Kp, Mq, Nr }
      + 耦合项 Yr, Nv 等（首摇与横荡耦合）
```

**非线性阻尼 D_n(ν)**（高速时重要，二次阻尼）：

```
D_n(ν) = -diag{ X|u|u·|u|, Y|v|v·|v|, Z|w|w·|w|,
                 K|p|p·|p|, M|q|q·|q|, N|r|r·|r| }
```

**第一阶段只做线性阻尼**，系数可参考类似船型的经验数据。

#### 5. 回复力 g(η) — 不实现

由 `KSPlugin_Buoyancy` 基于网格精确计算，精度高于简化稳心公式。

#### 6. 外力 τ — 不实现

由 `KSPlugin_Thrust` 生成推进器力/力矩。

### 职责边界

```
                        ┌─────────────────────┐
                        │   HydrodynamicsSystem │
                        │                     │
KSPlugin_Buoyancy       │  M_A (附加质量)      │
┌──────────────┐        │  C(ν) (科氏力)       │
│ F_b = ρgV_sub│        │  D(ν) (耦合阻尼)     │
│ τ_restore    │        │                     │
│ 阻尼=0       │        └─────────┬───────────┘
└──────────────┘                  │
                                  │ 累加
KSPlugin_Thrust                   │
┌──────────────┐                  │
│ τ_thrust     │──────────────────┤
└──────────────┘                  │
                                  ▼
                    ExternalWorldWrenchCmd (ECM)
```

| 插件 | 职责 | 力/力矩 |
|------|------|---------|
| `KSPlugin_Buoyancy` | 浮力 + 恢复力矩（阻尼系数=0） | `F_b` + `τ_restore` |
| `HydrodynamicsSystem` | M_A、C(ν)、D(ν) | `τ_hydro = -M_A·ν̇ - C(ν)·ν - D(ν)·ν` |
| `KSPlugin_Thrust` | 推进器力 | `τ_thrust` |

**关键约定**：Fossen 激活时，SDF 中浮力插件设 `linear_drag_coef=0, angular_drag_coef=0`。

### PreUpdate 执行顺序

```
1. KSPlugin_Buoyancy::PreUpdate
   → 读取 model Pose → 计算每个 body subVol → F_b + τ_restore
   → 累加写入 ExternalWorldWrenchCmd

2. HydrodynamicsSystem::PreUpdate
   → 读取 model Pose / Link Velocity / Inertial
   → 组装 M_A, C(ν), D(ν)
   → ν̇ = M⁻¹ · [τ - C(ν)·ν - D(ν)·ν - g(η)]
   → τ_hydro = M_A·ν̇ + C(ν)·ν + D(ν)·ν
   → 累加写入 ExternalWorldWrenchCmd

3. KSPlugin_Thrust::PreUpdate
   → 读取 thrust_level → τ_thrust
   → 累加写入 ExternalWorldWrenchCmd
```

### 各力源施加方式

| 来源 | 力 | 力矩 | 实现 | 力矩参考点 |
|------|----|------|------|-----------|
| 重力 | `(0,0,-mg)` | 0 | DART 内建（作用于 CoG） | CoG |
| 浮力 | `F_b = (0,0,ρgV_sub)` | `τ_b = (r_CoB - r_CoG) × F_b` | `KSPlugin_Buoyancy` 通过 `ExternalWorldWrenchCmd` 施加 | CoG |
| 推进器力 | `τ_thrust` | 独立施加 | `KSPlugin_Thrust` | 推进器作用点 |
| 水动力 | `τ_hydro = -M_A·ν̇ - C_A·ν - D·ν` | 独立施加 | `HydrodynamicsSystem` | link 原点 |

**DART 的 `ExternalWorldWrenchCmd` 将力施加在 COM（重心）上**。因此浮力 `F_b` 被 DART 直接作用在 CoG 处（力臂为 0，不产生力矩），我们需要显式外加 `τ_b = (r_CoB - r_CoG) × F_b` 来补偿浮心偏移产生的恢复力矩。

**无重复计算**：
- DART 产生的重力力矩为 0（重力作用在 CoG）；浮力力矩为 0（ExternalWorldWrenchCmd 的力作用在 CoG）；我们加的 `τ_b` 恰好把所有力"等效搬移"到 CoB 处。
- `HydrodynamicsSystem` 和 `KSPlugin_Thrust` 各自计算自己的水动力/推力，与恢复力矩正交。

**2026-07-15 修复**：恢复力矩参考点从 `modelPose.Pos()`（link 原点）修正为 `InertialComponent::Pose().Pos()`（CoG 偏移），支持用户通过 `ModelSettingsDialog` 自定义 X/Y/Z 方向的重心偏移。

### Wrench 累加协议

### SDF 配置格式

```xml
<plugin name="keship::HydrodynamicsSystem"
        filename="libHydrodynamicsSystem.so">
  <link_name>base_link</link_name>

  <!-- 附加质量系数 M_A (12个独立值) -->
  <added_mass>
    <X_udot>0.05</X_udot>   <!-- 无量纲，实际 = 系数 × m -->
    <Y_vdot>0.8</Y_vdot>
    <Z_wdot>0.8</Z_wdot>
    <K_pdot>0.1</K_pdot>
    <M_qdot>0.5</M_qdot>
    <N_rdot>0.5</N_rdot>
    <Y_pdot>0.0</Y_pdot>
    <Y_rdot>-0.2</Y_rdot>
    <K_vdot>0.0</K_vdot>
    <K_rdot>0.0</K_rdot>
    <M_wdot>0.0</M_wdot>
    <N_vdot>-0.2</N_vdot>
  </added_mass>

  <!-- 线性阻尼系数 D_l (对角项 + 耦合项) -->
  <linear_damping>
    <Xu>0.1</Xu>
    <Yv>0.2</Yv>
    <Zw>0.3</Zw>
    <Kp>0.05</Kp>
    <Mq>0.1</Mq>
    <Nr>0.1</Nr>
    <Yr>0.0</Yr>
    <Nv>0.0</Nv>
  </linear_damping>
</plugin>
```

---

## 第二部分：物理公式速查

| 特性 | 公式 | 说明 |
|------|------|------|
| 浮力 | $F_b = \rho g V_{sub}$ | 阿基米德原理，方向 +Z 世界 |
| 浮心 | $\mathbf{c}_{CoB} = \frac{\sum V_i \cdot \mathbf{c}_i}{\sum V_i}$ | 浸没体积形心 |
| 恢复力矩 | $\boldsymbol{\tau} = (\mathbf{c}_{CoB}^{world} - \mathbf{p}_{link}) \times \mathbf{F}_b$ | CoB 偏离 link 原点产生力偶 |
| 线性阻尼 | $\mathbf{F}_d = -c_{lin} \cdot m_{partial} \cdot \mathbf{v}$ | VRX 公式，体积加权 |
| 角阻尼 | $\boldsymbol{\tau}_d = -c_{ang} \cdot m_{partial} \cdot \bar{L}^2 \cdot \boldsymbol{\omega}$ | 体积加权特征长度 |
| 推力 | $\mathbf{F}_t = level \cdot F_{max} \cdot \mathbf{\hat{x}}_{body}$ | 沿推进器 local +X |
| 推力矩 | $\boldsymbol{\tau}_t = (\mathbf{p}_{body} - \mathbf{p}_{link}) \times \mathbf{F}_t$ | 作用点力矩 |

### 竖立稳定性理论

对竖立圆柱体（密度比 $s = \rho_c/\rho_w$），稳心高度 $GM = \frac{(s-1)L}{2} + \frac{R^2}{4sL}$。

- **稳定条件**：$D/L > \sqrt{8s(1-s)}$，最大值出现在 $s=0.5$ 处，$D/L > 1.414$
- **当前参数 D=5m, L=20m**：GM ≈ -4.84 m，强烈不稳定 → 横躺天然稳定

---

## 第三部分：Wavefield–Buoyancy 集成

### 背景与问题

在集成之前，波浪系统存在三层脱节：

| 层 | 实现 | 水面高度 | 状态 |
|----|------|----------|------|
| GPU 渲染 | WaveVisual (Gerstner shader) | 动态波浪 | 正常 |
| 浮力计算 | KSPlugin_Buoyancy | 固定 `fluidLevel_` 常量 (0.0) | 死水面 |
| 高度计算 | Wavefield::ComputeDepthSimply | 正弦叠加 | **无人调用，死代码** |

船舶在视觉上能看到波浪，但浮力计算始终使用静止水面，船舶不会随波浪上下颠簸。

### 目标

让浮力计算的水面高度与 WaveVisual 渲染的波浪一致，使船舶能响应波浪产生真实的垂荡运动。

### Gerstner 波浪理论

Gerstner 波是有限振幅的旋转波，水质点轨迹为圆形（深水）或椭圆形（浅水）。当 steepness = 0 时退化为纯正弦叠加：

```
h(x, z, t) = Σ a_i · cos(kx_i·x + kz_i·z - ω_i·t) · E(t)
```

其中 `E(t) = 1 - exp(-(t-t0)/tau)` 为指数缓起因子，避免仿真启动时的数值冲击。

**关键结论：** 默认 steepness=0 时，`ComputeDepthSimply` 与 GPU shader 产生完全相同的瞬时高度。

### 浮力计算算法

```
Buoyancy::calculateTotalVolumeContribution(linkWorldPose, fluidLevel)
```

1. 在世界空间构建水平裁剪面 `Plane(normal=(0,0,1), offset=fluidLevel)`
2. 通过 link pose 的逆旋转变换，将裁剪面变换到 mesh 局部空间
3. 遍历三角形，按顶点相对于裁剪面的位置分类：完全浸没 / 完全出水 / 与水线面相交
4. 累加体积并计算加权浮心

```
F_buoyancy = ρ · g · V_submerged · (0, 0, 1)    // 世界 Z 向上
τ_buoyancy = (COB_world - link_pos) × F_buoyancy  // 恢复力矩
```

### Phase 1 架构：中心点波浪采样

```
Transport 线程 (OnWavefield)
    │  gz::msgs::Param (波浪参数)
    ▼
Wavefield (mutex 保护)
    │  读取 waveHeight
    │
Sim 线程 (PreUpdate)
    │  fluidLevel = fluidLevel_ + waveHeight
    ▼
Buoyancy::calculateTotalVolumeContribution(pose, fluidLevel)
    │
    ▼
ExternalWorldWrenchCmd → 物理引擎
```

#### 数据流

1. **WaveVisual 插件** 在启动时通过 transport 发布波浪参数到 `/keship/wavefield/parameters`
2. **KSPlugin_Buoyancy::Configure** 订阅同一 topic
3. **OnWavefield 回调**（transport 线程）加锁更新 Wavefield 参数
4. **PreUpdate**（sim 线程）每帧采样波浪高度并传入浮力计算

#### 线程安全

| 线程 | 操作 | 锁 |
|------|------|-----|
| Sim (PreUpdate) | 读取 wavefield_ 计算高度 | dataPtr->mutex |
| Transport (OnWavefield) | 写入 wavefield_ 参数 | dataPtr->mutex |

读写通过同一把 `dataPtr->mutex` 互斥。

#### 兼容性设计

- **无 `<wavefield>` 配置**：`wavefield_.Active()` 返回 false，`waveHeight` 保持 0.0，行为退化为固定 `fluidLevel_`（完全向后兼容）
- **单网格模式**：`LoadSingleMeshBuoyancy` 创建一个 `BuoyancyBody` 放入 `bodies`，PreUpdate 遍历逻辑不变
- **多浮力体模式**：每个 body 独立计算自己的世界坐标和波浪高度

### 性能分析

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| ComputeDepthSimply | O(N) | N=波数（通常 3），纯三角函数运算 |
| calculateTotalVolumeContribution | O(M) | M=三角形数，逐三角形裁剪计算 |
| OnWavefield | O(1) | 仅在波浪参数变更时触发 |

每帧每个浮力体仅调用一次 `ComputeDepthSimply`，开销可忽略不计。

### Phase 2 展望：多点采样 + 倾斜水面

Phase 1 对每个浮力体中心采一个点，水面始终为水平面。Phase 2 通过多点采样计算波浪梯度，构造倾斜裁剪面，使船舶能响应波浪坡度产生横摇和纵摇。

```cpp
h0 = waveHeight(center)
hx = waveHeight(center + (dx, 0, 0))
hy = waveHeight(center + (0, dy, 0))
dhdx = (hx - h0) / dx
dhdy = (hy - h0) / dy
waterPlane.normal = normalize(-dhdx, -dhdy, 1)
waterPlane.offset = h0 - dot(normal, center)
```

现有代码已支持非水平裁剪面——`Plane` 的 normal 可以是任意方向，且会被正确变换到局部空间。入口改动即可，内部裁剪逻辑无需修改。

### 涉及文件

| 文件 | Phase 1 | Phase 2 |
|------|---------|---------|
| `src_cpp/plugins_physics/KSPlugin_Buoyancy.hh` | 已修改 | — |
| `src_cpp/plugins_physics/KSPlugin_Buoyancy.cc` | 已修改 | 待修改 |
| `src_cpp/plugins_physics/Wavefield.cc` | 已编入 KSPlugin_Buoyancy | — |
| `CMakeLists.txt` | 已修改 | — |
| `src_cpp/utils/Buoyancy.hh` | — | 待修改 |
| `src_cpp/utils/Buoyancy.cc` | — | 待修改 |
| `models/keship_03/model.sdf` | 已激活 | — |
| `worlds/keship_world.sdf` | 已激活 | — |

### 验证方法

1. 编译通过
2. 启动仿真，Waves 模型渲染正常
3. 船舶 keship_03 显示在水面上
4. 目视确认船舶随波浪上下颠簸，颠簸相位与视觉波浪一致
5. 通过 `gz topic -t /keship/wavefield/parameters` 发送修改后的波浪参数，验证船舶颠簸响应变化
6. 回归测试：去掉 `<wavefield>` 配置时，浮力行为与修改前一致
