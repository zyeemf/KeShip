# 波浪系统变更记录（2026-06-16 ~ 2026-06-17）

按时间顺序。每个条目注明**新建**/**修改**及具体改动。

---

## 1. 水域勾选框（✅ 测试成功，基线）

**新建**：
- `MenuPluginKeship.hh` — +`Q_PROPERTY(showWater)`, +`signal ShowWaterChanged`, +`slot OnToggleWater(bool)`, +`wavesSdfCache_`/`LoadWavesSdf`/`SpawnWaves`/`DeleteWaves`

**新建**：
- `MenuPluginKeship.cc` — +3 方法：
  - `LoadWavesSdf()` — ament_index_cpp → share dir → models/waves/model.sdf → ifstream → 缓存到 `wavesSdfCache_`
  - `SpawnWaves()` — `EntityFactory.set_sdf_filename("file://...")` → Request `/world/keship_world/create/blocking`
  - `DeleteWaves()` — `Entity.set_name("Waves")` + `Entity::MODEL` → Request `/world/keship_world/remove`
  - `OnToggleWater(bool)` — `showWater_ = _checked`, if checked → SpawnWaves(), else → DeleteWaves(), emit ShowWaterChanged

**新增 include**：
- `MenuPluginKeship.cc` — `gz/msgs/entity.pb.h`（用于 Entity::MODEL）, `ament_index_cpp/get_package_share_directory.hpp`

**修改**：
- `MenuPluginKeship.qml` — 第二按钮行末加了 `CheckBox { text: "水域场景"; checked: MenuPluginKeship.showWater; onClicked: MenuPluginKeship.OnToggleWater(checked) }`

**当时 SDF 状态**：`keship_world.sdf` 有 `<include model://waves>`，无 WavefieldPublisher 插件。

**当时状态**：勾选/取消正常 ✅

---

## 2. WavefieldPublisher 全局波浪参数发布（新建插件）

**新建**：
- `src_cpp/plugins_physics/WavefieldPublisher.hh` — 插件声明（System + ISystemConfigure + ISystemPreUpdate）
- `src_cpp/plugins_physics/WavefieldPublisher.cc` — ~120 行：
  - `WavefieldPublisherPrivate`：Wavefield, Node, Publisher, topic, simTime, lastPublishTime
  - `Configure`：Load(SDF) → Advertise → Subscribe → Publish 初始参数
  - `PreUpdate`：t<5s 每 1s 发一次，t≥5s 每 30s 发一次

**修改**：
- `keship_world.sdf` — 在 world 级 `<plugin>` 区添加了 `libWavefieldPublisher.so`（含 PMS 波浪参数）
- `CMakeLists.txt` — 新增 `WavefieldPublisher` 共享库目标

**关键点**：Publisher Subscribe 和 Publish **同一个 topic** `this->dataPtr->topic`（当时未加 `<topic>` 显式配置，默认为 `/wavefield/parameters`）

**当时状态**：日志刷屏（自触发循环），水面抖动

---

## 3. 统一 PMS 波浪方案（4 对话框改造）

**修改**：
- `StabilityExperimentDialog.cc` — 打开：`{CWR,amp=0,number=1}` → `{PMS,gain=0}`；关闭注释更新
- `IncliningExperimentDialog.cc` — 同上
- `HullStabilityDialog.cc` — 同上
- `DisplacementVerifyDialog.cc` — 同上

**当时状态**：未独立测试

---

## 4. 双 Topic 修复（Publisher 循环）

**修改**：
- `WavefieldPublisher.cc` — Subscribe topic 从 `this->dataPtr->topic` 改为 `ctrlTopic = topic + "_set"`
- `keship_world.sdf` — 加 `<topic>/keship/wavefield/parameters</topic>`（修复 topic 不一致：之前 Publisher 发到 `/wavefield/parameters`，而 WaveVisual/Buoyancy 订阅 `/keship/wavefield/parameters`）

**修改**：
- `StabilityExperimentDialog.cc` — Advertise topic 从 `/keship/wavefield/parameters` 改为 `/keship/wavefield/parameters_set`（2 处）
- `IncliningExperimentDialog.cc` — 同上
- `HullStabilityDialog.cc` — 同上
- `DisplacementVerifyDialog.cc` — 同上

**当时状态**：日志不刷屏了，但水面仍抖动

---

## 5. SDF `<include>` 反复删/加（试图修复抖动）

**修改记录**（按顺序）：

| 次序 | 改动 | 结果 |
|------|------|------|
| a | 删 `<include>`，在 constructor 加 `QTimer(500)` spawn | 崩溃（sun 冲突） |
| b | 恢复 `<include>`，constructor 移除 QTimer | 恢复原状 |
| c | 删 `<include>`，在 LoadConfig 加 `QTimer(0)` spawn | 未编译测试 |
| d | 回退到步骤 b 状态 | — |
| e | 删 `<include>`，在 LoadConfig 直接调 SpawnWaves()（无 timer） | 当前状态 |

**当前 SDF 状态**：无 `<include>`，有 WavefieldPublisher 插件

**当前 LoadConfig 状态**：
```cpp
void MenuPluginKeship::LoadConfig(const tinyxml2::XMLElement *_pluginElement)
{
  if (showWater_) SpawnWaves();
}
```

---

## 6. 勾选框无效 + 水面抖动修复（✅ 2026-06-17）

**问题**：Phase 2 加入 WavefieldPublisher 后，勾选框无法取消水域、水面持续抖动。

**根因**：
- `SpawnWaves()` 和 `DeleteWaves()` 使用实体名 `"Waves"`（大写 W），而 `models/waves/model.sdf` 中模型名为 `"waves"`（小写 w）
- Gazebo 实体名匹配大小写敏感，`DeleteWaves("Waves")` 找不到实体 → 静默失败 → 水面永远存在
- 再次 `SpawnWaves("Waves")` 创建第二个实例（与 `"waves"` 不冲突）→ 两个重叠水面 → z-fighting → 抖动

**修改**：
- `MenuPluginKeship.cc` — `SpawnWaves()` 中 `set_name("Waves")` → `set_name("waves")`
- `MenuPluginKeship.cc` — `DeleteWaves()` 中 `set_name("Waves")` → `set_name("waves")`
- `WaveVisual.cc` — `OnWavefield()` 增加参数变化检测（amplitude/period/direction/steepness/scale/angle/number），无变化时跳过 `paramsSet = false`，避免 WavefieldPublisher 定时重发导致 shader buffer 频繁重初始化

**验证**：勾选/取消正常，水面无抖动 ✅

---

## 当前所有修改文件清单

### 新建文件（6 个）
| 文件 | 说明 |
|------|------|
| `include/keship/utils/WaterArea.hh` | WaterArea + WaveParams 数据结构 |
| `src_cpp/utils/WaterAreaManager.hh` | 多水域管理器声明 |
| `src_cpp/utils/WaterAreaManager.cc` | TOML 加载 + XY 查找实现 |
| `src_cpp/plugins_physics/WavefieldPublisher.hh` | 波浪发布插件声明 |
| `src_cpp/plugins_physics/WavefieldPublisher.cc` | 波浪发布插件实现 |
| `config/keship_water_areas_example.toml` | 多水域示例配置 |

### 修改文件（10 个）
| 文件 | 改动摘要 |
|------|---------|
| `MenuPluginKeship.hh` | showWater Q_PROPERTY + 槽 + wavesSdfCache_ + 3 方法 |
| `MenuPluginKeship.cc` | LoadWavesSdf/SpawnWaves/DeleteWaves + LoadConfig spawn |
| `MenuPluginKeship.qml` | CheckBox"水域场景" |
| `keship_world.sdf` | -Waves `<include>`, +WavefieldPublisher `<plugin>` + `<topic>` |
| `CMakeLists.txt` | +WaterAreaManager, +WavefieldPublisher, +toml++ include |
| `StabilityExperimentDialog.cc` | 打开 PMS+gain=0，topic→_set |
| `IncliningExperimentDialog.cc` | 同上 |
| `HullStabilityDialog.cc` | 同上 |
| `DisplacementVerifyDialog.cc` | 同上 |
| `WavefieldPublisher.cc` | Subscribe topic→_set（在首次创建后多次修改） |
