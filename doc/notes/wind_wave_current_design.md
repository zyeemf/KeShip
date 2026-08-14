# 风浪流环境设计文档

> 日期：2026-07-16
> 涵盖：波浪物理模型、Gerstner 波浪理论、GPU 渲染 (WaveVisual)、风/流参数、UI 编辑器

---

## 一、波浪物理模型

### 1.1 Gerstner 波浪叠加

PMS (Pierson-Moskowitz Spectrum) 模型将海面高度表示为 $N$ 个正弦分量的叠加。在 `steepness=0` 时退化为纯正弦波（PMS 模式），反之开启 Gerstner 尖峰波形：

$$
h(\mathbf{x}, t) = E(t) \cdot \sum_{i = 0}^{N-1} a_i \cdot \cos\!\big(\mathbf{k}_i \cdot \mathbf{x} - \omega_i t + \phi_i\big)
$$

| 符号 | 物理含义 | 单位 | 如何获得 |
|------|---------|------|---------|
| $h$ | 海面相对于静水面的瞬时高度 | m | — |
| $\mathbf{x} = (x, y)$ | 世界水平坐标 | m | 船舶当前位置 |
| $t$ | 仿真时间 | s | `_info.simTime` |
| $N$ | 叠加分量个数 | — | 用户设置 `number`，默认 3 |
| $a_i$ | 第 $i$ 个分量的振幅 | m | PMS 频谱采样导出（由 `period`、`gain` 控制） |
| $\mathbf{k}_i$ | 第 $i$ 个分量的波数矢量 | rad/m | 方向由 `direction`$+n \cdot$`angle` 决定；大小 $k_i = \omega_i^2/g$（深水色散） |
| $\omega_i$ | 第 $i$ 个分量的角频率 | rad/s | $\omega_i = \omega_p \cdot \text{scale}^{\,n}$，其中 $\omega_p = 2\pi/$`period` |
| $\phi_i$ | 第 $i$ 个分量的初始相位 | rad | $\phi_i = i \cdot 2\pi/N$（均匀分布，避免拍频） |
| $E(t)$ | 指数缓起包络 | — | $E(t) = 1 - e^{-(t-t_0)/\tau}$，$\tau$ 由 `tau` 设定，默认 2 s |
| $\tau$ | 缓起时间常数 | s | 用户设置 `tau`，默认 2.0 |

**分量导出规则**：$a_i$、$\omega_i$、$\mathbf{k}_i$、$\phi_i$ 并非用户直接设置，而是由少量输入参数（`period`、`number`、`scale`、`angle`、`direction`、`gain`）通过 PMS 频谱采样自动导出（详见 §1.3）。核心思想是让分量频率 $\omega_i$ 在谱峰频率 $\omega_p$ 附近按等比间距排列，振幅 $a_i$ 服从 PM 能量谱。

**缓起机制**：$E(t)$ 使波浪从零幅值指数增长至满幅，时间常数 $\tau$ 避免仿真启动瞬间的数值冲击——船只不会在 $t=0$ 时被突然出现的波浪弹飞。

### 1.2 色散关系（深水）

$$
\omega^2 = g k
\qquad
k = \frac{\omega^2}{g}
\qquad
g = 9.81 \ \text{m/s}^2
$$

### 1.3 PMS 频谱采样

从 PM 频谱采样 3 个等间距频率点（$\alpha = 0.0081$ 为 Phillips 常数）：

$$
S(\omega) = \frac{\alpha g^2}{\omega^5}
    \exp\!\left(-\frac{5}{4}\!\left(\frac{\omega_p}{\omega}\right)^{\!4}\right)
\qquad
\omega_p = \frac{2\pi}{T_p}
$$

$$
\omega_i = \omega_p \cdot \text{scale}^{\,n}, \quad n \in \{-1, 0, 1\}
$$

$$
a_i = \text{gain} \cdot \sqrt{2 \cdot S(\omega_i) \cdot \Delta\omega_i}
$$

### 1.4 波浪分量相位均匀分布（2026-07-16 修复）

原先所有分量共享同一相位，3 个相近频率产生深拍频，每隔 ~13s 波浪接近完全平坦。修复后分量相位均匀分布：

$$
\phi_i = \phi_0 + i \cdot \frac{2\pi}{\text{number}}, \quad i = 0, 1, 2
$$

永不同时为 0，波浪不再周期性消失。

---

## 二、波浪参数传播

### 2.1 架构图

```
世界 SDF ──→ WavefieldPublisher (全局系统插件)
                │ gz::msgs::Param
                │ /keship/wavefield/parameters
                ↓
         ┌──────┴──────┐
         ↓              ↓
    WaveVisual         KSPlugin_Buoyancy
    (GPU Gerstner)     (Catto 浮力响应)
```

### 2.2 WavefieldPublisher

- 世界级系统插件，启动时从 SDF 读取 `wavefield` 配置
- Publish `gz::msgs::Param`（所有波浪参数键值对）
- 启动后 5s 内每秒发布一次，确保延迟 spawn 的插件收到
- 5s 后每 30s 发布一次
- 外部参数更新（对话框通过 `_set` topic）：立即重发

### 2.3 Wavefield — 核心物理引擎

支持两种工作模式：
- **Primary**：从 SDF 读取参数，Publish 给外部
- **Secondary**：从 gz::msgs::Param 订阅参数

提供两个高度计算方法：

| 方法 | 使用场景 | 说明 |
|------|---------|------|
| `ComputeDepthSimply` | 浮力响应（KSPlugin_Buoyancy） | 正弦叠加（steepness=0），精确高效 |
| `ComputeDepthDirectly` | 精确求解 | Newton 迭代制（steepness>0 时），用于 Gerstner 波形 |

### 2.4 浮力集成（KSPlugin_Buoyancy::PreUpdate）

Phase 2 多点采样实现：

```
① 读取当前时间 t = simTime.count()
② 3 点采样:
   h0 = ComputeDepthSimply(center, t, t0)
   hx = ComputeDepthSimply(center + (1,0,0), t, t0)
   hy = ComputeDepthSimply(center + (0,1,0), t, t0)
③ 计算波浪梯度:
   dhdx = (hx - h0), dhdy = (hy - h0)
   normal = (-dhdx, -dhdy, 1).normalize()
④ 构建倾斜裁剪面:
   plane(normal, h0 + fluidLevel)
⑤ Catto 三角裁剪 → subVol → F_b, τ_b
```

线程安全：`mutex` 保护 wavefield 参数读写，transport 线程写 / sim 线程读。

无波浪活动的退路：`wavefield_.Active() == false` → `waveHeight = 0` → 水平面（与旧行为兼容）。

### 2.5 ApplyWaveParamsToWavefield

在 `KSPlugin_Buoyancy::PreUpdate` 中被调用于同步不同水域的波浪参数。当船舶从一个水区移动到另一个水区时（`area->name != currentAreaName_`），切换波浪配置：

```cpp
void ApplyWaveParamsToWavefield(const WaveParams &_wp, Wavefield &_wf)
{
  _wf.SetNumber(_wp.number);
  _wf.SetAngle(_wp.angle);
  _wf.SetScale(_wp.scale);
  _wf.SetSteepness(_wp.steepness);
  _wf.SetAmplitude(_wp.amplitude);
  _wf.SetPeriod(_wp.period);
  _wf.SetDirection(_wp.direction);
  _wf.SetTau(_wp.tau);
  _wf.SetGain(_wp.gain);
}
```

---

## 三、Gazebo 波浪渲染（WaveVisual）

### 3.1 原理

WaveVisual 是 per-Waves-Model 的 Gazebo 系统插件，在 GPU 上运行 Gerstner 着色器：

```
Configure:
  ① 加载 vertex/fragment shader (GLSL 330)
  ② 读取 wavefield SDF 参数
  ③ 搜索 visual 实体上挂载的子 visual
  ④ 绑定 shader 材质（纹理、uniform 参数）

PreUpdate:
  ① 每秒更新一次 shader uniform: 波数、振幅、方向
  ② uniform "simTime" 更新当前模拟时间
```

### 3.2 Shader 参数

| uniform | 来源 | 说明 |
|---------|------|------|
| waveNumber[] | `Wavefield::Wavenumber_V()` | 分量波数 |
| angularFrequency[] | `Wavefield::AngularFrequency_V()` | 分量角频率 |
| steepness[] | `Wavefield::Steepness_V()` | 分量陡度 |
| amplitude[] | `Wavefield::Amplitude_V()` | 分量振幅 |
| direction[] | `Wavefield::Direction_V()` | 分量波向 |
| simTime | `_info.simTime` | 模拟时间 |

### 3.3 与物理同步

WaveVisual 的 SDF 中嵌入了 `wavefield` 参数。在 `WaterAreaVisualManager::MakeSurfaceSdf()` 中从 `WaterArea::waveParams` 读取写入。每个水区独立配置。

**已知限制**：
- 波数上限 hardcoded 为 3（shader 内部循环 `for i=0 to 2`）
- uniform 更新间隔 1s（低频变化场景够用，高频变化需改为每帧）

---

## 四、风/流参数（数据结构）

### 4.1 WindParams

```cpp
struct WindParams {
    double speed = 0.0;       // m/s
    double direction = 0.0;   // rad, 0 = 世界 +Y (北向)
};
```

### 4.2 CurrentParams

```cpp
struct CurrentParams {
    double speed = 0.0;       // m/s (表层)
    double direction = 0.0;   // rad
};
```

### 4.3 WaterArea 扩展字段

```cpp
struct WaterArea {
    // ... 现有字段 ...
    WaveParams waveParams;
    WindParams windParams;
    CurrentParams currentParams;
    bool waveDirectionAuto = true;  // 浪向自动跟随风向
};
```

### 4.4 浪-风-流方向关系

| 关系 | 默认行为 | 可覆盖 |
|------|---------|--------|
| 波浪方向 → 跟随风向 | ✓（`waveDirectionAuto = true`） | 取消勾选后独立编辑 |
| 流向 | 完全独立，默认 0 | 始终独立 |

---

## 五、风/流物理模型 — 实现方案

> 方案设计：2026-07-16
> 状态：待实施

### 5.1 风力公式

$$
F_{\text{wind}} = \frac{1}{2} \, \rho_{\text{air}} \, C_d \, A_{\text{proj}} \, |\mathbf{V}_{\text{rel}}| \, \mathbf{V}_{\text{rel}}
$$

$$
\rho_{\text{air}} \approx 1.225 \ \text{kg/m}^3 \quad\text{(空气密度)}
\qquad
C_d \approx 1.2 \quad\text{(水上建筑阻力系数)}
$$
$$
A_{\text{proj}} = \text{水上迎风面积}
\qquad
\mathbf{V}_{\text{rel}} = \mathbf{V}_{\text{wind}} - \mathbf{V}_{\text{ship}}
$$

风力力矩：

$$
\boldsymbol{\tau}_{\text{wind}} = (\mathbf{r}_{\text{aero}} - \mathbf{r}_{\text{CoG}}) \times \mathbf{F}_{\text{wind}}
$$

| 符号 | 含义 | 来源 |
|------|------|------|
| $\rho_{\text{air}}$ | 空气密度 1.225 kg/m³ | 常数 |
| $C_d$ | 阻力系数 ~1.2 | 经验值，模型 SDF 可配置 |
| $A_{\text{proj}}$ | 迎风投影面积 | 模型 SDF 参数（需新增 `frontal_area` / `lateral_area`） |
| $\mathbf{V}_{\text{rel}}$ | 相对风速 | `WaterArea::windParams` − ship velocity |
| $\mathbf{r}_{\text{aero}}$ | 风压中心（水线以上几何中心） | 模型 SDF 参数（需新增 `aero_center`） |
| $\mathbf{r}_{\text{CoG}}$ | 重心位置 | ECM InertialComponent |

### 5.2 流力公式 — 速度偏移法

流力**不做独立力施加**，而是在水动力计算中偏移速度输入——因为流改变的是船与水之间的相对速度：

```cpp
// HydrodynamicsSystem::PreUpdate
① 读取 WaterArea::currentParams
② V_current_world = (speed × cos(dir), speed × sin(dir), 0)
③ V_current_body = R_wb⁻¹ × V_current_world
④ nu_rel = nu_ship - [V_current_body_x, V_current_body_y, 0, 0, 0, 0]
⑤ computeHydroForces(nu_rel, nuDot, tau)   // 用相对速度
```

**零改动 FossenModel**——只在 `HydrodynamicsSystem::PreUpdate` 中偏移速度输入。

### 5.3 风力实现位置

在 `HydrodynamicsSystem::PreUpdate` 水动力计算之后追加（共用已有的 ECM 累加路径）：

```
PreUpdate():
  ① 速度偏移（流）→ computeHydroForces(nu_rel, ...)
  ② 读取风参数 → 计算 F_wind, τ_wind → 累加入 ECM
```

### 5.4 SDF 参数（模型专属）

在 `HydrodynamicsSystem` 的 SDF 插件段新增 `<wind>` 子元素：

```xml
<plugin name="keship::HydrodynamicsSystem" filename="libHydrodynamicsSystem.so">
  <link_name>base_link</link_name>
  ...
  <wind>
    <frontal_area>8.0</frontal_area>     <!-- 迎风正面面积 m² -->
    <lateral_area>12.0</lateral_area>    <!-- 迎风侧面面积 m² -->
    <aero_center_x>0.0</aero_center_x>   <!-- 风压中心 X 偏移 m -->
    <aero_center_y>0.0</aero_center_y>   <!-- 风压中心 Y 偏移 m -->
    <aero_center_z>1.5</aero_center_z>   <!-- 风压中心在 link 上方高度 m -->
    <drag_coeff>1.2</drag_coeff>         <!-- 阻力系数 -->
  </wind>
</plugin>
```

### 5.5 风/流参数来源

`WaterArea::windParams` / `WaterArea::currentParams` 已存储在 JSON 文件中。`HydrodynamicsSystem` 在 `Configure()` 中读取 JSON 文件初始化，`PreUpdate` 中每 250 步轮询检查更新（与 `KSPlugin_Buoyancy` 相同的轮询机制）。

### 5.6 文件改动清单

| # | 文件 | 操作 | 内容 |
|---|------|------|------|
| 1 | `HydrodynamicsSystem.hh` | 修改 | 新增风力参数成员、风/流轮询计数器 |
| 2 | `HydrodynamicsSystem.cc` `Configure()` | 修改 | 解析 SDF `<wind>` 段 |
| 3 | `HydrodynamicsSystem.cc` `PreUpdate()` | 修改 | 流速度偏移 + 风力计算 + ECM 累加 |
| 4 | `ModelManager.cc` `MakeHydroPluginSDF()` | 修改 | 生成 `<wind>` SDF 段 |
| 5 | `HydrodynamicsEditorDialog.cc` | 二期 | 船体面积/风压中心编辑器（初期可硬编码默认值 0，风/流力学不生效） |

### 5.7 关键设计决策

| 决策 | 选项 | 选择 | 理由 |
|------|------|------|------|
| 风力计算位置 | 新插件 / HydrodynamicsSystem 追加 | **HydrodynamicsSystem 追加** | 共享 Inertial/Pose/Velocity 访问，减少重复代码 |
| 风参数来源 | JSON 轮询 / gz-transport / SDF | **JSON 轮询** | 与 Buoyancy 共用机制，已验证可靠 |
| 船体迎风面积 | 自动 mesh 计算 / 用户配置 | **用户配置**（SDF `<wind>`） | 自动计算需水面线检测，超出当前范围 |
| 流力实现 | 独立力 / 速度偏移 | **速度偏移** | 物理正确，零改动 FossenModel |
| 风压中心 | 自动计算 / 用户配置 | **用户配置**（SDF `<wind>`） | 同上 |

### 5.8 不涉及

- 新插件 — 风力计算量极小，不需独立插件
- FossenModel 修改 — 流力用速度偏移，模型透明
- EnvironmentParamsDialog 修改 — 风/流参数 UI 已就位
- WaterArea / WaterAreaSerializer 修改 — 数据结构已完备

---

## 六、UI — 风浪流参数编辑器

### 6.1 EnvironmentParamsDialog

```
┌─ 水域环境参数 — 默认水域 ──────────────────────────┐
│                                                      │
│  ┌── 风参数 ────────────────────────────────────┐   │
│  │ 风速: [ 0.0] m/s  风向: [ 0.0] °             │   │
│  │ [✓] 浪向跟随风向                               │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 波浪参数 (PMS) ────────────────────────────┐   │
│  │ 周期: [ 5.0] s     方向: [ 0.0] °             │   │
│  │ 波数: [   3]       扩散角: [  0.4] rad         │   │
│  │ 尺度: [ 1.1]       波高增益: [  1.0]           │   │
│  │ 陡度: [ 0.0]       缓起时间 τ: [  2.0] s      │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│  ┌── 流参数 ────────────────────────────────────┐   │
│  │ 流速: [ 0.0] m/s  流向: [ 0.0] °             │   │
│  └──────────────────────────────────────────────┘   │
│                                                      │
│              [应用] [取消]                            │
└──────────────────────────────────────────────────────┘
```

### 6.2 交互

| 控件 | 行为 |
|------|------|
| `[✓] 浪向跟随风向` | 勾选时波浪方向灰色只读，自动=风向；取消勾选后独立编辑 |
| 风速 spinbox | 变化时实时更新浪向（若跟随勾选） |
| 风向 spinbox | 同上 |

### 6.3 入口

- 水域列表 → "环境"按钮
- 默认水域（勾选后）→ 列表中有"默认水域"行，点"环境"

---

## 七、多水区参数管理

### 7.1 JSON 格式（持久化）

```json
[{
  "name": "默认水域",
  "xMin": -250, "xMax": 250, "yMin": -250, "yMax": 250, "baseHeight": 0,
  "vertices": [],
  "waveModel": "PMS", "wavePeriod": 5.0, "waveNumber": 3,
  "waveScale": 1.1, "waveGain": 1.0, "waveDirection": 0.0,
  "waveAngle": 0.4, "waveSteepness": 0.0, "waveAmplitude": 0.0, "waveTau": 2.0,
  "windSpeed": 5.0, "windDirection": 0.785,
  "currentSpeed": 0.3, "currentDirection": 1.57,
  "waveDirectionAuto": true
}]
```

### 7.2 序列化器

`WaterAreaSerializer::SerializeWaterAreas` / `DeserializeWaterAreas` 完整覆盖所有 `WaterArea` 字段。新字段向后兼容——旧 JSON 文件中缺失的 wind/current 字段默认解析为 0。

### 7.3 SDF 生成

`WaterAreaVisualManager::MakeSurfaceSdf` 从 `WaterArea::waveParams` 读取波浪参数嵌入 Waves 模型 SDF。风/流参数通过 JSON 文件传递给 `KSPlugin_Buoyancy` 的 `Configure()` 读取。

---

## 八、文件清单

| 文件 | 操作 | 说明 |
|------|------|------|
| `WaterArea.hh` | 修改 | 新增 `WindParams`/`CurrentParams` + `WaterArea` 扩展字段 |
| `WaterAreaSerializer.cc` | 修改 | 序列化/反序列化追加风/流字段 |
| `Wavefield.cc` | 修改 | 分量相位均匀分布（2026-07-16 修复） |
| `EnvironmentParamsDialog.hh/.cc` | 新建 | 风浪流参数编辑对话框 |
| `MenuPluginKeship.hh/.cc` | 修改 | `OnBtn_EditEnv` slot |
| `MenuPluginKeship.qml` | 修改 | 水域列表"环境"按钮 |
| `CMakeLists.txt` | 修改 | 新增 `EnvironmentParamsDialog.cc` |

### 相关专题文档

| 文档 | 内容 |
|------|------|
| `keship_physics_design.md` | 6-DOF Fossen 模型 + Wavefield–Buoyancy 集成（第一部分、第二部分保留归档；第三部分已合并到本文档） |
| `wavefield_buoyancy_integration.md` | Wavefield–Buoyancy Phase 1 实现细节（已归档到本文档） |
| `wave_code_reading.md` | Wave 代码阅读笔记 |
| `wave_optimization_notes.md` | Wave 性能优化记录（待实施） |

---

## 九、海况设置参考

以下表格供用户打开"环境"对话框时参考，所有参数均可根据实际场景微调。

### A. 按水体类型

| 场景 | gain | period (s) | scale | 视觉特征 |
|------|------|-----------|-------|---------|
| 静水（实验池） | 0 | 5.0 | 1.0 | 完全平坦，用于稳性实验 |
| 小型池塘 | 0.05~0.15 | 3~5 | 1.05 | 细微波纹，几乎不颠簸 |
| 内河/湖泊 | 0.2~0.4 | 3~5 | 1.08 | 可见波纹，轻微起伏 |
| 近岸/港湾 | 0.4~0.7 | 4~6 | 1.1 | 明显波浪，中幅颠簸 |
| 开阔沿海 | 0.7~1.0 | 5~8 | 1.1 | 大幅波浪，明显颠簸 |
| 远洋大风浪 | 1.0~2.0 | 6~12 | 1.15 | 剧烈波浪，大幅摇摆 |

### B. 按道格拉斯海况等级

| 海况等级 | 描述 | 有效波高 $H_s$ (m) | 建议 gain | 建议 period (s) |
|---------|------|-----------------|----------|---------------|
| 0 | 完全平静（镜面） | 0 | 0 | — |
| 1 | 微浪 | 0~0.1 | 0.05~0.1 | 3~5 |
| 2 | 小浪 | 0.1~0.5 | 0.1~0.3 | 3~5 |
| 3 | 中浪 | 0.5~1.25 | 0.3~0.5 | 4~6 |
| 4 | 大浪 | 1.25~2.5 | 0.5~0.8 | 5~8 |
| 5 | 强浪 | 2.5~4.0 | 0.8~1.2 | 6~10 |
| 6 | 巨浪 | 4.0~6.0 | 1.2~1.8 | 8~12 |

### C. 波浪方向与风/流配合

| 教学场景 | 风向 (°) | 浪向跟随 | 流向 (°) | 说明 |
|---------|---------|---------|---------|------|
| 静止水面 | — | — | — | gain=0，关闭风浪流 |
| 顺风顺浪 | 0 | ✓ | 0 | 风浪流同向，阻力最小 |
| 顶风逆浪 | 180 | ✓ | 180 | 船舶阻力最大 |
| 横风横浪 | 90 | ✓ | 90 | 横摇明显，稳性试验用 |
| 风浪同向、流反向 | 0 | ✓ | 180 | 波浪陡化（风逆流） |
| 无风涌浪 | — | 取消跟随 | — | 远洋涌浪传播，用于 RAO 测定 |

### D. 调节提示

- **波浪陡峭程度**：`scale` 越大 → 分量频率差异大 → 拍频浅、波面更不规则；`number` 越大 → 叠加更稠密、波面更碎
- **波浪方向**：默认跟随风向（`waveDirectionAuto = true`），教学场景保持勾选；研究涌浪传播时可取消跟随并手动设向
- **缓起时间** $\tau$：仅在仿真启动时生效——值越大波浪越慢达到满幅，建议保持默认 2 s
- **陡度** `steepness`：保持 0 即纯正弦波（PMS），非零时会开启 Gerstner 尖峰波形，视觉效果更尖锐但计算需要 Newton 迭代
