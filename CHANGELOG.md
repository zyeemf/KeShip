# 更新日志（Changelog）

本项目遵循[语义化版本](https://semver.org/lang/zh-CN/)；版本号与发布流程见开发工作区的
`doc/guides/git_workflow.md` §5 / §7。
（说明：本仓库 `doc/` 仅保留 `BUILD.md` 与 `experiments/`；完整文档在开发工作区 `E:\AI_ws\KeShip开发\doc\`。）

## [v0.1.0] - 2026-10-12

### 新增

- **S3′ 几何附加质量**：按解析平衡吃水计算 M_A、逐浮体合成整船 6×6、
  **静止门控下一次下发**（环境变量 `KESHIP_MA_DRAFT` 控制，**默认关闭**）。
- `start_keship.sh` 接入自建 gz-physics 引擎插件与 DART 告警静默库
  （`KESHIP_PHYSICS_PREFIX` / `KESHIP_QUIET_INERTIA`，均可回退）。

### 变更

- 力矩参考点统一由 `ForceAccumulator` 折算（以 **link 原点** 为参考，替换原"按 CoM"口径）。
- `tools/check_remotes.sh` 支持指定分支参数（默认 `main`）。

### 验证

- **回归**：`KESHIP_MA_DRAFT=0` 与补丁前**逐位一致**（`max|Δ| = 0`，200 公共帧）。
- **稳态**：水平漂移 0.00282 m；排水量 0.2998~0.3002 m³ vs 理论 0.30（差 < 0.1%）。
- **告警**：DART `verifySpatialTensor` **0 行**。

### 边界

- S3′ 默认关闭；波浪工况下多半不触发（**安全降级**为 ksm 原值）。
- gz-physics 仍为 ROS vendor **7.6.0**（未升级；升级对 KeShip 无效且无收益）。
