# KeShip 环境安装与编译指南

> 本文件**随仓库分发**（仓库根 `doc/BUILD.md`，clone 后即可看到）。

> 供 `git clone` 后从零配置环境并完成编译。**实测环境**：Ubuntu 24.04.3 LTS + ROS 2 Jazzy + Gazebo Harmonic。
> 按本文 `apt install gz-harmonic` 装到的是 Harmonic **当前最新补丁版**（2026-10 为 8.15.0）；
> 若你的 gz-sim 已打过 KeShip 的 S2 补丁，功能会多一项（见 §5.1）。
>
> **版本口径（2026-10-27 修正）**：`gz-harmonic` 不锁补丁版，apt 装的永远是 Harmonic 系列**当前最新补丁版**——
> 2026-10 实测为 **8.15.0**（`apt-cache policy libgz-sim8` 可见）。同属 8.x，插件 ABI 兼容，
> **按本文安装即可，不必把版本压回 8.10.0**。唯一差别是 §5 末尾的「S2 能力探测」。

## 1. 系统要求
- Ubuntu 24.04 LTS（Noble）
- ROS 2 Jazzy Jalisco（`/opt/ros/jazzy`）
- Gazebo Harmonic（gz-sim8）

## 2. 安装 ROS 2 Jazzy（未安装时）
```bash
sudo apt update && sudo apt install -y software-properties-common
sudo add-apt-repository universe
sudo apt update && sudo apt install -y curl
sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
  -o /usr/share/keyrings/ros-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros2/ubuntu $(. /etc/os-release && echo $UBUNTU_CODENAME) main" \
  | sudo tee /etc/apt/sources.list.d/ros2.list > /dev/null
sudo apt update
sudo apt install -y ros-jazzy-desktop python3-colcon-common-extensions
```

## 3. 安装 Gazebo Harmonic（未安装时）

先添加 Gazebo 官方 apt 源（**必要**：`gz-harmonic` 不在 Ubuntu 默认源与 ROS 2 源中；2026-10-17 修正——原文档漏了加源，干净系统会报"无法定位软件包 gz-harmonic"）：

```bash
sudo curl -sSL https://packages.osrfoundation.org/gazebo.gpg \
  -o /usr/share/keyrings/pkgs-osrf-archive-keyring.gpg
echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/pkgs-osrf-archive-keyring.gpg] https://packages.osrfoundation.org/gazebo/ubuntu-stable noble main" \
  | sudo tee /etc/apt/sources.list.d/gazebo-stable.list
sudo apt update
sudo apt install -y gz-harmonic
```

## 4. 安装 KeShip 依赖
```bash
sudo apt update
sudo apt install -y \
  ros-jazzy-ros-gz-sim \
  ros-jazzy-ros-gz-bridge \
  libassimp-dev \
  libeigen3-dev \
  qtbase5-dev \
  qtdeclarative5-dev \
  qml-module-qtquick2 \
  qml-module-qtquick-controls2 \
  fonts-noto-cjk
```
说明：
- `ros-jazzy-ros-gz-sim`：编译必需（`CMakeLists.txt` 中 `find_package(ros_gz_sim REQUIRED)`）。
- `ros-jazzy-ros-gz-bridge`：运行必需（launch 中 `parameter_bridge` 动态桥接）。
- `libassimp-dev` / `libeigen3-dev` / Qt5 相关：通常随 `ros-jazzy-desktop` 与 `gz-harmonic` 自动安装，此处显式列出以确保完整。
- `fonts-noto-cjk`：中文字体。WSL 默认无中文字体，缺失会导致 GUI 标题/界面中文显示为方框或空白。
- `rclcpp` / `geometry_msgs` / `sensor_msgs` / `ament_*` 随 `ros-jazzy-desktop` 与 colcon 提供；
  `gz-gui8` / `gz-rendering8` / `gz-common5` 随 `gz-harmonic` 提供——2026-09-18 已与
  `CMakeLists.txt` 的 `find_package` 清单逐项核对，无遗漏。

## 5. 编译
```bash
cd ~/keship_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select keship --merge-install
source install/setup.bash
```
注意：Gazebo Harmonic 通过 apt 安装后**无需** `source /usr/share/gz/setup.sh`（该文件不存在），其 CMake 配置与库位于标准路径。

### 5.1 看两行输出，判断你的环境属于哪一种

`colcon build` 的 configure 阶段必定打印下面**之一**：

```
KeShip: gz-sim 具备 SetFluidAddedMass ⇒ 启用「完全出水取消 M_A」(S2)
KeShip: gz-sim **不具备** SetFluidAddedMass ⇒ 跳过 S2（自动降级，其余功能不变）
```

`Link::SetFluidAddedMass()` **不是上游 API**——上游 `Link` 只有只读的 `WorldFluidAddedMassMatrix()`，
这是 KeShip 给 gz-sim 打补丁加的方法。所以：

| 你的环境 | 输出 | 后果 |
|---|---|---|
| **官方 apt 版 gz-harmonic**（任何 8.x 补丁版；**8.15.0 已实测**，见 §7） | 第二行 | **编译通过、自动降级**；只是「船完全出水后取消附加质量 M_A」不做，**仅完全出水工况**的数值偏保守 |
| **已打 S2 补丁的 gz-sim**（自行打补丁后编译） | 第一行 | S2 生效 |

不做这类工况，官方版即可，差异仅限那一段。需要 S2 的，得给上游 gz-sim 打补丁后再编译——
**该补丁与安装流程暂未随仓库分发，需要请联系维护者**；补丁基线是 8.11.0，换到别的补丁版需重做补丁上下文。

## 6. 运行
```bash
~/keship_ws/start_keship.sh              # 一键启动（编译 + 启动）
# 或手动
ros2 launch keship keship.launch.py
```

## 7. 本次验证记录
- 缺失依赖：`ros-jazzy-ros-gz-sim`、`ros-jazzy-ros-gz-bridge`（其余 Qt5/assimp/Eigen3/gz-* 均已随系统安装就绪）。
- 修正：`README.md` 原 `source /usr/share/gz/setup.sh` 为过时命令，已移除。
- 编译验证：`colcon build --packages-select keship --merge-install` 通过，生成 6 个插件库（libKSPlugin_HydroBuoyancy / Thrust / VariableMass / WavesModel / WaveVisual / MenuPluginKeship）+ 脚本 + 资源。
- 代码修复：`CMakeLists.txt` 测试 target 以 `if(EXISTS src_cpp/test)` 保护（测试源码不入库）；移除指向不存在 `urdf/` 的 install 段。
- 2026-09-18 复核：依赖清单与 `CMakeLists.txt` `find_package` 逐项对照一致；本文件**随仓库分发并随 git 提交**，用户 clone 后即可按此配置环境。
- 2026-10-17 修正：§3 补 Gazebo apt 源配置（干净基准环境实测：不加源报 `E: 无法定位软件包 gz-harmonic`；原文写自开发环境，漏了机器上已有的源配置）
- 2026-10-27 验证（8.15.0 编译期）：取 apt 中 `libgz-sim8-dev=8.15.0` 的头文件（`dpkg -x` 解包，未安装、不改环境），
  对 `src_cpp/` 下 **29 个非 GUI、非 test 源文件**做语法编译——**全部通过**（未定义补丁宏，即自动降级路径）；
  反向强制 `-DKESHIP_HAS_SET_FLUID_ADDED_MASS` 则报 `Link has no member named SetFluidAddedMass`，
  说明 8.15 确实无该接口，§5.1 的能力探测是必需的、不能去掉。
  **范围**：语法编译，未做完整 colcon 链接与运行；GUI 源文件未纳入（依赖 AUTOMOC 生成物，需完整 build 目录）。
- 2026-10-27 修正（用户反馈）：apt 源里 `libgz-sim8` 候选已是 **8.15.0**，而文档原先写的 8.10.0 已过时；
  原文「实测环境 8.10.0」会被读成「必须 8.10.0」。已改为「任意 8.x 补丁版均可」＋新增 §5.1 能力探测说明
  （上游无 `SetFluidAddedMass`，官方 gz 走自动降级）。。