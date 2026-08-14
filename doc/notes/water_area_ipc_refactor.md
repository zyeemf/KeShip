# 水域 IPC 架构重构记录

> 日期：2026-07-14~15
> 触发 Bug：加载默认水域后日志重复刷屏 + 首次加载模型无浮力

---

## 背景

KeShip 中水域配置需要在 GUI 进程（gz-gui）和服务器进程（gz-sim）之间共享。GUI 负责创建/编辑/删除水域，Server 的 `KSPlugin_Buoyancy` 根据水域计算浮力。两个进程无法直接共享内存，需要 IPC 机制。

### 原始方案（TOML 文件轮询）

```
GUI ──写──→ /tmp/keship_water_areas.toml
                                       ↓
Server ──每 250 步轮询──→ LoadFromToml() → waterAreaMgr_
```

**优点**：文件 I/O 在 `Configure()` 中同步立即可用

**缺点**：
1. `PreUpdate()` 中 `static int reloadCounter` 跨所有实例共享 → 多模型时重载频率放大
2. `LoadFromToml` 每次重载都输出 `std::cout` 日志 → 刷屏
3. 依赖 `toml++` 外部库，TOML 格式冗余（wave 参数重复写）

---

## 方案迭代

### 尝试 1：gz-transport Topic 推送 + 3 秒 QTimer 重发

**思路**：GUI publish → Server subscribe → 回调更新。避免文件轮询。

```
GUI:  publish /keship/water_areas (StringMsg + JSON)
Server: node.Subscribe() → OnWaterAreasUpdate() → waterAreaMgr_
```

**实现**：GUI 调用 `PublishWaterAreasWithRetry()` 立即 publish + 3 秒后重发一次。Server `Configure()` 中 subscribe，回调中 `DeserializeWaterAreas` 更新 `waterAreaMgr_`。

**失败原因**：
- `Configure()` 中 subscribe 创建 gz-transport Node，**topic 发现（discovery）需要 200-500ms+**
- GUI 先发布、模型后 spawn 的典型时序中，消息在订阅建立前已发送
- 3 秒 QTimer 重发只能覆盖"模型在 3 秒内 spawn"的场景，长间隔必定失败
- **第二次加载模型成功**是因为首次加载已完成 gz-transport 发现（transport 层缓存了连接拓扑），第二次 subscribe 时连接已就绪

**结论**：`Configure()` 内部依赖 gz-transport 的异步发现是不可靠的。

### 尝试 2：gz-transport Service 同步调用

**思路**：GUI 注册 Service，Server `Configure()` 中 `node.Request()` 同步阻塞获取。

```
Server: node.Request("/keship/water_areas/get") → 阻塞等待 → 返回 JSON
```

**失败原因**：**gz-transport13 没有 `AdvertiseService` API**（编译错误：`'class gz::transport::v13::Node' has no member named 'AdvertiseService'`）。

### 尝试 3：Topic 请求 + spin-wait

**思路**：GUI 注册 request topic 的 subscriber，Server 发请求后自旋等待回复回调设置 `waterAreasConfigured_`。

```
Configure():
  node.Subscribe("/keship/water_areas")             ← 订阅回复
  reqPub.Publish(request)                           ← 发请求
  for (i=0; i<100; ++i)                             ← spin-wait 500ms
    if (waterAreasConfigured_) break;
    sleep(5ms)
```

**失败原因**：spin-wait 上限（500ms）仍小于 gz-transport 发现延迟。发现需要两次消息往返（请求 topic + 回复 topic），累计延迟远超自旋上限。增大自旋上限会在 `Configure()` 中引入不可接受的长阻塞，影响仿真启动体验。

### 尝试 4：SDF 嵌入 + Topic 推送（双通道）

**思路**：把水域 JSON 作为 `<water_areas_json>` SDF 元素嵌入模型 spawn 请求，`Configure()` 直接从 SDF 读取。Topic 仅用于运行时变更。

```
spawn: ModelManager 构建 SDF 时嵌入 <water_areas_json>...</water_areas_json>
Configure: _sdf->Get<std::string>("water_areas_json")  ← 同步读取
运行时: topic 订阅处理后续变更
```

**放弃原因**：需要改动 `ModelManager`（`BuoyancyConfig` 加字段 + `MakeBuoyancyPluginSDF` 输出 SDF 元素 + 4 个 spawn 路径设值 + 世界加载路径），增加了 Buoyancy 模块与水域模块的耦合。且方案理解的沟通成本高于实现成本。

---

## 最终方案：文件 I/O + JSON + 修复轮询

### 核心认识

**文件 I/O 是仅有的能在 `Configure()` 中同步获取状态的跨进程通信方式。`Configure()` 是同步上下文，`PreUpdate()` 在其返回后立即开始，任何依赖异步完成的初始化都不可靠。**

### 架构

```
GUI: WriteWaterAreasJson(path)
       → SerializeWaterAreas() → ofstream → JSON 文件
                        ↓
Server Configure: ifstream → DeserializeWaterAreas() → waterAreaMgr_
Server PreUpdate: 250 步轮询 → last_write_time() 检查 → 文件变更时重载
```

### 相对原始 TOML 方案的改进

| 项目 | TOML 原方案 | JSON 新方案 |
|------|------------|------------|
| 格式 | TOML（依赖 toml++） | JSON（手写解析器，零外部依赖） |
| 轮询计数器 | `static`（跨实例共享） | 成员变量（每实例独立） |
| 重载判断 | 无（每次都重载） | `last_write_time` 比较（文件未变则跳过） |
| 日志输出 | `std::cout` 无条件输出 | `gzmsg`（可通过 Gazebo 日志级别控制） |
| 代码量 | `LoadFromToml()` ~90 行 + `WriteWaterAreasToml()` ~45 行 | `DeserializeWaterAreas()` ~50 行 + `SerializeWaterAreas()` ~35 行，更精简 |

### 修改文件汇总

| # | 文件 | 操作 | 说明 |
|---|------|------|------|
| 1 | `src_cpp/utils/WaterAreaSerializer.hh` | 新建 | JSON 序列化/反序列化声明 |
| 2 | `src_cpp/utils/WaterAreaSerializer.cc` | 新建 | 手写 JSON 解析器（零外部依赖） |
| 3 | `src_cpp/utils/WaterAreaManager.hh` | 修改 | 移除 `LoadFromToml` |
| 4 | `src_cpp/utils/WaterAreaManager.cc` | 修改 | 移除 `LoadFromToml` + `<toml.hpp>` |
| 5 | `src_cpp/plugins_physics/KSPlugin_Buoyancy.hh` | 修改 | 新增 `waterAreaReloadCounter_`、`lastWaterAreasMtime_`；移除 topic 回调 |
| 6 | `src_cpp/plugins_physics/KSPlugin_Buoyancy.cc` | 修改 | `Configure()` JSON 文件同步读取；`PreUpdate()` 修复轮询（非 static + mtime 检测） |
| 7 | `src_cpp/plugins_gui/.../MenuPluginKeship.hh` | 修改 | `WriteWaterAreasToml` → `WriteWaterAreasJson`；移除 `waterAreasPub_`、`waterAreasConfigPath_` |
| 8 | `src_cpp/plugins_gui/.../MenuPluginKeship.cc` | 修改 | 所有水域变更处调用 `WriteWaterAreasJson()`；世界保存/加载适配 JSON |
| 9 | `CMakeLists.txt` | 修改 | `keship_utils` 新增 `WaterAreaSerializer.cc` |

### 各场景数据流

#### 勾选水域
```
GUI OnToggleWater(true):
  waterAreaMgr_.AddArea(默认水域)
  WriteWaterAreasJson()                    ← 写 /tmp/keship_water_areas.json
  waterVisualMgr_.SpawnAll()              ← 创建水面渲染

Server Configure() (模型 spawn 时):
  ifstream → DeserializeWaterAreas        ← 同步读取
  waterAreasConfigured_ = true
```

#### 添加自定义水域
```
GUI OnBtn_AddWaterArea():
  waterAreaMgr_.AddArea(新区)
  WriteWaterAreasJson()                    ← 覆盖写入
  waterVisualMgr_.SpawnAll()

Server PreUpdate() 下一轮询周期:
  last_write_time 检查 → 文件变更 → 重载
  waterAreaMgr_.Clear() + AddArea(新区)
```

#### 取消勾选
```
GUI OnToggleWater(false):
  waterAreaMgr_.Clear()
  WriteWaterAreasJson()                    ← 写空 JSON "[]"
  waterVisualMgr_.DeleteAll()

Server PreUpdate():
  重载 → DeserializeWaterAreas("[]") → areas 为空
  waterAreasConfigured_ = false → inWater = false → 失去浮力
```

---

## 经验教训

1. **`Configure()` 是同步上下文**：Gazebo 系统插件的 `Configure()` 在模型创建时被同步调用，其返回值后 `PreUpdate()` 立即开始。任何依赖异步完成的初始化（跨进程发现、网络握手）都会在首个 `PreUpdate()` 中暴露未完成状态。

2. **文件 I/O 是可靠的 fallback**：在两个独立进程中，文件是天然的共享状态。`ifstream` 零初始化延迟，`stat()` 不需要网络发现。即使不是最美观的方案，它是最可靠的。

3. **gz-transport 不适合启动初始化**：gz-transport 设计用于运行时的事件通知，它的发现机制本质上是异步的，不适合在 `Configure()` 这种关键路径中使用。`Configure()` 中任何 gz-transport 调用（subscribe/publish/request）都应假设可能还未完成发现。

4. **迭代记录很重要**：四种方案的失败都是有价值的排除路径。Topic 推送 → Service → spin-wait → SDF 嵌入，每一次排除都缩小了问题边界，最终收敛到文件 I/O + 修复轮询这一最小改动方案。记录每一步的失败原因可避免未来重蹈覆辙。
