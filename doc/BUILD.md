# KeShip 环境安装与编译指南

> 供 `git clone` 后从零配置环境并完成编译。实测环境：Ubuntu 24.04.3 LTS + ROS 2 Jazzy + Gazebo Harmonic 8.10.0。

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
```bash
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

## 5. 编译
```bash
cd ~/keship_ws
source /opt/ros/jazzy/setup.bash
colcon build --packages-select keship --merge-install
source install/setup.bash
```
注意：Gazebo Harmonic 通过 apt 安装后**无需** `source /usr/share/gz/setup.sh`（该文件不存在），其 CMake 配置与库位于标准路径。

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