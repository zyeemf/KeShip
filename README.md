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
cd ~/keship_ws/src && git clone https://gitee.com/zyeemf/keship.git   # 或 GitHub: zyeemf/KeShip
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

`colcon build` 的 configure 阶段会打印一行判定——gz-sim 是否具备 `SetFluidAddedMass`：

- **具备** ⇒「船体完全出水时取消附加质量 M_A」(S2) 生效；
- **不具备**（官方 apt 版即如此）⇒ 自动降级，其余功能不变，**仅完全出水工况的数值偏保守**。

`SetFluidAddedMass` 不是上游 API，需给 gz-sim 打补丁才有。原因、影响范围与判定方法见
[doc/BUILD.md §5.1](doc/BUILD.md)。

## 目录结构

```
keship_ws/
├── src/keship/              # 程序源码（ROS2 功能包）
│   └── models/              # 部分源模型（大模型不入库，本地保留）
├── models_all/              # 统一模型资源根目录
│   ├── ks_model/            # 模型清单库（.ksm 散文件，入库）
│   ├── ks_world/            # 世界清单目录（.ksw 目录 + .kswf 清单，入库）
│   └── robot_model/         # 原始模型（.dae/.stl，不入库，见下）
├── data/                    # 运行时数据（部分入库）
│   ├── models/              # 缓存模型 obj（小文件，入库）
│   ├── models_normalized/   # 网格规范化缓存（自动生成，不入库）
│   └── experiments/         # 实验临时目录（不入库）
├── task/                    # 实验任务（.toml，入库）
├── doc/                     # 文档（环境安装 BUILD / 实验指南 experiments）
└── scripts/                 # 运维脚本（含目录迁移脚本）
```

## 大模型放置指引

`models_all/robot_model/` 仅入库一层目录占位（.gitkeep），**模型子目录与文件均不入库**，需自行创建并放入：

- `models_all/robot_model/keship3/`：keship3.dae + ship.png + keship3.assxml
- `models_all/robot_model/wamv/`：wamv.dae
- `models_all/robot_model/yanhu/`：yanhu.dae + dirt.png
- 其他模型：按需在 `models_all/robot_model/` 下新建子目录

`data/models_normalized/` 为网格规范化缓存，由程序自动生成，**无需手动放置**（删除后会自动重建）。

## 分支

| 分支 | 用途 |
|---|---|
| `main` | 现行新历史，日常在此操作 |
| `master` / `legacy` | 旧历史存档，仅查阅，勿修改 |

克隆时若远端默认分支不是 `main`，显式指定：`git clone -b main <url>`。

## 许可

MIT License。模型与第三方资源各自的许可见 `models_all/` 下各目录说明。
