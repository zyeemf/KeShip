# 多片水域方案设计

**状态**：方案修订 | **日期**：2026-06-17 | **优先级**：中

---

## 需求变更

2026-06-17 重新确定场景需求，与原设计有本质差异。

### 原需求（已废弃）
- 默认整个场景是水（全球覆盖 6km×6km 水面）
- TOML 配置文件预定义多片水域
- 每片水域独立高度和波浪参数
- 水域之间物理联通

### 新需求
- **初始状态无水域**——场景默认是陆地
- **水域随地图加载**——加载一个校园/场景地图后，地图中定义水区（池塘、河流等）
- **水区局部存在**——只有水区范围内有水面和浮力，范围外是陆地/建筑
- **每个水区独立参数**——高度、波浪参数各不相同
- **视角进入水下可见**——水区范围内可以潜到水面以下

---

## 核心架构

### 整体数据流

```
地图模型加载
  └─→ 解析水区定义
       ├─→ 注册到 WaterAreaManager（浮力查找）
       └─→ 为每个水区生成独立 Waves 模型（视觉渲染）

船舶运动
  └─→ KSPlugin_Buoyancy::PreUpdate
       └─→ Lookup(x,y) 查找所属水区
            ├─ 在水区内 → 使用该区 baseHeight + waveHeight 计算浮力
            └─ 不在水区 → 跳过浮力（陆地上）
```

### 与当前实现的差异

| 方面 | 当前状态 | 未来状态 |
|------|---------|---------|
| 初始水域 | `showWater_=true`，启动即有一个全局 Waves | `showWater_=false`，启动无水 |
| 水面模型 | 1 个 6km×6km 全球 Waves | 每个水区 1 个小平面 Waves |
| 浮力基准 | 全局 `fluidLevel_=0`，所有位置有浮力 | 按水区查找，水区外无浮力 |
| 水区来源 | TOML 独立配置文件 | 随地图模型加载 |
| WaterAreaManager | 已编译但未被调用 | KSPlugin_Buoyancy 核心依赖 |

### WaterArea 数据结构（不变）

```
WaterArea {
    name: string
    xMin, xMax, yMin, yMax: double   // XY 矩形范围
    baseHeight: double                 // 静水面 Z 坐标
    waveParams: WaveParams             // 波浪参数
}
```

---

## 关键技术决策

### 水面几何生成
每个水区需要一个独立的水面几何体来挂载 WaveVisual。

| 方案 | 描述 | 结论 |
|------|------|------|
| Box 几何体 | 扁长方体，scale 匹配水区 XY 范围，Z 极薄 | **采用**——最简单，可复用 PrimitiveMeshGenerator |
| 程序化平面网格 | 自定义顶点生成 | 不必要，Box 已满足需求 |
| 裁剪全局 mesh | 从 6km 大平面裁剪 | 复杂，不支持 |

### 浮力切换
船舶从陆地区域进入水区时，浮力从 0 到有需要平滑过渡。

| 方案 | 描述 | 结论 |
|------|------|------|
| 距离渐变 | 边界附近按距离线性过渡 | 简单有效 |
| 硬切换 | 进入水区瞬间浮力生效 | 可能导致颠簸 |

建议先用硬切换，如有问题再加渐变。

### 多 WaveVisual 实例
每个水区的 Waves 模型有独立的 WaveVisual 插件和 Wavefield 参数。当前 WaveVisual 已支持多实例（每个实例独立订阅 `/keship/wavefield/parameters`），但需要改为每个实例订阅该水区专用的 topic（如 `/keship/wavefield/pond/parameters`），或改为每个水区有自己的 WavefieldPublisher。

**方案**：每个水区的 Waves 模型 SDF 自带完整 `<wavefield>` 段，不依赖全局 WavefieldPublisher。WavefieldPublisher 仅作为全局默认保留。

---

## 运行时行为

### 加载校园地图
```
加载 campus_map.sdf
  ├─ 模型 visual: 地面、建筑
  └─ 水区定义: pond (x=-50..30, y=30..90, h=2.0)
                river (x=0..200, y=-10..10, h=0.5)

系统自动:
  ├─ WaterAreaManager 注册 pond + river
  ├─ EntityFactory 创建 2 个 Waves 模型:
  │   ├─ "waves_pond"   — 80m×60m 平面，Z=2.0
  │   └─ "waves_river"  — 200m×20m 平面，Z=0.5
  └─ KSPlugin_Buoyancy 读取 Manager，每帧查找船舶位置
```

### 船舶在陆地上
- `Lookup(x,y)` → `nullptr`
- Buoyancy 跳过浮力计算 → 船舶无浮力支撑 → 如果无其他支撑则掉落

### 船舶驶入池塘
- `Lookup(x,y)` → 返回 `pond` 水区
- `fluidLevel = pond.baseHeight + waveHeight`
- 浮力生效 → 船舶浮在水面

### 视角进入水下
- 水区 WaveVisual 的透明水面允许视线穿透
- Gazebo 相机可以移动到水面以下
- 水下可见地形/建筑（如果地图有水下部分）

---

## 依赖关系

```
WaterArea 数据结构 ✅ 已完成
  └─→ WaterAreaManager ✅ 已完成
       ├─→ Step 1: KSPlugin_Buoyancy 集成 ← 下一步
       ├─→ Step 2: 动态水区 Waves 模型生成
       ├─→ Step 3: 地图模型驱动水区定义
       └─→ Step 4: GUI 水区编辑器
```

---

## 当前状态

| 组件 | 状态 |
|------|------|
| `WaterArea.hh` | ✅ 完成 |
| `WaterAreaManager.hh/.cc` | ✅ 完成（编译进 keship_utils，未被调用） |
| `keship_water_areas_example.toml` | ✅ 完成 |
| KSPlugin_Buoyancy 集成 | ❌ 待实施 |
| 动态水区 Waves 模型 | ❌ 待实施 |
| 地图驱动水区定义 | ❌ 待实施 |

---

## 变更记录

| 日期 | 变更 |
|------|------|
| 2026-06-16 | 初始方案：多片相邻水域，TOML 预定义，全局覆盖 |
| 2026-06-17 | 方案修订：场景驱动，默认无水，水域随地图加载，水区外无浮力 |
