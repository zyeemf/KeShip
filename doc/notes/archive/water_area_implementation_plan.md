# 多水区场景实施方案

**日期**：2026-06-17 | **依赖**：[multi_water_areas_design.md](multi_water_areas_design.md)

---

## 总览

实现场景驱动的水区系统：初始无水 → 加载地图后自动创建水区 → 船舶只在进入水区时有浮力。

分为 4 个步骤，每步独立可测。

---

## Step 1：KSPlugin_Buoyancy 集成 WaterAreaManager

**目标**：浮力计算不再使用全局 `fluidLevel_`，改为按船舶位置查找对应水区。

### 当前状态

```cpp
// KSPlugin_Buoyancy.hh — Implementation
double fluidLevel_ = 0.0;           // 全局单一基准面
Wavefield wavefield_;               // 全局波浪
gz::transport::Node node;           // 订阅全局 /keship/wavefield/parameters
```

```cpp
// KSPlugin_Buoyancy.cc — PreUpdate
double waveOffset = h0 + this->dataPtr->fluidLevel_;  // 固定基准面 0 + 波浪
```

### 改造方案

#### 1.1 数据结构变更

`KSPlugin_Buoyancy.hh` → `Implementation` 类：

```cpp
// 新增
#include "keship/utils/WaterArea.hh"
#include "../utils/WaterAreaManager.hh"

class KSPlugin_Buoyancy::Implementation
{
  // ... 保留现有 ...
  
  // 新增：多水区支持
  WaterAreaManager waterAreaMgr_;
  bool useWaterAreas_ = false;          // 是否启用多水区模式
  const WaterArea* currentArea_ = nullptr;  // 当前所在水区（每帧更新）
};
```

#### 1.2 Configure 阶段

```cpp
void KSPlugin_Buoyancy::Configure(...)
{
  // ... 现有逻辑 ...
  
  // 读取 SDF 中的水区配置路径
  if (_sdf->HasElement("water_areas_config"))
  {
    std::string configPath = _sdf->Get<std::string>("water_areas_config");
    if (this->dataPtr->waterAreaMgr_.LoadFromToml(configPath))
    {
      this->dataPtr->useWaterAreas_ = true;
      gzmsg << "KSPlugin_Buoyancy: 加载 "
            << this->dataPtr->waterAreaMgr_.Count()
            << " 片水区" << std::endl;
    }
  }
}
```

#### 1.3 PreUpdate 改造

```cpp
void KSPlugin_Buoyancy::PreUpdate(...)
{
  // ... 获取 modelPose, linkWorldPos, totalMass ...
  
  const WaterArea* currentArea = nullptr;
  double effectiveFluidLevel = this->dataPtr->fluidLevel_;
  
  if (this->dataPtr->useWaterAreas_)
  {
    currentArea = this->dataPtr->waterAreaMgr_.Lookup(
        linkWorldPos.X(), linkWorldPos.Y());
    if (!currentArea)
    {
      // 船舶不在任何水区 → 陆地 → 跳过浮力
      // 仅保留粘性阻力
      return;  // 或继续但不加浮力
    }
    effectiveFluidLevel = currentArea->baseHeight;
  }
  
  // ... 现有浮力计算，但 fluidLevel_ 替换为 effectiveFluidLevel ...
  
  // 波浪计算：如果当前水区有波浪参数，使用水区的 waveParams
  // 否则使用现有全局 Wavefield
}
```

#### 1.4 波浪参数按水区切换

每个水区有自己的 `WaveParams`，需要转为 `Wavefield` 或直接计算波浪高度。

简单方案：在 PreUpdate 中，如果在水区内，用该水区的 `WaveParams` 构造临时的波浪高度计算，覆盖全局 Wavefield 的结果。

复杂方案（后续再做）：每个水区有自己的 WavefieldPublisher + topic，Buoyancy 订阅多个 topic。

**Step 1 建议采用简单方案**：不依赖 gz-transport，直接在 Buoyancy 内部用 `WaveParams` 计算波浪高度。

#### 1.5 SDF 配置

Buoyancy 的 SDF 增加可选的水区配置：

```xml
<plugin filename="libKSPlugin_Buoyancy.so" name="keship::KSPlugin_Buoyancy">
  <!-- 现有参数 -->
  <fluid_density>1000.0</fluid_density>
  <fluid_level>0.0</fluid_level>
  
  <!-- 新增：水区配置文件（可选，不配则用全局 fluid_level） -->
  <water_areas_config>config/keship_water_areas.toml</water_areas_config>
</plugin>
```

### 文件变更

| 文件 | 改动 |
|------|------|
| `KSPlugin_Buoyancy.hh` | Implementation 新增 waterAreaMgr_ / useWaterAreas_ / currentArea_ |
| `KSPlugin_Buoyancy.cc` | Configure 加载 TOML；PreUpdate 增加水区查找和浮力切换 |

### 测试方式

1. 准备测试 TOML（例如一个 200m×200m 水区，baseHeight=5.0）
2. 生成圆柱体在 (0,0,6) → 应浮在水面 z≈5
3. 手动拖动圆柱体到 (500, 0, 0) → 在水区外 → 圆柱体下落（无浮力）
4. 拖动回 (0,0,0) → 恢复浮力

---

## Step 2：动态生成水区 Waves 模型

**目标**：每个水区有独立的水面可视化，而不是全局一个大 Waves。

### 当前状态

```
models/waves/model.sdf
  ├─ 6km×6km 大平面 mesh (meshes/waterlow.dae)
  └─ 1 个 WaveVisual 插件
```

`SpawnWaves()` / `DeleteWaves()` 控制这个全局 Waves 的显隐。

### 改造方案

#### 2.1 新建函数：按水域生成 Waves SDF

在 `WaterAreaManager` 或新建 `WaterAreaVisualManager` 中：

```cpp
// 根据 WaterArea 生成 Waves 模型 SDF 字符串
std::string MakeWaterSurfaceSdf(const WaterArea &_area)
{
  double w = _area.xMax - _area.xMin;
  double h = _area.yMax - _area.yMin;
  double cx = (_area.xMin + _area.xMax) * 0.5;
  double cy = (_area.yMin + _area.yMax) * 0.5;
  
  std::ostringstream sdf;
  sdf << "<?xml version=\"1.0\"?>\n"
      << "<sdf version=\"1.9\">\n"
      << "  <model name=\"waves_" << _area.name << "\">\n"
      << "    <static>true</static>\n"
      << "    <pose>" << cx << " " << cy << " " << _area.baseHeight << " 0 0 0</pose>\n"
      << "    <link name=\"link\">\n"
      << "      <visual name=\"water_surface\">\n"
      << "        <plugin filename=\"libWaveVisual.so\" name=\"keship::WaveVisual\">\n"
      << "          <shader>\n"
      << "            <vertex>materials/programs/GerstnerWaves_vs_330.glsl</vertex>\n"
      << "            <fragment>materials/programs/GerstnerWaves_fs_330.glsl</fragment>\n"
      << "            <parameters>\n"
      << "              <rescale>0.125</rescale>\n"
      << "              <bumpScale>75 75</bumpScale>\n"
      << "              <bumpSpeed>0.01 0.0</bumpSpeed>\n"
      << "              <hdrMultiplier>0.4</hdrMultiplier>\n"
      << "              <fresnelPower>5.0</fresnelPower>\n"
      << "              <shallowColor>0 0.1 0.2 1.0</shallowColor>\n"
      << "              <deepColor>0 0.05 0.2 1.0</deepColor>\n"
      << "            </parameters>\n"
      << "          </shader>\n"
      << "          <wavefield>\n"
      << "            <topic>/keship/wavefield/" << _area.name << "/parameters</topic>\n"
      << "            <wave>\n"
      << "              <model>" << _area.waveParams.model << "</model>\n"
      << "              <period>" << _area.waveParams.period << "</period>\n"
      << "              <number>" << _area.waveParams.number << "</number>\n"
      << "              <scale>" << _area.waveParams.scale << "</scale>\n"
      << "              <gain>" << _area.waveParams.gain << "</gain>\n"
      << "              <direction>" << _area.waveParams.direction << "</direction>\n"
      << "              <angle>" << _area.waveParams.angle << "</angle>\n"
      << "              <steepness>" << _area.waveParams.steepness << "</steepness>\n"
      << "              <amplitude>" << _area.waveParams.amplitude << "</amplitude>\n"
      << "              <tau>" << _area.waveParams.tau << "</tau>\n"
      << "            </wave>\n"
      << "          </wavefield>\n"
      << "        </plugin>\n"
      << "        <geometry>\n"
      << "          <box>\n"
      << "            <size>" << w << " " << h << " 0.01</size>\n"
      << "          </box>\n"
      << "        </geometry>\n"
      << "        <visibility_flags>8</visibility_flags>\n"
      << "      </visual>\n"
      << "    </link>\n"
      << "  </model>\n"
      << "</sdf>\n";
  return sdf.str();
}
```

关键点：
- 几何体使用 `<box>` 代替原来的 mesh，尺寸匹配水区 XY 范围，Z 极薄（0.01）
- 模型位姿设置在 (cx, cy, baseHeight)，Z 即静水面高度
- WaveVisual 的 `<wavefield>` 段内置完整波浪参数，不依赖外部 Publisher
- 每个水区有独立 topic：`/keship/wavefield/<水区名>/parameters`

#### 2.2 新建水区水面管理器

```cpp
class WaterAreaVisualManager
{
public:
  /// 创建（或重建）水区的水面模型
  void SpawnWaterSurface(const WaterArea &_area, gz::transport::Node &_node);
  
  /// 删除水区的水面模型
  void DeleteWaterSurface(const std::string &_areaName, gz::transport::Node &_node);
  
  /// 删除所有水区水面
  void DeleteAll(gz::transport::Node &_node);
  
private:
  std::set<std::string> spawnedNames_;  // 已生成的水面模型名
};
```

#### 2.3 勾选框改造

当前勾选框控制单一全球 Waves。改为：
- **取消勾选** → 删除所有水区 Waves 模型
- **勾选** → 重新创建所有水区 Waves 模型
- 如果没有任何水区注册 → 勾选无效果（无水面生成）

#### 2.4 MenuPluginKeship 集成

```cpp
class MenuPluginKeship
{
  // 新增
  WaterAreaManager waterAreaMgr_;
  WaterAreaVisualManager waterVisualMgr_;
  
  void LoadWaterAreaConfig(const std::string &_path);  // 加载 TOML + 创建水面
  void UnloadAllWaterAreas();                           // 删除所有水区水面
};
```

### 文件变更

| 文件 | 改动 |
|------|------|
| `src_cpp/utils/WaterAreaManager.hh` | 新增 `MakeWaterSurfaceSdf()` 静态方法 |
| `src_cpp/utils/WaterAreaManager.cc` | 实现 SDF 生成 |
| `src_cpp/utils/WaterAreaVisualManager.hh` | 新建：水面模型生命周期管理 |
| `src_cpp/utils/WaterAreaVisualManager.cc` | 新建：Spawn/Delete 实现 |
| `MenuPluginKeship.hh` | 新增 waterAreaMgr_ / waterVisualMgr_ |
| `MenuPluginKeship.cc` | 勾选框逻辑改为控制所有水区水面 |

### 测试方式

1. 准备 2 个水区的 TOML
2. 启动 KeShip → 默认无水
3. 勾选"水域场景" → 出现 2 片小水面（在水区范围内）
4. 取消勾选 → 水面消失
5. 从不同角度观察，确认水面只在水区范围内

---

## Step 3：地图模型驱动水区定义

**目标**：加载地图模型时自动解析水区定义并创建水面。

### 方案

#### 3.1 地图模型内嵌水区配置

地图 SDF 模型的 `<plugin>` 段携带水区定义：

```xml
<model name="campus_map">
  <pose>0 0 0 0 0 0</pose>
  <static>true</static>
  <!-- ... link/visual 定义地面、建筑 ... -->
  
  <!-- 水区定义 -->
  <plugin filename="libWaterAreaLoader.so" name="keship::WaterAreaLoader">
    <water_areas>
      <area>
        <name>pond</name>
        <x_min>-50</x_min> <x_max>30</x_max>
        <y_min>30</y_min>  <y_max>90</y_max>
        <base_height>2.0</base_height>
        <wave>
          <model>PMS</model>
          <period>3.0</period>
          <amplitude>0.1</amplitude>
        </wave>
      </area>
      <area>
        <name>river</name>
        <x_min>0</x_min>     <x_max>200</x_max>
        <y_min>-10</y_min>   <y_max>10</y_max>
        <base_height>0.5</base_height>
        <wave>
          <model>PMS</model>
          <period>5.0</period>
          <amplitude>0.05</amplitude>
        </wave>
      </area>
    </water_areas>
  </plugin>
</model>
```

#### 3.2 WaterAreaLoader 插件（新建）

轻量级 Gazebo world 插件：

```cpp
class WaterAreaLoader : public gz::sim::System,
                        public gz::sim::ISystemConfigure
{
  void Configure(...)
  {
    // 解析 SDF 中的 <water_areas>
    // 注册到全局 WaterAreaManager（通过 gz-transport topic 广播）
    // 或直接通过 Service 通知 MenuPluginKeship 创建水面
  }
};
```

### 替代方案（更简单，Step 3 采用）

不用新建插件。地图模型自带的 TOML 文件路径通过 SDF 传递，GUI 插件加载地图时自动处理。

地图目录结构：
```
models/campus_map/
  ├── model.sdf
  ├── meshes/
  │   ├── ground.dae
  │   └── ...
  └── water_areas.toml     ← 地图自带的水区配置
```

`model.sdf` 中引用：
```xml
<model name="campus_map">
  <keship:water_config>water_areas.toml</keship:water_config>
</model>
```

`MenuPluginKeship::OnBtn_LoadModel` 加载模型成功后，检查模型目录下是否有 `water_areas.toml`，有则自动调用 `LoadWaterAreaConfig()`。

### 测试方式

1. 准备一个带水区定义的示例地图模型
2. 启动 KeShip → 无水
3. 加载地图 → 自动出现水区水面
4. 删除地图 → 水区水面自动清除

---

## Step 4：GUI 水区编辑器

**目标**：可视化编辑水区（名称、范围、高度、波浪参数）。

### 方案

- 在"水域场景"勾选框旁增加"编辑水区"按钮
- 弹出对话框，列出所有水区（名称、范围、高度）
- 可添加/删除/编辑水区参数
- XY 范围可视化（3D 场景中绘制矩形线框，复用 Marker 机制）

### 不在此文档详述

Step 4 依赖 Step 1-3 完成后再设计 UI 细节。

---

## 编译集成

### CMakeLists.txt 新增（Step 2）

```cmake
# WaterAreaVisualManager
target_sources(keship_utils PRIVATE
  src_cpp/utils/WaterAreaVisualManager.cc
)
```

### CMakeLists.txt 新增（Step 3，如果采用插件方案）

```cmake
list(APPEND KS_plugins_physics
  WaterAreaLoader
)
# ... 循环编译同其他物理插件 ...
```

---

## 进度跟踪

| Step | 内容 | 状态 | 依赖 |
|------|------|------|------|
| 1 | KSPlugin_Buoyancy 集成 WaterAreaManager | ✅ 已实施 | — |
| 2 | 动态水区 Waves 模型 | ✅ 测试通过 | Step 1 |
| 3 | 地图驱动 + GUI 编辑器 | ✅ 测试通过 | Step 2 |
| 4 | GUI 水区编辑器 | 🔲 待实施 | Step 3 |
