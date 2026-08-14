# Step 2：动态水区 Waves 模型

**日期**：2026-06-18 | **依赖**：Step 1（✅ 已完成） | **状态**：✅ 测试通过

---

## 目标

替代当前单一 6km×6km 全局 Waves 模型，为每个水区生成独立的局部水面——尺寸精确匹配水区 XY 范围，水面在 Z=baseHeight 高度，搭载独立 WaveVisual 插件和该水区专属波浪参数。

---

## 几何体选择

### 方案对比

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| Box（扁长方体） | 简单 | 6 面体，侧面/底面也会被 Gerstner shader 变形 | ❌ |
| 缩放现有 mesh（waterlow.dae） | 顶点密度够 | 各水区比例因子不同，缩放后顶点不均 | ❌ |
| **Plane** | 单面、无侧面、原生支持、自动细分 | — | ✅ 采纳 |

### Plane 规范（SDF 1.9 官方确认）

来源：sdformat GitHub `sdf/1.9/plane_shape.sdf`

```xml
<element name="plane" required="0">
  <normal>  type="vector3" default="0 0 1"  </normal>
  <size>    type="vector2d" default="1 1" min="0 0"  </size>
</element>
```

- `normal`：平面法向，默认 Z 朝上（Gazebo 坐标系）
- `size`：(x 方向长度, y 方向长度)，仅对 Visual 限定范围；Collision 用 Plane 时尺寸无限
- 模型为 `static=true`，仅需 Visual，不需要 collision

---

## 新增文件

### `src_cpp/utils/WaterAreaVisualManager.hh`

```cpp
namespace keship {

class WaterAreaVisualManager
{
public:
  /// 为单水区生成 Waves 模型 SDF 字符串
  std::string MakeSurfaceSdf(const WaterArea &_area) const;

  /// 批量创建所有水区水面模型
  void SpawnAll(const std::vector<WaterArea> &_areas,
                gz::transport::Node &_node);

  /// 删除所有已创建的水面模型
  void DeleteAll(gz::transport::Node &_node);

private:
  std::set<std::string> spawnedNames_;
};

}
```

### `src_cpp/utils/WaterAreaVisualManager.cc`

#### MakeSurfaceSdf

```
输入：WaterArea（name, xMin/xMax, yMin/yMax, baseHeight, waveParams）
输出：完整 SDF XML 字符串

生成内容：
  <sdf version="1.9">
    <model name="waves_{area.name}">
      <static>true</static>
      <pose>cx cy baseHeight 0 0 0</pose>
      <link name="link">
        <visual name="water_surface">
          <plugin filename="libWaveVisual.so" name="keship::WaveVisual">
            <shader>
              <vertex>{abs_path}/GerstnerWaves_vs_330.glsl</vertex>
              <fragment>{abs_path}/GerstnerWaves_fs_330.glsl</fragment>
              <parameters>
                <rescale>0.125</rescale>
                <bumpScale>75 75</bumpScale>
                <bumpSpeed>0.01 0.0</bumpSpeed>
                <hdrMultiplier>0.4</hdrMultiplier>
                <fresnelPower>5.0</fresnelPower>
                <shallowColor>0 0.1 0.2 1.0</shallowColor>
                <deepColor>0 0.05 0.2 1.0</deepColor>
              </parameters>
            </shader>
            <wavefield>
              <topic>/keship/wavefield/{area.name}/parameters</topic>
              <wave>
                <model>{waveParams.model}</model>
                <period>{waveParams.period}</period>
                <number>{waveParams.number}</number>
                <scale>{waveParams.scale}</scale>
                <gain>{waveParams.gain}</gain>
                <direction>{waveParams.direction}</direction>
                <angle>{waveParams.angle}</angle>
                <steepness>{waveParams.steepness}</steepness>
                <amplitude>{waveParams.amplitude}</amplitude>
                <tau>{waveParams.tau}</tau>
              </wave>
            </wavefield>
          </plugin>
          <geometry>
            <plane>
              <normal>0 0 1</normal>
              <size>{w} {h}</size>
            </plane>
          </geometry>
          <visibility_flags>8</visibility_flags>
        </visual>
      </link>
    </model>
  </sdf>
```

关键点：
- 模型名 `waves_{name}` — 与 Step 1 ECM 检测命名一致，Buoyancy 可通过此名判断状态
- Shader 路径通过 `ament_index_cpp::get_package_share_directory("keship")` 转为绝对路径（因 EntityFactory `set_sdf()` 没有 SourceFilePath）
- Wavefield 同时包含 `<topic>`（预留动态更新）和 `<wave>` 内嵌参数（初始值）
- 每个水区独立 topic：`/keship/wavefield/{name}/parameters`
- Plane 尺寸 = (xMax-xMin, yMax-yMin)
- 位姿中心 = ((xMin+xMax)/2, (yMin+yMax)/2, baseHeight)

#### SpawnAll

```
for each area:
  ① name = "waves_" + area.name
  ② sdf = MakeSurfaceSdf(area)
  ③ EntityFactory: set_name(name), set_allow_renaming(false), set_sdf(sdf)
  ④ _node.Request("/world/keship_world/create/blocking", req, 10000, ...)
  ⑤ spawnedNames_.insert(name)
```

#### DeleteAll

```
for each name in spawnedNames_:
  ① Entity: set_name(name), set_type(MODEL)
  ② _node.Request("/world/keship_world/remove", req, 5000, ...)
clear spawnedNames_
```

---

## 现有代码修改

### `MenuPluginKeship.hh`

```diff
+#include "../utils/WaterAreaManager.hh"
+#include "../utils/WaterAreaVisualManager.hh"

  private:
+   WaterAreaManager waterAreaMgr_;
+   WaterAreaVisualManager waterVisualMgr_;
```

### `MenuPluginKeship.cc` — LoadConfig

```cpp
// 加载水区定义
if (!waterAreasConfigPath_.empty())
{
    waterAreaMgr_.LoadFromToml(waterAreasConfigPath_);
}

// 初始文件状态
std::ofstream("/tmp/keship_water_enabled") << '1';

// 初始 spawn
if (showWater_)
{
    if (waterAreaMgr_.Count() > 0)
        waterVisualMgr_.SpawnAll(waterAreaMgr_.Areas(),
            modelManager->GetNode());
    else
        SpawnWaves();  // 向后兼容：无 TOML 时用旧全局 Waves
}
```

### `MenuPluginKeship.cc` — OnToggleWater

```cpp
void MenuPluginKeship::OnToggleWater(bool _checked)
{
    showWater_ = _checked;

    if (_checked)
    {
        if (waterAreaMgr_.Count() > 0)
            waterVisualMgr_.SpawnAll(waterAreaMgr_.Areas(),
                modelManager->GetNode());
        else
            SpawnWaves();
    }
    else
    {
        if (waterAreaMgr_.Count() > 0)
            waterVisualMgr_.DeleteAll(modelManager->GetNode());
        else
            DeleteWaves();
    }

    std::ofstream("/tmp/keship_water_enabled") << (_checked ? '1' : '0');
    emit ShowWaterChanged();
}
```

### `CMakeLists.txt`

```diff
  add_library(keship_utils STATIC
    ...
    src_cpp/utils/WaterAreaManager.cc
+   src_cpp/utils/WaterAreaVisualManager.cc
  )
```

---

## 与 Step 1 的关系

| 层 | Step 1（物理） | Step 2（视觉） |
|----|---------------|---------------|
| 控制源 | `/tmp/keship_water_enabled` 文件标志 | 勾选框直接调用 SpawnAll/DeleteAll |
| 浮力 | Buoyancy PreUpdate 中读文件 → waterAreasEnabled_ | — |
| 水面 | — | 每个水区一个 Plane + WaveVisual |
| 兼容 | 无 TOML → fluidLevel_=0 legacy | 无 TOML → 旧全局 Waves legacy |
| 相互独立 | ✅ 文件标志不依赖视觉模型 | ✅ 视觉不依赖浮力标志 |

---

## 勾选框行为矩阵

| showWater_ | waterAreaMgr_.Count() | 视觉行为 | 浮力行为（Step 1） |
|-----------|----------------------|---------|-------------------|
| true | > 0 | SpawnAll 每水区小水面 | 文件 '1' → 水区浮力 |
| true | = 0 | SpawnWaves 旧全局 Waves | 文件 '1' → legacy 浮力 |
| false | > 0 | DeleteAll | 文件 '0' → 无浮力 |
| false | = 0 | DeleteWaves | 文件 '0' → 无浮力 |

---

## 后续（不在此 Step）

- Plane 顶点密度优化（如果波浪位移不够平滑）
- Per-area WavefieldPublisher 支持（每个水区独立波浪发布）
- 水面边缘渐变（避免硬边）
