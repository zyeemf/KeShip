# KSW 0.0.4 保存/载入链路修复（A/B/C + 树结构定稿）

> 日期：2026-08-09
> 状态：✅ 用户实测通过（"符合预期"）
> 关联：ksw_v4_world_manifest_plan.md（0.0.4 全新格式实施，方案文档未随仓库分发）

---

## 一、A：保存/载入对话框树"顶层叶子"挂接 bug

### 1.1 现象
保存/载入世界的树勾选界面中，**顶层/无链模型与水域叶子不可见、不可勾选**；
表现为载入时只加载了水面（水域），模型全部缺失；保存时顶层实体无法被勾选保存。

### 1.2 根因
`buildTree()` 中叶子用 `new QTreeWidgetItem(parentItem)` 创建，当 `parentItem == nullptr`
（顶层实体、无分组）时，节点**未挂入树**（不在任何父节点下）→ QTreeWidget 不显示、不可勾选、
也不参与收集。

### 1.3 修复
顶层叶子改由 `tree_->addTopLevelItem(leaf)` 挂接；同时统一"按子 ksw 链分组"逻辑：
- 走 `findOrCreateGroup(_parent, text)` 分段/查找/创建；`_parent == nullptr` 时查/建在树顶层。
- 空链实体归入分类组，非空链在链组内建"模型/水域"分类（详见第五节结构定稿）。

对应函数：
- `SaveWorldDialog::buildTree`（`SaveWorldDialog.cc` 约 L220 起，含 `findOrCreateGroup`、`catGroupOf`）
- `LoadWorldDialog::buildTree`（`LoadWorldDialog.cc` 约 L159 起，同构）

### 1.4 回归
修复后勾选树的叶子（模型/水域）全部可见可勾选，收集正常（`CollectCheckedLeaves` 只认 `UserRole` 水域标记 + 勾选态，递归遍历全部顶层项，不依赖分组文本）。

---

## 二、B：保存树显示"原名"，实例名仅作提交键

### 2.1 背景
模型在场景中的实例名（`modelList` 键）由 `spawn` 时按命名空间/重命名机制生成（如 `KeShip03n`），
与用户命名的原名（JSON 原键名/`.ksm` 文件名）不一致；v3 时代保存树直接显示实例名，用户辨认困难。

### 2.2 结构（保存侧行格式，4 段）
```
子World链 \t 勾选(1/0) \t 原名 \t 实例名
```
- 原名 = `WorldModelSource.baseName`（来源元数据中的 JSON 原键名，缺省倒退实例名）
- 实例名 = `ModelManager` 内部缓存键/SDF 查找/保存的真实名
- 兼容旧 3 段行（无第四段 → 原名=实例名）
- 水域行：`链 \t 勾选 \t 水域名`（兼容仅名字列表 = 默认全勾选）

### 2.3 界面
- **叶子显示**：原名（`displayName.section('/', -1)`，只显示最后一段）
- **收集（提交）**：`CollectCheckedLeaves` 返回叶子 `UserRole+1` 存的完整键（模型=实例名，水域=全链名）

行构造位于 `MenuPluginKeship::OnBtn_SaveWorld`（`MenuPluginKeship.cc` 约 L531，注释于 533）。

---

## 三、C：载入顺序重排 + 中文 topic 防护

### 3.1 现象
载入含水域的世界时水体模型初始"跳起/浮起"：水面还没 ready 时动态模型先被放置，
后续水面/水区出现 → 模型自由落体/弹跳收敛。

### 3.2 修复：载入顺序（`OnBtn_LoadWorld`）
1. 解析清单（模型 + 水域）；
2. 水域先建（`waterAreaMgr_`）+ 水面 `SpawnAll`（水面就绪）→ `Publish` 波浪参数；
3. 模型按 `isStatic` **静态优先**：`stable_partition`（先 static 后非 static）逐个 spawn；
4. **最后** `resume` 仿真（此前保持暂停，模型到位后才开始物理）。

### 3.3 中文命名空间 → ROS 2 非法 topic 崩溃
- 现象：命名空间含中文（如"新增/A"）时，rclcpp/gz-transport 构造 topic 名字失败/崩溃。
- 防护两级：
  1. **输入拦截**：载入对话框命名空间默认留空（不再预填世界名），正则校验
     `[A-Za-z0-9_/]*`（仅字母数字下划线斜杠，中文/空格禁输）；
  2. **兜底净化**：新增 `utils/TopicUtil.hh`（inline `keship::SanitizeRosTopic`，仅保留
     `[A-Za-z0-9_/~{}]` 与 `/`，其余字符（含中文）替换为 `_`），应用于 6 处 topic 构造：
     - `CameraViewerDialog.cc`（约 34）
     - `LidarViewerDialog.cc`（约 122）
     - `TaskMonitorDialog.cc`（约 664）
     - `SensorOverlayPlugin.cc` ×2（约 153/189）
     - `ModelManager.cc` `MakeSensorSDF` 传感器 topic（防御项）
     （注：`SensorOverlayPlugin` 当前不在 CMake 构建目标中，改动为防御性，无编译影响）

---

## 四、树结构定稿：模型 / 水域 / 子ksw 平铺（2026-08-09）

> 用户决策：**去掉中间"新增"容器**，顶级平铺，与即将实施的"模型管理面板"层次树统一。

### 4.1 保存对话框（无根节点：世界名可编辑，根无意义）
```
模型          ← 顶层新增/无链模型（原名）
水域          ← 顶层水域
子ksw1        （链组，组内"模型""水域"分类，多级递归）
子ksw2 …
```

### 4.2 载入对话框（保留世界名根节点 —— 载入来源需要展示）
```
<世界名>     ← 根（最高层级）
├─ 模型      ← 顶层无链
├─ 水域
└─ 子ksw1 …
```

### 4.3 实现：`catGroupOf(chain, type)`
- `chain` 空 → 顶层建/取"模型"或"水域"分类组（保存）或世界名根下（载入）
- 否则按 `/` 分段找/建链组，再在链组内建"模型/水域"分类组
- 收集：`CollectCheckedLeaves` 递归全部顶层（含新增的"模型/水域"分类组），按叶子 UserRole
  标记区分模型/水域，模型取 UserRole+1（实例名），水域取 UserRole+1（全链名）

### 4.4 同步
- 保存侧 `MenuPluginKeship.cc:533` 注释从"新增 分组（顶层）"改为"顶层"模型"分组"
- 两个 `.hh` 头注释同步（结构图 + 参数说明）

---

## 五、涉及文件

| 文件 | 改动 |
|------|------|
| `src_cpp/gui/keship/editors/SaveWorldDialog.cc/.hh` | 树挂接修复、原名/实例名 4 段行、`catGroupOf`、注释 |
| `src_cpp/gui/keship/editors/LoadWorldDialog.cc/.hh` | 同款修复、命名空间默认空 + 正则校验、保留根节点 |
| `src_cpp/gui/keship/MenuPluginKeship.cc/.hh` | OnBtn_SaveWorld 行构造（原名）、OnBtn_LoadWorld 顺序重排、注释同步 |
| `src_cpp/utils/TopicUtil.hh` | 新建：`keship::SanitizeRosTopic` |
| `src_cpp/gui/keship/viewers/CameraViewerDialog.cc`（约 34）、`LidarViewerDialog.cc`（约 122） | topic 净化 |
| `src_cpp/gui/keship/tasks/TaskMonitorDialog.cc`（约 664） | topic 净化 |
| `src_cpp/gui/sensor_overlay_plugin/SensorOverlayPlugin.cc`（约 153/189） | topic 净化（防御，未编译） |
| `src_cpp/utils/ModelManager.cc` `MakeSensorSDF` | topic 净化（防御） |

---

## 六、验证

1. 保存对话框：空链模型/水域显示在顶层"模型""水域"组，子 ksw 实体在其链组下（无"新增"）
2. 载入对话框：世界名根 → 顶层"模型/水域"组 + 各子 ksw 链组（无"新增"）
3. 勾选 → 保存 → 载入全流程往返一致（位置/水区/子链）
4. 载入顺序：水面先就绪、静态先、动态后、最后 resume → 模型不再"跳起"
5. 命名空间输入框默认空、禁中文、可 `/` 分层；极端中文输入不崩溃（topic 被净化）
6. 用户实测：全部"符合预期" ✅

---

## 七、后续（D：模型管理面板层次树）

面板将沿用本节结构（模型/水域/子ksw 平铺 + 折叠），实现"保存子世界"导出（
`SaveWorldManifest` 增加可选 `_rootChain` 参数剥离前缀）。见规划中的 D 讨论。