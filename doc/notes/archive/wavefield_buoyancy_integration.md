# Wavefield–Buoyancy 集成方案与算法文档

> KeShip 项目 | 2026-05-25 | ZhangYu

---

## 1. 背景与问题

### 1.1 三层脱节

在集成之前，波浪系统存在三层脱节：

| 层 | 实现 | 水面高度 | 状态 |
|----|------|----------|------|
| GPU 渲染 | WaveVisual (Gerstner shader) | 动态波浪 | 正常 |
| 浮力计算 | KSPlugin_Buoyancy | 固定 `fluidLevel_` 常量 (0.0) | 死水面 |
| 高度计算 | Wavefield::ComputeDepthSimply | 正弦叠加 | **无人调用，死代码** |

船舶在视觉上能看到波浪，但浮力计算始终使用静止水面，船舶不会随波浪上下颠簸。

### 1.2 目标

让浮力计算的水面高度与 WaveVisual 渲染的波浪一致，使船舶能响应波浪产生真实的垂荡运动。

---

## 2. 波浪数学模型

### 2.1 Gerstner 波浪理论

Gerstner 波是有限振幅的旋转波，水质点轨迹为圆形（深水）或椭圆形（浅水）。其自由表面方程为参数形式：

```
x(u,t) = u - Σ q·a_i/k_i · cos(θ_i) · sin(k_i·u - ω_i·t)
y(u,t) = u - Σ q·a_i/k_i · sin(θ_i) · sin(k_i·u - ω_i·t)
z(u,t) =     Σ a_i · cos(k_i·u - ω_i·t)
```

其中：
- `a_i` — 第 i 个波的振幅
- `k_i` — 波数向量（方向 + 波长），`|k_i| = 2π/λ_i`
- `ω_i` — 角频率，满足深水色散关系 `ω_i² = g·|k_i|`
- `θ_i` — 波传播方向角
- `q` — steepness 参数（0 = 纯正弦，1 = 完整 Gerstner）

### 2.2 ComputeDepthSimply — 纯正弦叠加（q = 0）

当 steepness = 0（即 `q = 0`）时，水平位移消失，自由表面退化为标量高度场：

```
h(x, z, t) = Σ a_i · cos(kx_i·x + kz_i·z - ω_i·t) · E(t)
```

其中 `E(t) = 1 - exp(-(t-t0)/tau)` 为指数缓起因子，避免仿真启动时的数值冲击。

**关键结论：** 默认 steepness=0 时，`ComputeDepthSimply` 与 GPU shader 产生完全相同的瞬时高度。

### 2.3 ComputeDepthDirectly — 含水平位移（q > 0）

当 steepness > 0 时，水平位移使评估点偏移，问题变为：

```
给定世界坐标 (X, Z)，求满足 x(u,t) = X 的 u，再代入 z(u,t) 得到高度
```

这是一个非线性求根问题，通过 Newton 迭代求解。对于通常的 `steepness=0` 配置不需要此方法。

### 2.4 GPU Shader 与 ComputeDepthSimply 一致性验证

shader 中顶点着色器 (`GerstnerWaves_vs_330.glsl`)：

```glsl
float s = sin(dot(D.xz * k, worldPos.xz) * k - w * t);
float c = cos(dot(D.xz * k, worldPos.xz) * k - w * t);
P.y += a * c;                     // 高度 = a·cos(...)
P.xz -= D.xz * a * s * steepness; // 水平位移（steepness=0 时此项为 0）
```

ComputeDepthSimply 计算：

```cpp
double d = k.x() * x + k.z() * z;
h += a * cos(d - omega * t);
```

两者在 `steepness=0` 时精确一致。Phase 1 采用 `ComputeDepthSimply`。

---

## 3. 浮力计算算法

### 3.1 总体流程

```
Buoyancy::calculateTotalVolumeContribution(linkWorldPose, fluidLevel)
```

1. 在世界空间构建水平裁剪面 `Plane(normal=(0,0,1), offset=fluidLevel)`
2. 通过 link pose 的逆旋转变换，将裁剪面变换到 mesh 局部空间
3. 遍历三角形，按顶点相对于裁剪面的位置分类：
   - **完全浸没**（三点均在平面以下）→ 计入全部体积
   - **完全出水**（三点均在平面以上）→ 跳过
   - **与水线面相交**（部分浸没）→ 线性插值求边-面交点，构建水线面多边形
4. 累加体积并计算加权浮心

### 3.2 三角形分类与裁剪

对每个三角形，计算三个顶点到裁剪面的有符号距离 `d_i = dot(normal, v_i) - offset`：

```
全部 di < 0 → 完全浸没，贡献 = 三角形体积（带符号）
全部 di > 0 → 完全出水，贡献 = 0
混合符号  → 相交，求多边形交点，分解为子三角形计算
```

### 3.3 浮力与恢复力矩

```
F_buoyancy = ρ · g · V_submerged · (0, 0, 1)    // 世界 Z 向上
τ_buoyancy = (COB_world - link_pos) × F_buoyancy  // 恢复力矩
```

浮力方向始终为世界 Z 轴正向（向上）。浮心 (COB) 通过体积加权平均计算，与 link 位置的偏移产生恢复力矩——这是船舶稳住不翻的机制。

### 3.4 粘性阻尼

```cpp
F_drag = -linear_drag_coef · mass · (V_sub/V_total) · v_linear
τ_drag = -angular_drag_coef · mass · (V_sub/V_total) · L²_char · ω_angular
```

体积加权：部分浸没时阻尼按比例缩放，避免船飞起时仍然全阻尼。特征长度 `L²_char = dx²+dy²+dz²` 从网格 AABB 计算。

---

## 4. Phase 1 实现：中心点波浪采样

### 4.1 架构

```
Transport 线程 (OnWavefield)
    │
    │ gz::msgs::Param (波浪参数)
    ▼
Wavefield (mutex 保护)
    │
    │ 读取 waveHeight
    │
Sim 线程 (PreUpdate) ──────────────────────────────►
    │
    │ fluidLevel = fluidLevel_ + waveHeight
    ▼
Buoyancy::calculateTotalVolumeContribution(pose, fluidLevel)
    │
    ▼
ExternalWorldWrenchCmd → 物理引擎
```

### 4.2 数据流

1. **WaveVisual 插件** 在启动时通过 transport 发布波浪参数到 `/keship/wavefield/parameters`
2. **KSPlugin_Buoyancy::Configure** 订阅同一 topic
3. **OnWavefield 回调**（transport 线程）加锁更新 Wavefield 参数
4. **PreUpdate**（sim 线程）每帧：
   - 首次调用时记录 `simTimeInit_`（仿真起始时间）
   - 更新 `simTime_`
   - 遍历每个浮力体，计算其世界坐标
   - 调用 `ComputeDepthSimply(worldPos, simTime, simTimeInit)` 获取该点的波浪高度
   - `fluidLevel = fluidLevel_ + waveHeight`
   - 传入 `calculateTotalVolumeContribution` 计算浮力

### 4.3 关键代码路径

#### SDF 配置

```xml
<plugin filename="libKSPlugin_Buoyancy.so" name="keship::KSPlugin_Buoyancy">
  <fluid_density>1000</fluid_density>
  <fluid_level>0.0</fluid_level>
  <linear_drag_coef>25.0</linear_drag_coef>
  <angular_drag_coef>2.0</angular_drag_coef>
  <link_name>base_link</link_name>
  <wavefield>
    <topic>/keship/wavefield/parameters</topic>
  </wavefield>
</plugin>
```

#### KSPlugin_Buoyancy::Configure (KSPlugin_Buoyancy.cc:33-92)

- 调用 `LoadParameters(_sdf)` 解析 SDF 参数（含 Wavefield）
- 若 `wavefield_.Topic()` 非空，订阅 transport topic
- 分流进入 LoadMultiBuoyancy 或 LoadSingleMeshBuoyancy

#### LoadParameters (KSPlugin_Buoyancy.cc:230-264)

```cpp
if (_sdf->HasElement("wavefield"))
    this->dataPtr->wavefield_.Load(_sdf);
```

#### PreUpdate 时间初始化 (KSPlugin_Buoyancy.cc:129-138)

```cpp
{
    std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
    if (this->dataPtr->wavefield_.Active() && !this->dataPtr->timeInitSet_) {
        this->dataPtr->simTimeInit_ = _info.simTime;
        this->dataPtr->timeInitSet_ = true;
    }
    this->dataPtr->simTime_ = _info.simTime;
}
```

#### PreUpdate 波浪高度采样 (KSPlugin_Buoyancy.cc:147-158)

```cpp
double waveHeight = 0.0;
{
    std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
    if (this->dataPtr->wavefield_.Active()) {
        waveHeight = this->dataPtr->wavefield_.ComputeDepthSimply(
            bodyWorldPose.Pos(),
            std::chrono::duration<double>(this->dataPtr->simTime_).count(),
            std::chrono::duration<double>(this->dataPtr->simTimeInit_).count());
    }
}
double fluidLevel = this->dataPtr->fluidLevel_ + waveHeight;
```

#### OnWavefield 回调 (KSPlugin_Buoyancy.cc:474-478)

```cpp
void Implementation::OnWavefield(const gz::msgs::Param &_msg) {
    std::lock_guard<std::mutex> lock(this->mutex);
    this->wavefield_.Load(_msg);
}
```

### 4.4 线程安全

| 线程 | 操作 | 锁 |
|------|------|-----|
| Sim (PreUpdate) | 读取 wavefield_ 计算高度 | dataPtr->mutex |
| Transport (OnWavefield) | 写入 wavefield_ 参数 | dataPtr->mutex |

读写通过同一把 `dataPtr->mutex` 互斥，保证 Wavefield 在计算过程中不会被中途修改。模式与 WaveVisual 完全一致。

### 4.5 时间处理

- `simTimeInit_`：首次进入 PreUpdate 且 Wavefield 活跃时记录，作为 `t=0` 参考点
- `simTime_`：每帧更新，反映当前仿真时间
- 使用 `std::chrono::duration<double>` 直接输出秒数，避免手动 `count() * 1e-9` 转换
- 缓起因子 `1 - exp(-(t-t0)/tau)` 依赖正确的 `t0`，确保仿真从不活跃→活跃时波浪平滑启动

### 4.6 兼容性设计

- **无 `<wavefield>` 配置**：`wavefield_.Active()` 返回 false，`waveHeight` 保持 0.0，行为退化为固定 `fluidLevel_`（完全向后兼容）
- **单网格模式**：`LoadSingleMeshBuoyancy` 创建一个 `BuoyancyBody` 放入 `bodies`，PreUpdate 遍历逻辑不变
- **多浮力体模式**：每个 body 独立计算自己的世界坐标和波浪高度

---

## 5. 性能分析

| 操作 | 复杂度 | 说明 |
|------|--------|------|
| ComputeDepthSimply | O(N) | N=波数（通常 3），纯三角函数运算 |
| calculateTotalVolumeContribution | O(M) | M=三角形数，逐三角形裁剪计算 |
| OnWavefield | O(1) | 仅在波浪参数变更时触发 |

每帧每个浮力体仅调用一次 `ComputeDepthSimply`，开销可忽略不计。

---

## 6. Phase 2 展望：多点采样 + 倾斜水面

### 6.1 原理

Phase 1 对每个浮力体中心采一个点，水面始终为水平面。Phase 2 通过多点采样计算波浪梯度，构造倾斜裁剪面，使船舶能响应波浪坡度产生横摇和纵摇。

### 6.2 多点采样梯度计算

```cpp
h0 = waveHeight(center)
hx = waveHeight(center + (dx, 0, 0))
hy = waveHeight(center + (0, dy, 0))
dhdx = (hx - h0) / dx
dhdy = (hy - h0) / dy
waterPlane.normal = normalize(-dhdx, -dhdy, 1)
waterPlane.offset = h0 - dot(normal, center)
```

### 6.3 裁剪面扩展

Buoyancy 类新增 `Planed` 重载：

```cpp
double calculateTotalVolumeContribution(
    const gz::math::Pose3d &_linkPose,
    const gz::math::Planed &_waterPlane);
```

现有代码已支持非水平裁剪面——`Plane` 的 normal 可以是任意方向，且会被正确变换到局部空间。入口改动即可，内部裁剪逻辑无需修改。

### 6.4 涉及文件

| 文件 | 修改 |
|------|------|
| `utils/Buoyancy.hh` | 新增 Planed 重载声明 |
| `utils/Buoyancy.cc` | 实现 Planed 重载 |
| `plugins_physics/KSPlugin_Buoyancy.cc` | PreUpdate 中多点采样 + 构造倾斜面 |

---

## 7. 涉及文件汇总

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

---

## 8. 验证方法

1. 编译通过
2. 启动仿真，Waves 模型渲染正常
3. 船舶 keship_03 显示在水面上
4. 目视确认船舶随波浪上下颠簸，颠簸相位与视觉波浪一致
5. 通过 `gz topic -t /keship/wavefield/parameters` 发送修改后的波浪参数，验证船舶颠簸响应变化
6. 回归测试：去掉 `<wavefield>` 配置时，浮力行为与修改前一致
