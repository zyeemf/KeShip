# KeShip 系统架构
- 本文档包含文件组织架构（目录树）、项目设计架构、数据流、专题文档笔记 
## 文件组织结构
```
keship/（ROS2 功能包，源码根 = src/keship/）
├── include/keship/utils/     # 公共接口头文件（WaterArea / SeaState / KeshipPaths）
├── src_cpp/
│   ├── physics/              # 物理系统插件
│   │   ├── KSPlugin_HydroBuoyancy.cc/hh  # 浮力 + 6-DOF Fossen 水动力合并插件
│   │   ├── KSPlugin_Thrust.cc/hh         # 推进器系统
│   │   ├── KSPlugin_VariableMass.cc/hh   # 可变质量 / 外部力施加器
│   │   ├── KSPlugin_WavesModel.cc/hh     # 波浪场全局管理系统（替代原 WavefieldPublisher）
│   │   ├── WaveVisual.cc/hh              # Gerstner 波浪 GPU 渲染
│   │   ├── Wavefield.cc/hh               # 波浪场参数计算（Wavefield 类）
│   │   └── components/Wavefield.hh       # ECM 波浪场组件（weak_ptr，多模型共享）
│   ├── gui/
│   │   ├── keship/                # 主菜单 + 子模块（MenuPluginKeship.cc/hh/qml/qrc）
│   │   │   ├── experiments/       # 实验对话框
│   │   │   │   ├── DisplacementVerifyDialog  # SP-01 排水量验证
│   │   │   │   ├── StabilityExperimentDialog # SP-02 漂浮圆柱体初稳性
│   │   │   │   ├── IncliningExperimentDialog # SP-03 初稳性倾斜实验
│   │   │   │   ├── HullStabilityDialog       # SP-04 数学船型稳性对比
│   │   │   │   ├── CodeRunnerDialog          # PR-01 学生编程
│   │   │   │   └── CylinderTestDialog        （旧测试）
│   │   │   ├── editors/          # 编辑器
│   │   │   │   ├── ModelSettingsDialog   # 模型属性设置（Full/OnlyPose/KSM 三模式）
│   │   │   │   ├── BuoyancyEditorDialog  # 浮力体配置
│   │   │   │   ├── ThrusterEditorDialog  # 推进器编辑
│   │   │   │   ├── HydrodynamicsEditorDialog # 水动力参数编辑
│   │   │   │   ├── SensorEditorDialog    # 传感器配置
│   │   │   │   ├── EnvironmentParamsDialog # 环境参数编辑
│   │   │   │   ├── WorldSettingsDialog   # 世界原点设置（经纬高/航向，运行时生效）
│   │   │   │   ├── InertiaCalculatorDialog # 惯性计算工具
│   │   │   │   ├── LoadWorldDialog / SaveWorldDialog  # 世界载入/保存（.ksw）
│   │   │   │   └── SelectWorldDialog / SelectKsmDialog # 世界/模型选择
│   │   │   ├── viewers/          # 传感器查看器
│   │   │   │   ├── CameraViewerDialog  # 相机图像查看器
│   │   │   │   └── LidarViewerDialog   # 激光点云查看器
│   │   │   ├── tasks/            # 任务框架
│   │   │   │   └── TaskSetDialog / TaskMonitorDialog / MapWidget
│   │   │   ├── water/            # 水域编辑
│   │   │   │   └── WaterAreaDrawDialog
│   │   │   └── shared/           # 共享组件
│   │   │       └── Buoyancy3DView / ShipHullDialog / LinesPlanDialog / FlowLayout / SensorPaintedItem / SensorView.qml
│   │   └── sensor_overlay_plugin/  # 传感器覆盖插件（SensorOverlayPlugin + SensorPanel.qml）
│   └── utils/                 # 工具类（keship_utils 静态库）
│       ├── ModelManager.cc/hh        # 模型全生命周期管理（Spawn/Cache/解析/重建/通知）
│       ├── Buoyancy.cc/hh            # 浮力计算核心（三角形裁剪 + 浸没体积）
│       ├── FossenModel.cc/hh         # 6-DOF Fossen 阻尼模型（M_A 由 DART <fluid_added_mass> 注入）
│       ├── Tetrahedron.cc/hh         # 四面体体积计算
│       ├── MeshUtils.cc/hh           # 统一 mesh 加载（Assimp）+ 规范化缓存
│       ├── MeshInertiaCalculator.cc/hh # mesh 四面体分解惯性计算
│       ├── PrimitiveMeshGenerator.cc/hh # 基本几何体网格生成
│       ├── CylinderMeshGenerator.cc/hh  # 圆柱体网格生成
│       ├── ShipHullGenerator.cc/hh   # 数学船型生成
│       ├── WaterAreaManager.cc/hh    # 水域管理
│       ├── WaterAreaSerializer.cc/hh # 水域 JSON 序列化
│       ├── WaterAreaVisualManager.cc/hh # 水域可视化
│       ├── TaskTomlParser.cc/hh      # 任务 TOML 解析
│       ├── TaskScoring.cc/hh         # 任务评分引擎
│       ├── StbImageLoader.cc         # stb 图像加载
│       ├── DiagnosticLog.cc/hh       # 诊断日志
│       └── TopicUtil.hh              # 话题工具
├── src_py/                    # Python 模块
│   ├── bridge.py              # Bridge 工具（parameter_bridge 参数构造）
│   ├── ks_launch.py           # 仿真环境启动逻辑
│   ├── ks_model.py            # 模型描述
│   └── setup.cfg
├── scripts/                   # ROS 2 脚本（keship_world_bridge / monitor_sim.py / keship_code_runner.py）
├── models/                    # 波浪渲染资源（GerstnerWaves GLSL；船舶模型见 models_all/）
├── worlds/                    # 世界文件（keship_world.sdf）
├── config/                    # 配置文件（插件 Config + 水域 TOML 示例）
├── launch/                    # 启动文件（keship.launch.py）
├── extern/                    # 第三方头文件库（stb / toml++）
└── hooks/                     # ament 环境钩子（resource_paths.dsv.in）

keship_ws/ 其他顶层目录：
├── models_all/                # 统一模型资源根目录（ks_model .ksm / ks_world .ksw/.kswf / robot_model 原始模型）
├── data/                      # 纯运行时工作目录（整体可删可重建：log/、models/、waves/、preview/、test/、experiments/、keship.lock、keship_water_areas.json）
├── task/                      # 实验任务（.toml）
├── scripts/                   # 运维脚本（migrate_models_all.sh）
├── doc/                       # 本文档目录（架构 / 开发指南 / 专题笔记）
├── start_keship.sh            # 一键启动脚本
└── README.md / AGENTS.md
```

**增删文件后需要更新文件组织架构！**

## 系统架构

### GUI 层

#### 主菜单 (MenuPluginKeship)
- Tab 布局（MenuPluginKeship.qml）：模型管理 / 虚拟实验；面板可双击折叠
##### 模型管理菜单
- 模型加载（SDF / KSM / 圆柱体）、模型保存（完整配置持久化为 .ksm）
- 模型列表管理、水域列表管理（添加默认水域 / 自定义水域绘制 / 删除）
- 世界载入/保存（.ksw）、环境参数（风浪流）、惯性计算工具、传感器查看器开关
##### 虚拟实验菜单
- 实验列表管理（experimentModel，SP-01~04 + PR-01 已启用，其余 SP/SC 置灰）
- SP-01 排水量验证 → DisplacementVerifyDialog
- SP-02 漂浮圆柱体初稳性 → StabilityExperimentDialog
- SP-03 初稳性倾斜实验 → IncliningExperimentDialog
- SP-04 数学船型稳性对比 → HullStabilityDialog
- PR-01 编程计算排水量 → CodeRunnerDialog

#### 模型设置对话框 (ModelSettingsDialog)
- 三种模式：Full（新建）/ OnlyPose（属性）/ KSM（配置回填）：
  Full 模式：全部可编辑；OnlyPose：浮力隐藏/静态置灰；KSM：浮力+惯性置灰
- 位置/姿态同行显示，3 个设置 checkbox 并行
- 惯性质量 + 惯性张量 3×3 矩阵 + 重心偏移 XYZ 同行
- CoG 3D 实时预览（橙色小球，Buoyancy3DView 复用）

#### 浮力体编辑器 (BuoyancyEditorDialog)
- 代理几何体配置（Box/Cylinder），多浮力体增删改
- Buoyancy3DView 3D 预览，volume 实时显示

#### 推进器编辑器 (ThrusterEditorDialog)
- 固定方向推进器配置 + 箭头 3D 预览（Buoyancy3DView）

#### 流体动力编辑器 (HydrodynamicsEditorDialog)
- Fossen 6-DOF 阻尼参数（M_A 由 DART 隐式处理，仅在 KSM 中保存/编辑）

#### 传感器编辑器 (SensorEditorDialog)
- IMU / GPS / 磁力计 / 摄像头 / 激光雷达 配置
- 每个实例可设置独立 name + pose（x y z roll pitch yaw），GUI 以度输入、内部转弧度，输出 `<sensor><pose>`，语义 = **相对 link 坐标系**（与浮力体/推进器一致）
- 单位约定：`SensorInstance::pose[3..5]` 存弧度；KSM 文件与 SDF 均按弧度存取

#### 模型加载坐标系规范化（Mesh 规范化缓存）
- **目标**：任意格式模型（DAE/OBJ/STL/glTF 等）加载后，视觉 / 碰撞 / GUI 预览 / 物理配置（CoG/浮力体/推进器/传感器）统一到"相对 link 的 Z-UP 右手系"，**不修改用户模型文件**。
- **机制**：`MeshUtils::NormalizeMeshToSDFCache(src)` 用 `LoadMeshFromFile`（assimp 烘焙节点矩阵 + DAE up_axis 校正）产出标准系顶点，重写为**无装配矩阵、无 up_axis 声明**的"干净 OBJ"+ MTL，缓存到**源模型目录 `normalized/` 子目录**（随源同生命周期，文件名 `<stem>_<key>`，key 自校验失效重建）；SDF 中所有 file mesh `<uri>` 替换为缓存路径（`ModelManager::NormalizeMeshUrisInSDF`，覆盖 `MakeSDFStringFromValidFileType` / `ParseKSMFile` baseSDF / `SpawnModelInGazeboFromSDFString` / `SpawnModelInGazeboFromFile` 及全部 Rebuild 分支）。
- **关键**：缓存 OBJ 顶点按 **gz-sim 渲染同款坐标**写入——对声明 `up_axis=Z_UP` 的 DAE 应用 gz ColladaLoader 同款映射 `(x,y,z)→(x,-z,y)`（gz 内部 Y-up，OBJ 无 up_axis 时原样渲染），其余格式（OBJ/STL/glTF 等）保持原样；保证 **import 模型渲染 ≡ DAE 直挂渲染**（几何与纹理方位一致，2026-08-04 修复，实验：缓存直挂 vs DAE 直挂 bbox/四色质心重合）。`LoadMeshFromFile` 内存数据仍为 Z-UP（物理/惯性/预览不受影响），仅缓存文件采用渲染坐标。
- **坐标系差异提示**：GUI 预览（`Buoyancy3DView` 等）与内部 2D 预览（footprint/投影图）直接 `LoadMeshFromFile(缓存)` 原样渲染 → 显示几何与源模型朝向差 90°；浮力体/推进器/传感器/相机 pose 按缓存渲染坐标系标定（实测 y 取负号后各设置在预览中回到合适位置，2026-08-04 用户验收记录）。
- **缓存失效**：源文件或纹理的 mtime + size 变化 → key 变化 → 自建重建；同源同目录陈旧缓存自动清理；缓存生成器语义变更通过 key 内嵌版本（`gzmap1`）强制失效重建。
- **输出格式**：OBJ + MTL（单 diffuse，UV/法线保留，纹理副本复制到缓存同目录并相对引用，避免跨目录丢失）。
- **限制**：无 up_axis 元数据格式（OBJ/STL）默认按 Z-UP 原样处理；`model://`/`http://` 等非 file:// uri 不参与规范化。

#### 调试/预览工具
- CylinderTestDialog — 圆柱体浮力测试
- CameraViewerDialog — 摄像头实时画面预览
- LidarViewerDialog — 激光雷达点云预览

#### 共享组件
- **Buoyancy3DView** — QOpenGLWidget，纯 OpenGL 3D 预览
  - 几何体：Box / Cylinder / Sphere / Arrow / 坐标轴
  - 轨道相机（鼠标拖拽旋转 / 滚轮缩放）
  - 被 ModelSettingsDialog（CoG 球）、BuoyancyEditorDialog（浮力体）、ThrusterEditorDialog（箭头）复用

### 物理层
- KSPlugin_HydroBuoyancy：浮力计算（Catto 三角形裁剪 + 浸没体积）、恢复力矩（相对 link 原点）、Fossen 6-DOF 阻尼、多水区切换
- KSPlugin_Thrust：推进器推力（多推进器累加写入 ExternalWorldWrenchCmd）
- KSPlugin_VariableMass：可变质量 / 外部力施加器
- KSPlugin_WavesModel：波浪场全局管理（每水区 Wavefield 对象 + ECM 组件，替代原 WavefieldPublisher）
- WaveVisual：Gerstner 波浪 GPU 渲染（gz-rendering8）
### Bridge 桥接层
- ros_gz_bridge — GZ ↔ ROS 传输（固定 bridge: /keship/model_ready）
- KeshipWorldBridge — 动态管理每模型 parameter_bridge 子进程
- ModelManager 通知机制 — C++ 创建/删除模型时通知 Python 侧
- 传感器、推进器数据传递

#### 数据流
```
GUI (QML) ──gz-transport──→ Gazebo ECM ──→ Physics Systems ──→ DART 物理引擎
                                  ↑                                    │
                                  └── ExternalWorldWrenchCmd ←─────────┘
                                  │
                                  └── Sensor Systems ──gz-transport──→ ros_gz_bridge ──→ ROS 2
```

每仿真步 PreUpdate 执行顺序：
1. KSPlugin_HydroBuoyancy — 读取 Pose → 浮力计算 + 阻尼计算 → 汇总写入 Wrench
2. KSPlugin_Thrust — 读取控制指令 → 累加推进器 Wrench

**Wrench 累加协议**：多插件共享 ExternalWorldWrenchCmd 组件（每步被物理系统 Clear 清零），必须使用累加模式。

**ExternalWorldWrenchCmd 施加语义（重要）**：
- gz-sim8 对 `msgs::Wrench` 的 `force_offset` 字段**忽略**，力施加点为 **link 原点**（非重心、非浮心）。
- 因此浮力恢复力矩必须以 **link 原点** 为参考计算：`τ = (CoB_world − linkWorldPos) × F_b`（KSPlugin_HydroBuoyancy.cc 现状）。切勿改为以重心为参考，否则 CoG≠link 原点时会引入多余力矩 `(linkOrigin−CoG)×F_b`。

### 工具层
#### 结构模型管理 ModelManager

模型全生命周期管理，位于 `src_cpp/utils/ModelManager`。

| 类别 | 方法 | 说明 |
|------|------|------|
| Spawn | `SpawnModelInGazeboFromFile` / `FromSDFString` / `SpawnCylinderInGazebo` | 三种 spawn 入口 |
| Cache | `CacheModelSDF` / `CacheBuoyancyConfig` / `CacheThrusterConfig` 等 | 缓存模型配置供编辑器还原 |
| 查询 | `GetCachedModelInertia` / `GetCachedModelStatic` / `GetCachedModelGeometry` | 从缓存 SDF 解析物理参数 |
| 解析 | `ParseInertiaFromSDF` / `ParseStaticFromSDF` (static) | 独立于缓存的 SDF 字符串解析，供 KSM 加载流程复用 |
| 重建 | `RebuildModelWithAllPlugins` | 应用编辑器修改后重新 assemble + spawn |
| 通知 | `NotifyModelSpawned` / `NotifyModelDeleted` | 模型增删通知（经 /keship/model_ready 桥接至 Python 侧；QML 列表刷新见 MenuPluginKeship::NotifyModelListChanged） |

#### 缓存体系
- `sdfCache_` 
- `buoyancyConfigCache_` 
- `thrustConfigCache_` 
- `hydroConfigCache_` 
- `sensorConfigCache_` 
- `staticCache_`
- `robot_model/<模型>/normalized/` — mesh 规范化缓存（OBJ+MTL+纹理副本，随源同目录、可整体删除重建，key 自校验失效重建；ksm 持久化源路径，加载时统一归一化）




### 专题文档笔记
| 文档 | 内容 |
|------|------|
| [notes/keship_physics_design.md](notes/keship_physics_design.md) | 物理设计：6-DOF Fossen 水动力学模型（坐标系、运动方程、矩阵分解）+ Wavefield–Buoyancy 集成（Gerstner 波浪、浮力算法、线程安全） |
| [notes/archive/wavefield_buoyancy_integration.md](notes/archive/wavefield_buoyancy_integration.md) | Wavefield–Buoyancy Phase 1 实现细节：代码路径、SDF 配置、时间处理、性能分析、Phase 2 展望 |
| [notes/ui_refinement_session.md](notes/ui_refinement_session.md) | UI 交互优化记录 (2026-05-26~27)：ModelSettingsDialog 布局重构、KSM 模式置灰、信号泄漏修复、BuoyancyEditor 删除按钮修复 |
| [notes/archive/virtual_experiment_framework.md](notes/archive/virtual_experiment_framework.md) | 虚拟仿真实验框架：船舶原理/智能控制/结构与制图三门课程 × 三个层级（观察/操作/设计），共 28 个实验及依赖链 |
| [notes/wave_system_changes_2026-06-16_17.md](notes/wave_system_changes_2026-06-16_17.md) | 波浪系统变更记录（2026-06-16~17）：所有新建/修改文件按时间顺序整理 |
| [notes/archive/wave_code_reading.md](notes/archive/wave_code_reading.md) | Wave 代码阅读笔记：Wavefield/WaveVisual/Buoyancy 数据流、shader uniforms、重构关键清单 |
| [notes/archive/wave_optimization_notes.md](notes/archive/wave_optimization_notes.md) | Wave 内部逻辑优化记录（3 性能 + 3 健壮性，2026-06；部分已由后续重构解决，状态待更新） |
| [notes/archive/water_area_implementation_plan.md](notes/archive/water_area_implementation_plan.md) | **多水区场景实施方案**：4 步实施计划（初始无水、地图驱动、局部水区） |
| [notes/archive/water_area_step2_plan.md](notes/archive/water_area_step2_plan.md) | **Step 2 详细方案**：per-area Plane Waves 模型，替代全局 6km mesh |
| [notes/archive/water_area_step1_plan.md](notes/archive/water_area_step1_plan.md) | **Step 1 详细方案**：浮力插件（旧 KSPlugin_Buoyancy）集成 WaterAreaManager |
| [notes/archive/multi_water_areas_design.md](notes/archive/multi_water_areas_design.md) | **方案已修订**：场景驱动水区方案（初始无水、水域随地图加载、水区外无浮力） |
| [notes/archive/wavevisual_sdf_warning_analysis.md](notes/archive/wavevisual_sdf_warning_analysis.md) | WaveVisual SDF 警告分析：`<include>` vs EntityFactory 加载差异、visual search BFS 路径 |
| [notes/math_hull_redesign_plan.md](notes/math_hull_redesign_plan.md) | 数学船型重构方案：Gazebo Z_UP 直接生成、OBJ 法线、DART 碰撞修复 |
| [notes/buoyancy_mesh_theory.md](notes/buoyancy_mesh_theory.md) | 浮力网格理论：Catto 算法、三角形裁剪、浸没体积计算 |
| [notes/archive/phase_d_student_programming.md](notes/archive/phase_d_student_programming.md) | Phase D 学生编程模块设计 |
| [notes/archive/map_model_2d_preview_plan.md](notes/archive/map_model_2d_preview_plan.md) | 2D 地图模型预览方案：ModelFootprint + Pose_V 订阅 + MapWidget 场景模型渲染层 |
| [notes/archive/map_model_render_schemes_plan.md](notes/archive/map_model_render_schemes_plan.md) | 多算法渲染方案：Rect/ConvexHull/AlphaShape/SubMeshHulls 四种轮廓算法 |
| [notes/map_model_texture_preview_plan.md](notes/map_model_texture_preview_plan.md) | ProjectedImage 纹理映射方案：纹理采样（主）+ 法向着色（兜底） |
| [notes/water_area_polygon_mesh.md](notes/water_area_polygon_mesh.md) | **水面多边形网格修复**：`MakeSurfaceSdf` 多边形分支、网格采样、OBJ 生成、OGRE 缓存突破 |
| [notes/archive/water_area_toml_persistence.md](notes/archive/water_area_toml_persistence.md) | **水域 TOML 持久化**：随世界保存/加载、polygon vertices 序列化、追加合并策略 |
| [notes/water_area_ipc_refactor.md](notes/water_area_ipc_refactor.md) | **水域 IPC 重构 (2026-07)**：TOML→JSON，修复轮询 bug，gz-transport 尝试记录 |
| [notes/fossen_physics_refactor.md](notes/fossen_physics_refactor.md) | **Fossen 物理重构 (2026-07)**：惯性校验、阻尼通用化、M_A 6×6 UI、D 矩阵预览 |
| [notes/wind_wave_current_design.md](notes/wind_wave_current_design.md) | **风浪流设计 (2026-07)**：Gerstner 波浪、PMS 频谱、GPU 渲染、风/流参数、UI 编辑器 |
| [notes/archive/gz_transport_shutdown_fix.md](notes/archive/gz_transport_shutdown_fix.md) | **gz‑transport 安全关闭 (2026-07)**：malloc_consolidate 根因、13 个订阅点扫描、两阶段关闭方案 |
| [notes/waves_global_refactor.md](notes/waves_global_refactor.md) | **波浪场全局化重构 (2026-08)**：gz-waves ECM 组件模式、KSPlugin_WavesModel、波浪参数 gain 时序 bug 根治、多水域 per-area |
| [notes/cylinder_hydro_physics.md](notes/cylinder_hydro_physics.md) | **圆柱体水动力物理化 (2026-08)**：fossen/pose-based 物理评估、垂荡 ζ 修正、M_A 转动单位 bug、pose-based 补 M_A |
| [notes/archive/ui_refinement_session2.md](notes/archive/ui_refinement_session2.md) | **UI 优化 (2026-08-01~03)**：Tab 折叠、SP 注释修正；C8 编辑器单例化、C9 水区编辑去波浪参数、D12 世界原点设置 |
| [notes/archive/sp01_experiment_report_design.md](notes/archive/sp01_experiment_report_design.md) | SP-02 实验设计方案（教学定位、前置知识，已归档） |
| [notes/sp02_experiment_guide.md](notes/sp02_experiment_guide.md) | SP-02 实验指导书（GM 三要素、临界条件 D/L=√(8s(1−s)) 验证） |
| [notes/sp03_experiment_guide.md](notes/sp03_experiment_guide.md) | SP-03 实验指导书（矩形驳船、倾斜实验法测 GM） |
| [notes/archive/sp03_experiment_guide.md](notes/archive/sp03_experiment_guide.md) | SP-04 实验指导书（船型系数对稳性影响，已归档） |
| [notes/archive/sp03_bug_analysis.md](notes/archive/sp03_bug_analysis.md) | SP-03 三层几何体 bug 分析（视觉/碰撞/浮力，mesh 碰撞体 DART 不支持） |
| [notes/archive/water_area_step3_plan.md](notes/archive/water_area_step3_plan.md) | Step 3 方案：GUI 水区编辑器 + 默认/自定义双模式 |
| [notes/water_area_step4_plan.md](notes/water_area_step4_plan.md) | Step 4.1 方案：多边形绘制 UX 增强 |
| [notes/圆柱体稳性分析.md](notes/圆柱体稳性分析.md) | 圆柱体可倾斜稳定理论推导（竖直/倾斜/水平三态精确解） |
| [notes/inertia_calculator_tool.md](notes/inertia_calculator_tool.md) | 惯性计算工具（mesh 四面体分解法） |
| [notes/fluid_added_mass_migration.md](notes/fluid_added_mass_migration.md) | Plugin M_A → DART `<fluid_added_mass>` 迁移方案 |
| [notes/archive/buoyancy_drag_fix.md](notes/archive/buoyancy_drag_fix.md) | 浮力阻尼迁移方案（**已归档**，旧双插件架构） |
| [notes/fluid_added_mass_research.md](notes/fluid_added_mass_research.md) | `<fluid_added_mass>` 技术调研（SDF→ECM→DART 全链路） |
| [notes/added_mass_cmd_plan.md](notes/added_mass_cmd_plan.md) | 水动力重构方案 A（M_A 常驻 DART、阻尼-only，已实施） |
| [notes/archive/cylinder_damping_plan.md](notes/archive/cylinder_damping_plan.md) | 圆柱体 Pose-Based 阻尼重构（SP-02 振荡不收敛根因） |
| [notes/ksw_v4_save_load_fix.md](notes/ksw_v4_save_load_fix.md) | KSW 0.0.4 保存/载入链路修复（树结构定稿） |
| [notes/water_area_tree_env_fixes.md](notes/water_area_tree_env_fixes.md) | 水域 UI 系列：层次树/按钮链路/环境参数入口/海况等级一致 |
| [notes/archive/gazebo解算fossen模型隐式显式方案.md](notes/archive/gazebo解算fossen模型隐式显式方案.md) | Fossen 模型在 Gazebo 求解的隐式/显式方案讨论 |
| [notes/water_area_edit_and_polygon_fix.md](notes/water_area_edit_and_polygon_fix.md) | 水域编辑体验（形状统一/交互简化）与多边形判定修复 |
| [notes/ui_refinement_session3.md](notes/ui_refinement_session3.md) | **UI 第三批优化 (2026-08-12)**：推进器名称可编辑、地图提示词统一、窗口开关不影响暂停状态 |
| [notes/data_arch_refactor_2026-08-12.md](notes/data_arch_refactor_2026-08-12.md) | **数据架构整理 (2026-08-12)**：data 纯运行时化、ksm 源路径持久化、网格缓存随源同目录、波浪场载入竞态修复、日志收敛 |



