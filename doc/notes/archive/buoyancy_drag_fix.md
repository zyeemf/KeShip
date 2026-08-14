# 浮力插件阻尼迁移方案（已归档）

> ⚠️ 本文描述旧架构（双插件独立运行），已被合并插件 `KSPlugin_HydroBuoyancy` 取代。
> 保留以记录历史设计决策。

## 问题

1. **空中自由落体有阻尼**：`HydrodynamicsSystem::PreUpdate` 无条件调用 `computeHydroForces`
   → `computeDampingForces`，没有检测水面。水阻尼在空中错误施加。
2. **纵摇振荡**：浮力插件激活时 `angDrag=0`，水动力插件 `Mq` 可能为 0，
   合并惯性 M_RB+M_A 后系统欠阻尼。

## 方案

### 原则
- 浮力统一由 `KSPlugin_Buoyancy` 负责
- 阻尼统一由 `HydrodynamicsSystem`（Fossen 6-DOF）负责
- 浮力插件中的冗余简单拖曳代码删除

### A. 删除浮力插件中的冗余阻尼

| # | 文件 | 行 | 改动 |
|---|------|------|------|
| A1 | `KSPlugin_Buoyancy.hh` | 87-88 | 删除 `linearDragCoef`、`angularDragCoef` |
| A2 | `KSPlugin_Buoyancy.cc` | 376-380 | 删除 `LoadParameters` 中拖曳解析 |
| A3 | `KSPlugin_Buoyancy.cc` | 316-354 | 删除 `PreUpdate` 中拖曳计算块 |
| A4 | `ModelManager.hh` | 286-288 | `MakeBuoyancyPluginSDF` 去掉 `_hydroActive` 参数 |
| A5 | `ModelManager.cc` | 1069-1117 | 同上，实现中去掉拖曳逻辑+SDF输出 |
| A6 | `ModelManager.cc` | 1226 | `RebuildModelWithBuoyancy` 调用点 |
| A7 | `ModelManager.cc` | 1823 | `SpawnModelWithBuoyancy` 调用点 |
| A8 | `ModelManager.cc` | 1985 | `RebuildModelWithThrust` 调用点 |
| A9 | `ModelManager.cc` | 2086 | `SpawnModelWithThrust` 调用点 |
| A10 | `ModelManager.cc` | 2377 | `RebuildModelWithHydro` 调用点 |
| A11 | `ModelManager.cc` | 2461 | `RebuildModelWithSensors` 调用点 |
| A12 | `ModelManager.cc` | 2697 | `RebuildModelWithAllPlugins` 调用点 |
| A13 | `ModelManager.cc` | 529-530 | `SpawnCylinderInGazebo` 硬编码拖曳行 |

### B. 给 HydrodynamicsSystem 加水面检测

| # | 文件 | 行 | 改动 |
|---|------|------|------|
| B1 | `ModelManager.hh` | 77 | `HydroConfig` 加 `double fluidLevel = 0.0` |
| B2 | `ModelManager.cc` | 2253 | `MakeHydroPluginSDF` 输出 `<fluid_level>` |
| B3 | `ModelManager.cc` | ~1234 | `RebuildModelWithBuoyancy`: `hydroCfg.fluidLevel = _config.fluidLevel` |
| B4 | `ModelManager.cc` | ~1831 | `SpawnModelWithBuoyancy`: 同 B3 |
| B5 | `ModelManager.cc` | ~1990 | `RebuildModelWithThrust`: `hydroCfg.fluidLevel = buoyCfg.fluidLevel` |
| B6 | `ModelManager.cc` | ~2091 | `SpawnModelWithThrust`: 同 B5 |
| B7 | `ModelManager.cc` | ~2383 | `RebuildModelWithHydro`: `HydroConfig hc = _config; hc.fluidLevel = bCfg.fluidLevel;` |
| B8 | `ModelManager.cc` | ~2468 | `RebuildModelWithSensors`: 同 B5 |
| B9 | `ModelManager.cc` | ~2704 | `RebuildModelWithAllPlugins`: 同 B5 |
| B10 | `HydrodynamicsSystem.hh` | — | 加 `fluidLevel_` 成员 |
| B11 | `HydrodynamicsSystem.cc` | Configure | 加 `<fluid_level>` 解析 |
| B12 | `HydrodynamicsSystem.cc` | PreUpdate | 在阻尼前加水面检测 |
