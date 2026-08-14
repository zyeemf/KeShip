# 波浪场全局化重构方案（Waves Global Refactor）

> 2026-08-01 制定
> 关联问题：双体船加载后波浪满幅 16 秒（gain=0 延迟生效）— 见 `keship_issues_log.md` 2026-08-01 记录

## 一、问题背景

**现象**：勾选默认水域、波高系数=0 后加载双体船模型（ke0801_1_newdamp.ksm），船起伏剧烈约 20 秒后才稳定。

**根因链**（经日志 + server_console.log 实证）：
1. 用户设置 gain=0（仿真约 7.4s）→ WavefieldPublisher 收到并转发，但模型 21.1s 才 spawn
2. 模型浮力插件 spawn 时 wavefield 从 SDF 初始化（模型 SDF 无 `<wave>` 参数 → **gain 默认 1.0**）
3. gz-transport **不重放历史消息**，浮力插件错过 gain=0 → 只能等 Publisher 下一次周期发布（约 37.4s）
4. **16 秒波浪满幅 ±0.12m**（浮筒吃水仅 0.11m，波幅≈吃水）→ 船被波浪驱动剧烈起伏

**深层架构问题**（多模型/多水域场景下必然出错）：
- 波浪参数经 topic 传递有**时序盲区**（后 spawn 的插件收不到历史消息）
- 波浪缓起包络 t0 = **每插件首次入水时刻**（`simTimeInit_`）→ 不同时刻加载的模型看到不同波浪幅值
- 波浪参数**未按水区应用**（`ApplyWaveParamsToWavefield` 被注释）→ 多水域时所有模型共用同一套参数
- 波浪"所有权"在每模型插件副本中，**不是全局属性**

## 二、参考架构（Virtual RobotX / asv_wave_sim）

VRX 主分支（PolyhedraBuoyancyDrag）：SDF 初始参数 + topic 周期发布（PublisherPlugin every=2.0）+ 插件本地计算 —— 旧模式。

**asv_wave_sim 最新架构（gz-waves，本方案采用）**：
- 世界级 `WavesModel` 系统：创建波浪场对象 → 写入 **ECM 组件**（`components::Wavefield` 存 weak_ptr）→ 每帧 `wavefield->Update(simTime)`
- 模型级 `Hydrodynamics` 插件：`_ecm.EntityByComponents(Name("wavefield"))` → 读组件 → 拿 **WavefieldConstWeakPtr** → 用 `WavefieldSampler` 采样波高
- **波浪场是全局对象（单一所有权），模型插件只获取引用采样；无 topic、无参数副本、无缓起、无时序问题**

## 三、目标架构（对齐 gz-waves + KeShip 多水域扩展）

```
┌─ 全局（世界级）─────────────────────────────────────────────┐
│ KSPlugin_WavesModel（新，替代 WavefieldPublisher）           │
│  ├─ 每水区：Wavefield 对象（shared_ptr 强持有）              │
│  │   + 实体 "wavefield_{水区名}" + Name 组件                 │
│  │   + ECM 组件 keship::components::Wavefield（存 weak_ptr） │
│  ├─ 首次 PreUpdate：读 data/keship_water_areas.json → 建波浪场 │
│  ├─ PreUpdate 每 1s：JSON mtime 检测 → 水区增删/参数变化 →    │
│  │   建/删波浪场实体、SetWaveParams 更新                     │
│  └─ 订阅 /keship/wavefield/parameters_set（PMS，回调仅存副本）│
│     → PreUpdate 应用：带 area 更新指定水区；无 area 更新全部  │
└────────────────────────────────────────────────────────────┘
             ↑ weak_ptr（ECM 组件）引用
┌─ 每模型（消费者）───────────────────────────────────────────┐
│ KSPlugin_HydroBuoyancy                                      │
│  ├─ 删除 wavefield_ 值成员 / topic 订阅 / 缓起 t0 逻辑      │
│  ├─ 每帧：水区 Lookup → 水区变化时按名查实体                 │
│  │   （"wavefield_{水区名}"）→ 读组件 → activeWavefield_     │
│  └─ 波高：wf->ComputeDepthSimply(pos, t, 0)（全局对象采样）  │
│     波浪场引用无效 → 波高 0（静水，优雅降级）                │
└────────────────────────────────────────────────────────────┘
```

## 四、改动清单

### 1. Wavefield 类改造（`src_cpp/physics/Wavefield.hh/.cc`）
| 改动 | 说明 |
|------|------|
| `using Ptr = std::shared_ptr<Wavefield>` / `ConstPtr` | 共享对象支持 |
| 新增 `void SetWaveParams(const keship::WaveParams &_wp)` | JSON 全量设置（内部 SetXxx + Recalculate） |
| WavefieldPrivate 加 `std::mutex` | 参数写 + `ComputeDepthSimply/Directly` 读持锁（临界区短，250Hz×多模型可接受） |
| 保留 `Load(_sdf)/Load(_msg)/Topic()/Parameters()` | WaveVisual（渲染）阶段二前仍用；PMS 部分更新复用 `Load(_msg)` |
| 缓起参数 `_timeInit` 保留（默认 0） | 调用方统一传 0 → 波浪从世界 0s 缓起，所有模型一致 |

### 2. 新增 `src_cpp/physics/components/Wavefield.hh`
```cpp
using Wavefield = gz::sim::components::Component<
    std::weak_ptr<keship::Wavefield>, class WavefieldTag>;
GZ_SIM_REGISTER_COMPONENT("keship_components.Wavefield", Wavefield)
```

### 3. 新增 `KSPlugin_WavesModel.cc/.hh`（世界级系统，替代 WavefieldPublisher）
- 首次 PreUpdate（对齐 gz-waves：Configure 时实体未齐）：读 JSON → 每水区创建 Wavefield（`SetWaveParams`）+ 实体 + Name（`"wavefield_" + area.name`）+ 组件（weak_ptr）；强引用存成员
- PreUpdate 每 1s：JSON mtime 检测 → 变化 → 重读：水区新增→建、删除→`RequestRemoveEntity`+释放、参数变→`SetWaveParams`
- PMS 订阅 `_set`：回调仅保存消息副本（ECM 操作必须回仿真线程）→ PreUpdate 应用：解析 `area` 键 → 有→该水区 `Load(_msg)`（部分更新）；无（实验对话框）→ 全部水区 `Load(_msg)`
- 不做周期发布（无 topic 消费者）

### 4. KSPlugin_HydroBuoyancy 改造（`src_cpp/physics/KSPlugin_HydroBuoyancy.cc/.hh`）
- **删除**：`wavefield_` 值成员、`simTimeInit_`、`timeInitSet_`、`OnWavefield`、Configure 订阅、`LoadParameters` 中 `wavefield_.Load(_sdf)`、析构 Unsubscribe
- **新增**（Implementation）：`std::weak_ptr<Wavefield> activeWavefield_;`
- PreUpdate 水区查找块内，`currentAreaName_` 变化时：
  ```cpp
  auto wfEntity = _ecm.EntityByComponents(gz::sim::components::Name("wavefield_" + area->name));
  auto comp = _ecm.Component<keship::components::Wavefield>(wfEntity);
  activeWavefield_ = comp ? comp->Data() : std::weak_ptr<Wavefield>{};
  ```
- 波高采样：`auto wf = activeWavefield_.lock();` 有效则 `wf->ComputeDepthSimply(c, t, 0.0)`；无效 → h0=0
- 删除缓起块（保留 `simTime_` 更新）

### 5. MenuPluginKeship（`src_cpp/gui/keship/MenuPluginKeship.cc/.hh`）
- `PublishWaveParams` 加 area 参数，消息加 `(*p)["area"] = area`
- 调用点传水区名：:351（`areas[0].name`）、:1553（新水区名）、:1579（`customWaterAreas_[_index].name`）、:1637（`updated.name`）
- :1595 删除水区 → 只写 JSON 不发 PMS（波浪场已删）

### 6. 实验对话框（Stability/Inclining/Hull/Displacement）
- **不改代码**：无 area 消息 → WavesModel 更新全部水区（保持"实验期间全局静水/恢复"语义）

### 7. keship_world.sdf
- :134-150 WavefieldPublisher 插件段 → 替换为：
  ```xml
  <plugin filename="libKSPlugin_WavesModel.so" name="keship::KSPlugin_WavesModel"/>
  ```

### 8. 清理
| 文件 | 改动 |
|------|------|
| `src_cpp/physics/WavefieldPublisher.cc/.hh` | **删除** |
| `CMakeLists.txt` | 删 WavefieldPublisher 目标；新增 KSPlugin_WavesModel 目标（+ Wavefield.cc） |
| `src_cpp/utils/ModelManager.cc` | 删 3 处 `<wavefield><topic>` 段（:535-537, :667-669, :1088-1090） |
| `models/keship_03/model.sdf`、`models/waves/model.sdf` | 删 wavefield 段（静态模型文件） |
| `test/gen_test_world.sh` | 删 wavefield 段（测试 world 无波浪场=静水） |

## 五、需求覆盖验证

| 需求 | 机制 |
|------|------|
| 运行中修改波浪参数 | GUI→JSON（1s 轮询）+ PMS（立即，带 area）→ 波浪场对象更新 → 所有引用模型同步 |
| 多水域 per-area | 每水区独立波浪场实体；模型按所在水区换引用 |
| 波浪全局属性 | 波浪场由 WavesModel 独占持有，模型仅 weak_ptr 采样 |
| spawn 时序（本次 bug） | 波浪场实体在 world 启动首帧创建；模型 spawn 后首帧即查到 → 根治 |
| 多模型一致性 | 同水区共享同一对象；t0=0 统一缓起 |

## 六、风险与对策
1. **线程安全**：WavesModel（参数更新）与多模型插件（250Hz 采样）并发 → Wavefield 内部 mutex
2. **组件注册**：GZ_SIM_REGISTER_COMPONENT 在插件库加载时注册（HydroBuoyancy/WavesModel 都 include，先加载者注册）
3. **PMS 部分更新**：实验对话框只发 model+gain → 复用 `Load(_msg)`（TryGetParam 只更新消息含的键）
4. **测试 world**：无波浪场 → 采样 0（静水），优雅降级
5. **渲染 WaveVisual**：本次不改（SDF 快照+重建链路已工作）；阶段二迁移到组件模式

## 七、验证计划
1. 编译：`colcon build --packages-select keship --merge-install`
2. 场景 1（bug 复现）：勾选默认水域 + 波高=0 + 加载 KSM → waveH 第 1 帧起即 0；船 ~5s 稳定
3. 场景 2（多水域）：A 区 gain=1 / B 区 gain=0 → 模型按所在水区看波浪
4. 场景 3（运行中修改）：GUI 改水区 gain → 立即（PMS）+ 1s 内（JSON）生效
5. 场景 4（一致性）：同水区多模型 waveH 同相位同幅值
6. 场景 5（实验回归）：SP-01/02/03 打开静水、关闭恢复
7. 场景 6（测试回归）：gen_test_world 测试 world 无波浪（静水）

## 八、实施与验证记录（2026-08-01）
- 全部改动实施完成，`colcon build --packages-select keship --merge-install` 通过
- **headless 服务器验证通过**（ros_gz_sim create spawn 测试模型）：
  ```
  [Msg] KSPlugin_WavesModel: 配置完成（波浪场全局管理）
  [Msg] KSPlugin_WavesModel: 加载 1 片水区波浪场
  [Msg] KSPlugin_WavesModel: 创建波浪场 [默认水域] gain=0 entity=6
  [Msg] KSPlugin_HydroBuoyancy: 配置实体 7
  [Msg] KSPlugin_HydroBuoyancy: 进入水区 [默认水域] 波浪场引用 有效
  ```
- **诊断日志确认**（mylogs/20260801_111900/hydrobuoyancy_base_link.csv）：
  waveHeight 共 61 行采样 **全部为 0**（此前 bug：模型加载后波浪满幅 ±0.12m 持续 16 秒）
- 遗留：多水域/运行中修改/实验对话框场景需 GUI 交互实测（待用户验证）

## 九、多实例隔离（GZ_PARTITION，2026-08-01 补充）

> **⚠️ 已废弃（2026-08-02~03）**：多实例方案整体放弃，改为**单例模式**。
> start_keship.bat 闪退（`for /f` 嵌套单引号致 cmd 解析失败）+ 多实例 gz-transport 干扰无法根治，
> 最终决策：启动脚本加 `flock` 单例互斥，重复启动直接拒绝；分区机制全部回退
> （launch.py `partition` 参数、start_keship.sh `--partition`、KeshipPaths.hh 分区后缀）。
> 详见 keship_issues_log.md（问题日志未随仓库分发）专题章节。以下内容仅存档。

**问题**：水域胡乱晃动（未加载模型）。根因：多个 gz sim 实例在同一 gz-transport 总线时，
`/world/keship_world/create`、`/world/keship_world/remove`、`/scene/info` 等**同名服务**跨实例互通，
GUI 的 Waves 模型 spawn/删除可能路由到错误实例 → 水面模型反复增删/重叠 → 晃动。
与本次重构无关（历史遗留），残留孤儿进程会触发（如验证脚本 kill 失败）。

**方案**：GZ_PARTITION 环境变量隔离（gz-transport 标准机制）——不同分区的进程完全互不可见。

改动：
| 文件 | 改动 |
|------|------|
| `~/keship_ws/start_keship.sh` | 新增 `--partition <名称>` 参数（默认 `keship`），`export GZ_PARTITION` |
| `src/keship/launch/keship.launch.py` | 新增 `partition` launch 参数（优先级：参数 > 环境变量 > 默认），`os.environ['GZ_PARTITION']` 全局设置 |
| `program/start_keship.bat` | 支持实例名参数（`start_keship.bat keship2`） |
| `program/start_keship.vbs` | 顶部 `PARTITION` 常量可改 |

**验证**（headless 双实例，partition=keship_a / keship_b）：
- 向 A 发 create → A 场景出现 `iso_test_model`；B 场景 **No models in world** ✓ 完全隔离
- 残留进程 0 ✓

**使用方式**：
- 单实例：双击 `start_keship.vbs`（默认分区 keship），无需改动
- 多实例：第二个实例用 `start_keship.bat keship2` 或
  `GZ_PARTITION=keship2 ~/keship_ws/start_keship.sh`

## 十、多实例隔离加固（A+B+C，2026-08-02 补充）

> **⚠️ 已废弃（2026-08-02~03）**：同第九章——多实例方案放弃，改为单例模式，A/B/C 全部回退。

**问题复现**（2026-08-02 双实例测试）：两实例均用默认分区 keship → gz-transport 服务互通
（scene 查询/set_pose 跨实例路由 → 实例 2 "自动出现"实例 1 的双体船、双体船跑飞）；
共享 JSON 被实例 2 启动时空覆盖（`LoadConfig` 无条件 `WriteWaterAreasJson`）→
波浪场反复增删 → 水域抖动；且实例 1 的水域配置（gain=0）被覆盖丢失。

**加固改动**：
| 方案 | 文件 | 内容 |
|------|------|------|
| A | `program/start_keship.bat` | 启动前检测 WSL 中 gz sim 实例数（`pgrep -f 'gz sim'`）；>0 时**提示输入实例名**（默认 keship2） |
| B | `include/keship/utils/KeshipPaths.hh` | `KeshipWaterAreasFile()` 按 GZ_PARTITION 隔离：≠keship 时用 `keship_water_areas_<partition>.json`（9 处调用点自动一致；单实例路径不变） |
| C | `MenuPluginKeship::LoadConfig` | 启动时若 waterAreaMgr 为空 → **从 JSON 恢复**（而非写空覆盖）；else 分支写回恢复内容 |

**验证**（headless 双实例，server_console.log 确认）：
- 实例 A（keship，非空 JSON）：`加载 1 片水区波浪场` + `创建波浪场 [默认水域] gain=0` ✓
- 实例 B（keship2，独立 JSON 不存在）：无波浪场（0 片）✓ 配置隔离
- B 启动后 A 无新增日志、无"移除"；A 的 JSON mtime/内容全程未变 ✓
- 残留进程 0 ✓

**说明**：
- 之前（2026-08-02 07:46）双实例事故导致 data/keship_water_areas.json 被覆盖为空，
  用户需重新勾选水域（B+C 方案防止此类丢失再发生）
- 竞态：两实例几乎同时启动时 bat 检测可能均为 0（极端场景）；B+C 使同分区时损害最小化
