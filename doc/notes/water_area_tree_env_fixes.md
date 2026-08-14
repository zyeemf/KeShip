# 水域 UI 系列：层次树 / 按钮链路 / 环境参数入口 / 启动空场景 / 海况等级一致

> 日期：2026-08-10~11
> 状态：✅ 用户实测通过（"符合预期"）
> 关联：panel_hierarchy_tree_plan.md（层次树方案实施，方案文档未随仓库分发）、SeaState.hh 单元测试 test_sea_state.cc（测试源不入库）

---

## 一、层次树折叠修复（MenuPluginKeship.qml）

### 1.1 问题
- 文字过窄被截断/空白行（文本宽度未留保底）；行高过大；缩进 `anchors.left` 未生效（缺挂载）；
- 折叠展开不完整：仅部分组可折叠，子世界链内 models/waters 组行不受折叠控制。

### 1.2 修复
- 文字宽度 `Math.max(...)` 保底；模型行按钮区压至 36px；Row 补 `anchors.left`；
- 折叠展开全面化：三类组键统一 `chain + "/" + gtype`（models/waters/sub），默认全展开；
- 搜索模式下跳过折叠过滤（搜索语义优先）；
- 子世界链内 models/waters 组行自身可见性受 `treeExpanded[chain + "/sub"]` 控制（闭合判定链）。

---

## 二、水域按钮链路（MenuPluginKeship.cc / QML）

| 问题 | 修复 |
|------|------|
| "添加默认水域"按钮被误删 | 恢复 `OnBtn_AddDefaultWaterArea()`：一键 250×250 矩形、固定名"默认水域"、查重防御（同名已存在则拦截并日志）、`enabled: customWaterAreaCount === 0` |
| 删除水域后按钮仍灰 | `OnBtn_DeleteWaterArea` 按名字 `remove_if` 全删 customs 同名 |
| 启动时树 base=0 不显示 | `LoadConfig` 恢复+SpawnAll 后补 `NotifyModelListChanged()` |
| 恢复的水区不可编辑/删除 | **水域模式态体系拆除**：删除 `showWater_`/`OnToggleWater`/`ShowWaterChanged`/`isShowWater`/Q_PROPERTY 全套；`LoadConfig` 恢复循环无条件恢复全部水区并 push `customWaterAreas_` |

### 2.1 WaterAreaSerializer 版本化
- 新格式 `{"version":2,"areas":[...]}`；`DeserializeWaterAreas` 遇旧格式裸数组一律丢弃并警告；
- 单测（Serializer 往返）ALL PASS，临时文件已清理。

---

## 三、环境参数入口（2026-08-10）

### 3.1 功能
水域树行新增"环境"按钮（海况列与"编辑"之间；Text 宽度公式预留 130→178）→ 槽
`OnBtn_ShowWaterEnv(int _index)`：
1. `EnvironmentParamsDialog` 通用化（构造入参 `WaterArea`，`getWaterArea()` 只改风浪流字段，名称/几何不变）；
2. customs 同名同步 → `Areas` 重建 → `RebuildWaterAreas`（JSON 持久 + 水面重建）→ `PublishWaveParams`（gazebo 即时生效）。

### 3.2 涉及文件
- `MenuPluginKeship.hh/.cc`（槽声明/实现）、`MenuPluginKeship.qml`（按钮）
- `editors/EnvironmentParamsDialog.hh/.cc`（对话框；与 `WaterAreaDrawDialog` 职责分离：后者几何+名称）

---

## 四、启动即空场景（2026-08-11）

### 4.1 需求
关闭 gazebo 时场景中的水域，下次启动**不得复活**——启动 keship 时场景为空。

### 4.2 根因
`LoadConfig` 启动时从 `data/keship_water_areas.json` 恢复水域（AddArea + customs push + SpawnAll），
原注释为"重启/多实例继承；避免启动时空写覆盖共享文件"——单机场景下成为"水域复活"的来源。

### 4.3 修复
- 删除 `LoadConfig` 的恢复块（`DeserializeWaterAreas` 不再在 GUI 侧用于启动恢复）；
- 启动直接 `WriteWaterAreasJson()` 写空 `{"version":2,"areas":[]}`：清掉上次会话残留数据，同时
  通过文件 mtime 变化通知物理插件清场：
  - `KSPlugin_WavesModel`：PreUpdate 每 1s 轮询 `last_write_time` → `RebuildFromJson` 移除旧波浪场实体；
  - `KSPlugin_HydroBuoyancy`：每 250 帧检查 mtime → 重载水区（空 → `waterAreasConfigured_=false`）；
- 启动时序竞态（服务器先读旧 JSON 建旧场 → GUI 后写空）由上述热更新在 1s 内收敛，最终
  数据/视觉/物理三者一致，无撕裂残留。

### 4.4 不受影响
载入含水域 ksw（LoadWorld 登记 customs + 写 JSON + SpawnAll）、添加默认水域/自定义绘制、编辑、
删除、保存世界（勾选水区 → ksw 内持久）全链路。

---

## 五、海况等级一致（选择 = 生成）（2026-08-11）

### 5.1 现象
对话框选"1级·微浪"确定后，等级显示/参数反推为 **2 级**，与选择不一致。

### 5.2 根因
1 级 Hs 代表值 0.05m 对应 PM 理论 Tp = sqrt(0.05/0.04) ≈ 1.12s，低于渲染下限
`kMinTp = 2.5s`（波长 λ ≈ 9.76m 须 ≥ 水面网格步长，否则"一格一格"渲染）→ Tp 被截断为 2.5s；
而 `onSeaStateSelected` 波高增益**写死 1.0** → 反推 `Hs = 0.04 · 1.0 · 2.5² = 0.25m` → 落入
2 级小浪带 [0.1, 0.5) → 显示 2 级。其余等级往返"侥幸一致"（spin 1 位小数取整均未跨界）。

### 5.3 修复（SeaState.hh + 对话框 + 单测）
1. `SeaState::ParamsFromLevel` 新增 **4 参重载**（`&gain`）：
   `gain = MidHs(level) / (0.04 · Tp_actual²)`（Tp 为截断后实际值）→ Hs 恒等于该级中值 →
   选择等级恒等于反推等级；无截断时增益数学上恰为 1.0（原语义不变）；0 级设 0。
   原 3 参版本委托 4 参（`nullptr`）。
2. `EnvironmentParamsDialog::onSeaStateSelected` 改用 4 参并写入 `waveGainSpin_`；
   消息框显示真实增益（`f,2`）；1 级时提示"周期低于渲染下限 2.5s，增益自动补偿，等级对应不变"。
3. 单测（test_sea_state.cc）新增 0~9 级全往返断言：选级 → 参数 → `LevelFromHs(HsFromParams)` 恒等于所选等级
   （含 1 级截断补偿 gain=0.2 / 4 级无截断 gain=1 / 0 级无浪）→ **ALL PASS**。

### 5.4 默认水域参数标准化
`OnBtn_AddDefaultWaterArea` 与 `WaterAreaDrawDialog` 构造（自定义绘制）的初始 waveParams 原为零构造默认
（`gain=0.05, period=5.0` → Hs=0.05 显示 1 级，但参数与"选 1 级"不同：周期 5s vs 2.5s、增益 0.05 vs 0.2、
风速 0 vs 1.42m/s）。
修复：两条添加路径统一调用 `ParamsFromLevel(1, &period, &windSpeed, &gain)` 初始化
（周期 2.5s / 增益 0.2 / 风速 1.42m/s）——显示 1 级不变，参数与对话框选"1级"完全一致；
用户后续在编辑器中手动修改则以用户值为准。旧数据/载入 ksw 不受影响。

### 5.5 验证
- 单测 ALL PASS（含新往返断言）；
- 用户实测：海况选择各等级、"添加默认水域"后打开环境对话框参数与标准 1 级一致 ✅。
