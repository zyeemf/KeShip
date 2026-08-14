# Step 1：KSPlugin_Buoyancy 集成 WaterAreaManager

**日期**：2026-06-17 | **状态**：✅ 测试通过 | **依赖**：WaterArea 数据结构 + WaterAreaManager（✅ 已完成）

---

## 目标

浮力计算不再使用全局 `fluidLevel_`，改为按船舶位置查找对应水区。船舶在水区外时不产生浮力（陆地上）。

---

## 涉及文件

| 文件 | 改动类型 |
|------|---------|
| `include/keship/utils/WaterArea.hh` | 新增 `Wavefield` 前向声明 + `ToWavefield()` |
| `WaterAreaManager.hh` | 新增 `LoadFromToml` 返回值可空时默认逻辑 |
| `KSPlugin_Buoyancy.hh` | Implementation 新增水区成员 |
| `KSPlugin_Buoyancy.cc` | Configure 读 TOML + PreUpdate 水区查找 + 浮力切换 |
| `ModelManager.hh` | BuoyancyConfig 新增 `waterAreasConfig` 字段 |
| `ModelManager.cc` | MakeBuoyancyPluginSDF 生成 `<water_areas_config>` |

---

## 改动 1：KSPlugin_Buoyancy.cc — WaveParams → Wavefield 转换函数

**问题**：`WaveParams` 和 `Wavefield` 是两套参数体系。Buoyancy 的 PreUpdate 使用 `Wavefield::ComputeDepthSimply` 计算波浪高度，需要把 `WaveParams` 转为 `Wavefield` 才能复用现成算法。但 `WaterArea.hh` 在 `include/keship/utils/` 目录，不能直接引用 `src_cpp/plugins_physics/Wavefield.hh`（编译路径不匹配）。

**方案**：在 `KSPlugin_Buoyancy.cc` 文件内写一个 static 辅助函数，保持 `WaterArea.hh` 纯数据不引入物理依赖。

```cpp
// KSPlugin_Buoyancy.cc 顶部（namespace {} 内或文件内 static）
static void ApplyWaveParamsToWavefield(const WaveParams &_wp, Wavefield &_wf)
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

注意：`Wavefield` 没有 `SetModel` 公有方法（model=CWR/PMS），当前 `WaveParams::model` 默认为 `"PMS"`，`Wavefield` 构造默认也是 `"PMS"`。model 切换功能后续 Step 再处理。

---

## 改动 2：KSPlugin_Buoyancy.hh — Implementation 新增水区成员

```cpp
// 新增 include
#include "keship/utils/WaterArea.hh"
#include "../utils/WaterAreaManager.hh"

class KSPlugin_Buoyancy::Implementation
{
public:
  // === 现有成员（不变）===
  double fluidDensity_{1000.0};
  double fluidLevel_{0.0};
  gz::math::Vector3d gravity_{0.0, 0.0, -9.81};
  double linearDragCoef{0.5};
  double angularDragCoef{0.05};
  std::vector<BuoyancyBody> bodies;
  double cylinderRadius_{0.0};
  double cylinderLength_{0.0};
  int cylinderSegments_{32};
  gz::transport::Node node;
  std::mutex mutex;
  Wavefield wavefield_;
  std::chrono::steady_clock::duration simTime_{0};
  std::chrono::steady_clock::duration simTimeInit_{0};
  bool timeInitSet_{false};
  void OnWavefield(const gz::msgs::Param &_msg);

  // === 新增：多水区支持 ===
  WaterAreaManager waterAreaMgr_;
  bool useWaterAreas_ = false;         ///< 启用多水区模式
  std::string currentAreaName_;        ///< 当前所在水区名（检测切换）
};
```

---

## 改动 3：KSPlugin_Buoyancy.cc — Configure 加载 TOML

在 `Configure()` 中 `LoadParameters(_sdf)` 之后、解析 link 之前，插入：

```cpp
// 尝试加载水区配置（可选）
if (_sdf->HasElement("water_areas_config"))
{
  auto configPath = _sdf->Get<std::string>("water_areas_config");
  if (this->dataPtr->waterAreaMgr_.LoadFromToml(configPath))
  {
    this->dataPtr->useWaterAreas_ = true;
    gzmsg << "KSPlugin_Buoyancy: 水区配置已加载 — "
          << this->dataPtr->waterAreaMgr_.Count() << " 片水区" << std::endl;
  }
}
```

---

## 改动 4：KSPlugin_Buoyancy.cc — PreUpdate 水区查找 + 浮力切换

**现有结构**（简化）：

```
PreUpdate {
  获取 modelPose, linkWorldPos, totalMass
  锁定 mutex, 初始化波浪时间
  for each body {
    计算波浪高度 h0/hx/hy
    waveOffset = h0 + fluidLevel_
    求浸没体积 → 浮力 + 恢复力矩
  }
  粘性阻力
  ApplyForces
}
```

**改造后结构**：

```
PreUpdate {
  获取 modelPose, linkWorldPos, totalMass
  
  // 【新增】水区查找
  double effectiveFluidLevel = fluidLevel_;
  bool inWater = true;
  if (useWaterAreas_) {
    area = waterAreaMgr_.Lookup(linkWorldPos.X(), linkWorldPos.Y());
    if (!area) {
      inWater = false;   // 陆地上，跳过浮力
    } else {
      effectiveFluidLevel = area->baseHeight;
      // 水区切换 → 更新波浪参数
      if (area->name != currentAreaName_) {
        currentAreaName_ = area->name;
        area->ApplyToWavefield(wavefield_);
        // 重新标记时间起点（让波浪渐入效果重置）
        timeInitSet_ = false;
      }
    }
  }

  if (inWater) {
    锁定 mutex, 初始化波浪时间
    for each body {
      计算波浪高度 h0/hx/hy
      waveOffset = h0 + effectiveFluidLevel   // 【改动】用 effectiveFluidLevel 而非 fluidLevel_
      求浸没体积 → 浮力 + 恢复力矩
    }
  }

  粘性阻力（始终计算，水陆通用）
  ApplyForces
}
```

### 具体代码改动位置

**改动 a**：在 PreUpdate 中，`totalMass` 计算之后、`rhoG` 之前，插入水区查找逻辑：

```cpp
  /// === 新增：多水区查找（在获取 totalMass 之后）===
  double effectiveFluidLevel = this->dataPtr->fluidLevel_;
  bool inWater = true;

  if (this->dataPtr->useWaterAreas_)
  {
    const WaterArea *area = this->dataPtr->waterAreaMgr_.Lookup(
        linkWorldPos.X(), linkWorldPos.Y());
    if (!area)
    {
      inWater = false;
    }
    else
    {
      effectiveFluidLevel = area->baseHeight;

      std::lock_guard<std::mutex> lock(this->dataPtr->mutex);
      if (area->name != this->dataPtr->currentAreaName_)
      {
        this->dataPtr->currentAreaName_ = area->name;
        ApplyWaveParamsToWavefield(area->waveParams, this->dataPtr->wavefield_);
        this->dataPtr->timeInitSet_ = false;
      }
    }
  }
```

**改动 b**：将现有的 `const double rhoG = ...` 到 `ApplyForces` 之间的浮力计算包裹在 `if (inWater)` 内：

```cpp
  gz::math::Vector3d totalForce(0, 0, 0);
  gz::math::Vector3d totalTorque(0, 0, 0);
  double totalSubVol = 0.0;

  if (inWater)   // ← 新增包裹
  {
    const double rhoG = this->dataPtr->fluidDensity_
                      * std::abs(this->dataPtr->gravity_.Z());

    // ... 现有：时间初始化 + body 循环 + 浮力力矩计算 ...
    // 其中 waveOffset 行改为:
    //   double waveOffset = h0 + effectiveFluidLevel;  // ← fluidLevel_ → effectiveFluidLevel
  }

  /// 粘性阻力（水陆通用，保持在 if 外面）
  double partialMass = totalMass;

  if (inWater)
  {
    double totalFullVol = 0.0;
    for (auto &body : this->dataPtr->bodies)
      totalFullVol += body.totalVolume;
    if (totalFullVol > 1e-9)
      partialMass = totalMass * (totalSubVol / totalFullVol);
  }
  // else: on land, use full mass (partialMass = totalMass)

  auto linVel = this->linkObj_.WorldLinearVelocity(_ecm);
  auto angVel = this->linkObj_.WorldAngularVelocity(_ecm);

  if (linVel)
  {
    gz::math::Vector3d dragForce =
        -this->dataPtr->linearDragCoef * partialMass * (*linVel);
    totalForce += dragForce;
  }

  if (angVel)
  {
    double weightedLenSq = 0.0;
    for (auto &body : this->dataPtr->bodies)
      weightedLenSq += body.totalVolume * body.characteristicLengthSq;
    double totalFullVol = 0.0;
    for (auto &body : this->dataPtr->bodies)
      totalFullVol += body.totalVolume;
    if (totalFullVol > 1e-9)
      weightedLenSq /= totalFullVol;
    if (weightedLenSq < 1e-6)
      weightedLenSq = 1.0;
    gz::math::Vector3d dragTorque =
        -this->dataPtr->angularDragCoef * partialMass * weightedLenSq * (*angVel);
    totalTorque += dragTorque;
  }
```

**改动 c**：`waveOffset` 从 `fluidLevel_` 改为 `effectiveFluidLevel`：

```cpp
// 原来：
double waveOffset = h0 + this->dataPtr->fluidLevel_;

// 改为：
double waveOffset = h0 + effectiveFluidLevel;
```

---

## 改动 5：ModelManager.hh — BuoyancyConfig 新增字段

```cpp
struct BuoyancyConfig
{
  std::string linkName = "base_link";
  double fluidDensity = 1000.0;
  double fluidLevel = 0.0;
  double linearDrag = 0.5;
  double angularDrag = 0.05;
  std::vector<BuoyancyBodyConfig> bodies;

  std::string waterAreasConfig;   // ← 新增：水区 TOML 配置文件路径（空=不使用）
};
```

---

## 改动 6：ModelManager.cc — MakeBuoyancyPluginSDF 生成水区配置

在 `MakeBuoyancyPluginSDF()` 中，`fluid_level` 行之后、`</plugin>` 之前，插入：

```cpp
  // 水区配置（可选）
  if (!_config.waterAreasConfig.empty())
  {
    sdf << "      <water_areas_config>"
        << _config.waterAreasConfig
        << "</water_areas_config>\n";
  }

  sdf << "    </plugin>\n";   // 现有行（闭合标签）
```

---

## 改动 7：WaterAreaManager.cc — 增加空配置保护

`LoadFromToml` 在配置为空或不存在文件时静默失败。需要加一个特殊处理：

```cpp
bool WaterAreaManager::LoadFromToml(const std::string &_path)
{
  if (_path.empty()) return false;
  // ... 现有实现 ...
}
```

---

## 编译影响

`WaterArea.hh` 新增 `#include "Wavefield.hh"` 会产生循环依赖吗？

- `Wavefield.hh` 引用 gz-math, gz-msgs, eigen → 不依赖 WaterArea
- `WaterArea.hh` 新增引用 `Wavefield.hh` → 单向依赖 ✓

`KSPlugin_Buoyancy.hh` 新增：
- `#include "keship/utils/WaterArea.hh"` → 需要 CMake include path 能解析
- `#include "../utils/WaterAreaManager.hh"` → 相对路径（与现有 Wavefield.hh 一致）

---

## 测试方案

### 前置准备

创建测试 TOML（`config/test_water_area.toml`）：

```toml
[[water_areas]]
name = "test_pool"
x_min = -100.0
x_max = 100.0
y_min = -100.0
y_max = 100.0
base_height = 5.0

[water_areas.wave]
model = "PMS"
period = 5.0
number = 3
scale = 1.1
gain = 0.0      # 静水测试，无波浪干扰
amplitude = 0.0
```

### 测试 1：浮力在水区内生效

1. 启动 KeShip
2. 勾选水域场景 → 确保 Waves 存在（视觉参考）
3. 圆柱体测试工具 → 生成圆柱体在 (0, 0, 6)
4. `waterAreasConfig = config/test_water_area.toml` 传入 BuoyancyConfig
5. **预期**：圆柱体浮在 z≈5.0 水面

### 测试 2：水区外无浮力

1. 同一个圆柱体，手动拖动到 (500, 0, 6)
2. **预期**：圆柱体下落（无水区覆盖，无浮力）

### 测试 3：重新进入水区恢复浮力

1. 拖动圆柱体回 (0, 0, 6)
2. **预期**：圆柱体重新浮在水面

### 测试 4：水区切换波浪参数

1. 在 TOML 中配两个水区（不同 baseHeight）
2. 拖动圆柱体从水区 A 到水区 B
3. **预期**：浮力基准面随水区自动切换

---

## 后续工作（不在此 Step）

- Wavefield 的 `SetModel` 支持（CWR/PMS 切换）
- 水区边界平滑过渡（避免颠簸）
- 全局 WaterAreaManager 共享（避免每个 Buoyancy 实例独立加载 TOML）
- 与水区 Waves 可视化的联动（Step 2）

---

## 可行性检查结果

### ✅ 已验证通过

| 检查项 | 结论 |
|--------|------|
| Include 路径 | `"../utils/WaterAreaManager.hh"` 与现有 `"../utils/Buoyancy.hh"` 模式一致，可行 |
| `keship/utils/WaterArea.hh` | `include_directories(include)` 全局生效，所有目标可见 |
| `toml.hpp` | `kship_utils PUBLIC extern/toml++` → KSPlugin_Buoyancy 链接 keship_utils → 路径可见 |
| WaterAreaManager 线程安全 | Configure 写入 → PreUpdate 只读（Lookup 为 const），无竞争 |
| 双锁死锁 | 水区更新锁 / 时间初始化锁 / body 循环锁 均为独立作用域，不重叠 |
| 向后兼容 | `useWaterAreas_` 默认 false，无 `<water_areas_config>` 时不启用 |
| Wavefield::Active() 无需 topic | 仅用 ComputeDepthSimply，不依赖 transport 订阅 |
| KSM 兼容 | `waterAreasConfig` 字段默认空，旧 .ksm 文件不解析此字段 → 不启用 |

### ⚠️ 发现并已修复

| 问题 | 修复 |
|------|------|
| **Drag 力在水区外为 0**：`partialMass = totalMass * (0 / totalFullVol) = 0`，船舶在陆地上无阻力滑行 | 修改 drag 段：`inWater=false` 时 `partialMass = totalMass`（全质量） |

### ⚠️ 已知局限（不影响 Step 1 功能）

| 局限 | 说明 |
|------|------|
| Wavefield SetXXX 重复计算 | 9 个 setter 逐个调用，每个都触发 Recalculate()，效率低但不影响正确性 |
| 单点水区查找 | 用 linkWorldPos 单点判断，大型船舶跨边界时可能不精确 |
| 每 Buoyancy 实例独立 TOML | 多个船舶各加载一次相同文件，内存和 IO 浪费 |
| model 类型不支持 | 所有水区默认 PMS，不支持 CWR |

---

## 后续修复记录

### 2026-06-18 #2：勾选框始终无效 — 控制流缺陷

**现象**：经历 Topic/Service/ECM/File 多种通信方案，勾选框取消后浮力始终存在。

**根因**：文件读取被锁在 `if (waterAreasConfigured_)` 内部。TOML 未加载时整块跳过，`inWater` 始终为 `true`，浮力永远在 `fluidLevel_=0`。导致的结果就是无论勾选框状态如何，浮力一直存在在 z=0 位置。

**最终方案（✅ 测试通过）**：重组控制流
- 文件读取提到 `if (configured_)` **外层**，无条件每帧执行
- `if (!enabled) → inWater = false` 在自定义水区逻辑之前判断
- `LoadConfig` 末尾写入 `'1'` 确保启动时文件状态与勾选框一致

### 2026-06-18：代码清理 ✅
- 移除 `<atomic>` include（`std::atomic<bool>` 已回退）
- 移除 `ecm_` 成员 + `<Name.hh>` include（ECM Each<Name> 方案残留）
- 移除 `HasMultipleAreas()` / `ConfigPath()` / `configPath_`（从未被调用）
- 4 文件，仅删除 12 行

---

## 最终状态

| 功能 | 状态 |
|------|------|
| WaterArea 数据结构 | ✅ |
| WaterAreaManager（TOML + XY 查找） | ✅ |
| KSPlugin_Buoyancy 集成（Configure 加载 TOML） | ✅ |
| PreUpdate 水区查找 + 浮力/陆地分支 | ✅ |
| 勾选框联动浮力启用/禁用（文件标志） | ✅ |
| BuoyancyConfig 传播 + Rebuild 保留 | ✅ |
| 路径解析（ament_index_cpp 绝对路径） | ✅ |
| 旧代码兼容（无 TOML 走 fluidLevel_ 旧路径） | ✅ |

