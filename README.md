# KeShip

**船舶与海洋工程虚拟仿真实验平台**——在电脑里造一条船，给它水域、海况、任务，看它怎么动。
ROS 2 Jazzy + Gazebo Harmonic（gz-sim8）+ Qt5，MIT 许可。

> 本仓库只讲**从源码构建与二次开发**。只做实验 / 上课的使用者请用预装环境，
> 安装与使用说明随发布渠道提供，不在本仓库范围内。

## 环境要求

| 项 | 要求 |
|---|---|
| 系统 | Ubuntu 24.04 LTS（Noble） |
| ROS 2 | Jazzy（`/opt/ros/jazzy`） |
| Gazebo | Harmonic（gz-sim8），**任意 8.x 补丁版均可** |
| 其他 | Qt 5.15、Eigen3、assimp、中文字体（`fonts-noto-cjk`） |

依赖逐项清单、Gazebo apt 源配置、常见报错，见 **[doc/BUILD.md](doc/BUILD.md)**。

## 构建

```bash
# 仓库根就是工作区（内含 src/keship），因此直接克隆成 ~/keship_ws；
# 目标目录须为空或不存在，否则 git 会拒绝。
git clone https://gitee.com/zyeemf/keship.git ~/keship_ws   # 或 GitHub: zyeemf/KeShip
cd ~/keship_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select keship --merge-install
source install/setup.bash
```

## 运行

```bash
./start_keship.sh                    # 一键启动
# 或
ros2 launch keship keship.launch.py
```

## 版本能力差异

**实际编进去的是 CMake 先找到的那一份 gz-sim**。装了 `ros-jazzy-ros-gz-sim` 时，
ROS 自带的 `/opt/ros/jazzy/opt/gz_sim_vendor`（当前 8.11.0）排在 `CMAKE_PREFIX_PATH` 最前，
通常优先于 apt 装的 `gz-harmonic`（2026-10 为 8.15.0）。两者都属 8.x、插件 ABI 兼容，按需安装即可。

两套都**不带** KeShip 的 S2 补丁，行为一致，configure 会打印一行判定：

- **具备** `SetFluidAddedMass` ⇒「船体完全出水时取消附加质量 M_A」(S2) 生效（需自行打补丁）；
- **不具备** ⇒ 自动降级，其余功能不变，**仅完全出水工况的数值偏保守**。

**colcon 默认不显示 configure 的输出**，要看这行得自己取：

```bash
grep "gz-sim" log/latest_build/keship/stdout_stderr.log
# 或编译时直接放行所有输出：
# colcon build --packages-select keship --merge-install --event-handlers console_direct+
```

`SetFluidAddedMass` 不是上游 API。原因、影响与补丁说明见 [doc/BUILD.md §5.1](doc/BUILD.md)。

## 目录结构

```
keship_ws/
├── src/keship/              # 程序源码（ROS2 功能包）
│   └── models/              # 部分源模型（大模型不入库，本地保留）
├── models_all/              # 统一模型资源根目录
│   ├── ks_model/            # 模型清单（当前仅 ke0812_1.ksm 入库）
│   ├── ks_world/            # 世界清单目录（.ksw 目录，当前未随仓库分发）
│   └── robot_model/         # 原始模型（.dae/.stl，部分入库，见下）
├── data/                    # 运行时数据（整体不入库，程序自动生成）
│   ├── models/              # 缓存模型 obj
│   ├── models_normalized/   # 网格规范化缓存
│   └── experiments/         # 实验临时目录
├── task/                    # 实验任务（.toml，入库）
├── doc/                     # 文档（环境安装 BUILD / 实验指南 experiments）
└── scripts/                 # 运维脚本（含目录迁移脚本）
```

## 模型文件放置

`models_all/robot_model/` 下各目录状态不同（以 `git ls-files` 为准）：

| 目录 | 状态 | 内容 |
|---|---|---|
| `keship3/` | **随仓库分发** | keship3.dae（约 13 MB）+ ship.png |
| `yanhu/` | **随仓库分发** | yanhu.dae（约 21 MB）+ dirt.png |
| `wamv/`、`oil_platform/`、`platform_zjy/` | **不入库**，需自行放置 | 见各模型来源说明 |

需要别的模型时，在 `models_all/robot_model/` 下新建子目录放入即可。**更大型的模型（几十 MB 以上）
请走仓库外分发，不要新增提交**——`keship3`、`yanhu` 是早期遗留，不再作为先例。

`data/models_normalized/` 是网格规范化缓存，由程序自动生成（删掉会重建），无需手动放置。

## 分支

| 分支 | 用途 | 状态（2026-10-27） |
|---|---|---|
| `dev` | **现行开发分支**，Gitee 默认分支，clone 后即在此 | 最新 |
| `main` | 稳定版 | 停在 v0.1.0（2026-10-12），落后 `dev` 87 个提交 |
| `master` / `legacy` | 旧历史存档（均指向 2026-08-13 的同一旧提交），仅查阅，勿修改 | 冻结 |

日常开发在 `dev`；要用稳定版：`git checkout main`。

## 许可

MIT License。模型与第三方资源各自的许可见 `models_all/` 下各目录说明。
