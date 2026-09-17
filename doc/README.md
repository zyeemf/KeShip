# KeShip 文档导航

> **本目录是 KeShip 项目文档的工作位置**（随代码进 git）。
> 读文档从这里进；写文档的规则见 [DOC_WORKFLOW.md](DOC_WORKFLOW.md)。

| 项 | 内容 |
|---|---|
| 权威路径 | `\\wsl.localhost\KeShipV0010\home\keship\keship_ws\doc\` |
| 最近整理 | 2026-09-17（按维护者指示移出全部规划类文档，只留配置必须；79 → 39 份） |
| 历史留底 | Windows 侧 `E:\AI_ws\KeShip开发\doc_old\`（清理前逐字节快照，含全部被移出文档） |
| 体检脚本 | `python3 tools/check_docs.py`（在 `keship_ws/` 下运行） |

---

## 一、按目的找文档

| 我想…… | 去这里 |
|---|---|
| 从零配环境、编译跑起来 | [BUILD.md](BUILD.md) |
| 了解项目概况、技术栈、怎么启动 | [keship_developer_guide.md](keship_developer_guide.md) |
| 搞清楚系统怎么搭的、代码在哪 | [keship_architecture.md](keship_architecture.md) |
| 查某个 Bug 以前是怎么修的 | [keship_issues_log.md](keship_issues_log.md) |
| 找某个专题的深入记录 | 下方「专题笔记」 |
| 查 ksw 世界格式的完整定义 | [keship_plans.md](keship_plans.md)（在册 2 份） |
| 找已移出的旧方案 / 路线图 | 留底 `E:\AI_ws\KeShip开发\doc_old\` |
| 我要写 / 改文档，先看规矩 | [DOC_WORKFLOW.md](DOC_WORKFLOW.md) |

---

## 二、根目录文档

| 文档 | 用途 |
|---|---|
| [BUILD.md](BUILD.md) | 环境与编译：从 git clone 到跑起来 |
| [DOC_WORKFLOW.md](DOC_WORKFLOW.md) | 文档规范与强制更新机制（**改文档前先看**） |
| [keship_architecture.md](keship_architecture.md) | 系统架构：目录树、模块设计、数据流、专题笔记索引 |
| [keship_developer_guide.md](keship_developer_guide.md) | 项目介绍、技术栈、编译运行指令 |
| [keship_issues_log.md](keship_issues_log.md) | 问题日志：Bug 记录、根因与修复过程（按时间轴） |
| [keship_plans.md](keship_plans.md) | 计划索引：在册 ksw 格式文档 + 已移出清单 |

---

## 三、专题笔记 `notes/`（29 份，全部为事实记录 / 现行依据）

### 物理 / 水动力学

| 笔记 | 内容 |
|---|---|
| [keship_physics_design.md](notes/keship_physics_design.md) | 物理设计总纲：6-DOF Fossen、Wavefield–Buoyancy 集成 |
| [dynamics_audit_2026-08-14.md](notes/dynamics_audit_2026-08-14.md) | 动力学代码与理论审计（哪些实测正确勿改） |
| [fossen_physics_refactor.md](notes/fossen_physics_refactor.md) | Fossen 重构：DampingTerm、KSM 36 元素格式 |
| [cylinder_hydro_physics.md](notes/cylinder_hydro_physics.md) | 圆柱体水动力物理化与验证数据 |
| [keBoat04_hydro_params_check_2026-08-14.md](notes/keBoat04_hydro_params_check_2026-08-14.md) | KeBoat04 参数物理合理性全项核验 |
| [buoyancy_mesh_theory.md](notes/buoyancy_mesh_theory.md) | 浮力网格理论：Catto 算法、绕序 Bug 根因 |
| [圆柱体稳性分析.md](notes/圆柱体稳性分析.md) | GM / 倾倒临界直径公式推导 |
| [inertia_calculator_tool.md](notes/inertia_calculator_tool.md) | 惯性计算工具三算法架构 |
| [added_mass_cmd_plan.md](notes/added_mass_cmd_plan.md) | 水动力重构方案 A（M_A 常驻 DART、阻尼-only） |
| [fluid_added_mass_research.md](notes/fluid_added_mass_research.md) | DART M_A 注入机制调研（C2 表错误已知，对照 audit 用） |
| [fluid_added_mass_migration.md](notes/fluid_added_mass_migration.md) | 21 元素映射与 300 倍缩放 Bug 修复 |
| [math_hull_redesign_plan.md](notes/math_hull_redesign_plan.md) | 数学船型重构记录（绕序表、设计决策） |
| [ma_dart_removal_fix.md](notes/ma_dart_removal_fix.md) | M_A 注入 DART 决策依据：显式方案数值发散实测证据 |

### 波浪 / 水域

| 笔记 | 内容 |
|---|---|
| [wind_wave_current_design.md](notes/wind_wave_current_design.md) | 现行波浪物理与参数依据（第五章风/流方案待实施） |
| [waves_global_refactor.md](notes/waves_global_refactor.md) | 波浪场全局架构（现行工作方式） |
| [water_area_ipc_refactor.md](notes/water_area_ipc_refactor.md) | 水域 JSON + 轮询 IPC 机制依据 |
| [water_area_polygon_mesh.md](notes/water_area_polygon_mesh.md) | 水面多边形网格修复 |
| [water_area_edit_and_polygon_fix.md](notes/water_area_edit_and_polygon_fix.md) | 水域编辑体验与射线法判定 |
| [water_area_tree_env_fixes.md](notes/water_area_tree_env_fixes.md) | 序列化 v2 格式、启动空场景等现行行为 |

### GUI / 交互

| 笔记 | 内容 |
|---|---|
| [ui_refinement_session.md](notes/ui_refinement_session.md) | UI 优化记录（三模式行为矩阵） |
| [ui_refinement_session3.md](notes/ui_refinement_session3.md) | UI 第三批优化（暂停状态还原等） |

### 世界文件 / 数据架构

| 笔记 | 内容 |
|---|---|
| [ksw_v4_save_load_fix.md](notes/ksw_v4_save_load_fix.md) | KSW 0.0.4 保存/载入链路（树结构定稿、topic 净化） |
| [data_arch_refactor_2026-08-12.md](notes/data_arch_refactor_2026-08-12.md) | data 纯运行时化、ksm 源路径持久化、缓存策略 |

### 实验教学

| 笔记 | 内容 |
|---|---|
| [sp02_experiment_guide.md](notes/sp02_experiment_guide.md) | SP-02 漂浮圆柱体初稳性实验指导书 |
| [sp03_experiment_guide.md](notes/sp03_experiment_guide.md) | SP-03 初稳性倾斜实验指导书 |
| [sp04_experiment_guide.md](notes/sp04_experiment_guide.md) | SP-04 数学船型稳性对比实验指导书 |

### 排查警示 / 体系全景

| 笔记 | 内容 |
|---|---|
| [sp03_bug_analysis.md](notes/sp03_bug_analysis.md) | DART 不支持 mesh 碰撞体的三层几何体分析（做 mesh 碰撞前必读） |
| [virtual_experiment_framework.md](notes/virtual_experiment_framework.md) | 实验体系全景：三门课程 × 三层级 × 28 实验依赖链 |
| [wavevisual_sdf_warning_analysis.md](notes/wavevisual_sdf_warning_analysis.md) | SDF schema 警告为 gz 内部行为、无害的排查结论 |

---

## 四、方案文档 `plans/`（2 份）

仅保留仍在生效的 ksw 格式规范，逐份说明见 [keship_plans.md](keship_plans.md)。

> 2026-09-17 前的全部规划文档（方案 12 份 + notes 方案 3 份 + 归档 24 份 + 工具配置 1 份）
> 已移出仓库：留底 `E:\AI_ws\KeShip开发\doc_old\`，暂存 `~/keship_ws_removed_20260917/`。

---

## 五、维护规则

新增 / 修改文档前请先看 [DOC_WORKFLOW.md](DOC_WORKFLOW.md)，要点：

1. 文档只在此目录落盘，不在别处留活动副本；
2. 增删源文件 → 同步更新 `keship_architecture.md` 目录树；
3. 修 Bug → 在 `keship_issues_log.md` 追加一行；
4. 新增笔记 → 必须在架构文档专题表登记，否则体检会报未登记；
5. 改完跑 `python3 tools/check_docs.py`。

---

## 六、待确认清单（需项目负责人拍板）

> 2026-09-17 更新：原 9 项中，Qt 版本已拍板（Qt5 5.15）；规划文档处置已执行完毕（本清单第 1 项）。

| # | 事项 | 状态 |
|---|---|---|
| 1 | 规划类文档移出仓库（plans 12 + notes 3 + archive 24 + subagent_routing 1） | ✅ 2026-09-17 执行，留底 doc_old |
| 2 | `keship_timeline.md` 拆分 | 随清理移出；「关键技术决策 7 条」由架构文档承载 |
| 3 | 第三份副本 `E:\KeShip\ks_workspace\program\` | 待定 |
| 4 | 两个 Windows 启动入口 | 待定（当前以 `start_keshipv0010.bat` 为准） |
| 5 | Qt 版本 | ✅ Qt5 5.15（CMakeLists L53） |
| 6 | `notes/圆柱体稳性分析.md` 公式排版 | 待定 |
| 7 | 部分笔记缺状态标记 | 待定（非强制） |
| 8 | `controlAlgorithm/newControlPy0815_1~3` 三副本 | 待定 |
| 9 | Windows 侧 `_archive_2026-09-16/` 与 `doc_old/` 的最终去留 | 待定 |
