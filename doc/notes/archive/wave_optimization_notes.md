# Wave 内部逻辑优化记录

**日期**: 2026-06-16 | **来源**: Wave 代码阅读 | **状态**: 待实施

---

## 🟡 性能优化

### O1: SetXxx() 重复 Recalculate()
- **位置**: `Wavefield.cc` — 所有 setter (SetAmplitude/SetPeriod/SetAngle/...)
- **问题**: 每个 setter 调用后立即 `Recalculate()`，批量修改 N 个参数 = N 次重建
- **方案**: 加 `BeginBatch()`/`EndBatch()` 或 `SetParams(struct)` 一次性设置
- **影响**: 中等 — 参数修改不频繁（仅在对话框或实验启动时触发）

### O2: ComputeDepthSimply 3 次重复调用
- **位置**: `KSPlugin_Buoyancy.cc:156-160`
- **问题**: 每浮力体每仿真步调用 3 次 `ComputeDepthSimply`，每次 O(N) 遍历全部分量
- **方案**: 合并为 `ComputeDepthWithGradient(c, t, t0, &h0, &dhdx, &dhdy)` 在单次遍历中完成
- **影响**: 中等 — 减少了 2/3 的三角函数计算（每步 ~18 次 cos/sin）

### O3: 互斥锁粒度过粗
- **位置**: `WaveVisualPrivate` 和 `KSPlugin_Buoyancy::Implementation`
- **问题**: 同一把 mutex 同时保护 simTime_ 和 wavefield 参数，读时间和写参数互相阻塞
- **方案**: 拆为 `timeMutex_` + `wavefieldMutex_`
- **影响**: 低 — 时间写入极快（~ns）且参数更新极少

---

## 🟠 健壮性

### R1: WaveVisual 硬编码 3 波分量
- **位置**: `WaveVisual.cc:322-356`
- **问题**: `amplitudeV[3]`、`wavenumberV[3]`、`omegaV[3]` 硬编码数组大小，若 Number()≠3 越界 UB
- **方案**: 读取 `wavefield.Number()` 动态分配，或至少 `GZ_ASSERT(Number() == 3, "...")`
- **影响**: 低 — 当前所有配置固定 3 分量，但未来可能变化

### R2: 缺少默认波浪发布者
- **位置**: 全局
- **问题**: WaveVisual + Buoyancy 都订阅 topic，但启动时无人发布 → wavefield 不激活 → 水面是平的
- **方案**: 新建轻量 `WavefieldPublisher` 系统插件，world 加载时发布初始参数到 topic
- **影响**: 高 — 当前波浪参数完全依赖外部按需发布，容易遗漏

### R3: WaveVisual scene 指针重复获取
- **位置**: `WaveVisual.cc:413`
- **问题**: `sceneFromFirstRenderEngine()` 在 OnUpdate 中每帧调用直到非空
- **方案**: 移入首次 OnUpdate 后的缓存逻辑（当前已有 `if (!this->scene)` 保护，影响微小）
- **影响**: 极低

---

## 🔵 代码质量（已在审计中，此处不重复）

| 编号 | 问题 | 记录位置 |
|------|------|---------|
| B3 | `static int diagCounter` 跨实例共享 | 审计 B3 |
| H1 | g=9.8 vs 9.81 不一致 | 审计 H1 |
| H4 | Buoyancy.cc 并行版本死代码 ~250 行 | 审计 H4 |
| C3 | `M_PI` → `constexpr kPi` | 审计 C3 |

---

## 实施建议

| 时机 | 优化项 |
|------|--------|
| 水域勾选框之后 | R2（默认波浪发布者）— 功能独立，影响大 |
| 多水域重构时 | O1、O2、R1、审计 Bug — 与架构改动一起做 |
| 日常维护 | O3、R3 — 微小改进，顺手修 |
