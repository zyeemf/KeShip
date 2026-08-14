# Wave 代码阅读笔记

**日期**: 2026-06-16 | **目的**: 为水域重构做准备

---

## 1. 架构总览

水面系统由 4 层组成：

```
┌──────────────────────────────────────────────────────────┐
│  SDF 配置                                                 │
│  keship_world.sdf <include> → models/waves/model.sdf     │
│  含 WaveVisual 插件 + <wavefield> 参数                    │
└────────────────────┬─────────────────────────────────────┘
                     │
     ┌───────────────┴───────────────┐
     │  gz-transport topic:          │
     │  /keship/wavefield/parameters │
     │  消息类型: gz::msgs::Param    │
     └───────────────┬───────────────┘
                     │
     ┌───────────────┼───────────────┐
     ▼               │               ▼
┌──────────┐         │       ┌────────────────┐
│WaveVisual│         │       │KSPlugin_Buoyancy│
│渲染线程  │◄────────┘       │仿真线程         │
│          │                 │                 │
│· BFS找   │                 │· ComputeDepth   │
│  Visual  │                 │  Simply(x,y,z,t)│
│· 创建    │                 │· 3点采样→波浪面 │
│  Material│                 │· Catto浮力计算  │
│· 上传GPU │                 │                 │
│  uniforms│                 │                 │
└──────────┘                 └────────────────┘
```

### GPU vs CPU：为什么两边各算各的

Publisher 发布同一套参数（振幅、波数、频率、方向），GPU 和 CPU **各自独立计算**：

- **GPU (WaveVisual)**：vertex shader 对水面 mesh 每个顶点执行 `h = Σ aᵢ·cos(kᵢ·dot - ωᵢ·t)`，每秒百万次
- **CPU (Buoyancy)**：`ComputeDepthSimply(x,y,t)` 对船舶位置 3 个采样点执行同一公式，每秒 750 次（250Hz × 3）

GPU 计算结果存在显存中，**不能回读到 CPU** 使用——GPU→CPU 回读延迟在毫秒级，每物理步都读会彻底拖垮帧率。任何仿真系统都不会这么做。两边用同一套参数、同一公式、同一 t，数学结果完全一致。

### 关键发现

| 项目 | 当前状态 | 对多水域的影响 |
|------|---------|---------------|
| Wavefield 实例 | 全局唯一（一个 topic） | 需要每水域独立实例 |
| 波浪参数传播 | 通过 gz-transport topic 全局广播 | 需要 per-area topic 或消息路由 |
| 水面模型 | 单个静态模型 (6km×6km) | 需要多个小模型或 shader 支持区域 |
| 浮力基准面 | 单一 `fluidLevel_` + 全局波浪高度 | 需要 XY→水域查询 + 水域专属参数 |
| GPU shader | 单组 Gerstner 3 波分量 uniforms | 单 instance 多区域需改 shader |

---

## 2. Wavefield — 波浪参数生成

**文件**: `src_cpp/plugins_physics/Wavefield.hh/.cc`

### 类结构

```
Wavefield (public interface)
  └─ WavefieldPrivate (PIMPL, all data + logic)
       ├─ 输入参数: number, amplitude, period, direction, angle, scale, steepness, model, tau, gain
       ├─ 派生参数: angularFrequency, wavenumber, wavelength (deep water dispersion)
       └─ 分量向量: amplitudes[N], wavenumbers[N], angularFrequencies[N], 
                    phases[N], steepnesses[N], directions[N]
```

### 两种频谱模型

**PMS (Pierson-Moskowitz)** — 默认，`model="PMS"`：
```cpp
Pm(ω, ωP) = 0.0081 × g² / ω⁵ × exp(-1.25 × (ωP/ω)⁴)
// 3 个分量的 ω 按 geometric spacing 分布在 ωP 周围:
// ω₁ = ωP×(1-1/scale), ω₂ = ωP, ω₃ = ωP×scale
// 振幅: a = gain × √(2 × Pm(ω) × Δω)
```

**CWR (Constant Wavelength-Amplitude Ratio)** — `model="CWR"`：
```cpp
// 振幅按 scale^n 缩放，n ∈ {-1, 0, 1} (3 waves)
// ω 由深水色散关系反推: ω = √(g × k)
```

### 深水色散关系
```cpp
kGravity = 9.81  // ⚠ H1: 与 Buoyancy 中 9.81 一致（都是 9.81）
ω² = g × k       // DeepWaterDispersionToOmega
k  = ω² / g      // DeepWaterDispersionToWavenumber
```

### 两种深度计算模式

**ComputeDepthSimply**（O(N)，当前使用）：
```cpp
h = 0
for each wave i:
    θ = kᵢ × (x·dxᵢ + y·dyᵢ) - ωᵢ × t
    h += aᵢ × cos(θ)
return h × (1 - exp(-(t-t₀)/τ))  // 指数渐入
```
- 强制 steepness=0，无水平位移
- 与 GPU shader 一致（q=0 的简化版本）
- Buoyancy 用它做 3 点采样构建局部水面

**ComputeDepthDirectly**（Newton 迭代，备用）：
- 完整 Gerstner 波（含 q 参数）
- 需要 Newton 求解 2D 非线性系统以反算水平位移
- 当前**未被调用**（死代码路径）

### 两种工作模式

| 模式 | 参数来源 | SDF 示例 |
|------|---------|---------|
| Primary | SDF `<wavefield><wave>` 直接读取 | 含 size/cell_count/number/amplitude 等 |
| Secondary | 订阅 topic，接收 gz::msgs::Param | 仅含 `<topic>` |

当前配置：WaveVisual 是 **Secondary**（仅订阅 topic），但 KSPlugin_Buoyancy 也是 **Secondary**。那么谁发布？—— 查看代码发现 `KSPlugin_Buoyancy::LoadParameters()` 中如果 SDF 有 `<wavefield>` 且含 `<wave>`（primary 模式），则 Load 后会调用 `FillParameters()` 和 `Parameters()` 来发布。但当前 world 中的 buoyancy 插件 SDF 未配 wavefield（由 WaveVisual 端配置），所以实际数据流是：

**实际流程**：WaveVisual Configure 中 `wavefield.Load(_sdf)` 读取 SDF 参数（primary 模式），然后 WaveVisual 并不发布参数——它只订阅。KSPlugin_Buoyancy 也订阅同一 topic。**缺少发布者！**

等等，让我重新看。WaveVisual 的 SDF 中 wavefield 段包含 `<topic>/keship/wavefield/parameters</topic>` 但没有 `<wave>` 子段。所以 Wavefield.Load(SDF) 只读取了 topic 名称，wave 参数全用默认值（amplitude=0）。那波浪从哪来？

再看 KSPlugin_Buoyancy 的 LoadParameters：如果 SDF 有 `<wavefield>` 且含 `<wave>`，则 Load 为 primary 模式。但 Buoyancy 的 SDF 是由 ModelManager 动态生成的，可能不含 wavefield 段。

**实际运行时谁发布参数？** 查看 `keship_world.sdf` 没有单独的 wavefield publisher 插件。这意味着：
- 如果没外部 publisher，WaveVisual 用默认参数（amplitude=0）→ 水面是平的
- Buoyancy 也用默认参数 → wavefield_.Active() 为 false → 用 fluidLevel_ 直算

**结论**：当前系统中**没有自动运行的波浪参数发布者**。波浪参数要么通过外部 `gz topic` 命令发布，要么由实验对话框（如 SP-03 HullStabilityDialog）动态发布。SP-03 在打开对话框时发布 CWR 参数（静水），关闭时发布 PMS 参数。

✅ 这个发现很重要：**多水域方案中，每个水域的波浪参数发布需要明确设计**。

---

## 3. WaveVisual — GPU 波浪渲染

**文件**: `src_cpp/plugins_physics/WaveVisual.hh/.cc`

### 生命周期

```
Configure (sim thread, 启动时一次)
  ├─ 解析 SDF: shader 路径 + wavefield 参数
  ├─ wavefield.Load(_sdf) → 读取 SDF 参数
  ├─ 存储 entity ID + visualName
  ├─ 注册 SceneUpdate 回调 (渲染线程)
  ├─ 注册 RenderTeardown 回调
  └─ 订阅 /keship/wavefield/parameters

PreUpdate (sim thread, 每步)
  └─ 记录 simTime_ (mutex 保护)

OnUpdate (渲染线程, 每帧)
  ├─ 首次: 获取 Scene 指针
  ├─ 首次: FindVisual() — BFS 搜索场景图匹配 entity ID
  ├─ 首次: EnsureMaterial() — 创建材质 + 绑定 vertex/fragment shader
  ├─ 首次: UploadShaderParams() — 上传全部 shader uniforms
  └─ 每帧: UpdateTime() — 更新时间 uniform "t"

OnWavefield (transport 线程回调)
  ├─ wavefield.Load(msg) → 更新参数
  └─ paramsSet = false → 下次 OnUpdate 重新 upload
```

### FindVisual — 场景图 BFS

```cpp
// 从 RootVisual 开始 BFS
// 匹配条件: node->HasUserData("gazebo-entity") 
//           && UserData == this->entity
// 找到后存为 rendering::VisualPtr
```

**这是 plugins_physics 下的插件挂在 `<visual>` 上的原因**：场景图中的 `gazebo-entity` user data 存储的是 visual entity ID，不是 model 或 link ID。所以 WaveVisual 插件必须声明在 `<visual>` 内而不是 `<model>` 内。

### Shader uniforms

**Vertex Shader 参数**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `worldviewproj_matrix` | auto | MVP 矩阵（由 Ogre 自动绑定） |
| `rescale` | float | 法线缩放 |
| `bumpScale` | float[2] | 凹凸贴图缩放 |
| `bumpSpeed` | float[2] | 凹凸贴图移动速度 |
| `Nwaves` | int | 波分量数 (3) |
| `amplitude` | float[3] | 3 分量振幅 |
| `wavenumber` | float[3] | 3 分量波数 |
| `omega` | float[3] | 3 分量角频率 |
| `dir0/1/2` | float[2]×3 | 3 分量传播方向 |
| `steepness` | float[3] | 3 分量陡度 |
| `tau` | float | 渐入时间常数 |
| `t` | float | 仿真时间（每帧更新） |
| `camera_position_object_space` | auto | 相机位置 |

**Fragment Shader 参数**：
| 参数 | 类型 | 说明 |
|------|------|------|
| `hdrMultiplier` | float | HDR 亮度 |
| `fresnelPower` | float | Fresnel 反射强度 |
| `shallowColor` | float[4] | 浅水颜色 RGB |
| `deepColor` | float[4] | 深水颜色 RGB |
| `bumpMap` | texture2D | 法线贴图 (wave_normals.dds) |
| `cubeMap` | textureCube | 天空盒反射 (skybox_lowres.dds) |

### 对多水域的影响

**单 WaveVisual 实例同时只能渲染一组 shader 参数**。多水域方案有两个选择：

**方案 A — 多实例**：每片水域一个独立的 WaveVisual 插件 + 几何体
- 优点：每水域独立 shader 参数
- 缺点：多组 GPU draw calls，需要多个水面模型

**方案 B — 单实例 + shader 改造**：修改 GLSL shader 支持 XY 区域参数
- 在 vertex shader 中根据 `worldPos.xy` 判断属于哪个水域
- 但不同水域 baseHeight 不同 → mesh 顶点 Z 需要位移 → vertex shader 需要 baseHeight uniform
- 复杂度高，不推荐

**推荐方案 A**。

---

## 4. KSPlugin_Buoyancy — 浮力波浪耦合

**文件**: `src_cpp/plugins_physics/KSPlugin_Buoyancy.hh/.cc`

### 波浪相关成员

```cpp
class Implementation {
    Wavefield wavefield_;          // 独立的 Wavefield 实例（secondary mode）
    duration simTime_;             // 当前仿真时间
    duration simTimeInit_;         // 首次激活时间
    bool timeInitSet_ = false;     // 是否已记录初始时间
    double fluidLevel_ = 0.0;      // 静水面 Z 坐标
};
```

### 每步浮力计算流程 (PreUpdate)

```
1. 记录 simTime（mutex 保护）
2. 首次激活时记录 simTimeInit_
3. 对每个浮力体:
   a. 计算 bodyWorldPose = modelPose × bodyPoseOffset
   b. 在 3 个点采样波浪高度 (ds=1m):
      h0 = ComputeDepthSimply(c.x, c.y, t, t0)
      hx = ComputeDepthSimply(c.x+1, c.y, t, t0)
      hy = ComputeDepthSimply(c.x, c.y+1, t, t0)
   c. 计算波浪梯度: dhdx=(hx-h0)/1, dhdy=(hy-h0)/1
   d. 构造波浪面: waveNormal=(-dhdx,-dhdy,1).Normalized()
      waveOffset = h0 + fluidLevel_
   e. Plane<double> waterPlane(waveNormal, waveOffset)
   f. Catto 算法: body.buoyancy->calculateTotalVolumeContribution(pose, plane)
   g. 浮力 + 阻尼 + 恢复力矩
```

### 关键点

- **3 点采样间距固定 1m**：对缓变波浪足够，但对 steepness>0 的 Gerstner 波不够精确（此时应改用 ComputeDepthDirectly）
- **波浪面 = 静水面 + 局部波浪高度**：`waveOffset = h0 + fluidLevel_`，其中 `fluidLevel_` 来自 SDF 配置
- **每浮力体独立计算波浪面**：不同位置的水面倾斜方向不同 → 多体船各段受力更真实

### 对多水域的影响

需要在步骤 3b 之前插入水域判定：

```cpp
// 伪代码
WaterArea area = waterManager.Lookup(bodyWorldPose.Pos());
double baseZ = area.baseHeight;     // 替代 fluidLevel_
Wavefield& wf = area.wavefield;     // 水域专属参数
h0 = wf.ComputeDepthSimply(c, t, t0);
waveOffset = h0 + baseZ;
```

---

## 5. Waves 模型定义

**文件**: `models/waves/model.sdf`

```xml
<model name="waves">
  <static>true</static>          <!-- 无物理模拟 -->
  <link name="link">
    <visual name="wave_visual">
      <plugin filename="libWaveVisual.so" name="keship::WaveVisual">
        <shader> ... GerstnerWaves vertex/fragment shader </shader>
        <wavefield>
          <topic>/keship/wavefield/parameters</topic>
        </wavefield>
      </plugin>
      <geometry>
        <mesh><uri>meshes/waterlow.dae</uri></mesh>  <!-- 6km×6km -->
      </geometry>
      <visibility_flags>8</visibility_flags>  <!-- 相机 layer bit 3 -->
    </visual>
  </link>
</model>
```

**关键点**：
- `static=true` → 不参与物理碰撞/动力学
- `visibility_flags=8` → 仅在相机 layer bit 3 可见
- 单一 6km×6km 平面 mesh → 对于多水域需要多个小 mesh

---

## 6. 世界文件中的加载方式

**文件**: `worlds/keship_world.sdf` (lines 167-171)

```xml
<include>
  <name>Waves</name>
  <pose>0 0 0 0 0 0</pose>
  <uri>model://waves</uri>
</include>
```

**加载方式**：`<include>` 在 SDF world 加载时由 Gazebo Server 解析，调用 `Model::Load` → 创建 ECM entities → 触发插件 Configure。**不是** EntityFactory 动态创建，不在 ModelManager 管理范围。

---

## 7. 数据流全景

```
                    ┌─────────────────────┐
                    │ 外部 Publisher?       │
                    │ (SP-03 dialog 等)     │
                    │ gz topic -t          │
                    │ /keship/wavefield/   │
                    │ parameters           │
                    │ -m gz.msgs.Param     │
                    └──────────┬──────────┘
                               │ gz::transport::Node::Publish
                               ▼
              ┌────────────────────────────────┐
              │  /keship/wavefield/parameters  │
              │  (gz::msgs::Param: 含全部参数) │
              └────────────┬───────────────────┘
                           │ Subscribe
              ┌────────────┼────────────┐
              ▼            │            ▼
    ┌──────────────┐       │   ┌──────────────────┐
    │ WaveVisual   │       │   │ KSPlugin_Buoyancy│
    │ OnWavefield  │       │   │ OnWavefield      │
    │ Load(msg)    │       │   │ Load(msg)        │
    │ paramsSet=   │       │   │ Update component │
    │ false        │       │   │ vectors          │
    └──────┬───────┘       │   └────────┬─────────┘
           │                │            │
    ┌──────▼───────┐       │   ┌────────▼─────────┐
    │ OnUpdate     │       │   │ PreUpdate        │
    │ (渲染线程)   │       │   │ (仿真线程)       │
    │ UploadShader │       │   │ ComputeDepth     │
    │ Params → GPU │       │   │ Simply ×3 → 波浪面│
    │ UpdateTime   │       │   │ → Catto浮力      │
    └──────────────┘       │   └──────────────────┘
```

---

## 8. 重构关键清单

### 可直接复用的模块
- `Wavefield` 类 — 参数生成 + 深度计算，设计良好，**可原样用于多水域**（每水域实例化一个）
- `ComputeDepthSimply` — 高效 O(N) 波浪高度采样，多水域只需改用各水域自己的 Wavefield 实例
- gz-transport topic 模式 — 可扩展为 per-area topic

### 需要修改的模块
| 模块 | 改动 |
|------|------|
| WaveVisual | 支持多实例（每水域一个），或 shader 参数化多区域 |
| KSPlugin_Buoyancy | `fluidLevel_` → XY 水域查询；`wavefield_` → WaterArea→wavefield |
| Waves model SDF | 单个大 mesh → 多个小 mesh（每水域一个） |
| keship_world.sdf | 单个 `<include>` → 动态 EntityFactory 管理 |
| 波浪参数发布 | 新增 WaterAreaManager + per-area topic 或统一管理器 |

### 已知问题（已在审计中记录）
- H1: `Wavefield::kGravity=9.81` vs Buoyancy 中 `gravity_.Z()=-9.81` — 一致，无问题
- Wavefield 构造函数中 `Recalculate()` 在 3 分量 vector 为空时调用 → 会导致 Amplitude_V() 等返回空 vector → WaveVisual 的 `UploadShaderParams` 访问 `[0]`/`[1]`/`[2]` 可能越界
  - 但 Wavefield 默认 number=3，所以 vectors 有 3 个元素，无实际问题
  - 若改为 number!=3，WaveVisual 的硬编码 3 分量假设会崩溃
