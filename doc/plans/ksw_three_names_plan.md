# KSW 三名体系与问题修复方案（V6）

> 状态：**已完成（测试符合预期）**
> 日期：2026-08-12
> 关联：`ksw_world_storage_v5_plan.md`（V5 方案已实施，本方案为其后续修复补充）
> 范围：MenuPluginKeship.cc / MenuPluginKeship.qml / editors/SelectWorldDialog.{hh,cc}(新建) / CMakeLists.txt

> 最终验证（用户实测）：命名 KeBoat03 树/ksw 显示一致；双击 .ksw 直接加载；点击 w1 整组折叠；单引号 SDF 可加载；非法实例名被拦截——全部符合预期。

---

## 一、三名体系（用户确认的语义）

| 名字 | 来源 | 保存 ksw 时 | 载入 ksw 后 |
|---|---|---|---|
| **用户命名** | 载入 ksm/dae 等文件时对话框输入（m1、KeBoat03）；添加水域时输入（water1）；载入 ksw 时输入命名空间（zoo） | **作为 JSON 键/文件名**（模型、水域）；zoo 作为层级（subWorlds.zoo） | 树显示原名；spawn 拼接用 |
| **来源名** | ksm/SDF 内原始 `<model name>`（Keboat） | 不参与（被用户命名覆盖） | — |
| **spawn 名称** | 载入 ksw 的命名空间 + 用户命名拼接（zoo_m1） | — | 运行时实例名（唯一性） |

**核心原则**：用户每次载入/添加时起的名字（用户命名）必须贯穿保存；载入 ksw 的命名空间（zoo）作为层级（链）保留；来源名仅存在于最初文件内，被用户命名覆盖后不再回退。

**典型流程验证**：
1. 载入 ksm 命名 m1 → 保存 myworld.ksw 键 = m1
2. 添加水域命名 water1 → 键 = water1
3. 载入 myworld.ksw 起名 zoo → `subWorlds.zoo.models.m1`、`subWorlds.zoo.waters.water1`
4. zoo 下模型/水域显示 m1、water1（baseName）
5. spawn 拼接：`zoo_m1`、`zoo_water1`（mergeSegs 去重）

---

## 二、测试发现问题（用户实测 myworld.ksw 流程）

| # | 现象 | 根因 |
|---|---|---|
| P1 | 载入 ke0812_1.ksm 命名 KeBoat03，树/ksw 却显示 Keboat | V5 实施时 `OnBtn_LoadModel` 的 baseName 用了 SDF 内原始名（originName），忽略用户输入 |
| P2 | 双击 `.ksw` 文件夹是进入目录而非加载 | `QFileDialog::Directory` 双击目录=导航，Qt 无双击选择钩子 |
| P3 | 载入 ksw（命名 w1）后点击 w1 组，仅"模型/水域"组行隐藏，叶子（模型/水域实体）仍显示 | buildRows 折叠键不一致：sub 组点击设 `w1/sub`，叶子判定用 `w1/models`、`w1/waters` |

---

## 三、修复方案

### P1：载入模型 baseName = 用户输入实例名

| 改动 | 文件 |
|---|---|
| `OnBtn_LoadModel`：删除 `originName` 提取块（V5 新增，仅删提取、**保留**"替换 baseSDF name 为实例名"块）；三处来源登记 `baseName = instanceName`（KSM 分支 / 创建模式 hydroDialog lambda / 直接 spawn 分支）；lambda 捕获列表移除 `fileNameBaseName` | MenuPluginKeship.cc |
| **保留** `fileNameBaseName` 变量声明（cc:1008，**文件名合法性校验用**，QRegExp 检查，非登记用途） | MenuPluginKeship.cc |
| **R1 修复（现存 bug）**：`SpawnModelInGazeboFromFile` 的 **SDF 文件分支**（cc:529-541）读文件后把 `<model name>` 替换为实例名再发送（当前原样发送 + `set_allow_renaming(false)` → 实例名≠SDF name 时 spawn 失败；dae/glb 分支已一致） | ModelManager.cc |

**效果**：用户命名保存为 JSON 键/文件名/ksm 内 SDF name（统一）；载入 ksw 路径不受影响（链=命名空间、键=ksw 内用户命名）。

### P2：自定义世界选择对话框（双击加载）

| 改动 | 文件 |
|---|---|
| 新建 `editors/SelectWorldDialog.{hh,cc}`：列出当前父目录（默认 `KeshipWorldsDir()`）下全部 `*.ksw` 子目录；**双击条目即选中加载**；"浏览..."按钮（QFileDialog::Directory）更换父目录；"确定"按钮（选中条目时可用，无选中禁用）；空目录提示；条目 `sortItems` 排序；浏览对话框 `setSidebarUrls(browseUrls)` | editors/SelectWorldDialog.{hh,cc} |
| `OnBtn_LoadWorld`：QFileDialog 目录选择改为使用 SelectWorldDialog；选中后 `filePath = dir + "/keship_world.kswf"` 逻辑保留 | MenuPluginKeship.cc |
| CMakeLists.txt：`MenuPluginKeship` 源列表加入 `src_cpp/gui/keship/editors/SelectWorldDialog.cc` | CMakeLists.txt |

**设计**：简洁列表（QListWidget），条目=目录名（不含路径），双击/确定返回完整路径；父目录记忆于内部成员；界面文案中文。

### P3：buildRows 叶子祖先 sub 折叠判定

| 改动 | 文件 |
|---|---|
| `buildRows` 中 `kind === "model" \|\| kind === "water"` 分支：保留原分类组键判定（`chain/models`、`chain/waters`）；**新增祖先链 sub 组检查**——沿 `chain` 逐级向上（`anc` 从 chain 起，取 `anc/sub` 键，再截去末段），任一 `=== false` 则隐藏 | MenuPluginKeship.qml |

**伪代码**：
```
if (r.kind === "model" || r.kind === "water") {
    var gk = r.chain + "/" + (model ? "models" : "waters")
    if (treeExpanded[gk] === false) continue
    var anc = r.chain
    while (anc.length) {
        if (treeExpanded[anc + "/sub"] === false) { /* 祖先 sub 折叠 → 隐藏 */ }
        var idx = anc.lastIndexOf("/")
        anc = (idx < 0 ? "" : anc.substr(0, idx))
    }
}
```

**效果**：点击 sub 组（w1）→ 分类组行 + 全部叶子隐藏；点击"模型"分类组 → 仅模型叶子隐藏（水域不受影响）；展开恢复。

---

## 四、复测链路

1. 载入 ke0812_1.ksm 命名 KeBoat03 → 树显示 KeBoat03；保存 → ksw 键/ksm 文件名/ksm 内 name 均 KeBoat03
2. 三艘 Keboat 编队（载入 ksm×3 命名 m1/m2/m3）→ 保存键 m1/m2/m3 不冲突
3. 载入 myworld.ksw 起名 zoo → 树 zoo > [模型组 > 原名, 水域组 > 原名]；spawn 名 zoo_原名
4. 双击 `.ksw` 目录直接加载；"浏览..."更换父目录
5. 点击 zoo 组 → 整组折叠（组行+叶子全隐藏）/展开恢复；点击"模型"分类组 → 仅模型层折叠
6. 保留 V5 验证（多层 .ksw 文件夹、覆盖清空、子世界导出、防前缀固化）

---

## 五、待讨论事项

- [x] P1 中"来源名"（Keboat）在载入模型时是否仍需要展示/提示（已确认：被用户命名覆盖后不再出现）
- [x] P2 对话框是否需要显示世界内模型/水域预览（已确认：仅目录名列表）
- [x] R1（SDF 文件分支 spawn name 不匹配）是否纳入（已确认：纳入）
- [x] P3 折叠动画/视觉反馈（已确认：无动画，行直接显隐）

## 五-b、复查加固（R1a/R1b/R1c，已执行）

复查发现的低风险项处理记录：

| # | 问题 | 处理 | 文件 |
|---|---|---|---|
| R1a | 单引号 SDF（`<model name='x'>`）不兼容：name 替换找不到 → spawn 仍失败 | SDF name 替换兼容双/单引号（`"model name=\""` 与 `"model name='"` 两分支） | ModelManager.cc |
| R1b | ModelSettingsDialog 无实例名字符校验：含 `"`/`'`/`/` 会破坏 SDF name 替换与保存文件名 | `validateAndAccept` 增加"仅字母数字下划线"校验（空名检查之后、查重之前） | ModelSettingsDialog.cc |
| R1c | SelectWorldDialog 双击依赖 `currentItem` | 改用信号 `itemDoubleClicked(QListWidgetItem*)` 的 item 参数 | SelectWorldDialog.cc |

## 六、执行批次

| 批次 | 内容 | 验证 |
|---|---|---|
| 批次1 | P1 + R1（MenuPluginKeship.cc / ModelManager.cc） | 编译通过 |
| 批次2 | P3（MenuPluginKeship.qml buildRows 祖先 sub 折叠） | 编译通过 |
| 批次3 | P2（SelectWorldDialog 新建 + OnBtn_LoadWorld 改用 + CMakeLists） | 编译通过 |
| 加固 | R1a/R1b/R1c | 编译验证中 |
