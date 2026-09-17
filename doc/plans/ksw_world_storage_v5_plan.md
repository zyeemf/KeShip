# KSW 世界存储改造方案（V5：命名空间层级化 + 世界目录后缀化）

> 状态：**已实施（V5 基础 + V6 补充，测试符合预期）**
> 日期：2026-08-12
> 范围：MenuPluginKeship.cc / ModelManager.cc / SaveWorldDialog.cc / LoadWorldDialog.cc
> 后续补充：见 `ksw_three_names_plan.md`（三名体系 + P1/P2/P3 + R1 系列加固）

---

## 一、背景与目标

### 1.1 现状问题（实证确认）

对用户实际产物 `models_ksw/world0812/` 检查发现：

| 位置 | 实际内容 |
|---|---|
| `keship_world.ksw` JSON 键 | `world_Keboat`（带前缀） |
| ksm 文件名 | `world_Keboat.ksm`（带前缀） |
| ksm 内 SDF `<model name>` | `Keboat`（**原名**） |

对比 `models_ksw/W0811_1/`：键 = `KeBoat03`（无前缀）。同一模型家族两种结果。

**根因链**：
1. `OnBtn_LoadModel` 加载 KSM 时把缓存 SDF 的 `<model name>` **替换为用户输入的实例名**（cc:994-1002）
2. 直接加载的模型**无来源登记**（`SetModelSource` 仅存在于载入 ksw 路径 cc:637）→ `SaveWorldManifest` 的 `leafName = baseName.empty() ? name : baseName` 回退到**实例名**作键/文件名
3. 载入 ksw 时输入的命名空间（如 `world1`）只进实例名（`world1_boat`），**不进树层级、不进 JSON 结构**，"world1 下的 boat"落空
4. 源 ksw 键一旦被污染，`baseName` 固化前缀，链式传染

### 1.2 目标

三层语义严格分离：
- **原名 baseName**（boat）= 文件内标识（JSON 键、ksm 名、SDF `<model name>`）
- **层级**（world1）= 树结构 / JSON 嵌套（subWorlds）
- **实例名**（world1_boat）= 仅运行时 spawn 唯一性，靠来源登记关联

---

## 二、已确认的设计决策

| # | 决策 | 内容 |
|---|---|---|
| D1 | 世界名层级化 | 保存为 `subWorlds.world1.models.boat`（选项1：子世界嵌套） |
| D2 | 版本兼容 | **不兼容旧版 ksw**（旧无后缀目录、旧清单名均不再读取） |
| D3 | 世界目录后缀 | 文件夹 = `<世界名>.ksw/`；清单文件 = `keship_world.kswf`（ksw file） |
| D4 | 保存交互 | 用户只输入名称，目录自动拼接+自动建（无"是否创建"提示）；**同名世界提示是否覆盖** |
| D5 | 加载交互 | 直接选 `<世界名>.ksw` **文件夹**加载（不用点进去选文件） |
| D6 | 命名空间输入 | **禁止 `/`**，仅字母数字下划线（validator `[A-Za-z0-9_]*`）→ ns 恒单段 |
| D7 | 命名空间预填 | 载入对话框预填 ksw 世界名（attributes.name，合法时） |
| D8 | 跨链同名 | 不同树/链下允许重名（模型与水域），由文件夹/JSON 结构隔离 |
| D9 | 子世界文件夹 | 仅作 ksm 存放结构，无独立清单、不可单独加载；独立导出用"保存子世界" |

---

## 三、核心机制

### 3.1 实例名前缀去重（mergeSegs）

命名空间（ns，恒单段）与 ksw 链（path）合并时去重叠段：

```
ns=[world1] + path=[world1, subA] → 合并 [world1, subA] → 实例名 world1_subA_boat
ns=[world1] + path=[world1]       → 合并 [world1]       → 实例名 world1_boat
```

当前实现（尾首匹配，D6 下已足够）：`while (!rest.empty() && !_ns.empty() && rest.front() == _ns.back()) rest.erase(rest.begin());`

### 3.2 保存目录结构（H2）

```
models_ksw/<世界名>.ksw/
├── keship_world.kswf                    ← 清单（M2）
├── <顶层模型原名>.ksm                    ← 无链模型直接放顶层
├── world1.ksw/                           ← 链一层一个 .ksw 文件夹
│   └── boat.ksm                          ← 原名，文件夹隔离
└── world2.ksw/
    └── boat.ksm                          ← 跨链同名不冲突
```

JSON `file` 字段 = 相对路径：`"world1.ksw/boat.ksm"`（多层链：`world1.ksw/subA.ksw/boat.ksm`）。

### 3.3 SDF 名称解耦（H1 修正版）

- **运行时**：缓存 SDF 的 `<model name>` = **实例名**（spawn / 编辑重建兼容，见 F1/F2）
- **保存时**：`SaveModelToKSM` 写 `===BASE_SDF===` 段时把 `<model name>` 替换为**原名**（来源 baseName）
- 效果：ksm 文件内 name 永远 = 原名 → 再次加载 `originName` 提取到原名 → 前缀永不固化

---

## 四、完整改动方案

### 4.1 H1：SDF 名称解耦（前缀污染根治）

| # | 改动 | 文件 |
|---|---|---|
| H1-1 | **`SaveModelToKSM` 加参数 `_sdfName`**（保存原名，默认空）：写 BASE_SDF 时替换 `<model name>` 为 `_sdfName`（非空且不同时） | ModelManager.hh / ModelManager.cc |
| H1-2 | 调用点① `SaveWorldManifest`（cc:3464）传 `leafName`（原名） | ModelManager.cc |
| H1-3 | 调用点② `OnBtn_SaveModel`（cc:1884）查来源 `baseName`，无来源传空 | MenuPluginKeship.cc |
| H1-4 | **`OnBtn_LoadWorld`**（cc:596）：`CacheModelSDF` 前把 baseSDF 的 `<model name>` 替换为实例名（修复 F2 现存 bug，与 OnBtn_LoadModel 行为统一） | MenuPluginKeship.cc |
| H1-5 | `OnBtn_LoadModel`：**保留**替换块（spawn/rebuild 兼容）+ 保留 `originName` 提取/来源登记（已完成） | MenuPluginKeship.cc |

### 4.2 H2：按链建多层 .ksw 文件夹

| # | 改动 | 文件 |
|---|---|---|
| H2-1 | `SaveWorldManifest` 模型循环：`relDir = "<链1>.ksw/<链2>.ksw/"`（按 stripPrefix 后的 subChain 拼接），`create_directories(_dirpath + "/" + relDir)`；ksm 写入 `_dirpath/<relDir><原名>.ksm`；JSON `file` = `relDir + leafName + ".ksm"` | ModelManager.cc |
| H2-2 | `LoadWorldManifest` 补全逻辑：`file` 非绝对路径（不以 `/`、`\` 开头）→ `file = dir + "/" + file`（支持多段相对路径） | ModelManager.cc |
| **L** | **`OnBtn_LoadWorld` 来源链去重**：`src.path = mergeSegs(nsSeg, item.path)`、`wsrc.path = mergeSegs(nsSeg, wi.path)`（当前为直接拼接，链与 ns 重叠时树/保存出现重复层级，如 `world1 > world1 > subA`） | MenuPluginKeship.cc |

### 4.3 M1+M2：清单 kswf + 覆盖清空（F4 修正）

| # | 改动 | 文件 |
|---|---|---|
| M2-1 | `SaveWorldManifest`：`jsonPath = _dirpath + "/keship_world.kswf"` | ModelManager.cc |
| M2-2 | `OnBtn_LoadWorld`：`filePath = dirPath + "/keship_world.kswf"`（cc:437）；注释同步（cc:423） | MenuPluginKeship.cc |
| M2-3 | `SaveWorldDialog` 帮助文案（cc:78）：`keship_world.kswf` | SaveWorldDialog.cc |
| M1-1 | **清理逻辑**（覆盖时，F4）：先收集 `_dirpath` 内全部条目到 vector，再逐个 `remove_all`（文件/子目录统一），每次操作后 `ec.clear()` 防中断 | ModelManager.cc |

### 4.4 M3：命名空间输入限制（D6）

| # | 改动 | 文件 |
|---|---|---|
| M3-1 | `LoadWorldDialog` validator：`[A-Za-z0-9_/]*` → `[A-Za-z0-9_]*`；placeholder 去掉 `base_1/subA` 示例；预填合法性检查去掉 `/` 分支 | LoadWorldDialog.cc |

> mergeSegs 保持现状（D6 下 ns 恒单段，尾首匹配已正确）。

### 4.5 L2：水域勾选键 = 链/原名（跨链重名可单独勾选）

| # | 改动 | 文件 |
|---|---|---|
| L2-1 | `OnBtn_SaveWorld` 水域行第 3 段 = `segs + "/" + disp`（叶子显示末段=原名，勾选键含链） | MenuPluginKeship.cc |
| L2-2 | `OnBtn_SaveWorld` 水域过滤 key = `chainText(wsrc.path) + "/" + baseName` | MenuPluginKeship.cc |
| L2-3 | `OnBtn_SaveSubWorld` 水域行第 3 段 = `rel + "/" + disp`（相对链） | MenuPluginKeship.cc |
| L2-4 | `OnBtn_SaveSubWorld` 水域过滤 key = **相对链** + `/` + baseName（F3 修正：必须与行一致，否则勾选全失配） | MenuPluginKeship.cc |
| L2-5 | 无来源水域（手动添加）：行第 3 段与过滤 key 均退化为实例名（已推演一致 ✓） | — |

### 4.6 加载交互（D5）

| # | 改动 | 文件 |
|---|---|---|
| B3-1 | `OnBtn_LoadWorld`：QFileDialog 目录模式（`Directory` + `ShowDirsOnly`）；选中后校验目录名以 `.ksw` 结尾（否则提示）；`filePath = dirPath + "/keship_world.kswf"` | MenuPluginKeship.cc |

### 4.7 保存交互（D4）

| # | 改动 | 文件 |
|---|---|---|
| B1-1 | `SaveWorldDialog::refreshDirEdit/updateDirFromName`：拼接 `base + "/" + worldName + ".ksw"` | SaveWorldDialog.cc |
| B1-2 | `dirEdit_` 只读（`setReadOnly(true)`）；浏览按钮仅更换父目录；世界名变化自动跟随 | SaveWorldDialog.cc |
| B1-3 | `onAccept`：目录不存在自动 `mkpath`（无询问）；已存在弹"同名世界已存在，是否覆盖？"；保留 `.ksw` 结尾防御性校验 | SaveWorldDialog.cc |

---

## 五、关键约束与前置事实（复查确认）

1. **`allow_renaming(false)`**：`RebuildModelWithHydro`（cc:2862）等编辑重建函数要求 **SDF 内 name == 实例名** → 缓存 SDF 必须统一为实例名（H1-4/H1-5 的依据）
2. **`set_allow_renaming(true)`**：`SpawnModelInGazeboFromSDFString`（cc:1287）允许 name 不同（但缓存统一后不再依赖）
3. **`SaveModelToKSM` 调用点仅 2 处**（SaveWorldManifest cc:3464、OnBtn_SaveModel cc:1884）→ 加参数改动面可控
4. **`keship_world.ksw` 引用点 5 处**（MenuPluginKeship.cc 423/437、SaveWorldDialog.cc 78、ModelManager.cc 3387/3559）→ kswf 改动面确认
5. **`file` 补全**（cc:3643-3646）：当前仅无 `/` 补 dir → 改非绝对路径都补；ksw 由 keship 生成（恒相对路径），无 Windows 绝对路径风险
6. **清理迭代器**：删除当前条目后继续迭代不保证安全 → 先收集后删除（F4）
7. **载入 `dir` 推断**（cc:3612）：`_filepath` 父目录 = `<世界名>.ksw` ✓ 不受 kswf 影响

---

## 六、已知问题与风险

| # | 风险 | 处置 |
|---|---|---|
| R1 | 覆盖确认后保存中途失败（failCount>0）→ 旧世界已被清空 | 接受（用户已确认覆盖）；可考虑提示"保存失败，旧世界已覆盖"（低优先级） |
| R2 | 已保存的新格式世界（`keship_world.ksw` 清单名）升级后不可加载 | 已确认不兼容，需重新保存 |
| R3 | 同名水域跨链：树中两个叶子同名（各自组下）| 正常，组可区分；保存对话框勾选键含链可单独勾选 |
| R4 | `mergeSegs` 多段重叠 | D6（禁 `/`）后不存在；保留尾首实现 |

---

## 七、复测链路（执行后验证）

1. **载入**：选 `<世界名>.ksw` 文件夹（选普通目录被拦截）→ ns 预填世界名 → 树顶层显示 `world1` 组，下含模型 `boat`、水域（原名）
2. **保存**：输入名称 → 目录自动 `models_ksw/<名称>.ksw`（自动创建无询问）→ 生成 `keship_world.kswf` + `world1.ksw/boat.ksm`；JSON：`subWorlds.world1.models.boat`、`file="world1.ksw/boat.ksm"`、模型键 `boat`
3. **跨链同名**：载入 world1+world2 → 保存 → `world1.ksw/boat.ksm` 与 `world2.ksw/boat.ksm` 并存互不覆盖
4. **再载入**（ns=world1）→ 实例名 `world1_boat`（无重复前缀）
5. **直接加载模型**：加载 Keboat.ksm → 保存 → 键/文件 = `Keboat`；**ksm 内 `<model name>` = Keboat**（H1 验证）
6. **编辑重建**：载入 ksw 后编辑流体动力/浮力体/推进器 → 模型重建成功（F2 修复验证）；直接加载模型编辑 → 重建成功
7. **覆盖保存**：同名提示 → 确认后递归清空旧内容（含子文件夹）
8. **命名空间输入**：`/` 被 validator 拦截
9. **水域单独勾选**：两个链下同名水域可分别勾选保存
10. **子世界导出**：保存子世界 → 独立 `<子世界名>.ksw/`，链内实体相对保存
11. **删除关联**：删除模型仍正常（来源登记按实例名）

---

## 八、待讨论事项

- [ ] R1：覆盖后保存失败是否提示？（低优先级）
- [ ] R3：同名水域在保存对话框的显示是否需要带链区分？（当前叶子显示原名，组区分）
- [ ] 水域文件夹：是否需要与模型一样按链建文件夹？（当前 waters 全在清单内，无文件）
- [ ] `OnBtn_SaveModel`（单模型保存）原名还原：无来源时保持实例名原样（现状），是否接受？
