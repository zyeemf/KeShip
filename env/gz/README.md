# env/gz —— 自建 gz-sim（L1）资产

> 来源：2026-10-12 从 `data/test/`（**未入库**、gitignore 内）**转正**至此 —— 解决 PENDING ㉓
> 「data/test 资产未入库：磁盘丢失即永久丢失」风险。
> 方案依据：[../plans/env_setup_automation_plan.md](../plans/env_setup_automation_plan.md) §3 L1 / §4.2

## 这是什么

KeShip 的 **S2「完全出水取消 M_A」** 依赖对 gz-sim 打的 **S1 patch**（新增运行时
`Link::SetFluidAddedMass` 通路）；而运行时默认加载的是 **ROS vendor gz-sim（无补丁）**。

⇒ **L1** = 自建一份打过补丁的 gz-sim，部署到固定前缀（`/opt/keship-gz` 或
用户前缀 `$HOME/keship-gz`，免 sudo）；`start_keship.sh` 通过 `LD_LIBRARY_PATH`
让其优先加载 ⇒ S2 才在 GUI 下真正生效。

## 文件

| 文件 | 作用 |
|---|---|
| `patch_gzsim_addedmass_s1.py` | **S1 patch 脚本**（精确匹配替换 + 唯一命中断言 + `.bak_s1` 备份；源码路径硬编码 `~/gz-build/gz-sim-8.11.0`） |
| `patch_gzsim_addedmass_s1b.py` | S1 patch 补充/修正 |
| `gzsim_s1.patch` | S1 patch 的 diff 形式 |
| `keship_s2.patch` | S2 相关 patch |
| `build_gz_sim_test.sh` | 试构建脚本（不改代码，验证"能构建/能加载"） |
| **`deploy_gz.sh`** | **L1 部署编排**：解压 → patch → cmake → make → `install` 到 `/opt/keship-gz` → 验证 |
| `kship_quiet_inertia.cpp` | DART 告警**静默库源码**（B）；由 `deploy_gz.sh` 编译为 `~/gz-build/kship_quiet_inertia.so` |
| `diag_gzsim_dir.sh` | 诊断：自建 gz 是否被正确加载 |

## 用法

```bash
sudo bash env/gz/deploy_gz.sh            # 完整 L1 部署（需 sudo，装到 /opt/keship-gz）
bash env/gz/deploy_gz.sh --prefix=$HOME/keship-gz   # 免 sudo：装用户前缀（推荐）
bash env/gz/deploy_gz.sh --dry-run       # 只打印将执行的命令
bash env/gz/diag_gzsim_dir.sh            # 诊断当前加载的是哪份 gz
```

`start_keship.sh` 会自动探测前缀：`/opt/keship-gz` 优先，其次 `$HOME/keship-gz`，
也可用环境变量 `KESHIP_GZ_PREFIX` 显式指定。

## ⚠ 版本错配与 ROS vendor 栈（2026-10-13 运行时验证发现的必要知识）

自建 gz-sim **8.11.0** 与 Ubuntu 系统装着的 gz 栈（gz-common 5.7.1 / libgz-sim8 8.10）
**版本不匹配**：只装 gz-sim 会在运行时报

```
libgz-sim8.so.8: undefined symbol:
  gz::common::SystemPaths::ClearFindFileCallbacks()   (symbol lookup error)
```

解法和 CMake 当年的行为一致 —— **gz-common 等其余组件直接复用 ROS Jazzy vendor 的
Jetty 版栈**（`/opt/ros/jazzy/opt/gz_*_vendor/lib`，common 正是 5.8.0，与 8.11.0 匹配）。
`start_keship.sh` 已把自建前缀置于 `LD_LIBRARY_PATH` 最前、并把全部 vendor 目录追加在
其后 ⇒ 打过补丁的 `libgz-sim8.so` 优先命中，其余组件由 vendor 补齐。

**判定自建库是否真的加载**：`gz sim --version` 显示的是系统版本（`gz` CLI 经 gz 配置
解析库，**不是**部署失败的信号）；正确做法是 `LD_DEBUG=libs` 看
`calling init: .../keship-gz/lib/libgz-sim8.so.8`。

## 插件侧符号探测（KSPlugin_HydroBuoyancy）

S2 调用 `Link::SetFluidAddedMass` 前会 `dlsym` 探测符号（源码注释：直接调用会让 gz sim
进程以 exit 127 死亡）；**符号缺失 ⇒ 静默跳过 S2**（功能降级但不崩）。⇒ 若部署不到位，
日志里**根本不会出现**「已取消（完全出水）」——这正好可作部署是否生效的判据。

## 判据（T4 已确立）

- `SetFluidAddedMass` 符号在自建 `libgz-sim8.so.8.11.0` 中**已定义**
- 落体实验 `a_on/a_off = 0.5263`（与理论逐位吻合）
- **运行时加载验证（2026-10-13）**：`LD_DEBUG=libs` 抓到
  `calling init: $HOME/keship-gz/lib/libgz-sim8.so.8`，无符号错误，`gz sim -s --iterations 3` 干净退出
- GUI 下 S2 生效（`a/g → 1.0`）⇒ 仍需张老师 GUI 验收（`s2-gui-verify`）

## 边界

- **本目录不含源码树**（几十 MB ~ GB，不入库）；源码包放 `~/gz-build/`（环境内）。
- **`data/test/` 中的同名文件已废弃** —— 转正后以本目录为**唯一来源**。
- **源码路径硬编码**：`patch_gzsim_addedmass_s1.py` 写死 `~/gz-build/gz-sim-8.11.0`；
  `deploy_gz.sh` 的 `--src/--tarball` 可覆盖（默认也指向该处）。
- **`~/gz-build/install`（10-05 试构建产物）不再需要** —— 正式部署已改为
  `~/keship-gz`（`deploy_gz.sh` 产出）；原 E5 探针脚本里的 `$HOME/gz-build/install`
  回退分支属历史兼容逻辑。
