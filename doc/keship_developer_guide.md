# KeShip 开发指南

## 项目介绍
KeShip 是一个关于船舶虚拟仿真平台的开发项目，旨在让船舶与海洋工程专业学生更方便地进行船海类虚拟仿真实验。与其他船舶仿真平台不同（比如 Virtual RobotX），即使没有编程经验，用户也可以通过 GUI 来配置和运行仿真。

## 技术栈
- **仿真引擎**：Gazebo Harmonic (gz-sim8)
- **中间件**：ROS 2 Jazzy + ros_gz_bridge
- **语言**：C++17 / Python 3.12
- **UI**：Qt5 5.15 (QWidget + QOpenGLWidget) + QML —— 以 `src/keship/CMakeLists.txt` 的 `find_package(Qt5 5.15)` 为准
- **数学库**：Eigen3
- **网格加载**：Assimp (libassimp-dev)

## 代码规范
- 使用现代 C++ 特性（智能指针、lambda 表达式等）
- 完善的错误处理和日志记录

## 项目位置

| 内容 | 路径 |
|------|------|
| 源码根 | `\\wsl.localhost\KeShipV0010\home\keship\keship_ws\src\keship` |
| 工作空间根 | `\\wsl.localhost\KeShipV0010\home\keship\keship_ws` |
| 文档（唯一权威） | `\\wsl.localhost\KeShipV0010\home\keship\keship_ws\doc` |

> Windows 资源管理器地址栏粘贴上述 UNC 路径即可访问。项目文档**只**维护在 `doc/` 下，
> 详见 [doc/README.md](README.md)；改文档的规矩见 [doc/DOC_WORKFLOW.md](DOC_WORKFLOW.md)。

> **Git 仓库**：仓库根为 `keship_ws`（CodeArts 私有仓库），默认分支 `main`（新历史），旧历史备份于 `legacy` 分支；文本资产入库（代码/ksm/kswf/task/脚本），大模型文件不入库但目录结构保留（.gitkeep，放置指引见 keship_ws/README.md）。

## 编译运行指令

> 完整环境安装（依赖 + 编译）见 [BUILD.md](BUILD.md)。

### 一键启动

```bash
# WSL 终端中：
~/keship_ws/start_keship.sh              # 编译 + 启动
~/keship_ws/start_keship.sh --no-build   # 跳过编译，直接启动

# Windows 中（双击任选其一）：
E:\AI_ws\KeShip开发\start_keshipv0010.bat        # 当前推荐入口（→ KeShipV0010 发行版）
E:\KeShip\ks_workspace\program\start_keship.bat  # 旧入口，有黑框日志，调试用（→ KeShip 发行版）
E:\KeShip\ks_workspace\program\start_keship.vbs  # 旧入口，无控制台窗口（→ KeShip 发行版）
```

启动脚本自动完成：GPU 预热 → 编译 → 激活环境 → 启动（崩溃自动重试最多 3 次）。

> **注意：两个 .bat 指向的 WSL 发行版不同**（`KeShipV0010` 与 `KeShip`）。
> 当前开发与源码都在 `KeShipV0010`，请使用进入该发行版的那个入口，否则会起错环境。

> **单例互斥**：同一时刻只允许一个 KeShip 实例运行。启动脚本内置互斥锁（flock + 进程检测），
> 已有实例运行时重复启动会被明确提示并拒绝——同时运行多个实例会互相干扰导致仿真错乱
> （gz-transport 服务与话题互相干扰、水域模型 spawn/删除错乱、波浪参数互篡改）。

### 手动启动（调试用）

```
cd \\wsl.localhost\KeShipV0010\home\keship\keship_ws
source /opt/ros/jazzy/setup.bash                          # 激活 ROS 2 环境
colcon build --packages-select keship --merge-install     # 编译 keship 包
. install/setup.bash                                      # 激活本包环境
ros2 launch keship keship.launch.py                       # 启动 KeShip 仿真
```

### 工具脚本

| 文件 | 位置 | 说明 |
|------|------|------|
| `start_keship.sh` | `~/keship_ws/` (WSL 工作空间根) | 一键启动：单例互斥 → GPU 预热 → 编译 → 激活环境 → 启动（崩溃自动重试） |
| `start_keshipv0010.bat` | Windows 侧工作区 | 双击启动，菜单选择「快速启动 / 编译并启动」，进入 `KeShipV0010` |
| `start_keship.vbs` | `E:\KeShip\ks_workspace\program\` (Windows 侧) | 无黑框双击启动器（旧入口） |
| `start_keship.bat` | `E:\KeShip\ks_workspace\program\` (Windows 侧) | 有日志黑框启动器，调试用（旧入口） |
| `migrate_models_all.sh` | `~/keship_ws/scripts/` | 模型目录重构一次性迁移脚本（复制留存版，含 ksm 引用重写与 kswf 重命名，已执行归档） |
| `check_docs.py` | `~/keship_ws/tools/` | 文档体检：架构覆盖、链接有效性、方案索引、重复副本、笔记规范 |

启动脚本参数：`--no-build`（跳过编译）、`--headless`（无渲染模式）、`--no-gpu-warm`（跳过 GPU 预热）

```bash
# 文档体检（改完文档必跑）
cd ~/keship_ws && python3 tools/check_docs.py
```

## 文档

| 文档 | 内容 |
|------|------|
| [README.md](README.md) | **文档导航总入口**（含全量清单，读文档从这里开始） |
| [DOC_WORKFLOW.md](DOC_WORKFLOW.md) | **文档规范与强制更新机制**（写文档前先看） |
| [BUILD.md](BUILD.md) | 环境安装与编译指南（git clone 后从零配置） |
| [keship_architecture.md](keship_architecture.md) | 系统架构：目录树、模块设计、数据流、专题笔记索引 |
| [keship_issues_log.md](keship_issues_log.md) | 问题日志：Bug 记录、根因与修复过程（按时间轴追加） |
| [keship_plans.md](keship_plans.md) | 方案索引：`doc/plans/` 的唯一入口 |
| [notes/](notes/) | 专题笔记（活跃） |
| [notes/archive/](notes/archive/) | 已过时 / 已被取代的笔记，仅供追溯 |

> **改动触发的文档更新义务**见 [DOC_WORKFLOW.md](DOC_WORKFLOW.md) §3「强制更新触发表」。
> 简而言之：动源码改架构树，修 Bug 记日志，加笔记必登记，改完跑体检。
