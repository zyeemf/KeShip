# 结构树交互改造方案（美化 / 按钮收拢 / 子世界属性删除 / 层次调整 / 复制）

- 日期：2026-08-18（v2：补充问题 6「Rebuild 丢失子世界来源」根因与修复设计）
- 状态：方案定稿，待实施
- 范围：`keship_ws/src/keship`（GUI QML + C++）

## 1. 背景与需求

模型管理 Tab 的结构树（`MenuPluginKeship.qml` 中 ListView 平铺模拟树，数据源
`MenuPluginKeship::GetWorldHierarchy()`）存在 6 个问题：

1. 多个模型 / 水域 / 子 world 并存时树展示不好看（平铺无树线、组行仅 +/- 文本、无图标、无选中高亮）；
2. 每个实体行按钮过多放不下（模型行 7 个按钮：属性/浮力体/推进器/流体动力/传感器/保存/删除；
   水域行 3 个；子 world 行 1 个），需收拢为下拉列表；
3. 子 world 只有保存按钮，缺删除按钮与属性按钮（可修改名称、位姿等加载时填的参数）；删除时应整链删除；
4. 结构树层次不可调：期望能把模型放入某个子 world、或把子 world A 中的模型移到子 world B；
5. 模型、水域、子 world 均需复制功能；
6. **加载 ksw 后修改模型属性（惯性）确认时模型被 Rebuild，结构树中模型会跑到子 world 之外**
   （浮力体/推进器/流体动力/传感器编辑器同样受影响）。

## 2. 关键事实（现状约束）

- "子 world" 是**纯逻辑链**：实体仍独立 spawn 在 `keship_world` 顶层，链仅记录在
  `ModelManager::WorldModelSource.path`（模型）与 `WaterWorldSource.path`（水域）元数据中；
  树按此分组显示，保存时按此建 `xxx.ksw/` 嵌套文件夹。
- 因此**移动/改名只改元数据即可，无需重新 spawn 实体**；实例名是内部键（缓存/SDF 查找），
  显示名与保存键用 `baseName`。
- 树行 `midx` / `widx` 索引与 `std::map<name,id>` 遍历顺序一致（`GetWorldHierarchy` 与
  `GetModelNameAndIDByIndex` 同序），现有 C++ 槽可安全按索引调用。
- 保存世界 JSON 键名 = `baseName`（同链下必须唯一，复制件 baseName 需加后缀防键冲突）。
- 删除模型 `DeleteModelInGazeboByName` 内部已清理 `modelSource_`；水区删除需新增
  `ModelManager::RemoveWaterSource(name)`。
- **问题 6 根因**（`ModelManager.cc` 代码级确认）：`RebuildModelWithAllPlugins`（:3289）保存位姿后
  调用 `DeleteModelInGazeboByName`（:1522，成功时无条件 `modelSource_.erase`，:1547）→ 重新组装
  SDF spawn，但**从不恢复 source** → 树分组丢失（跑到顶层）。受影响的 5 个重建路径：
  `RebuildModelWithAllPlugins`（属性对话框改惯性）、`RebuildModelWithBuoyancy`（:1800）、
  `RebuildModelWithThrust`（:2600）、`RebuildModelWithHydro`（:2972）、`RebuildModelWithSensors`
  （:3048）。仅改位姿走 `SetModelPoseByIDInGazebo`（:840）不重建，不受影响。

## 3. 方案

### 3.1 树美化（保留平铺架构，重写 delegate 视觉）

- 组行（子树头：模型/水域/子 world）：
  - 左侧色条 + 折叠箭头图标 + 加粗文字；
  - 三色区分：模型组 `#c8d8e8`、水域组 `#c8e8d8`、子 world 组 `#e8d8c8`；
- 叶行：
  - 缩进区按 `depth` 逐级绘制 1px 树连接竖线（Repeater）；
  - 行高 30 → 32；hover 高亮 + 选中态 `#b8d0f0`；
  - 名称前类型色块图标（模型=蓝、水域=青）；
  - 水域海况列保留；
- 折叠（`treeExpanded`）与搜索（`buildRows` / `treeFilter`）逻辑原样保留。

### 3.2 按钮收拢为 "⋯" 下拉菜单

- 每行右侧固定 "⋯" 按钮（宽 24px）→ QML Menu（若 gz-gui 卡片内层级异常则退回自定义 Popup）；
- 菜单项按 `kind` 动态生成，全部复用现有 C++ 槽 + 新增槽：

| 行类型 | 菜单项 |
|---|---|
| 模型 | 属性 / 复制 / 移动到子世界 ─ 浮力体* / 推进器* / 流体动力* / 传感器 / 保存 / 删除（* 按 isStatic / mathHull 显隐） |
| 水域 | 环境 / 编辑 / 复制 / 移动到子世界 / 删除 |
| 子 world | 属性 / 保存子世界 / 复制 / 删除 |

- 删除类菜单项点击前弹确认框（QML Dialog）。

### 3.3 子 world：属性 + 删除

- **属性**（新 `SubWorldSettingsDialog`，C++ QDialog，参数对应加载时填写项）：
  - 名称：链段改名（字母/数字/下划线正则校验）；确定后遍历链下全部模型/水域更新
    `source.path` 前缀（纯元数据）；
  - 位姿：整体偏移 dx/dy/dz/droll/dpitch/dyaw → `ModelManager::SetModelPoseByIDInGazebo`
    应用到链内全部模型（角度换算弧度）；
  - 链内实体只读清单（模型/水域数量与名称）。
- **删除**：确认框 → 递归收集链下全部模型逐个 `DeleteModelInGazeboByName`（内部清理
  modelSource）+ 链下全部水域走现有水区删除逻辑（customs 移除 + `waterAreaMgr_.Clear()`
  重建 + 新增 `RemoveWaterSource` 清理水域来源）→ `RebuildWaterAreas()`。
  链无物理节点，实体删净后链自然消失。

### 3.4 层次调整（移动）

- 新 `MoveToChainDialog`（C++ QDialog）：目标链下拉（现有链列表）+ 可编辑输入新建链 + "顶层"选项；
- 模型/水域移动：`OnBtn_MoveModelByIndex(midx, chain)` / `OnBtn_MoveWaterByIndex(widx, chain)` —
  仅更新 `source.path` 元数据 + `NotifyModelListChanged()`，不重 spawn；
- 子 world 整体移动：`OnBtn_MoveSubWorld(chain, targetParentChain)` — 链下全部实体 path 前缀替换；
- 约束：目标链下已存在同名（baseName）实体时拦截并提示。

### 3.5 复制（复制时弹属性对话框，可命名与改位姿）

- **模型** `OnBtn_DuplicateModelByIndex(midx)`：
  - 弹现有 `ModelSettingsDialog`（Ksm 模式）：默认名称 = 原名_副本（去重 _副本2…）、
    默认位姿 = 原件当前位姿（需给对话框新增 `setPose()` 预填）；
  - 确认后复制全部缓存配置（SDF/浮力/推进/流体动力/传感器/静态标记），新实例名下缓存；
  - 按配置走 `SpawnModelWithThrust` / `SpawnModelWithBuoyancy` / `SpawnModelInGazeboFromSDFString`；
  - 复制 `source` 元数据（path 相同，baseName = 新名，避免同链 JSON 键冲突）；
  - 实例名冲突由对话框名称列表校验拦截。
- **水域** `OnBtn_DuplicateWaterArea(widx)`：
  - 新轻量 `DuplicateWaterDialog`（C++ QDialog）：名称（默认原名_副本，去重）+ dx/dy/baseHeight
    微调；几何（bounds/vertices）、风浪流参数整体继承；
  - 复制 `WaterArea` → `customWaterAreas_` + `waterAreaMgr_.AddArea` + 复制 `waterSource`
    （path 相同，baseName = 新名）+ `RebuildWaterAreas()` + `PublishWaveParams(新水区)`。
- **子 world** `OnBtn_DuplicateSubWorld(chain)`：
  - 弹对话框（新链名 = 原名_副本，去重 + 整体位姿偏移）；
  - 链下每个模型/水域套用上述复制流程，path 前缀替换为新链，实体位置 = 原位置 + 偏移。

### 3.6 问题 6 修复：Rebuild 保留来源元数据

- `DeleteModelInGazeboByName(name, bool _preserveSource = false)` 加第二参数：
  仅 `_preserveSource=true` 时跳过 `modelSource_.erase`；
- 5 个 Rebuild 函数调用点传 `true`（Buoyancy/Thrust/Hydro/Sensors/AllPlugins）；
- 真删除路径（`DeleteModelInGazeboByIndex` / `DeleteAllModelsInGazebo` / UI 删除）保持默认
  `false` 清理语义不变；
- `sdfCache_` 本就保留（Rebuild 依赖），无回归；修复后载入 ksw → 任何编辑器改动确认
  → 树仍留在原子 world 链内，保存/载入世界链结构还原一致。

## 4. 文件改动清单

| 文件 | 改动 |
|---|---|
| `src_cpp/gui/keship/MenuPluginKeship.qml` | 树 delegate 重写（美化 + ⋯ 菜单）+ 新槽调用 + 确认框 |
| `src_cpp/gui/keship/MenuPluginKeship.hh/.cc` | 新增 8 槽（复制×3 / 删除 / 属性 / 移动×3）+ 私有辅助（链收集、去重命名、移动/复制核心逻辑、水区删除清理 waterSource） |
| `src_cpp/gui/keship/editors/SubWorldSettingsDialog.{hh,cc}` | **新增**：子 world 改名 + 整体位姿偏移 + 实体清单 |
| `src_cpp/gui/keship/editors/MoveToChainDialog.{hh,cc}` | **新增**：目标链选择（下拉 + 新建 + 顶层） |
| `src_cpp/gui/keship/editors/DuplicateWaterDialog.{hh,cc}` | **新增**：水区复制命名 + 位置微调 |
| `src_cpp/gui/keship/editors/ModelSettingsDialog.hh/.cc` | 新增 `setPose()` 预填复制位姿 |
| `src_cpp/utils/ModelManager.hh/.cc` | 新增 `RemoveWaterSource(name)`；`DeleteModelInGazeboByName` 加 `_preserveSource` 参数 + 5 处 Rebuild 调用点传 true |
| `CMakeLists.txt` | 注册 3 个新 .cc |
| `doc/keship_plans.md` | 本方案索引条目 |

## 5. 验证清单

1. `colcon build --packages-select keship` 编译通过；
2. `start_keship.sh --no-build` 启动，手动验证：
   - 树视觉：组行配色/树线/选中高亮/折叠/搜索；
   - 各菜单项动作正确（属性/浮力体/推进器/流体动力/传感器/保存/环境/编辑/删除）；
   - 模型/水域/子 world 复制：弹对话框默认名与位姿正确、去重命名、spawn 成功、树刷新；
   - 移动：模型/水域移到已有链、新建链、移到顶层；子 world 整体移动；同名冲突拦截；
   - 子 world 属性：改名后树更新、保存/载入世界链还原一致；整体偏移生效；
   - 子 world 删除：整链模型+水域删除、树刷新、水面重建；
3. 保存世界 + 载入世界回归：JSON 无键冲突、链结构还原正确；
4. **问题 6 回归**：载入 ksw（含子 world 链）→ 改模型属性（惯性）确认 → 树中模型仍在原子
   world 内；浮力体/推进器/流体动力/传感器编辑器改动后同样不丢链；保存/载入世界链还原一致。
