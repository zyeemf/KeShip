# KeShip 开发指南

## 项目介绍
KeShip 是一个关于船舶虚拟仿真平台的开发项目，旨在让船舶与海洋工程专业学生更方便地进行船海类虚拟仿真实验。与其他船舶仿真平台不同（比如 Virtual RobotX），即使没有编程经验，用户也可以通过 GUI 来配置和运行仿真。

## 技术栈
- **仿真引擎**：Gazebo Harmonic (gz-sim8)
- **中间件**：ROS 2 Jazzy + ros_gz_bridge
- **语言**：C++17 / Python 3.12
- **UI**：Qt5 5.15 (QWidget + QOpenGLWidget) + QML
- **数学库**：Eigen3
- **网格加载**：Assimp (libassimp-dev)

## 代码规范
- 使用现代 C++ 特性（智能指针、lambda 表达式等）
- 完善的错误处理和日志记录



> **Git 仓库**：仓库根为 `keship_ws`（CodeArts 私有仓库），默认分支 `main`（新历史），旧历史备份于 `legacy` 分支；文本资产入库（代码/ksm/kswf/task/脚本），大模型文件不入库但目录结构保留（.gitkeep，放置指引见 keship_ws/README.md）。

## 编译运行指令

> 完整环境安装（依赖 + 编译）见 [BUILD.md](BUILD.md)。

### 一键启动
```bash
# WSL 终端中：
~/keship_ws/start_keship.sh              # 编译 + 启动
~/keship_ws/start_keship.sh --no-build   # 跳过编译，直接启动

# Windows 中：
# 双击 E:\KeShip\ks_workspace\program\start_keship.bat   （有日志，调试用）
# 双击 E:\KeShip\ks_workspace\program\start_keship.vbs   （无控制台窗口）
```

启动脚本自动完成：GPU 预热 → 编译 → 激活环境 → 启动（崩溃自动重试最多 3 次）。

> **单例互斥**：同一时刻只允许一个 KeShip 实例运行。启动脚本内置互斥锁（flock + 进程检测），
> 已有实例运行时重复启动会被明确提示并拒绝——同时运行多个实例会互相干扰导致仿真错乱
> （gz-transport 服务与话题互相干扰、水域模型 spawn/删除错乱、波浪参数互篡改）。

### 手动启动（调试用）
```
cd \\wsl.localhost\KeShipV0010\home\keship\keship_ws
source /opt/ros/jazzy/setup.bash                          # 激活 ROS 2 环境
colcon build --packages-select keship --merge-install   # 编译 keship 包
. install/setup.bash                                     # 激活本包环境
ros2 launch keship keship.launch.py                      # 启动 KeShip 仿真
```

### 工具脚本

| 文件 | 位置 | 说明 |
|------|------|------|
| `start_keship.sh` | `~/keship_ws/` (WSL 工作空间根目录) | 一键启动：单例互斥 → GPU 预热 → 编译 → 激活环境 → 启动（崩溃自动重试） |
| `start_keship.vbs` | `program/` (Windows 侧) | 无黑框双击启动器，推荐日常使用 |
| `start_keship.bat` | `program/` (Windows 侧) | 有日志黑框启动器，调试用 |
| `migrate_models_all.sh` | `~/keship_ws/scripts/` | 模型目录重构一次性迁移脚本（复制留存版，含 ksm 引用重写与 kswf 重命名，已执行归档） |

启动脚本参数：`--no-build`（跳过编译）、`--headless`（无渲染模式）、`--no-gpu-warm`（跳过 GPU 预热）




## 文档索引

| 文档 | 内容 |
|------|------|
| [keship_architecture.md](keship_architecture.md) | 系统架构 |
| [BUILD.md](BUILD.md) | 环境安装与编译指南（git clone 后从零配置） |
| [notes/archive/ma_dart_removal_fix.md](notes/archive/ma_dart_removal_fix.md) | DART 隐式 M_A（`<fluid_added_mass>`）+ 空中抵消 — 根因分析、方案迭代、Gazebo 加速度 API 使用方法 |

> **说明**：历史文档（keship_plan / keship_timeline / keship_issues_log / plans/ 系列）未随本仓库分发，相关引用见各笔记内文。



