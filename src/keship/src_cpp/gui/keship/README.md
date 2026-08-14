# MenuPluginKeship — KeShip 主菜单 GUI 插件

Gazebo Harmonic（gz-gui8）主菜单插件。提供"模型管理 / 虚拟实验"两个 Tab，
通过 gz-transport 与 Gazebo 仿真服务器交互，经 ModelManager 完成模型的加载、
配置、保存与重建，并经 /keship/model_ready 桥接至 ROS 2。

## 结构
- `MenuPluginKeship.cc/hh` — 插件主体（模型管理、实验入口、模型列表通知）
- `MenuPluginKeship.qml` — QML 界面（Tab 布局、实验列表 experimentModel）
- `menu.qrc` — Qt 资源
- `editors/` — 模型/浮力体/推进器/水动力/传感器/环境/世界/惯性/载入保存/选择 等编辑器
- `experiments/` — 虚拟实验对话框（SP-01~04、PR-01 编程、CylinderTest 旧测试）
- `viewers/` — 传感器查看器（相机 / 激光雷达）
- `tasks/` — 任务框架（TaskSetDialog / TaskMonitorDialog / MapWidget）
- `water/` — 水域编辑（WaterAreaDrawDialog）
- `shared/` — 共享组件（Buoyancy3DView / ShipHullDialog / LinesPlanDialog / FlowLayout / SensorPaintedItem）

## 构建
随 keship 功能包一起编译（根目录 `CMakeLists.txt`）：
    colcon build --packages-select keship --merge-install

## 运行
由启动脚本（`start_keship.sh`）或 `ros2 launch keship keship.launch.py` 加载，
插件经 Gazebo GUI 配置注入（`config/MenuPluginKeship.Config`）。

## 参考
- 系统架构：`doc/keship_architecture.md`
- 开发指南：`doc/keship_developer_guide.md`