# env/gz —— 自建 gz-sim（L1）资产

> 来源：2026-10-12 从 `data/test/`（**未入库**、gitignore 内）**转正**至此 —— 解决 PENDING ㉓
> 「data/test 资产未入库：磁盘丢失即永久丢失」风险。
> 方案依据：[../plans/env_setup_automation_plan.md](../plans/env_setup_automation_plan.md) §3 L1 / §4.2

## 这是什么

KeShip 的 **S2「完全出水取消 M_A」** 依赖对 gz-sim 打的 **S1 patch**（新增运行时
`Link::SetFluidAddedMass` 通路）；而运行时默认加载的是 **ROS vendor gz-sim（无补丁）**。

⇒ **L1** = 自建一份打过补丁的 gz-sim，部署到固定前缀 `/opt/keship-gz`；
`start_keship.sh` 通过 `LD_LIBRARY_PATH` 让其优先加载 ⇒ S2 才在 GUI 下真正生效。

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
bash env/gz/deploy_gz.sh --dry-run       # 只打印将执行的命令
bash env/gz/diag_gzsim_dir.sh            # 诊断当前加载的是哪份 gz
```

## 判据（T4 已确立）

- `gz sim --version`（带 `LD_LIBRARY_PATH=/opt/keship-gz/lib`）= **自建版本**
- `SetFluidAddedMass` 符号在 `libgz-sim8.so` 中**已定义**
- 落体实验 `a_on/a_off = 0.5263`（与理论逐位吻合）
- GUI 下 S2 生效（`a/g → 1.0`）⇒ 需张老师 GUI 验收

## 边界

- **本目录不含源码树**（几十 MB ~ GB，不入库）；源码包放 `~/gz-build/`（环境内）。
- **`data/test/` 中的同名文件已废弃** —— 转正后以本目录为**唯一来源**。
