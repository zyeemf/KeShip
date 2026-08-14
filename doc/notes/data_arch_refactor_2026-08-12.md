# KeShip 数据架构整理 + 波浪场竞态修复

> 日期：2026-08-12 | 状态：✅ 用户实测通过
> 关联提交：0cda5c3（数据架构）、631923e（波浪场竞态）、后续日志收敛提交

---

## 一、背景与目标

- 混乱点：模型资产（ksm/ksw）在 `models_all/`，但生成的中间数据、用户配置、日志散落 `data/` 与根目录，语义不清
- 目标（用户定稿）：
  - `data/` = **纯运行时工作目录**：整体可删除、可重建、不入库
  - 资产与产物彻底分离；缓存可随时重建；配置归位

## 二、目录规范（最终形态）

```
keship_ws/
├── src/ task/ doc/ scripts/          # 源码与资产（入库）
├── models_all/                       # 模型资产（入库；robot_model 仅本地）
│   ├── ks_model/*.ksm                # 模型清单（uri 存源路径）
│   ├── ks_world/<名>.ksw/            # 世界清单 + ksm（含水域 waters 段）
│   └── robot_model/<模型>/           # 源模型 + 其规范化缓存
│       ├── keship3.dae/.assxml/ship.png   # 源（本地，不入库）
│       └── normalized/               # 规范化缓存（可删可重建，不入库）
│           ├── keship3_<key>.obj / .mtl / _tex.png
└── data/                             # 纯运行时（整体不入库，可删可重建）
    ├── log/                          # 日志（原根目录 log/ mylogs/ 收敛于此）
    ├── models/ waves/ preview/ test/ experiments/
    ├── keship.lock  keship_water_areas.json
    └── （由 EnsureKeshipDataDirs 自动创建）
```

## 三、网格规范化缓存策略

- **缓存位置**：`源模型目录/normalized/`（随源同生命周期；原 `data/models_normalized/` 废弃）
- **文件名**：`<stem>_<key>`（key = SHA1(源路径+mtime+size+纹理)[:8]；去掉了原 dirHash 前缀，根治"链式哈希堆积"）
- **命中判定**：`<stem>_<key>.obj + .mtl` 同时存在即复用；源/纹理变化 → key 变 → 自动重建并清理旧 key 版本（`CleanupOldCaches`）
- **data 删除不影响缓存**：缓存跟源走，真正"一次规范化、长期复用"
- **清理按钮**（GUI）：语义改为"清空所有 robot_model/*/normalized/"（`CleanupUnusedMeshCache` 重写，不再扫描 ksm 引用）

## 四、ksm 持久化源路径（治本）

- **根因**：`SaveModelToKSM` 把内存 SDF 里的 `file://<ws>/data/models_normalized/xxx.obj` 剥前缀后写进 ksm → ksm 固化缓存路径 → 缓存删后无法重建（链式哈希无法反查源）
- **修复**：
  - `ModelManager` 新增成员 `meshCacheToSrcUri_`（缓存绝对路径 → 源 uri），`NormalizeMeshUrisInSDF`（去 static 改成员函数）归一化时记录
  - `SaveModelToKSM` 保存时先按映射**反查回源路径**（`robot_model/keship3/keship3.dae`），未命中（数学船模等）才走原剥前缀逻辑
- **效果**：ksm 永远存源路径；加载时统一归一化生成缓存；data 可安全清空
- **遗留**：数学船模（SpawnMathHull 的 obj）无外部源，其 ksm 仍存 `data/models/*.obj` 路径，data 清空后无法重建——后续需"船型参数持久化"彻底闭环（暂缓）

## 五、data 纯运行时化

- `data/` 整体 .gitignore（一行 `data/`），删除原 20+ 行分项规则与全部 .gitkeep（程序自动建目录）
- 删除 `data/settings.json`（GUI quick_paths 无写入方，死功能）及 `KeshipSettingsFile()`、`SidebarUrls` 读取块
- `keship_water_areas.json` = GUI→插件 IPC 快照（1s/mtime 热更新），水域资产在 kswf 的 waters 段
- `git rm -r --cached data`：Wigley obj 等运行时产物移出仓库

## 六、波浪场载入竞态修复

**现象**：删 data 后启动 → 载入 ksw → 船浮着（水域/浮力正常）但不随波浪起伏；手动"应用"环境参数后恢复。

**根因**（载入世界期间仿真暂停）：
1. 载入世界流程（08-09 定稿）：暂停 → 写 JSON → spawn 模型 → resume（防跳起）
2. 暂停期 WavesModel 的 PreUpdate `if (paused) return` → 1s 轮询被跳过，波浪场不建
3. resume 后首帧：HydroBuoyancy"进入水区"一次性查找 `wavefield_<水区名>` → 不存在 → `缺失(静水)`，且 `currentAreaName_` 已记住、永不重查
4. resume 后 ~1s：WavesModel 轮询建好波浪场，但船已错过 → 永久静水
5. 08-11"启动写空 JSON"改造前，JSON 残留旧水域 → world 首帧已建同名波浪场 → 载入时"侥幸命中"，掩盖了该竞态

**修复**（3 点）：
| 文件 | 改动 |
|------|------|
| `KSPlugin_WavesModel.cc` | PreUpdate 去掉 `paused return`，每帧检查 JSON mtime（`RebuildFromJson` 有 mtime 守卫，未变化零开销）→ 载入暂停期写完 JSON 下一帧即建波浪场，resume 前就绪 ← 核心 |
| `KSPlugin_WavesModel.cc` | PMS 到达时若目标水区波浪场未建立，先 `RebuildFromJson` 再应用（消除"PMS 目标水区不存在"警告）；顺带修复 `ApplyPendingPms` 未命名参数 |
| `KSPlugin_HydroBuoyancy.cc/.hh` | `activeWavefield_` 缺失时每 30 帧重试查找（兜底竞态），日志仅在状态翻转时输出（不刷屏） |

**验证**：删 data → 启动 → 载入 ksw → 船立即随 1 级海况起伏；日志 `波浪场引用 有效`；无 PMS 警告。

## 七、日志目录收敛

- 新增 `KeshipLogDir()` = `data/log/`（`KeshipPaths.hh`，EnsureKeshipDataDirs 自动创建）
- `KSPlugin_HydroBuoyancy.cc` / `KSPlugin_Thrust.cc`：`DiagnosticLog::init` 从硬编码 `/home/keship/keship_ws/mylogs` → `keship::KeshipLogDir()`（Thrust 补 include KeshipPaths.hh）
- `start_keship.sh`：`colcon build` 加 `--log-base "$KSHIP_WS/data/log"`（构建日志也收敛）
- 根目录 `log/ mylogs/ buoy_ut_logs/` 规则保留在 .gitignore（防御手动 colcon 场景），不再产生新日志

## 八、验证记录

1. 载入 myworld2aa.ksw：船可见、水面正常 ✅
2. 保存世界后 ksm `<uri>` 仍为 `robot_model/keship3/keship3.dae`（不固化缓存路径）✅
3. `rm -rf data` 重启：船仍显示（缓存跟源走），data 自动重建 ✅
4. 载入 ksw 后船立即随波浪起伏（无需手动应用）✅
5. `colcon build` 通过 ✅
