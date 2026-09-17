# Keship 问题日志

## 开发时间轴

| 日期 | 事件 |
|------|------|
| 2026-02 ~ 2026-05-04 | Phase 0 早期：基础框架搭建、浮力引擎原型 |
| 2026-05-05 | Phase 0 Bug 修复密集期：#1~#11, 阻尼问题 |
| 2026-05-06 | Phase 0 设计讨论：代理几何体方案、浮力体编辑器设计、PrimitiveMeshGenerator 实现 |
| 2026-05-07 | Phase 0 功能增强：Marker 3D 可视化(#15)、Bug #13/#14 修复 |
| 2026-05-08 | Phase 0 GUI 改进：保存按钮(#16)、3D 覆盖层探索（失败）、3D 可视化方案记录 |
| 2026-05-09 | Phase 0.5：QOpenGLWidget 3D 预览(#18)、文件模型浮力体编辑器支持(#19) |
| 2026-05-10 | Phase 0 完结：文件模型 + 代理几何体编辑器 + 3D 预览 |
| **2026-05-11** | **Phase 1 Round 1**：推进器底层（ThrusterConfig + KSPlugin_Thrust + ModelManager） |
| **2026-05-11** | **Phase 1 Round 2**：推进器编辑器 GUI + 3D 箭头渲染 |
| **2026-05-11** | **Phase 1 Round 3**：圆柱体推力端到端测试 + 修复（方向/直接力/零推进器） |
| **2026-05-11** | **Phase 1.5**：.ksm 模型保存/加载 ✅ |
| **2026-05-11** | **Phase 2 理论**：Fossen 6-DOF 方程整理到架构文档 ✅ |
| **2026-05-11** | **Phase 2 实现**：FossenModel 工具类 + HydrodynamicsSystem 插件 ✅ |
| **2026-05-11** | **Phase 2.1**：流体动力学编辑器 GUI + .ksm hydro 段 ✅ |
| **2026-05-11** | **Phase 2 fix**：浮力插件自动零阻尼 ✅ |
| **2026-05-12** | **Code Review 修复**：C_A(ν)·ν 索引、M_A 系数、Editor 保存、KSM pose 解析等 7 个 bug ✅ |
| **2026-05-12** | **第二轮修复**：KSM Cyl 字段、spawn pose、空 hydro 缓存、延迟 spawn ✅ |
| **2026-05-12** | **Phase 2.2**：惯性编辑 + 属性对话框三模式重构 ✅ |
| **2026-05-12~13** | **Phase 3**：ROS 2 动态桥接 ✅ — 多次修订，最终完成 |
| **2026-05-13** | **稳定性修复**：spawn rep 检查、allow_renaming、KSM pose、ParseKSMFile try-catch 等 7 项 ✅ |
| **2026-05-13** | **GUI 整理**：暂停/继续按钮、移除形状下拉、保存/载入世界 ✅ |
| **2026-05-14** | **Phase 4.1**：Gazebo 内置传感器集成 ✅ — 5种传感器 + SensorEditorDialog + KSM扩展 + bridge动态桥接 |
| **2026-05-15** | **未来规划整理**：扩展至 Phase 7（轨迹绘制、水下航行器、场景任务、GUI增强、CFD耦合） |
| **2026-05-15** | **Phase 4.1 Bug 修复** ✅：static publisher 消息丢失、SpawnModelInGazeboFromSDFString 缺 post-spawn、NotifyModelSpawned 签名统一、NavSat SDF 格式修正、bridge 分隔符/作用域修复、BDS 标签+ topic remap |
| **2026-05-15** | **KSM 加载全零 hydro 阻尼 Bug 修复** ✅：hasHydro 检测从"系数非零"改为"HYDRO 段有内容" |
| **2026-05-15** | **传感器数据可视化** ✅：CameraViewerDialog + LidarViewerDialog + rclcpp 集成 + QML 下拉选择 |
| **2026-05-15** | **ComboBox 误弹窗修复** ✅：onCurrentTextChanged → onActivated，避免模型重建时自动弹出相机窗口 |
| **2026-05-15** | **激光雷达点云渲染修复** ✅：自适应缩放 → QPainter 渲染 + 滚轮缩放 + 线程安全修复 |
| **2026-05-16** | **激光雷达渲染清理** ✅：诊断日志移除、死码清理、确认 QPainter 方案可行 |
| **2026-05-16** | **多实例传感器 + 位姿编辑** ✅：SensorConfig 重构（5 bool → vector<SensorInstance>）+ SensorEditorDialog 重写 + KSM/Bridge 适配 |
| **2026-05-16** | **传感器编辑器 UI 修复** ✅：保存后保持参数面板显示 + viewer 窗口置顶 |
| **2026-05-17** | **传感器查看器重构** ✅：从 QQuickView+SensorPaintedItem 回退到 QDialog+QLabel/QPainter 方案，修复黑屏问题 |
| **2026-05-17** | **传感器下拉过滤** ✅：RefreshSensorList 只保留 Camera/Lidar，IMU/BDS/磁力计不显示 |
| **2026-05-17** | **删除传感器自动选中** ✅：onDeleteSensor 删除后自动 setCurrentRow(0)，保持参数面板可见 |
| **2026-05-17** | **查看器生命周期管理** ✅：openViewers_ 追踪已打开查看器，传感器重建/删除后自动关闭对应查看器；重复打开同一传感器激活已有窗口 |
| **2026-06-01** | **SP-03 Phase A 完成** ✅ — ShipHullGenerator 重构、OBJ 法线、动态 deckZ、波浪控制 |
| **2026-06-02** | **Phase B 完成** ✅ — ShipHullDialog + SpawnMathHull + LinesPlanDialog |
| **2026-06-02** | **Phase C 完成** ✅ — 排水量验证实验 (DisplacementVerifyDialog) |
| **2026-06-02** | **Phase D 完成** ✅ — 学生编程模块初级 (CodeRunnerDialog + keship_code_runner.py, Python/C) |
| **2026-06-04** | **Phase 10 方案修订** ✅ — 重新编号 Phase 9~15，Phase 13 拆分出 Phase 10 任务框架，TOML 格式简化 |
| **2026-06-04~05** | **Phase 10 实施** ✅ — TaskTomlParser + TaskScoring 新建，MapWidget/TaskSetDialog 修复，TaskMonitorDialog 重写，单例化集成 |
| **2026-06-16** | **WSL GPU 崩溃修复 + 一键启动** ✅ — GPU 预热脚本、崩溃自动重试、Windows VBS 双击启动 |
| **2026-06-16** | **多片水域方案设计** 📋 — [multi_water_areas_design.md](notes/multi_water_areas_design.md)：独立高度+波浪参数、水域联通、4 阶段实施计划 |
| **2026-07-29** | **圆柱体振荡不收敛** — 三个根因：(1) `cylinderDefaults` 系数量级方向错误（径向低、轴向高）(2) UI 路径中 `<fluid_added_mass>` 未被注入 DART（M_A 丢失）(3) `aboveWater` 阈值切断阻尼，出水后重新激发振荡。修复方案 A 含 3 个文件改动（HydrodynamicsEditorDialog.cc + ModelManager.cc + KSPlugin_HydroBuoyancy.cc） |
| **2026-07-31** | **圆柱体阻尼收敛 ✅** — 方案 A（线性ζ法 + Morison 二次项）实施后复测：roll 阻尼力矩 1.9 万→129 万 N·m，不再发散，但发现倾倒后绕轴自转衰减不了（yaw 阻尼 τ=62s 不足）。yaw 修复：linYaw=0.15·m·L²/12 + qYaw=0.5ρCd·2πr·L_sub·r²；随后纯物理化：移除 ζLin 经验法，全部系数来自几何/流体物理。终测（cy1 Fossen 1°）：4s 倾倒侧躺、roll 稳定 88~93° 无翻转、wy ±0.073 双向小幅振荡。Pose-Based 无需改（湿面积积分已含 yaw 阻尼） |
| **2026-07-31** | **波浪设置不生效 Bug 修复 ✅** — waveGain=0 下 waveHeight 仍 ±0.37m。根因链：`ApplyWaveParamsToWavefield` 被注释（M_A 重构遗留）+ 对话框只写 JSON 不发 PMS + SDF 默认 gain=1。修复：5 入口统一 PMS 下发（持久发送器 + 300ms 补发）+ WavefieldPublisher JSON 双通道（Configure + 每 1s mtime 轮询）。验证 20260731_174827：waveHeight 38.8s 后恒 0.0000，静水真实生效 |
| **2026-07-31** | **运行时路径迁移 ✅** — `/tmp`、`~/.keship` → `$HOME/keship_ws/data/`：新增 `KeshipPaths.hh` 路径中心化（9 处源码 + 2 脚本 + 历史数据拷贝）。验证 20260731_173322：data/keship_water_areas.json 写入成功（waveGain:0，旧谜底 = 库路径问题）、/tmp 无残留、data/waves|preview|test 自动创建 |
| **2026-07-31** | **静水终测收敛 ✅** — 20260731_174827（waveHeight≡0）：wy 93s <0.005 rad/s（124s=0.0020）、roll 稳定 1.570 无翻转、pz 单调衰减（124s ±0.09m，ζ_eff≈0.07 预计 ~180s 达 0.05m）、tauD 与 0.5ρCdAv² 吻合。验收按物理现实定稿（"30s 内几乎静止"与 ζ=0.10 不可兼得） |
| **2026-07-31** | **物理模型评估 ✅（不再改动）** — M_A 由 DART `<fluid_added_mass>` 注入，圆柱 SDF 无 M_A（只影响瞬态不影响稳态）；横荡-首摇耦合项对称圆柱影响≈0；多水区波浪参数取第一个水区。三项均留待真实船型阶段 |
| **2026-06-16** | **Wave 代码阅读任务** ✅ — 为水域重构做准备：Wavefield/WaveVisual/Buoyancy 数据流梳理 |
| **2026-06-16** | **水域勾选框** ✅ — MenuPluginKeship 新增 CheckBox，通过 remove/create 服务控制 Waves 模型显示/隐藏 |
| **2026-06-16** | **WavefieldPublisher** ✅ — 新建全局波浪参数发布插件，启动即发布 PMS 参数，定时重发；不改 WaveVisual/Buoyancy |
| **2026-06-16** | **统一 PMS 波浪方案** ✅ — 4 实验对话框静水改用 PMS+gain=0，Publisher 加立即重发，去掉 CWR 依赖 |
| **2026-06-17** | **多片水域 Phase 1** ✅ — WaterArea + WaveParams 数据结构、WaterAreaManager（TOML 加载 + XY 查找）、示例配置 |
| **2026-06-17** | **波浪系统变更记录** 📋 — [wave_system_changes_2026-06-16_17.md](notes/wave_system_changes_2026-06-16_17.md)：勾选框/Publisher/PMS/双Topic/SDF 全部改动按时间整理 |
| **2026-06-17** | **水域勾选框无效 + 水面抖动 Bug 修复** ✅ — 实体名大小写不一致导致多实例重叠 |
| **2026-06-17** | **Step 1 实施** ✅ — KSPlugin_Buoyancy 集成 WaterAreaManager，浮力按水区查找，水区外无浮力 |
| **2026-06-17** | **Step 1 勾选框联动 Debug** ⚠️ — 经 Topic/Service/ECM/File 四种方案迭代，定位为控制流缺陷 |
| **2026-06-17** | **Step 1 勾选框联动修复** ✅ — 文件读取提到 Configured 外层，勾选框取消后浮力消失 |
| **2026-06-17** | **Step 1 代码重构** ✅ — 删除迭代过程中残留的无效代码（atomic, ecm\_, Name.hh, HasMultipleAreas 等）|
| **2026-06-18** | **Step 2 实施 + 测试通过** ✅ — Plane 几何体 per-area Waves，纹理绝对路径修复，勾选框联动 |
| **2026-06-18** | **Step 4 完成** ✅ — 2D 多边形绘制 + 名称输入 + 悬停提示 + 双击编辑顶点 |
| **2026-06-18** | **已知局限记录** 📋 — MapWidget 无 2D 底图，空白网格上绘制顶点 |
| **2026-06-21** | **新问题记录** 📋 — 2D 地图看不到加载的地图模型，[方案文档](notes/map_model_2d_preview_plan.md) |
| **2026-06-21** | **2D 地图模型预览实施** ✅ — Step 1~3 完成，12 文件改动，编译通过，[方案文档](notes/map_model_2d_preview_plan.md) |
| **2026-06-21** | **代码审查发现** 📋 — 3 项遗留问题记录（见下方"2D 地图预览遗留问题"） |
| **2026-06-22** | **多算法渲染方案实施** ✅ — 4 种方案（Rect/ConvexHull/AlphaShape/SubMeshHulls），默认 ConvexHull |
| **2026-06-22** | **修复：右键双击误关闭水区对话框** ✅ — `mouseDoubleClickEvent` 加 `LeftButton` 过滤 |
| **2026-06-22** | **修复：编辑水区后水面创建失败** ✅ — `SpawnAll` 轮询 `/scene/info` 确认旧实体消失再创建，删除外层 `DeleteAll` |
| **2026-06-22** | **新问题记录** 📋 — 凸包预览不准确 + 水面矩形非多边形（见"当前高优先级待办"） |
| **2026-06-22** | **ProjectedImage 方案实施** ✅ — 三角形投影填充 2D 图像（256×256，边缘函数法），默认方案 |
| **2026-06-22** | **修复：坐标系不匹配** ✅ — `LoadMeshFromFile` 后加 `ConvertToZUp(Y_UP)`，确保投影为俯视图 |
| **2026-06-22** | **新计划记录** 📋 — ProjectedImage 纹理映射（法向着色兜底），见 [方案文档](notes/map_model_texture_preview_plan.md) |
| **2026-06-22** | **纹理映射方案实施** ✅ — stb_image + UV提取 + 纹理采样 + 法向兜底，编译通过 |
| **2026-06-22** | **修复：GetCachedModelGeometry 未初始化 radius 导致误入 Cylinder 分支** ✅ — 新增 ModelGeometryType 枚举，CacheModelFootprint 改为 switch(type) |
| **2026-06-23** | **水面多边形网格修复** ✅ — `MakeSurfaceSdf` 新增 polygon 分支：网格采样 + OBJ 生成（含 UV/法线），`vertices>=3` 时用 `<mesh>` 替代 `<plane>`
| **2026-06-23** | **OBJ 警告消除** ✅ — `WritePolygonObjFile` 加 `o water_surface` 对象名声明 |
| **2026-07-14~15** | **水域 IPC 架构重构** ✅ — TOML → JSON，修复 PreUpdate 轮询 bug（static 计数器），文件 I/O 替代 gz-transport，详见 [water_area_ipc_refactor.md](notes/water_area_ipc_refactor.md) |
| **2026-07-15** | **Fossen 物理模型重构** ✅ — 惯性校验 + 阻尼项通用化(order 任意/交叉耦合) + M_A 6×6 对称矩阵 UI + D 矩阵彩色拓扑预览，详见 [fossen_physics_refactor.md](notes/fossen_physics_refactor.md) |
| **2026-07-15** | **set_pose 并发 Bug** ✅ — 属性对话框 OK 后模型不规律运动，根因 `SetModelPoseByIDInGazebo` 与仿真并发导致位置/速度不一致，修复为暂停态调用 |
| **2026-07-15** | **恢复力矩 CoG 参考点修复** ✅ — 浮力插件恢复力矩参考点从 link 原点修正为 `InertialComponent::Pose()`（重心偏移），支持用户自定义 X/Y/Z CoG 偏移 |
| **2026-07-16** | **风浪流参数系统** ✅ — `WindParams`/`CurrentParams` 数据结构，`EnvironmentParamsDialog` 风浪流编辑器，浪向跟随风向自动同步，详见 [wind_wave_current_design.md](notes/wind_wave_current_design.md) |
| **2026-07-16** | **gz‑transport 安全关闭** ✅ — 13 个 Subscribe 点全扫描，6 个组件实施两阶段关闭，消除 `malloc_consolidate` 崩溃，详见 [gz_transport_shutdown_fix.md](notes/gz_transport_shutdown_fix.md) |
| **2026-07-16** | **目录结构重组** ✅ — `plugins_physics`→`physics`, `menu_plugin_keship` 拆分为 `gui/keship/{experiments,editors,viewers,tasks,water,shared}` |
| **2026-07-16** | **架构清理** ✅ — 删除 `KSPlugin_Controller` 空骨架，`VariableLoad`→独立插件 `KSPlugin_VariableMass`（质量修改替代外力施加） |
| **2026-07-16** | **SP 实验重编号** ✅ — 排水量验证实验→SP-01，原 SP-01~03→SP-02~04 顺延，按钮从模型管理 tab 移入实验 tab |
| **2026-07-16** | **风/流力学方案设计** 📋 — 风力公式（航空阻力）+ 流力速度偏移法，5 文件改动规划，详见 [wind_wave_current_design.md](notes/wind_wave_current_design.md) §五 |
| **2026-07-24~25** | **HydroBuoyancy 合并** ✅ — `KSPlugin_Buoyancy` + `HydrodynamicsSystem` → `KSPlugin_HydroBuoyancy`；更新 `ModelManager::MakeBuoyancyPluginSDF` 签名及 8 处调用点；删除 4 个旧插件文件；修复 3 个结构缺陷（bodies.clear、loadMeshData.clear）；WSL 编译通过 |
| **2026-07-27** | **合并后 Bug 修复（4个）** ✅ — ①力矩参考点（link 原点）、②空bodies跳过水动力（guard 修正）、③Wrench 顺序竞争（SET→ACCUMULATE）、④static diagCounter（→成员 `logWriteCounter`）；文档同步更新 |
| **2026-07-28** | **Issue 2 尝试一：显式 Fossen + 原生加速度 API** ❌ — 无水域自由落体正常 ✅，有水域瞬间发散 ❌（λ=1.975>1）|
| **2026-07-28** | **Issue 2 根治：DART 隐式 M_A + 空中抵消** ✅ — `<fluid_added_mass>` 放回 DART（隐式积分，无条件稳定），空中施加 `+M_A·ν̇ + C_A·ν` 抵消（λ=0.664<1 指数收敛）。详见 [notes/ma_dart_removal_fix.md](notes/ma_dart_removal_fix.md) |
| **2026-07-31** | **运行多次后 Gazebo 卡死 + 强杀后无法启动** 📋 — WSL 强杀导致实例状态残留（journald 损坏）+ OGRE 渲染缓冲泄漏（约 20 次后卡死）；恢复手段 `wsl --shutdown`。详见下方专题章节 |
| **2026-08-02~03** | **start_keship.bat 闪退 + 多实例干扰 → 单例化** ✅ — `for /f` 嵌套单引号致 cmd 解析失败闪退（连 echo 未显示）；分区机制回退；启动脚本加 flock 单例互斥，详见下方专题章节 |
| **2026-08-04** | **缓存 OBJ 渲染与 DAE 直挂不一致修复（纹理/姿态错乱根因）** ✅ — 根因：gz ColladaLoader 对 `up_axis=Z_UP` DAE 做 `(x,y,z)→(x,-z,y)`（内部 Y-up），而缓存 OBJ 无 up_axis 原样渲染，旧缓存顶点（assimp Z-up）与 DAE 直挂差 90° → 侧立/纹理错乱。修复：缓存写出时对 Z_UP DAE 应用 gz 同款映射（`MeshUtils::NormalizeMeshToSDFCache`），`LoadMeshFromFile` 内存数据仍为 Z-up（物理/惯性/预览不受影响）；key 内嵌 `gzmap1` 版本强制旧缓存失效重建；`test_mesh_normalize` 增加 gz 映射断言（Z_UP 源按映射后 bbox 校验）。验证：dump_verts（gz-common 加载）缓存顶点 ≡ gz-common(DAE)；gz 渲染对照 bbox/四色质心完全重合（x[271,356] y[403,497]） |
| **2026-08-04** | **缓存坐标系语义 — 用户实测验收记录** ✅ — 启动 keship 加载 KSM：贴图正确；模型坐标原点相对旧缓存有变化（预览中原点在左浮筒顶端→右浮筒顶端，部件整体偏移约 1.98m≈模型高轴范围），浮力体/推进器/传感器/相机位置按新渲染坐标系重新标定（实测将 y 位置加负号后各设置在预览界面回到合适位置，camera 位于两浮筒之间，画面符合预期）。结论：修复后符合预期，无新问题。坐标系差异为缓存按 gz 渲染语义写入（`(x,-z,y)`）所致，`LoadMeshFromFile` 内存语义（Z-up）未变 |
| **2026-08-04** | **wam_0804_2 新双体船静水持续晃动诊断 ✅** — 波高增益 0 下持续晃动。根因：HYDRO 的 heave/roll/pitch 阻尼全 0（dampt=2/3/4 系数 0、damp=Xu..Nv 全 0、附加质量全 0）→ 无阻尼自由振荡；次因质心 z=0.3 高于浮心 z≈-0.87、纵摇 GM 偏小。修复：对话框填线性阻尼 Zw/Kp/Mq=1500/900/1150（Xu 400/Yv 600/Nr 800/Yr −135/Nv −135，取自 ke0803）+ 附加质量 m11/m22/m33/m44/m55=360/270/132.5/135/480、m15/m51=−405。验证：用户实测加载后"很稳定" ✅。详见下方专题 |
| **2026-08-03** | **加载层坐标规范化（Mesh 规范化缓存）** ✅ — 任意格式模型（DAE/OBJ/STL/glTF）spawn 时统一规范化为 Z-UP 标准 OBJ（`NormalizeMeshToSDFCache`，缓存 `data/models_normalized/`，mtime+size 失效重建，纹理副本随缓存）；`NormalizeMeshUrisInSDF` 替换 SDF 全链路 file mesh uri（`MakeSDFStringFromValidFileType`/`ParseKSMFile`/`SpawnModelInGazeboFromSDFString`/Rebuild 等）；消除 gz 渲染与 GUI 预览的镜像不一致（keship3 宽轴）；新增 `test_mesh_normalize` 单测（DAE/OBJ/STL 三格式恒等 PASS）+ `gen_test_world` 世界 uri 校验 + `gz sdf -k` 通过 |
| **2026-08-01~02** | **UI 优化第一批** ✅ — SP 注释修正（Stability→SP-02、Inclining→SP-03、HullStability→SP-04）；Tab 折叠（真 cardPane + 原生动画、expListView TypeError 修复、内容区固定 550） |
| **2026-08-03** | **UI 优化第二批** ✅ — C8 编辑器单例化（4 编辑器按 `类型/模型名` 单例，已开置前）；C9 水区编辑去波浪参数（用默认值）；D12 世界原点设置（经纬高/航向，运行时生效不持久化）。详见 [ui_refinement_session2.md](notes/ui_refinement_session2.md) |
| **2026-08-03** | **添加传感器后模型丢失崩溃修复** ✅ — `RebuildModelWithSensors` 组装 SDF 缺 `</model></sdf>` 闭合 → create 解析失败（Error Code 30）→ 模型消失 → 持续"未找到模型" → 后续操作崩溃。修复：补闭合标签 + 4 编辑器重建失败弹警告。详见下方专题章节 |
| **2026-08-03** | **传感器 pose 全类型支持** ✅ — IMU/GPS(NavSat)/磁力计 SDF 输出补齐 `<sensor><pose>`（原仅 Camera/Lidar 输出），统一"相对 link"语义；GUI 与 KSM 本就含 pose 字段，无需改动。验证：`gen_test_world` 重编冒烟 + `gz sdf -k` 校验通过 |

| **2026-08-05** | **波浪显示修复：一格一格/周期性强** ✅ — ①海况换算加 Tp 下限 2.5s（1 级海况波长 1.95m→9.76m）；②修复 `GerstnerWaves_vs_330.glsl` 的 P.y 误用 dx bug + 3 分量→8 分量（命名 uniform + phase）；③网格按 PMS 最高显著分量波长自适应（每波长 ≥3 格）；④`RecalculatePms` 的 omegaSpacing 动态生成（避免 number>3 越界 NaN）；⑤scale 1.6→1.1（PMS 谱带内采样，打破高频 aliasing）。验证：`colcon build` ✅，user 实测"好很多了" ✅ |

| **2026-08-07** | **双推直线方案定稿** ✅ — 方案③（offset=推力点、循环内逐推进器 `AddWorldWrench`）实测原地旋转；根因 `ExternalWorldWrenchCmd` 每帧单次消费、后写覆盖先写（`Physics.cc` L2578-2613/L4108-4113）；修复为方案①循环外单次写入（累加 `totalForce`+`totalTorque=Σ(P−CoM)×F`），实测双推"可以走直线" ✅ |

| **2026-08-08** | **UI 三项修复 + 世界存取修复** ✅ — ①菜单整体背景统一淡绿 `#d0d0d8c0`（root Rectangle 加 color）；②"默认水域"行三个控件（CheckBox+2按钮）`anchors.verticalCenter` 垂直居中；③载入世界修复 4 缺陷：`LoadWorldManifest` 解析限定 `"models"` 数组区间（括号配平+跳过字符串），不再误命中水区 JSON 中的 name 字段，纯水区世界（无模型）可正常载入；`OnBtn_LoadWorld` 载入前弹确认框"可能与场景已有模型冲突，请谨慎载入！"+`DeleteAllModelsInGazebo` 清场 +`waterAreaMgr_.Clear()` + 载入后 `waterVisualMgr_.SpawnAll` 刷新水面；文件模型缓存核查 `CacheNormalizedSDF` 已存在无需改动。验证：`qmllint` 无输出 ✅、解析逻辑模拟测试 3/3 PASS ✅、`colcon build` 通过 ✅ |

| **2026-08-09** | **KSW 0.0.4 保存/载入链路修复（A/B/C + 树结构定稿）** ✅ — A：保存/载入对话框树顶层叶子挂接 bug（`QTreeWidgetItem(nullptr)` 节点未挂入树 → 顶层实体不可见不可勾选 → 载入只出水面不出模型）→ `addTopLevelItem` + 统一子 ksw 链分组；B：保存树显示**原名**（来源 baseName）、实例名仅作提交键（4 段行 `链/勾选/原名/实例名`）；C：载入顺序重排（水域+水面先就绪 → 静态模型先 → 动态 → 最后 resume，消除模型"跳起"）+ 中文命名空间→非法 ROS2 topic 崩溃防护（新建 `utils/TopicUtil.hh` 注净函数应用 6 处 + 载入对话框命名空间默认留空/正则校验）；树结构定稿：去掉中间"新增"容器，保存对话框无根、载入对话框保留世界名根，均顶层平铺 `模型/水域/子ksw链组`（`catGroupOf`）。详见 [notes/ksw_v4_save_load_fix.md](notes/ksw_v4_save_load_fix.md)。验证：用户实测全部"符合预期" ✅ |

| **2026-08-10** | **层次树折叠修复 + 水域按钮链路** ✅ — ①树折叠全面化（models/waters/sub 三类组键 `chain+"/"+gtype`，默认全展开、搜索模式跳过折叠过滤）+ 子世界链内 models/waters 组行可见性受 `treeExpanded[chain+"/sub"]` 闭合判定链；②文字保底宽 `Math.max(...)` 消除空白行、模型行按钮压至 36px、Row 补 `anchors.left` 缩进生效；③恢复"添加默认水域"按钮（一键 250×250 矩形，固定名"默认水域"，查重防御拦截重复添加，`enabled: customWaterAreaCount === 0`）；④删除修复（按名 `remove_if` 全删 customs 同名，修复删后按钮仍灰 bug）；⑤`LoadConfig` 启动补 `NotifyModelListChanged()` 刷新（修复启动树 base=0 不显示）；⑥**水域模式态体系拆除**（删除 `showWater_`/`OnToggleWater`/Q_PROPERTY 全套，`LoadConfig` 恢复循环无条件恢复全部水区并 push `customWaterAreas_`，修复"恢复的水区不可编辑/删除"旧债）；⑦`WaterAreaSerializer` 版本化 v2（`{"version":2,"areas":[...]}`，旧格式裸数组丢弃并警告，单测 ALL PASS） |

| **2026-08-11** | **环境参数入口 + 启动空场景 + 海况等级一致** ✅ — ①水域行"环境"按钮 → 新增 `OnBtn_ShowWaterEnv(int _index)`：`EnvironmentParamsDialog` 通用化（风/浪/流/海况）、customs 同名同步、Areas 重建、`RebuildWaterAreas`、`PublishWaveParams` 下发（水面即时变化+持久化）；②**启动即空场景**：删除 `LoadConfig` 的 JSON 恢复块（需求：关闭时的水域不得跨重启继承），启动写空 JSON —— 物理插件（WavesModel 1s 轮询/HydroBuoyancy 250 帧，均按 mtime 热更新）自动清空波浪场与水区配置，数据/视觉/物理三者一致；③**海况等级一致**（选择=生成）：根因 = 1 级 Hs=0.05 的理论 Tp=1.12s 被渲染下限 `kMinTp=2.5` 截断后 gain 仍写死 1.0 → 反推 Hs=0.25 漂移成 2 级；修复 = `SeaState::ParamsFromLevel` 新增 4 参（增益按实际周期反算 `gain=Hs/(0.04·Tp²)`，1 级自动补偿 0.2，无截断时恒为 1.0）+ 对话框选级消息框显示真实增益与截断提示；④默认水域/自定义水域初始参数标准化为"标准 1 级"（周期 2.5s/增益 0.2/风速 1.42m/s），与对话框选"1级"完全一致；⑤单测新增 0~9 级全往返断言 ALL PASS。详见 [notes/water_area_tree_env_fixes.md](notes/water_area_tree_env_fixes.md)。验证：用户实测海况选择/默认水域参数"符合预期" ✅ |

| **2026-08-11** | **水域编辑统一 + 参考显示** ✅ — ①**形状统一**：默认水域生成矩形四角 vertices（顺时针），与手动输入/旧矩形 ksw 兜底四角同序 —— 默认水域与自定义水域一样以点构成形状、编辑可见可拖拽；②编辑路径无条件传多边形（不再仅传自定义，旧矩形 ksw 水域兜底四角），ksw 保存/载入 vertices 持久化闭环；③**编辑参考显示**：绘制/编辑水域对话框显示全部水域 —— 其他水域灰虚线框+名称只读参考（MapWidget 新增 `setReferenceWaterAreas`/`fitToAll`/`paintReferenceAreas`，绘制在模型之上、当前多边形之下，不进鼠标交互）；新增/编辑流程均传参考（编辑排除自身）；④修复**"编辑重置风浪参数"存量 bug**（编辑后周期/增益/风速被清零：`setInitialArea` 参数注入改为先取当前值、手动输入保存合并保留 env 参数）；⑤**交互简化**：移除"手动输入"按钮（删除 `WaterAreaEditorDialog` 文件/CMakeLists/引用）、双击顶点改单对话框同时编辑 X/Y。详见 [notes/water_area_edit_and_polygon_fix.md](notes/water_area_edit_and_polygon_fix.md) |

| **2026-08-11** | **水域多边形判定修复** ✅ — 现象：三角形水域"包围盒内、多边形外"处不掉落（误判在水域内）；根因：`WaterArea::Contains` 仅做 AABB 包围盒判定，凹/非矩形顶点集合不参与；修复：包围盒快速拒绝 + 射线法多边形判定（vertices≥3 按真实多边形，矩形恒为 1-2 个叉积保持原语义）；`test_buoyancy_unit`/`test_sea_state` ALL PASS，用户实测"符合预期" ✅。详见 [notes/water_area_edit_and_polygon_fix.md](notes/water_area_edit_and_polygon_fix.md) |

| **2026-08-12** | **UI 改善第三批** ✅ — ①推进器名称可编辑（空名/重名校验 + `onSaveBody` 返回 bool 失败中止关闭 + 自动命名防重名，重名=推力 topic 冲突）；②地图提示词统一移至地图下方一行（不再遮挡地图），底部去掉写死"双击设航向"，绘制水域/任务设定/任务执行窗口清理重复/误导提示；③窗口开关不再影响仿真暂停/继续（新增 `SetSimulationPaused` 统一通道，模型属性对话框/载入世界记录打开前状态、结束后还原）。验证：`colcon build` ✅、user 实测符合预期 ✅。详见 [notes/ui_refinement_session3.md](notes/ui_refinement_session3.md) |

| **2026-08-14** | **模型目录重构 + Git 仓库重构** ✅ — 四旧目录（models_ksm/models_robot/models_env/models_ksw）复制合并至 `models_all/{ks_model,robot_model,ks_world}`（复制留存版）；ksm 引用相对路径化（重写 75 处，完整前缀替换）；6 个 `keship_world.ksw`→`.kswf` 重命名（8 世界全部可加载）；`SelectKsmDialog` 双通道 + 各对话框默认目录；git 根提升 keship_ws（新历史 main、旧历史 legacy 备份、master 保护分支保留旧历史）；大模型不入库、目录结构 .gitkeep 保留。详见 [plans/model_dir_reorg_plan.md](plans/model_dir_reorg_plan.md) |

---

## 中段子串替换导致路径断裂 (2026-08-14) ✅

**现象**：模型目录迁移首次执行后抽查发现 `.ksm` 内 uri 变成 `file:///home/keshiprobot_model/keship3/keship3.dae`、`file://data/models/Wigley_vis.obj` —— 路径断裂。

**根因**：重写替换串采用"中段子串"（`/keship_ws/models_robot/` → `robot_model/`），只替换中间段，残余前缀与替换结果直接拼接：
- `file:///home/keship` + `robot_model/` = `file:///home/keshiprobot_model/`（缺失 keship_ws 分隔段）
- `file://` + `data/models/` = `file://data/models/`（file:// 前缀残留）

**修复**：替换串必须含**完整前缀**（`file://` + 绝对路径整段 → 相对路径）：
- `file:///home/keship/keship_ws/models_robot/` → `robot_model/`；裸路径版本同规则兜底
- 删除 models_all 重建复制品后重跑，全部 75 处重写正确，残留校验 0

**教训**：批量文本替换前先做小样本演练（本次首版未抽查即全量执行，靠事后校验发现）；替换串设计须保证"替换单元 = 完整可替换前缀 + 目标相对路径"边界清晰。

## 双推直线方案定稿：方案①单次写入 (2026-08-07) ✅

**现象**：方案③（offset=推力点、torque=0，循环内逐推进器 `AddWorldWrench`）实施后，模型开始**原地旋转**；方案①（质心力矩 + 原点施加、循环外单次写入）双推 200N×2 实测**可以走直线**。

**根因**：`ExternalWorldWrenchCmd` 组件每帧只被 gz-sim 物理系统消费**一次**且**后写覆盖先写**：
- 消费：`gz-sim8` `src/systems/physics/Physics.cc` L2578-2613（`UpdatePhysics` 中 `Each<ExternalWorldWrenchCmd>` → `AddExternalForce` + `AddExternalTorque`）
- 清空：`UpdateSim` L4108-4113（步进后 `Data().Clear()`，一帧生效一次）

因此方案③ 在推进器 `for` 循环内对每个推进器各调一次 `AddWorldWrench`，前一个推进器的 wrench 被下一个覆盖 → 实际只有**最后一个推进器**的力被施加 → 净力矩 `(P_last−CoM)×F ≠ 0` → 原地旋转。这与 offset 数学无关，是组件单件消费机制所致。

**修复**（`KSPlugin_Thrust.cc`）：
- 循环内只累加 `totalForce`、`totalTorque = Σ(P_i−CoM)×F_i`
- 循环外**单次** `Link::AddWorldWrench(_ecm, totalForce, totalTorque)`（力施于 link 原点）
- 保留 `totalTorque` 诊断列（绕质心计算值，仅诊断）

**结论**：
- 方案①（质心力矩 + 原点施加、单次写入）为最终采用方案
- 方案③（offset=推力点、torque=0）因 `ExternalWorldWrenchCmd` 单件消费机制不可行，弃用（除非把每个推进器做成独立 link 实体，成本高，不推荐）

**验证**：`colcon build --packages-select keship --merge-install` 通过 ✅；用户实测双推 200N×2 可以走直线 ✅

---

## 波浪显示修复：一格一格/周期性强 (2026-08-05) ✅

**现象**：1 级海况水面呈现"一格一格"（网格状），2 级海况周期性明显；修复过程中曾出现水面完全不可见（NaN 回归）与周期性增强（scale 失衡）。

**根因链（4 项叠加）**：
1. **网格欠采样**：`WaterAreaVisualManager::GeneratePolygonMesh` 的 `divs` clamp [50,200]，500m 水区步长 2.5m；1 级海况 Tp=1.12s → 波长仅 1.95m < 步长 → 波浪呈方块状。
2. **shader bug**：`GerstnerWaves_vs_330.glsl:121` 的 `P.y -= q*a*dx*s` 应为 `dy`（波浪方向/陡度非 0 时位移错误）。
3. **硬编码 3 波分量（R1 遗留）**：`WaveVisual::UploadShaderParams` 固定 `amplitudeV[3]`，UI 波数 1~20 会越界/截断。
4. **PMS 规则性强**：`number=3`、`scale=1.1`、相位均匀 `i·2π/n` → 合成波呈规则条纹；且渲染 shader 与 `ComputeDepthSimply` 均未使用 phase（两者不一致）。

**修复（5 项，9 文件）**：
1. **海况换算加周期下限**（`SeaState.hh` / `test_sea_state.cc`）：`TpFromHs` 加 `kMinTp=2.5s` → 1 级海况波长 1.95m→9.76m，2 级及以上不受影响；新增 3 条断言。
2. **shader dy bug + 动态波数**（`GerstnerWaves_vs_330.glsl` / `WaveVisual.cc` / `Wavefield.cc` / `EnvironmentParamsDialog.cc`）：P.y 改 dy；3 硬编码波→8 组命名 uniform（amp/k/om/st/ph/dir ×8）；`UploadShaderParams` 按 `Number()` 动态填充；`ComputeDepthSimply/Directly` theta 加 phase 与渲染一致；UI 波数上限 20→8。
3. **网格按波长自适应加密**（`WaterAreaVisualManager.cc`）：步长按 PMS 最高显著分量波长（n=(number-1)/2，λ_min=λ_p/scale^((number-1)/2)）计算，每波长 ≥3 格，divs 上限 200→400。
4. **RecalculatePms 动态带宽**（`Wavefield.cc` / `Wavefield.hh`）：omegaSpacing 固定 3 元素→按 number 动态生成（n=i-(number-1)/2 主波居中，带宽取相邻频率间隔：内部点两侧各半、边界单侧、number=1 全谱）；number=3 与原实现逐位一致（回归安全），number=1~8 无 NaN。**此修复解决了水面不可见的 NaN 回归**（Phase 4 默认 number 3→6 触发越界）。
5. **scale 回退 1.1**（`WaterArea.hh` / `Wavefield.cc` / 配置文件 ×2）：默认 `number 3→6` 保留、`scale 1.6→1.1`。数学依据（PMS 谱半功率带宽 ω∈[0.80,1.375]ωp）：scale=1.6 时频率跨度 10.5 倍，仅 2 个分量在谱带内（双频拍频）+ 最高频分量波长 0.93m 远小于网格步长（渲染 aliasing）；scale=1.1 时 6 个分量全部均衡且在带内（波长均 ≥6.1m 可解析）。

**验证**：
- `colcon build` 编译通过 ✅
- `test_sea_state`（25 条断言）/ `test_buoyancy_unit` 全部 PASS ✅
- 独立验证程序：number=3 与原实现逐位一致、number=1~8 振幅全部有限非负（无 NaN）✅
- 修复后日志 `amplitudes: 0.0040, 0.0054, 0.0063, 0.0066, 0.0064, 0.0058`（6 分量全均衡）✅
- **user 实测："好很多了"** ✅

**涉及文件**：`SeaState.hh`、`test_sea_state.cc`、`GerstnerWaves_vs_330.glsl`、`WaveVisual.cc`、`Wavefield.cc`、`Wavefield.hh`、`WaterAreaVisualManager.cc`、`EnvironmentParamsDialog.cc`、`WaterArea.hh`、`config/keship_water_areas.toml`、`config/keship_water_areas_example.toml`

---

## wam_0804_2 持续晃动诊断 (2026-08-04) ✅

**现象**：新双体船模型 `models_ksm/wam_0804_2.ksm`，波高增益 0（静水）下加载后持续晃动。

**根因**：
1. **主因 — 三方向零阻尼**：HYDRO 段 dampt 表 heave(2)/roll(3)/pitch(4) 系数全 0，线性 `damp=Xu..Nv` 全 0，附加质量全 0。Fossen 阻尼（FossenModel.cc:87-103）只对非零系数项出力 → 垂荡/横摇/纵摇无阻尼，静水下初始扰动形成等幅自由振荡永不衰减（垂荡周期约 2.5s）。
2. **次因 — 纵摇稳性弱**：质心 (1.4, 0.9, **0.3**) 高于浮心 z≈**-0.87** 约 1.17m；浮筒 r=0.16 竖直细柱水线面小（BM≈0.36）→ GM_pitch 偏小，加重摆动。
3. 坐标系自洽（浮筒/质心与 wamv bbox 对齐），非坐标系问题。

**修复（HydrodynamicsEditorDialog 对应值）**：
- 线性阻尼：Xu **400** / Yv **600** / **Zw 1500** / **Kp 900** / **Mq 1150** / Nr **800** / Yr **−135** / Nv **−135**
- 附加质量：m00 15 / m11 360 / **m22 270** / **m33 132.5** / **m44 135** / m55 480 / m15 −405 / m51 −405
- 来源：`ke0803`（300kg 级双体船已验证参数）；对话框 Xu..Nr 与 KSM dampt 线性项等价（`Zw ⟺ dampt=2,<val>,2,1`），保存后自动写为 dampt 表，插件直接生效。

**验证**：用户实测加载后"很稳定"，晃动消失 ✅。

**备注（留档）**：
- 量级差异：ke0803（300kg、浮筒 r0.26 水平 2.6m）vs wamv（250kg、浮筒 r0.16 竖直 3.5m、排水体积约 ×0.51）→ 原版系数偏"肉"（过阻尼），如需更真实响应可换适配版：Zw 1100 / Kp 700 / Mq 850，附加质量 m22/m33/m44≈140/70/70、m55 250（垂荡 ζ≈0.88 近临界）。
- 若后续出现低频纵摇（抬头低头、周期数秒）：质心 z 0.3→0.05~0.1，或浮筒 pose z 0.1→0.4（提高浮心）。
- 惯量 120/340/370 与模型尺寸（3.76×1.89×1.46 m）匹配，无需改；质量 250kg 与排水量自洽。

---

## 添加传感器后模型丢失崩溃 (2026-08-03) ✅

**现象**：模型添加传感器 → "应用并重建模型" → Gazebo 报 `Error Code 30: Error parsing XML from string` → 模型消失，控制台持续打印"服务调用scene：未找到模型KeShip03" → 后续操作崩溃。

**根因**：`ModelManager::RebuildModelWithSensors`（ModelManager.cc:2432-2454）组装 SDF 末尾**缺 `</model></sdf>` 闭合标签**（其余 7 处组装函数 Buoyancy/Thrust/Hydro/VariableMass 均有闭合，唯独此函数遗漏）。create 服务解析失败 → 新模型未创建（旧模型已删除）→ 场景悬挂。

**修复**：
1. `ModelManager.cc` `RebuildModelWithSensors` 补 `sdf << "  </model>\n" << "</sdf>\n";`
2. `MenuPluginKeship.cc` 4 个编辑器（浮力/推进器/流体动力/传感器）accepted 回调检查重建返回值，失败弹 `QMessageBox::warning`（"模型重建失败，请重试"），避免静默悬挂

**验证**：用户实测添加相机成功 ✅；控制台 `[Wrn] [Ogre2Camera.cc:423] SetVisibilityMask: Mask bits c0000000 ...` 为**良性警告**（gz-sensors 默认 mask 含 ogre2 保留位 bit30/31，渲染端自动忽略，上游已知问题 gz-rendering#830，方案 A 不处理，等上游修复）。

---

## UI 优化（2026-08-01~03）✅

详见 [ui_refinement_session2.md](notes/ui_refinement_session2.md)。

**第一批（08-01~02）**：
- SP 注释修正：Stability→SP-02、Inclining→SP-03、HullStability→SP-04
- Tab 折叠：`toggleCollapsed()` 取真 cardPane（`root.parent.parent.parent`），state 匹配走原生 200ms 动画、否则 height 兜底；expListView 605/608 TypeError（delegate 根补 `width: expListView.width`）；内容区固定 550 防底部锚定控件滑移；清理 console.log

**第二批（08-03）**：

| 任务 | 内容 | 关键点 |
|------|------|--------|
| C8 | 编辑器单例化 | key=`类型/模型名`（如 `Buoyancy/keship_01`）；`acquireEditor`：已开→置前返回 nullptr，否则 factory 创建 + WA_DeleteOnClose + `destroyed` 擦除；4 slot（Buoyancy/Thrust/Hydro/Sensor）改造；ModelSettingsDialog 与创建链不动 |
| C9 | 水区编辑去波浪参数 | 删波浪 QGroupBox 段 + 6 spinbox 成员 + set/get 波浪读写；`getWaterArea()` 保留，waveParams 用默认值（WaterArea.hh 成员初始化） |
| D12 | 世界原点设置 | 新建 `WorldSettingsDialog`（纬度±90/经度±180/海拔±9999m/航向±180°，默认 36.00558/120.12091/0/0）；`OnBtn_WorldSettings` → Request `/world/keship_world/set_spherical_coordinates`（1000ms）；QML Tab1 第 1 行加"世界设置"按钮；CMakeLists 注册 |

**涉及文件**：`MenuPluginKeship.hh/.cc/.qml`、`WaterAreaEditorDialog.hh/.cc`、`WorldSettingsDialog.hh/.cc`（新建）、`CMakeLists.txt`

**验证**：qmllint 无输出 ✅；`colcon build` Finished ✅；用户实测待启动验证。

---

## start_keship.bat 闪退 + 多实例干扰 → 单例化 (2026-08-02~03) ✅

**现象**：
1. `program/start_keship.bat` 双击闪退——控制台一闪而过，**连 `echo` 标题都没显示**，gazebo 未启动；`start_keship20260718_back.bat`（旧版）可正常启动
2. 背景：曾为实现多实例引入分区机制（bat 检测实例数 → 提示输入实例名 → `--partition` 传参 + launch.py `GZ_PARTITION` + KeshipPaths.hh 水区文件分区后缀），修改后闪退

**根因**：
- **闪退**：`for /f "delims=" %%i in ('wsl -d KeShip -- bash -c "pgrep -f 'gz sim' | wc -l"')` — cmd 的 `for /f` 命令定界符与内部嵌套单引号 `'gz sim'` 冲突，cmd 解析失败直接退出，后续行（含 `pause`）不执行
- **多实例干扰**：gz-transport 服务/话题互相干扰（水域 spawn/删除错乱、波浪参数互篡改）——多实例方案本身脆弱

**方案决策**：放弃多实例，改为**单例模式**（用户确认）

**改动**：

| 文件 | 改动 |
|------|------|
| `program/start_keship.bat` | 重写：删除 `for /f` 检测、实例名交互、`--partition` 传参；保留"快速(1)/编译(2)"模式选择；结构对齐 back.bat |
| `program/start_keship.vbs` | 删除 `PARTITION` 常量与 `--partition` 传参 |
| `~/keship_ws/start_keship.sh` | 开头加单例互斥：`flock -n` 原子锁（`data/keship.lock`，进程退出自动释放）+ `pgrep -f "[g]z sim -v 4"` 辅助检测（`[g]z` 防自匹配）；删除 `--partition` 参数解析与 `GZ_PARTITION` export |
| `launch/keship.launch.py` | 回退：删除 partition 参数段、`DeclareLaunchArgument('partition')`、`os.environ['GZ_PARTITION']` 设置 |
| `include/keship/utils/KeshipPaths.hh` | 回退：`KeshipWaterAreasFile()` 删除 GZ_PARTITION 判断，恢复固定 `keship_water_areas.json` |
| `keship_developer_guide.md` | 多实例说明改为单例互斥说明 |

**验证**：
- `colcon build` 编译通过 ✅
- bash/python 语法检查通过 ✅
- flock 跨进程互斥：第二进程被阻塞 ✅、释放后可重新获取 ✅
- `[g]z` 模式：不误匹配自身 ✅、能匹配模拟 gz sim 进程 ✅
- 用户实测：bat 正常启动、运行中重复启动被拒绝 ✅

**保留**：`start_keship20260718.bat`、`start_keship20260718_back.bat` 作为旧版备份不动。

---

## 运行多次后 Gazebo 卡死 + 强杀后无法启动 (2026-07-31) 📋

**现象**：
1. 运行约 20 次后 Gazebo 卡死（画面冻结）
2. Windows 任务管理器强杀后，再次启动报错退出、无窗口
3. `wsl --shutdown` 后重新启动一切正常

**调查发现**：
- `~/.gz/sim/log` 有 551 个日志目录、407MB（最大单日志 203MB，历史遗留 `libHydrodynamicsSystem.so` 每步打印 nu/tau，现世界已用 KSPlugin_HydroBuoyancy）
- journald 日志文件损坏（"corrupted or uncleanly shut down"）— 强杀导致 WSL 未干净关闭
- ogre2.log 反复出现 `Deleting mapped buffer without having it unmapped` — OGRE 渲染缓冲泄漏警告
- dmesg 的 `dxg Ioctl failed: -22/-2` 经查证为 WSL 已知问题（microsoft/WSL#11293），属启动时探询的正常噪声，非损坏证据

**根因分析**：
1. **强杀 → WSL 实例非正常终止 → 内部状态残留**（残留进程/共享内存/未关闭句柄）→ 下次启动冲突报错
2. **OGRE 渲染缓冲泄漏** → 每次运行泄漏 GPU 缓冲 → 约 20 次后累积到阈值 → 渲染线程卡死（解释"约 20 次"的规律性）

**结论**：底层 WSL/OGRE bug 无法根治（微软 issue 未修复）；问题源头是"任务管理器强杀"这一操作习惯，正常退出（窗口 X / Ctrl+C）不会产生残留。

**当前方案**：卡死后 `wsl --shutdown` → 重新启动即可恢复（用户已验证有效）

**待办**：
- [ ] 下次复现启动报错时，记录报错文本并检查残留进程，确认主因是残留进程还是渲染泄漏
- [ ] 可选改进：.bat 增加"修复模式"选项（wsl --shutdown + 自动重启）；启动前自检 WSL 响应

---

## 代码审查：输出参数反模式 (2026-06-22)

| # | 位置 | 问题 | 严重度 |
|---|------|------|--------|
| 1 | `GetCachedModelGeometry` Box/Cylinder 分支 | 部分输出参数未写，依赖 switch 正确分支 | 低 (当前安全，防御不足) |
| 2 | `KSPlugin_Buoyancy.cc:383` | `cylinderRadius_ > 0.0` 用作几何类型哨兵 | 低 (与本次修复同一类反模式) |

---

## 2D 地图预览遗留问题 (2026-06-21 → 2026-06-22)

| # | 问题 | 严重度 | 状态 |
|---|------|--------|------|
| 1 | `CacheModelFootprint` 仅从 `SpawnModelInGazeboFromFile` 调用，缺少 `SpawnModelInGazeboFromSDFString` / `SpawnCylinderInGazebo` / `SpawnMathHull` 三个路径 | 中 | ✅ 已修复 (2026-06-23) |
| 2 | ~~`OnBtn_EditWaterArea` 中 `setPolygonVertices` 覆盖 `fitToSceneModels` 视图~~ | 低 | ✅ 已修复（随轮询修复一同处理） |
| 3 | `WriteWaterAreasToml` 不写多边形顶点到 TOML，自定义多边形水区形状在保存/加载后会丢失 | 中 | ✅ 2026-06-23 已修复 |
| **4** | **2D 凸包预览不准确** — 凹形建筑被凸包填充，需更精确轮廓 | 中 | 📋 新增 |
| **5** | **Gazebo 水面为矩形非多边形** — `MakeSurfaceSdf` 只用包围盒生成 Plane，忽略多边形顶点 | **高** | ✅ 2026-06-23 已修复 |

---

## 当前高优先级待办 (2026-07-16)

| 优先级 | 问题 | 关联文档 |
|--------|------|---------|
| **中** | **2D 凸包预览不准确** — 凹形建筑被凸包填充，需更精确轮廓 | [map_model_render_schemes_plan.md](notes/map_model_render_schemes_plan.md) |

---

## 今日修复 (2026-06-29)

### MapWidget 地图拉伸修复 ✅

**问题**：缩放窗口时地图跟着拉伸/收缩，正方体俯视图变成长方形。

**根因**：MapWidget 缺少 `resizeEvent`，窗口尺寸变化时 `pw/ph` 变化导致 `sx ≠ sy`（像素非正方形）。多次尝试失败（back-face culling、DetectFileUpAxis、preserveAspectRatio、面积恒定等均不满足需求）。

**最终方案**：保存 `scale_`（像素/米）为成员变量，仅由鼠标滚轮改变，窗口 resize 时用 `scale_` 重新计算视口范围。

**修改**：
- `MapWidget.hh`：新增 `resizeEvent` override、`initViewScale()`、`scale_` 成员
- `MapWidget.cc`：
  - `fitViewToWidget()` 重写为基于 `scale_` 计算 `rangeX/Y = pw(ph) / scale_`
  - `initViewScale()` 从初始范围 + 窗口大小计算初始 `scale_ = min(pw/rangeX, ph/rangeY)`
  - `resizeEvent` 调用 `fitViewToWidget()` + `update()`
  - `wheelEvent` 结尾更新 `scale_ *= (1.0 / zoom)`
  - `setPolygonVertices` / `fitToSceneModels` 调用 `initViewScale()` 替代原先的 `fitViewToWidget()`

**效果**：窗口拉伸时地图内容像素大小不变；滚轮缩放独立于窗口大小。

**涉及文件**：`MapWidget.hh`, `MapWidget.cc`

### WaterAreaDrawDialog 重复提示移除 ✅

**问题**：绘制水域对话框顶部已有 QLabel 提示"左键放置顶点 | 拖拽移动 | 右键删除 | 中键平移 | 滚轮缩放 | 双击闭合"，地图中央又重复显示同一提示。

**修复**：`WaterAreaDrawDialog.cc` 中 `setPolygonMode(true)` 后添加 `map_->setHintText("")` 清除中央提示文本。不影响 TaskSetDialog 等其他使用 `setHintText` 的地方。

**涉及文件**：`WaterAreaDrawDialog.cc`

---

### 水域 IPC 架构重构 (2026-07-14~15) ✅

**关联专题文档**：[notes/water_area_ipc_refactor.md](notes/water_area_ipc_refactor.md)

**问题**：
1. 勾选默认水域后日志不断重复刷屏 `WaterAreaManager: 加载水域 [默认水域]`
2. 首次加载模型自由落体无浮力，第二次加载才有

**根因分析**：
1. **重复加载**：`KSPlugin_Buoyancy::PreUpdate()` 中 `static int reloadCounter` 被所有插件实例共享，多实例时重载频率放大，且每次无条件调用 `LoadFromToml` 输出 `std::cout` 日志
2. **首次无浮力**：后续尝试用 gz-transport topic 替代文件轮询，但 `Configure()` 中 topic discovery 需 200-500ms+，首次 `PreUpdate` 时连接未就绪，`waterAreasConfigured_` 仍为 false

**方案迭代**（详见专题文档）：

| 尝试 | 方案 | 失败原因 |
|------|------|---------|
| 1 | gz-transport Topic 推送 + QTimer 重发 | `Configure()` 中 topic 发现未完成 |
| 2 | gz-transport Service 同步调用 | gz-transport13 无 `AdvertiseService` API |
| 3 | Topic 请求 + spin-wait 500ms | 发现延迟 > 自旋上限 |
| 4 | SDF 嵌入 + Topic（双通道） | 改动 ModelManager，耦合度高 |
| **最终** | **文件 I/O + JSON + 修复轮询** | ✅ |

**最终方案核心改动**：

| 类别 | 改动 |
|------|------|
| 文件 I/O | GUI `WriteWaterAreasJson()` → JSON 文件；Server `Configure()` `ifstream` 同步读取（零延迟） |
| 轮询修复 | `static int` → 成员变量 `waterAreaReloadCounter_`；增加 `last_write_time` 时间戳检测，文件未变跳过重载 |
| 新序列化器 | `WaterAreaSerializer` — JSON 格式，零外部依赖，替代 `toml++` |
| 格式迁移 | TOML → JSON：世界文件存取、IPC 数据传递全部 JSON |
| 移除 | `WaterAreaManager::LoadFromToml`、`MenuPluginKeship::WriteWaterAreasToml`、`waterAreasConfigPath_`、所有 gz-transport topic/Service 调用、QTimer 重发 |

**修改文件（9 个）**：
`WaterAreaSerializer.hh/.cc`（新建）、`WaterAreaManager.hh/.cc`、`KSPlugin_Buoyancy.hh/.cc`、`MenuPluginKeship.hh/.cc`、`CMakeLists.txt`

**关键认知**：文件 I/O 是仅有的能在 `Configure()` 中同步获取初始状态的跨进程通信方式。`Configure()` 是同步上下文，`PreUpdate()` 在其返回后立即开始，任何依赖异步完成的初始化都不可靠。

---

## 当前最新状态 (2026-06-17)

### Phase 0-4.1 ✅ 已完成
- 浮力引擎、多浮力体、文件模型加载、QOpenGLWidget 3D 预览
- 推进器系统、Fossen 水动力学、.ksm 保存/加载
- ROS 2 动态桥接、Gazebo 内置传感器

### 2026-06-17 水域勾选框修复 ✅
- **Bug**：Phase 2 加入 WavefieldPublisher 后，勾选框无法取消水域、水面持续抖动
- **根因**：`SpawnWaves()`/`DeleteWaves()` 使用实体名 `"Waves"`（大写），而 SDF 模型名为 `"waves"`（小写），Gazebo 实体名匹配大小写敏感导致删除静默失败 → 重复实例叠加 → z-fighting 抖动
- **修复**：
  - `MenuPluginKeship.cc`：`SpawnWaves()` + `DeleteWaves()` 中 `set_name("Waves")` → `set_name("waves")`
  - `WaveVisual.cc`：`OnWavefield()` 增加参数变化检测，WavefieldPublisher 重复发布相同参数时跳过 shader 重上传（兜底防抖）
- **验证**：勾选/取消正常，水面无抖动 ✅

### 2026-06-17 多水区 Step 1 实施 ✅
- **改造 8 个文件**：KSPlugin_Buoyancy 集成 WaterAreaManager，浮力按时查找水区、水区外无浮力
- **详情**：[water_area_step1_plan.md](notes/water_area_step1_plan.md)
- **涉及文件**：KSPlugin_Buoyancy.{hh,cc}、ModelManager.{hh,cc}、WaterAreaManager.cc、MenuPluginKeship.{hh,cc}、keship_world.sdf
- **新建文件**：`config/keship_water_areas.toml`（测试水区，400m×400m，baseHeight=5m）
- **测试入口**：点击"圆柱测试" → BuoyancyConfig 传入 waterAreasConfig → 生成的 Buoyancy SDF 含 `<water_areas_config>` → KSPlugin_Buoyancy::Configure 加载 TOML → PreUpdate 按水区查找
- **编译验证**：通过 ✅

### 2026-06-17 Step 1 勾选框联动修复 ✅
- **Bug**：勾选框取消后浮力仍存在（经 Topic/Service/ECM/File 四种跨进程通信方案迭代）
- **根因**：并非跨进程通信问题，而是控制流缺陷——文件读取和水区启用判断被锁在 `if (waterAreasConfigured_)` 内部，TOML 未加载时整块跳过，`inWater` 永远为 `true`，浮力永远在 `fluidLevel_=0`
- **修复**：文件读取提到 Configured 外层，`if (!enabled) → inWater = false` 无条件执行；`LoadConfig` 写初始文件
- **验证**：取消勾选 → 浮力消失 ✅

### 2026-06-16~17 新增 ✅
- **一键启动**：`start_keship.sh`（WSL）+ `start_keship.vbs`（Windows 无黑框）+ `start_keship.bat`（调试）
- **WSL GPU 崩溃修复**：Python ctypes 预热 EGL/D3D12 管线 + 启动自动重试
- **水域勾选框**：UI CheckBox → remove/create 服务控制 Waves 模型显隐
- **WavefieldPublisher**：新建全局波浪参数发布插件（PMS，定时重发），不改 WaveVisual/Buoyancy
- **统一 PMS 波浪方案**：4 实验对话框静水改用 PMS+gain=0，Publisher 立即重发，去掉 CWR
- **Wave 代码阅读**：[wave_code_reading.md](notes/wave_code_reading.md)
- **Wave 优化记录**：[wave_optimization_notes.md](notes/wave_optimization_notes.md)（待实施）
- **多片水域方案**：[multi_water_areas_design.md](notes/multi_water_areas_design.md)（待实施）
- **多片水域 Phase 1**：WaterArea + WaveParams 数据结构、WaterAreaManager（TOML + XY 查找）

### SP-01/02/03 虚拟实验 ✅ 已完成
- SP-01 漂浮圆柱体初稳性、SP-02 矩形驳船倾斜、SP-03 数学船型稳性对比
- 波浪控制：实验对话框打开=PMS+gain=0（静水），关闭=PMS+gain=1（恢复）

### Phase A: ShipHullGenerator v2 ✅ 已完成
- Gazebo Z_UP 直接生成水密网格（6 种船型）
- OBJ 含顶点法线，DART ODE 碰撞正常
- 静水力参数 + 排水量曲线 + 型值表
- 压载块 deckZ 动态追踪

### Phase B: 数学船型模型加载 + 型线图 ✅ 已完成
- 主界面"数学船型"入口 → ShipHullDialog → SpawnMathHull
- LinesPlanDialog 横剖线/半宽水线/纵剖线三视图
- 模型列表浮力体按钮自动隐藏（mesh 浮力不适用于代理几何体编辑器）
- 波浪响应（wavefield 配置）

### WSL 一键启动工具 ✅ 已完成 (2026-06-16)
- **新建文件**：`~/keship_ws/start_keship.sh`（WSL 启动脚本）、`program/start_keship.vbs`（Windows 无黑框启动）、`program/start_keship.bat`（Windows 调试启动）
- 功能：GPU 预热 → 编译（可选） → 环境激活 → 启动（崩溃自动重试）
- 用户操作：双击 `start_keship.vbs` 即可启动，无需手动输入命令

### WSL GPU 崩溃问题 (2026-06-16)
- **现象**：WSL 中首次启动 Gazebo 偶发 segfault（~30% 概率），重新运行一次正常
- **崩溃路径**：OgreNext GL3Plus → Mesa EGL → Gallium → WSL D3D12 → NVIDIA WSL 驱动 (`libnvwgf2umx.so`) → `Segfault at 0xa130`
- **根因**：WSLg D3D12/NVIDIA GPU 驱动初始化竞争条件，首次创建 EGL 上下文时驱动状态未完全就绪
- **修复**：
  - `start_keship.sh` Step 1：Python ctypes 预加载 `libEGL.so` / `libd3d12.so`，提前触发 GPU 管线初始化
  - `start_keship.sh` Step 4：检测退出码 139 (SIGSEGV)/134 (SIGABRT)，自动重试最多 3 次
- **文件**：`~/keship_ws/start_keship.sh`

## Phase 0 — 基础框架与浮力系统 (2026-02 ~ 2026-05)

### 问题 #1: GUI 窗口宽度不足
- **发现**: 2026-05-05 前
- **现象**: 菜单插件按钮被截断
- **根因**: keship_world.sdf 中 MenuPluginKeship 窗口宽度 230px 太窄
- **修复**: 宽度 230→480, 高度 350→420
- **文件**: `keship/worlds/keship_world.sdf`

### 问题 #2: 库名冲突
- **发现**: 2026-05-05 前
- **现象**: 编译链接错误，重复符号
- **根因**: 旧包 `src/menu_plugin_keship/` 与 `src_cpp/` 下新代码产生同名库
- **修复**: 删除旧包 `src/menu_plugin_keship/`

### 问题 #3: QML 布局不美观
- **发现**: 2026-05-05 前
- **现象**: 按钮堆在一行，形状下拉与按钮混淆
- **修复**: 按钮拆分为两行，第一行"载入世界/载入模型/圆柱体浮力测试"，第二行"形状下拉"

### 问题 #4: 碰撞几何体导致 DART 崩溃
- **发现**: 2026-05-05 前
- **现象**: Gazebo 启动后 segfault
- **根因**: OBJ mesh 缺少顶点法线导致 DART 物理引擎 segfault
- **修复**: 碰撞改为 SDF 内置 `<cylinder>`，OBJ 仅用于 visual + 浮力计算

### 问题 #5: 过量日志导致 SIGKILL
- **发现**: 2026-05-05 前
- **现象**: 仿真运行数秒后被系统 kill
- **根因**: `Buoyancy.cc` 热路径每步输出数百行 cout，缓冲区爆满
- **修复**: 改为仅初始化日志 + NaN 警告；并行计算改为单线程

### 问题 #6: OBJ 渲染崩溃
- **发现**: 2026-05-05 前
- **现象**: 3D 视图崩溃/闪退
- **根因**: Qt/Gazebo OBJ loader 无材质无纹理无法线时 crash
- **修复**: visual+collision 都改用 SDF 内置 `<cylinder>`，OBJ 仅浮力计算内部使用

### 问题 #7: 临时 OBJ 文件管理
- **发现**: 2026-05-05
- **现象**: 每次生成圆柱体需要写临时 OBJ 文件
- **根因**: `ModelManager::SpawnCylinderInGazebo` 生成 OBJ 文件再加载
- **修复**: 不再生成 OBJ 文件；通过 SDF 传递 `<cylinder_radius>`/`<cylinder_length>`/`<cylinder_segments>` 参数；`KSPlugin_Buoyancy` 调用 `GenerateCylinderMesh()` 在内存中生成网格

### Bug #8: 浮力计算使用错误变量
- **发现**: 2026-05-05
- **现象**: 部分浸没三角形体积计算错误
- **根因**: `Buoyancy.cc:896` (case -1 水上三角形) 中用了 `tri.area_` 而非 `tri.volume_`
- **修复**: 改为 `tri.volume_`

### Bug #9: 所有三角形面积 = 0.5
- **发现**: 2026-05-05
- **现象**: 网格总容积仅为理论值的 ~33%（圆柱体 390 vs 393 m³）
- **根因**: `gz::math::Vector3d::Normalize()` 就地修改向量。`cross.Normalize()` 后 `cross.Length()` 永远=1.0，导致 `area = 1.0 * 0.5 = 0.5`
- **修复**: 在 `Normalize()` 之前保存 `area = cross.Length() * 0.5`
- **影响范围**: `loadMeshData(aiMesh*)` 和 `loadMeshData(MeshDataSimple)` 两个重载

### Bug #10: LoadMeshAndInitBuoyancy 控制流错误
- **发现**: 2026-05-05
- **现象**: 圆柱体参数分支后仍尝试加载 mesh 文件
- **根因**: cylinder 分支后的 mesh 文件加载代码在 if-else 外部
- **修复**: 改为正确的 if-else 结构

### Bug #11: linkZ 恒为 0 (Pose 读取错误)
- **发现**: 2026-05-05
- **现象**: 圆柱体下沉但 linkZ/subVol 始终不变，浮力不随深度增长
- **根因**: `components::Pose` 对 link 实体返回的是相对 model 的局部坐标，始终不变
- **修复**: `this->linkEntity_` → `this->entity_`，读取 model 的世界 Pose
- **文件**: `KSPlugin_Buoyancy.cc`

### Bug #12: 初始姿态设置失效
- **发现**: 2026-05-06
- **现象**: GUI 设置 roll/pitch 后生成模型无倾斜
- **根因**: `SpawnCylinderInGazebo` / `SpawnModelInGazeboFromFile` 直接将欧拉角 `_pos[3..5]` (roll/pitch/yaw) 写入 protobuf 四元数字段 `mutable_orientation()->set_x/y/z()`，遗漏 w 分量。`q = (roll, pitch, yaw, 0)` 不是单位四元数
- **修复**: `gz::math::Quaterniond q(roll, pitch, yaw)` 转换后写入 x, y, z, w 四分量
- **文件**: `ModelManager.cc`

### 阻尼不生效（不编号）
- **发现**: 2026-05-06
- **现象**: 即使设置 drag 系数，圆柱体振荡无衰减
- **根因 1**: 缺少 `EnableVelocityChecks(_ecm, true)` 调用，Physics 系统不填充速度组件
- **根因 2**: 阻尼公式无 mass scaling。直接 `c * v` 而非 VRX 的 `c * m_partial * v`，对于 ~200,000 kg 的圆柱体，有效阻尼力差了 5 个数量级
- **修复**:
  - 添加 `linkObj_.EnableVelocityChecks(_ecm, true)` 到 Configure
  - 对齐 VRX 公式：$F_d = -c_{lin} \cdot m_{partial} \cdot v$，其中 $m_{partial} = m \cdot V_{sub}/V_{total}$
  - 改用 `Link::WorldLinearVelocity()` / `Link::WorldAngularVelocity()` 替代直接 ECM 组件读取
  - 默认系数调整：linear=0.5, angular=0.05
- **参考**: `vrx/vrx_gz/src/PolyhedraBuoyancyDrag.cc`
- **文件**: `KSPlugin_Buoyancy.cc`, `KSPlugin_Buoyancy.hh`, `ModelManager.cc`

---

### 功能 #15: 浮力体 3D 可视化 (Marker)
- **日期**: 2026-05-07
- **需求**: 在浮力体编辑器中可视化观察浮力体形状和位置
- **实现**:
  - 启用 `keship_world.sdf` 中 `MarkerManager` 插件
  - `BuoyancyEditorDialog` 新增 gz::transport 节点，发布到 `/marker` topic
  - 每个浮力体 2 个 marker：半透明填充（alpha=0.25/0.5 选中时）+ LINE_LIST 线框
  - Box 线框：12 条边；Cylinder 线框：24 段上下圆 + 竖线
  - 300ms QTimer 自动刷新模型位姿，marker 实时跟随
  - 参数修改/添加/删除时实时更新 marker
  - 对话框关闭/应用时 DELETE_ALL 清理
  - 选中浮力体：橙色高亮，未选中：浅蓝色
- **文件**: `BuoyancyEditorDialog.cc/.hh`, `MenuPluginKeship.cc`, `keship_world.sdf`

---

## Phase 0 — 复杂模型浮力方案讨论 (2026-05-06)

### 嵙海三号模型复杂度分析
- **模型文件**: `keship/models/keship_03/mesh/kehaisanhao.dae`
- **文件大小**: 12 MB
- **三角形数**: 78,337
- **来源**: Blender 4.4 导出
- **结论**: 直接用于 Catto 每步浮力计算不可行（78K 三角形 × 250 Hz 物理步 ≈ 19.5M 次/秒）

### 方案对比
| | 方案 A：直接网格 | 方案 B：代理几何体 |
|---|---|---|
| 三角形数 | 78,337 | ~230 |
| 性能 | 物理步极可能超时 | 实时流畅 |
| 配置 | 零配置 | 需 GUI 浮力体编辑器 |
| 水密性 | 依赖模型质量 | 内置 shapes 保证 |
| 精度 | 理论上最高 | 工程可接受 |

### 决策
选用**方案 B（代理几何体）**，对齐 VRX 做法。

### 浮力体编辑器设计讨论 (2026-05-06)

**关键设计决策**：

| 设计点 | 决策 | 理由 |
|--------|------|------|
| 生成器 | 统一为 `PrimitiveMeshGenerator`（Box + Cylinder 合并） | 单一入口，后续扩展球体/圆锥 |
| SDF 格式 | 多个 `<buoyancy>` 子块，每个指定 geometry + pose | 对齐 VRX，不引入额外配置文件 |
| 编辑器范围 | 每次只针对一个模型 | 浮力体是模型 SDF 的一部分 |
| 3D 预览 | 首版用 Gazebo marker 线框 | 开发量小；内嵌 3D 视口留 Phase 4 |
| 交互方式 | 参数面板 spinbox 驱动，线框实时更新 | 3D 拖拽手柄开发量巨大，Phase 4 |
| 生效方式 | 自动重建模型（保存 pose → 删除 → 新 SDF spawn → 恢复 pose） | ModelManager 已有 API 可直接拼装 |
| 一键包围盒 | 留按钮位置，Phase 4 实现 | 降低配置门槛，但非 MVP |

**开发顺序**：PrimitiveMeshGenerator → KSPlugin_Buoyancy 多子块支持 → ModelManager 浮力体 SDF → GUI 编辑器 → Marker 3D 线框 → 一键包围盒

### 实现完成总结 (2026-05-06)

**新建文件（4个）**：

| 文件 | 说明 |
|------|------|
| `src_cpp/utils/PrimitiveMeshGenerator.hh` | Box/Cylinder 统一生成器接口（PrimitiveType 枚举 + BoxParams/CylinderParams 结构体） |
| `src_cpp/utils/PrimitiveMeshGenerator.cc` | Box（8顶点+12三角形）+ Cylinder（复用原有逻辑）实现 |
| `src_cpp/plugins_gui/.../BuoyancyEditorDialog.hh` | Qt 对话框头文件（列表+参数面板+marker集成） |
| `src_cpp/plugins_gui/.../BuoyancyEditorDialog.cc` | 完整编辑器实现：浮力体列表管理、参数编辑、marker推送、应用重建 |

**修改文件（8个）**：

| 文件 | 变更 |
|------|------|
| `CylinderMeshGenerator.cc` | `GenerateCylinderMesh()` 委托 `PrimitiveMeshGenerator` |
| `KSPlugin_Buoyancy.hh` | 新增 `BuoyancyBody` 结构体、`bodies` 向量、`LoadMultiBuoyancy` 方法 |
| `KSPlugin_Buoyancy.cc` | Configure 分流（多体/单体）、PreUpdate 多体迭代累加、SDF 子块解析 |
| `ModelManager.hh` | 新增 `BuoyancyBodyConfig`/`BuoyancyConfig` 结构体、`RebuildModelWithBuoyancy`/`MakeBuoyancyPluginSDF` |
| `ModelManager.cc` | 实现 SDF 生成（多 buoyancy 子块格式）+ 模型重建（保存pose→删除→生成→spawn→恢复pose） |
| `MenuPluginKeship.hh` | 新增 `OnBtn_EditBuoyancy(int _index)` slot |
| `MenuPluginKeship.cc` | 编辑浮力体入口实现：打开编辑器→获取配置→调用重建 |
| `MenuPluginKeship.qml` | 模型列表每行增加"浮力体"按钮 |

**CMakeLists.txt**：新增 `PrimitiveMeshGenerator.cc`（keship_utils）、`BuoyancyEditorDialog.cc`（MenuPluginKeship）、`gz-msgs10` 依赖

---

### Bug #13: RebuildModelWithBuoyancy pose 保存错误导致重建后模型位置重置
- **发现**: 2026-05-07
- **现象**: 添加box浮力体重建后圆柱体竖直下沉
- **根因**:
  - `GetModelPoseByName` 使用 `/scene/info` 服务获取模型pose，该服务返回SDF静态pose `(0,0,0,0,0,0)` 而非物理引擎动态位姿
  - `GetModelPoseByIDFromGazebo` 使用 `/state` 服务获取真实位姿，但未被用于重建流程
  - spawn新模型时未在 `EntityFactory` 请求中设置pose，导致模型先spawn在原点再尝试移动
- **修复**:
  - 改用 `GetModelPoseByIDFromGazebo`（`/state` 服务）保存物理真实位姿
  - spawn新模型时通过 `EntityFactory.mutable_pose()` 直接在保存的位姿生成
  - 移除多余的 `SetModelPoseByIDInGazebo` 后置移动调用
  - `KSPlugin_Buoyancy::LoadMultiBuoyancy` 添加 per-body 诊断日志
  - `KSPlugin_Buoyancy::PreUpdate` 添加前10步 subVol 诊断日志
- **文件**: `ModelManager.cc`, `KSPlugin_Buoyancy.cc`

### Bug #14: BuoyancyEditorDialog 浮力体参数保存错误（信号级联 + 添加逻辑缺陷）
- **发现**: 2026-05-07
- **现象**: 运行时日志显示重建后圆柱体 box=(10,10,10)、Box浮力体 box=(1,1,1)。用户设定10×10×10后点"添加"，参数被写入圆柱体而非新box
- **根因**:
  - **根因A**: `updateParamPanel()` 设置12个spinbox时每个 `setValue()` 触发 `onParamChanged()`，信号级联导致隐藏spinbox默认值覆盖浮力体参数
  - **根因B**: `onAddBody()` 用结构体默认值创建新body，不读取当前spinbox值。用户先改spinbox再点添加时，修改值被 `onParamChanged` 写入当前选中body(圆柱体)，新body仍为默认值
- **修复**:
  - 新增 `isLoadingParams_` 标志位，`updateParamPanel()` 期间屏蔽 `onParamChanged`
  - `onAddBody()` 改为从当前spinbox取值初始化新浮力体
  - `onParamChanged()` 中移除不必要的 `updateList()` 调用
- **文件**: `BuoyancyEditorDialog.hh`, `BuoyancyEditorDialog.cc`

---

### 本次会话修改总结 (2026-05-07)

**修改文件（5个）**：

| 文件 | 变更 |
|------|------|
| `ModelManager.cc` | `RebuildModelWithBuoyancy` 改用 `/state` 服务保存物理位姿，spawn时直接设置位姿，移除多余 `SetModelPoseByIDInGazebo` |
| `KSPlugin_Buoyancy.cc` | `LoadMultiBuoyancy` + `PreUpdate` 添加诊断日志（per-body volume/subVol输出） |
| `BuoyancyEditorDialog.hh` | 新增 `isLoadingParams_` 成员变量 |
| `BuoyancyEditorDialog.cc` | `isLoadingParams_` 防信号级联；`onAddBody` 从spinbox取值初始化；`onParamChanged` 移除 `updateList()` |
| `keship_issues_log.md` | 记录 Bug #13, #14 |

**验证结果**: 圆柱体 l=20 正确保留，Box 10×10×10 正确加载，浮力体添加功能正常

### Bug #15: 浮力体编辑器 3D marker 不显示
- **发现**: 2026-05-07
- **现象**: 打开浮力体编辑器时3D视图中没有半透明填充和线框标记
- **根因**: 对话框自建 `gz::transport::Node` 未正确接入GUI进程transport域，MarkerManager收不到消息
- **修复**: 复用 `ModelManager` 中已验证可通信的 transport 节点
  - `ModelManager` 新增 `GetNode()` 公开接口
  - `BuoyancyEditorDialog` 构造函数接受 `gz::transport::Node &` 参数
- **文件**: `ModelManager.hh`, `BuoyancyEditorDialog.hh/.cc`, `MenuPluginKeship.cc`

### 功能 #16: 浮力体编辑器 GUI 交互改进
- **日期**: 2026-05-08
- **改动**:
  1. 新增"保存"按钮 — spinbox 修改不再自动写入 config_，需点保存才持久化
  2. "添加"按钮改用结构体默认值 — 不再从 spinbox 取值
  3. 打开对话框时自动选中第一个浮力体
- **影响**: `onParamChanged` 仅更新预览；新增 `onSaveBody` 方法
- **文件**: `BuoyancyEditorDialog.hh`, `BuoyancyEditorDialog.cc`

### 功能 #17: EntityFactory 浮力体 3D 覆盖层可视化 → 已回退
- **日期**: 2026-05-08 ~ 2026-05-09
- **方案**: 每个浮力体 spawn 独立 SDF 视觉模型（无碰撞、微质量、半透明），refreshTimer_ 跟随主模型
- **失败原因**: ①同名冲突（创建/删除竞态）；②不透明模型表面遮挡半透明覆盖层；③随水面晃动
- **结论**: 代码已全部回退，删除 `initOverlays/updateOverlayGeometries/destroyOverlays/makeOverlaySDF/setPaused/refreshModelPose` 等方法及所有 marker 死代码
- **文件**: `BuoyancyEditorDialog.hh`, `BuoyancyEditorDialog.cc`, `MenuPluginKeship.cc`

### 3D可视化探索记录（失败路径，勿重复尝试）

| 方案 | 失败原因 |
|------|---------|
| transport `/marker` topic | ROS2 gz-transport13 与系统 gz-gui8 版本不兼容，消息不通 |
| 直接操控 Ogre2 场景 | CreateVisual/DestroyVisual 在 Qt 主线程执行，与 Ogre2 渲染线程冲突导致 segfault |
| RenderUtil::MarkerManager | gz-sim8 的 MarkerManager 是服务端组件，GUI 插件不可达 |
| EntityFactory 覆盖层模型 | 同名冲突、遮挡、晃动，无法可靠渲染 |

### 功能 #18: QOpenGLWidget 浮力体 3D 预览视图 ✅
- **日期**: 2026-05-09
- **方案**: QOpenGLWidget 纯 OpenGL，极简着色器（无光照纯色），轨道相机
- **关键修复**:
  - Qt3D→QOpenGLWidget（解决 segfault）
  - `vao.create()` 显式创建（解决所有几何体共享默认 VAO）
  - `glDepthMask(GL_FALSE)` 半透明物体不互挡
  - `pendingType_` 替代 `config_.type`（类型预览即时响应）
  - `isValid()` 保护 GL 上下文
- **已知问题**: 选中切换时颜色变化偶有延迟
- **文件**: `Buoyancy3DView.hh/.cc`（新建）, `BuoyancyEditorDialog.hh/.cc`, `CMakeLists.txt`

---

### 功能 #19: 文件模型支持浮力体编辑器 (Phase 0.5) ✅
- **日期**: 2026-05-09 ~ 2026-05-10
- **新增功能**:
  - 勾选浮力时不先 spawn 模型，直接打开编辑器配置代理几何体
  - `SpawnModelWithBuoyancy` 拼接完整 SDF 后首次生成模型
  - `MakeSDFStringFromValidFileType` 支持 9 分量惯性张量
  - `GetCachedModelGeometry` 支持 mesh 路径提取
  - 3D 视图渲染真实 mesh（`LoadMeshFromFile` + 顶点居中）
- **关键修复**:
  - SDF 截断：`SpawnModelWithBuoyancy`/`RebuildModelWithBuoyancy` 从 `</link>` 截断后拼接插件
  - 惯性张量索引修复（`it[4]`=iyy, `it[8]`=izz）
  - VAO 显式 `create()`（修复所有浮力体共用默认 VAO）
  - 深度写入关闭（修复半透明物体互相遮挡）
- **文件**: `ModelManager.hh/.cc`, `MenuPluginKeship.cc`, `BuoyancyEditorDialog.hh/.cc`, `Buoyancy3DView.hh/.cc`, `CMakeLists.txt`

### Phase 0 完结总结 (2026-05-10)

**已完成功能**:
- 圆柱体浮力测试工具
- 多浮力体管理（添加/删除/保存/列表）
- 浮力体参数编辑（类型/尺寸/位姿/实时预览）
- 应用并重建模型
- 对话框内嵌 QOpenGLWidget 3D 预览（模型+浮力体）
- 文件模型加载 + 代理浮力体配置
- Mesh 模型渲染 + 顶点自动居中
- 对话框内嵌 3D 预览（QOpenGLWidget）

**已知遗留问题**:
- 选中切换时颜色偶有延迟
- 浮力体无 wireframe 线框（glPolygonMode 不可用）
- 文件模型碰撞体为 mesh，DART 不创建碰撞几何体（浮力不受影响）

---

### Phase 1.5 (2026-05-11) — .ksm 模型保存/加载 ✅

**新建/修改文件**：

| 文件 | 变更 |
|------|------|
| `ModelManager.hh` | +`SaveModelToKSM` / +`ParseKSMFile` |
| `ModelManager.cc` | +110 行：三段序列化（ofstream）+ 反序列化（状态机 + boost::split） |
| `ModelSettingsDialog.hh` | +`setKsmMode()` / +`ksmInfoLabel_` |
| `ModelSettingsDialog.cc` | KSM 模式隐藏浮力/惯性勾选框，显示"浮力体/推进器配置已在文件中" |
| `MenuPluginKeship.hh` | +`OnBtn_SaveModel(int)` slot |
| `MenuPluginKeship.cc` | `OnBtn_SaveModel`（QFileDialog → SaveModelToKSM）；`OnBtn_LoadModel` 扩展 .ksm 分支（ParseKSMFile → setKsmMode → cache 三段 → spawn） |
| `MenuPluginKeship.qml` | 模型列表 +"保存"按钮（40px），文本宽度 -210 |

**文件格式**：纯文本三段式 `===BASE_SDF===` / `===BUOYANCY===` / `===THRUST===`，body 行逗号分隔，无外部库依赖。
**设计要点**：加载后三段缓存填充，浮力体/推进器编辑按钮直接可用。

---

### Phase 3 (2026-05-12) — ROS 2 动态桥接 ✅

**架构**：
```
launch → Gazebo + ros_gz_bridge(/keship/model_ready) + KeshipWorldBridge
GUI 创建模型 → C++ NotifyModelSpawned → gz-transport → ros_gz_bridge → ROS 2
KeshipWorldBridge 收到 → 子进程 parameter_bridge（pose + 推进器 topic）
```

**新建文件**：`keship_world_bridge.py`（动态 bridge 管理）、`scripts/keship_world_bridge`

**修改文件**：`ModelManager.hh/.cc`（NotifyModelSpawned/Deleted）、`launch/keship.launch.py`（固定 model_ready bridge + WorldBridge 节点）、`package.xml`（+rclpy/std_msgs/ros_gz_bridge）、`CMakeLists.txt`（install scripts）

**关键设计**：
- 每个模型一个 `parameter_bridge` 子进程（动态启动/停止）
- `NotifyModelSpawned` 发布 JSON 到 `/keship/model_ready`（含模型名、推进器列表）
- `NotifyModelDeleted` 通知清理
- publisher 缓存为 static（避免每次创建 Advertise）

**审查修复**：移除 dynamic_pose 桥接（类型不匹配）、publisher 缓存、Notify 补缺 rebuild 方法、launch 清理死导入

---

### Phase 2.2 (2026-05-12) — 惯性编辑 + 属性对话框重构 ✅

**ModelSettingsDialog 三模式**：`Full`（新模型加载）/ `OnlyPose`（已 spawn 属性）/ `Ksm`（KSM 加载）

**惯性编辑流程**：属性按钮 → 从缓存 SDF 回填惯性 → 修改 → 惯性变化时 UpdateCachedSDFInertia + RebuildModelWithAllPlugins

**新增方法**：`GetCachedModelInertia`、`UpdateCachedSDFInertia`、`RebuildModelWithAllPlugins`、`ModelSettingsDialog::setInertiaValues`

**移除**：`setKsmMode()`、OnBtn_LoadModel 中注释死的 SetModelInertia 代码

---

### 第二轮修复 (2026-05-12) — spawn 流程 + KSM 格式

- **延迟 spawn**：圆柱体测试改为先缓存 SDF → 弹 hydro 对话框 → accept → spawn
- **spawn pose 参数**：`SpawnModelWithBuoyancy/Thrust` 新增可选 `_pos` 参数
- **KSM Cyl 字段**：保存格式补齐 12 fields（`Cyl,r,l,0,0,`）
- **空 hydro 不缓存**：KSM 加载时只在系数非零时 `CacheHydroConfig`
- **lambda pos 安全捕获**：`std::make_shared<std::array<double,6>>`

---

### Code Review 修复 (2026-05-12)

**Critical**:
- `FossenModel::computeHydroForces` C_A(ν)·ν 全 6 行索引修正（线速度/角速度互换）
- `FossenModel::buildAddedMassMatrix` M_A(3,1) 用 K_vdot (c[8]) 替代 Y_pdot (c[6])
- `BuoyancyEditorDialog/ThrusterEditorDialog::onApplyAndRebuild` accept() 前先 onSaveBody()
- `ParseKSMFile` 浮力体 pose 解析索引修正 (parts[6..11], 条件 >=12)

**Important**:
- KSM 只有浮力无推进器时调用 SpawnModelWithBuoyancy（而非 SpawnModelWithThrust）
- HydrodynamicsEditorDialog 预填默认值确认无问题（构造时已预填）

**Minor**:
- 删除 HydrodynamicsEditorDialog::massLabel_ 未初始化指针 + QLabel include

---

### Phase 2 (2026-05-11) — FossenModel + HydrodynamicsSystem ✅

**新建文件（4个）**：

| 文件 | 说明 |
|------|------|
| `src_cpp/utils/FossenModel.hh` | FossenModel 类：M_A/D 矩阵存储、computeHydroForces |
| `src_cpp/utils/FossenModel.cc` | 矩阵组装（buildAddedMassMatrix/buildDampingMatrix）+ 水动力计算（τ = -M_A·ν̇ - C_A·ν - D·ν） |
| `src_cpp/plugins_physics/HydrodynamicsSystem.hh` | 插件声明（Configure + PreUpdate + 有限差分状态） |
| `src_cpp/plugins_physics/HydrodynamicsSystem.cc` | SDF 解析 + 刚体参数读取 + 速度世界→BODY 转换 + 有限差分 ν̇ + 力世界→BODY 逆转换 + ECM 累加 |

**修改文件（1个）**：

| 文件 | 变更 |
|------|------|
| `CMakeLists.txt` | keship_utils 新增 FossenModel.cc + Eigen3::Eigen 链接；KS_plugins_physics 新增 HydrodynamicsSystem |

**测试方式**：
1. 在模型 SDF 中手动添加 HydrodynamicsSystem 插件（含 `<added_mass>` / `<linear_damping>`）
2. 将浮力插件的 `linear_drag_coef` / `angular_drag_coef` 设为 0
3. Gazebo 中观察水动力学行为差异

---

### Phase 2 Bug Fix (2026-05-11) — 浮力插件自动零阻尼

**问题**：配置 Fossen 后，浮力插件默认 drag=0.5 仍生效，与 Fossen D 矩阵形成双重阻尼，10000N 推力推不动 275 吨圆柱体。

**修复**：`MakeBuoyancyPluginSDF(_config, _hydroActive)` 新增 bool 参数。当模型有 Hydro 配置时，自动输出 `linear_drag_coef=0, angular_drag_coef=0`。

**修改文件**：`ModelManager.hh`（签名）+ `ModelManager.cc`（实现 + 6 处调用点传入 hasHydro）

---

### Phase 1 Round 3 (2026-05-11) — 端到端测试与修复

**测试结果**：GUI 创建推进器 → 重建 → topic 发推力 → 模型运动 ✅

**修复**：
- gz-transport13 Subscribe 不支持 lambda 隐式转换 → 显式 `std::function<void(...)>` 包装
- `std::atomic<double>` 不可移动 → `std::vector<std::unique_ptr<ThrusterBody>>`
- `double % int` 非法 → `((segs-i) % segs) / (double)segs`
- 推力方向反转：箭头 = 螺旋桨朝向，力朝反方向推船
- 控制接口改为直接力 (N)，clamp 到 [-maxForce, maxForce]
- 允许零推进器重建（去掉推力插件）

---

### Phase 1 Round 2 (2026-05-11) — 推进器编辑器 GUI + 3D 箭头预览

**新建文件（2个）**：

| 文件 | 说明 |
|------|------|
| `src_cpp/plugins_gui/.../ThrusterEditorDialog.hh` | 推进器编辑器对话框（列表+参数+3D预览） |
| `src_cpp/plugins_gui/.../ThrusterEditorDialog.cc` | 完整实现：列表管理、参数编辑（pose+maxForce）、3D箭头预览、应用重建 |

**修改文件（5个）**：

| 文件 | 变更 |
|------|------|
| `Buoyancy3DView.hh` | 新增 `Thruster3DInfo` 结构体；新增 `syncThrusters`/`updateThruster`/`setSelectedThrusterIdx` 方法；新增 `buildArrowGeo`/`thrusterMatrix`；新增 `arrowGeo_`/`thrusters_`/`selectedThrustIdx_` 成员 |
| `Buoyancy3DView.cc` | 新增 `buildArrowGeo`（杆+锥头，8段octagonal）；`paintGL` 追加箭头渲染（半透明+不写深度）；`thrusterMatrix` 位姿矩阵；`syncThrusters`/`updateThruster`/`setSelectedThrusterIdx`；析构清理 arrowGeo_ |
| `MenuPluginKeship.hh` | 新增 `OnBtn_EditThrust(int)` slot |
| `MenuPluginKeship.cc` | 新增 `OnBtn_EditThrust` 实现：打开 ThrusterEditorDialog → 还原缓存配置 → accepted 时调用 RebuildModelWithThrust |
| `MenuPluginKeship.qml` | 模型列表每行新增"推进器"按钮（45px）；文本宽度调整 -125→-170 |
| `CMakeLists.txt` | `MenuPluginKeship` 源文件新增 `ThrusterEditorDialog.cc` |

**GUI 布局**：左侧（推进器列表 + 参数面板[类型Fixed/pose/maxForce/保存]）+ 右侧（QOpenGLWidget 3D箭头预览）
**3D 箭头**：共享几何体（杆部+锥形头部），每个推进器独立位姿矩阵渲染，选中橙色/未选浅蓝

---

### Phase 1 Round 1 (2026-05-11) — 推进器底层基础设施

**新建文件（2个）**：

| 文件 | 说明 |
|------|------|
| `src_cpp/plugins_physics/KSPlugin_Thrust.hh` | ThrusterBody 结构体 + KSPlugin_Thrust 插件类（Configure/PreUpdate/ApplyForces） |
| `src_cpp/plugins_physics/KSPlugin_Thrust.cc` | SDF `<thrust>` 子块解析 + transport 订阅 `/keship/thrust/<model>/<name>` + Wrench 累加 |

**修改文件（3个）**：

| 文件 | 变更 |
|------|------|
| `ModelManager.hh` | 新增 `ThrusterType` 枚举、`ThrusterBodyConfig`/`ThrusterConfig` 结构体；新增 `MakeThrustPluginSDF`/`RebuildModelWithThrust`/`SpawnModelWithThrust`/`GetCachedThrusterConfig`/`CacheThrusterConfig` 方法；新增 `thrusterConfigCache_` |
| `ModelManager.cc` | 实现 `MakeThrustPluginSDF`（生成 `<thrust>` SDF 子块）、`RebuildModelWithThrust`（保留 buoyancy + 新增 thrust）、`SpawnModelWithThrust`、`GetCachedThrusterConfig`；`RebuildModelWithBuoyancy`/`SpawnModelWithBuoyancy` 追加推进器保留逻辑 |
| `CMakeLists.txt` | `KS_plugins_physics` 列表新增 `KSPlugin_Thrust` |

**设计要点**：
- Wrench 累加协议：`ApplyForces` 读取-累加-写回，与 BuoyancySystem 可共存
- 线程安全：`ThrusterBody::currentLevel` 为 `std::atomic<double>`，transport 回调原子写入，PreUpdate 原子读取
- 订阅 topic：`/keship/thrust/<modelName>/<thrusterName>`，消息类型 `gz::msgs::Double`
- 推力方向 = 推进器 local +X 在世界系的方向，大小 = level × maxForce，level ∈ [-1, 1]

**验证方式（无 GUI）**：
```
# 1. 启动 Gazebo，加载圆柱体
# 2. 手动调用 RebuildModelWithThrust 或直接 spawn SDF 含推力插件
# 3. 发送推力指令：
gz topic -t /keship/thrust/cylinder_test/主推进器 -m gz.msgs.Double -p "data: 0.5"
```

### (试验失败) 3D覆盖层渲染方案（详细）
- **日期**: 2026-05-08
- **尝试**: 直接调用 Ogre2 渲染API创建半透明/线框Visual
- **结果**: `updateAllTransforms` 线程 segfault
- **根因**: 在 Qt 主线程操作 Ogre2 场景与渲染线程冲突

---

### Bug: KSM 加载全零 hydro 时浮力阻尼未归零
- **发现**: 2026-05-15
- **现象**: 加载 KSM 文件（hydro 系数全为 0），模型表现出阻尼；打开流体动力对话框看到全零，点击"应用并加载模型"后模型不再有阻尼
- **根因**: `OnBtn_LoadModel` 和 `OnBtn_LoadWorld` 加载 KSM 后，用"系数是否非零"判断是否缓存 hydro 配置。全零时不缓存 → `MakeBuoyancyPluginSDF(_config, hasHydro=false)` → 浮力插件使用默认阻尼 (linear=0.5, angular=0.05)。而对话框"应用"时无条件缓存 → rebuild 时 `hasHydro=true` → drag=0
- **修复**: 将 `hasHydro` 检测从"系数非零"改为"HYDRO 段有内容"。在调 ParseKSMFile 前将 `hydroCfg.linkName=""` 清空，解析后 `!hydroCfg.linkName.empty()` 即表示 HYDRO 段有 `link_name=` 行（SaveModelToKSM 仅在 hydro 缓存存在时才写入 link_name + 系数行）
- **文件**: `MenuPluginKeship.cc`（OnBtn_LoadModel + OnBtn_LoadWorld 两处）

---

### 传感器数据可视化 (2026-05-15) — Phase 4.3 ✅

**功能**：在 keship 面板集成相机画面和激光雷达点云查看器。

**新建文件（4个）**：
| 文件 | 说明 |
|------|------|
| `CameraViewerDialog.hh` | 相机弹窗，QLabel + ROS 2 subscription → 实时画面 |
| `CameraViewerDialog.cc` | Subscribe `{模型}/camera` → rgb8 → QImage → QTimer 刷新 |
| `LidarViewerDialog.hh` | 激光雷达弹窗，含 LidarViewWidget（QOpenGLWidget 2D 点云） |
| `LidarViewerDialog.cc` | Subscribe `{模型}/lidar` → LaserScan → 极坐标转笛卡尔 → GL_POINTS |

**修改文件（4个）**：
| 文件 | 变更 |
|------|------|
| `CMakeLists.txt` | +`find_package(sensor_msgs)`, MenuPluginKeship 链接 `rclcpp` + `sensor_msgs`, 新增两个 .cc 源文件 |
| `MenuPluginKeship.hh` | +`rosNode_`, `spinThread_`, `sensorList_`, `SensorListChanged` signal, `RefreshSensorList()` / `OnSensorSelected()` slots |
| `MenuPluginKeship.cc` | 构造函数 rclcpp::init + rosNode + spin 线程; RefreshSensorList 查询 SensorConfig 缓存拼接列表; OnSensorSelected 解析字符串弹出对应 viewer |
| `MenuPluginKeship.qml` | 模型列表下方新增 GroupBox + ComboBox，绑定 sensorList |

**数据流**：Gazebo sensor → ros_gz_bridge → ROS 2 topic → rclcpp subscription → mutex buffer → QTimer → Qt UI

**设计要点**：
- rosNode_ 由 MenuPluginKeship 统一管理，各 viewer dialog 各自持有 subscription（独立生命周期）
- 传感器列表在模型增删时自动刷新（ModelNamesChanged → RefreshSensorList）
- 下拉格式 "模型名 / 传感器类型"，无传感器模型不出现
- 多 viewer 可同时打开，互不影响

**修复：ComboBox 误弹窗**：
- **现象**：SensorEditorDialog 点击"应用并重建模型"后，sensorList_ 从空变为非空，ComboBox 自动选中第一项触发 onCurrentTextChanged，弹出相机窗口
- **修复**：QML 中 `onCurrentTextChanged` → `onActivated`，后者仅在用户手动点击下拉菜单时触发，模型数据变化不会误触发

---

### 激光雷达点云渲染修复 (2026-05-15)

**问题 1：点云不可见**：
- **现象**：LidarViewWidget 窗口打开但看不到绿色点云
- **尝试 1（自适应缩放）**：根据有效数据动态决定显示范围 — 用户指出尺度不稳
- **尝试 2（OpenGL + 滚轮缩放）**：glBegin/glEnd 在 Gazebo GUI 进程中无效（Core Profile 不兼容）
- **最终方案（QPainter + 滚轮缩放 + 诊断确认）**：QWidget + QPainter 替代 QOpenGLWidget；默认标称射程 50m，滚轮缩放 2m~100m；首帧 setScanData 初始化 dist_=50；添加诊断日志验证全链路通畅（ROS 回调→timer→setScanData→paintEvent 360 点画 67 有效点）后移除诊断代码

**问题 2：线程安全**：
- **根因**：QTimer 回调中用 `ranges_` 等成员传参 `setScanData`，但锁 scope 仅覆盖 `newData_` 的读取，ROS 回调可能同时写入 `ranges_`
- **修复**：将所有数据拷贝放入同一锁 scope，拷贝完成后再传参

**问题 3：inf/NaN 过滤**：
- **根因**：雷达无回波方向值为 `inf`，原范围检查 `rg < rangeMin_ \|\| rg > rangeMax_` 虽能排除 +inf，但对 NaN 无效
- **修复**：显式添加 `std::isnan(rg) \|\| std::isinf(rg)` 前置过滤

---

### 多实例传感器 + 位姿编辑 (2026-05-16) ✅

**需求**：每类传感器可配 N 个实例，各自独立 name + pose。5 种类型均支持。顶层设计可扩展（新增传感器类型只需加枚举值 + SDF 工厂分支 + bridge 映射项）。

**数据结构**：`SensorType` 枚举 + `SensorInstance{type, name, pose[6]}` + `SensorConfig{linkName, vector<SensorInstance>}`。`has(type)` / `count(type)` 辅助方法。

**改动文件（9 个）**：

| 文件 | 变更 |
|------|------|
| `ModelManager.hh` | 新增 SensorType/Instance，重写 SensorConfig |
| `ModelManager.cc` | MakeSensorSDF 遍历 switch、NotifyModelSpawned JSON、KSM 保存/解析 |
| `SensorEditorDialog.hh/.cc` | 完全重写：列表 + 参数面板 + 类型选择 |
| `MenuPluginKeship.cc` | RefreshSensorList 遍历 instances、OnSensorSelected 3 段解析 |
| `CameraViewerDialog.hh/.cc` | 构造函数 +instName，topic=`{model}/{instName}` |
| `LidarViewerDialog.hh/.cc` | 同上 |
| `keship_world_bridge` | bool dict → 实例列表迭代，SENSOR_MAP 映射表 |

**KSM 新格式**：`instance=Camera,front_cam,0,0,1,0,0,0`

---

### 传感器编辑器 UI 修复 (2026-05-16)

**问题 1：保存后参数面板消失**：
- **根因**：`onSaveInstance()` 调用 `updateList()` → `clear()` 触发 `currentRowChanged(-1)` → `selectedIdx_` 重置为 -1 → `updateParamPanel()` 隐藏面板
- **修复**：保存前记录 `savedIdx`，`updateList()` 后 `listWidget_->setCurrentRow(savedIdx)` 重新选中

**问题 2：viewer 窗口被 Gazebo 3D 视图遮挡**：
- **修复**：`CameraViewerDialog` / `LidarViewerDialog` 构造函数加 `setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint)`

---

### Phase 13 方案修订 + Phase 10 实施 (2026-06-04~05) ✅

**方案修订**：
- Phase 9~15 重新编号：Phase 13 拆分为 Phase 10（任务框架）+ Phase 11~13（SC 实验），Phase 12→9（结构与制图）
- TOML 格式简化：去掉 `[[task.scoring]]`，tracking 统一用 `[[task.waypoints]]`
- 评分体系简化：7 指标 → 每种任务类型固定 3 指标

**实施 (14 文件, 5 新建 + 9 修改)**：
- 新建 `extern/toml++/toml.hpp` — toml++ v3.4.0 header-only
- 新建 `TaskTomlParser.hh/.cc` — 基于 toml++ 的标准 TOML load/save
- 新建 `TaskScoring.hh/.cc` — 3×3 评分引擎（waypoint/tracking/dp 各 3 指标）
- 重写 `MapWidget` — 三色航路点、DP 目标圆、比例尺、自动平移、航向编辑、只读模式
- 重写 `TaskSetDialog` — QStackedWidget 面板切换、onSave→bool、closeEvent 保护、权重 UI
- 重写 `TaskMonitorDialog` — TaskMonitorState 共享状态、steady_clock、航路点/偏差/DP 检查、自动评分
- 修改 `MenuPluginKeship` — 单例模式 + destroyed→nullptr、推进器信息传递
- 修改 `CMakeLists.txt` — 新增源文件 + toml++ include 路径

**测试修复 (6 项)**：
- DP 面板不显示 → QStackedWidget 替代 setVisible()
- 取消保存后窗口关闭 → onSave() 返回 bool，closeEvent 仅在成功时 accept
- 静止船舶得 64 分 → 最低任务门槛（< 3% 时限 → 5/100，waypoint 0 航路点 → 0）
- 监控地图可编辑航路点 → MapWidget::setEditable(false) 只读模式
- DeleteAllModelsInGazebo segfault → 先收集模型名到 vector，再逐个删除（避免迭代器失效）
- 任务状态发布 → ROS 2 topic `/keship/task/{model}`（std_msgs/String JSON），供学生控制程序订阅航路点/目标/进度

**后续修复与增强 (2026-06-05~06, 11 commits)**：
- tracking 轨迹编辑器：5 种轨迹形状（直线/正弦/圆/椭圆/自定义拖拽画线）+ 参考点放置
- tracking 体验优化：路径走廊渲染、去掉能耗指标、简化 2 指标评分
- 代码审查修复 6 bugs：clip rect 裁剪比例尺、wpStatus_ 清空、setThrusterInfo 覆盖、freeDraw 非左键、圆 cw/ccw 公式
- TaskSetDialog 构造 segfault：mapWidget_ 未初始化 → setupTrackingPanel 前移
- 布局优化：紧凑间距 + 地图大比例 + QStackedWidget→setVisible 消除空白
- 界面修复：spinbox prefix 替代 label、形状参数紧凑化、坐标轴与裁剪区分离
- 切换守卫：lastTaskType_ 防重复 radio 点击数据丢失

**Git 提交记录 (15 commits)**：
```
de7cd44 Fix: 重复点击同一 radio 导致数据丢失
ee1671b Fix: 切换任务类型时 MapWidget 状态未完全重置
204b1c1 Fix: 分离坐标轴与裁剪区
69a3882 Fix: 参考点放满后点击被吃掉
ee90ad1 Fix: 三个界面问题 (prefix/紧凑/clip)
9f749a9 Fix: shapeStack_ QStackedWidget→setVisible
aae091a Fix: paramStack_ QStackedWidget→setVisible
fff7628 Refactor: 布局优化 紧凑间距+地图大比例
b54a7f1 Fix: 构造 segfault mapWidget_ 未初始化
a9fc226 Fix: 代码审查修复 (6 bugs)
abde19c Feat: tracking 轨迹编辑器
206cde7 Refactor: tracking 体验优化
a9009bc Refactor: 任务状态发布改为 ROS 2
9874b64 Feat: 任务状态发布
3ea8baa Fix: DeleteAllModelsInGazebo segfault
```

---

## 代码质量审计 (2026-05-24)

> 全代码库扫描（GUI 插件 22 文件 + 物理插件/工具类），共 62 项。

### 严重 Bug（优先修复）

| # | 文件:行 | 问题 |
|---|---------|------|
| B1 | `Buoyancy.cc:800` | case -1 穿透到 case 1（缺 break），特定三角形水面切割时浸没体积计算错误 |
| B2 | `Buoyancy3DView.cc:462` | 箭头几何体前端面索引引用后端面顶点环，箭头渲染扭曲 |
| B3 | `KSPlugin_Buoyancy.cc:117` | `static int diagCounter` 跨实例共享，多船时其他船不输出诊断 |
| B4 | `MenuPluginKeship.cc:260-272` | SDF 字符串用固定偏移 `find`/`replace` 改模型名，遇引号变化即失效 |

### 高风险

| # | 文件 | 问题 |
|---|------|------|
| H1 | `Wavefield.cc:302` | g=9.8 vs Buoyancy 中 9.81 不一致 |
| H2 | `KSPlugin_Thrust.cc:80-91` | transport 回调裸指针捕获 unique_ptr，移动后悬空 |
| H3 | `Buoyancy.cc:163-170` | O(N²) 顶点查找，已有 PointHash 机制未使用 |
| H4 | `Buoyancy.cc:508-762` | 并行版本未调用（死代码 ~250 行） |
| H5 | `ModelManager.cc:893-935` | `GenerateTetrahedronsFromMesh` 始终产生 0 个四面体 |

### 主要重构目标

| # | 范围 | 说明 |
|---|------|------|
| R1 | 编辑器三件套 | BuoyancyEditorDialog / ThrusterEditorDialog / SensorEditorDialog ~60% 结构重复，抽取基类 |
| R2 | ModelManager Rebuild ×7 | 存位姿→删→等→取 SDF→拼→spawn→等 ID，~500 行重复，抽 `RebuildModel(name, fullSDF)` |
| R3 | Wrench 累加 ×3 | KSPlugin_Buoyancy/Thrust/HydrodynamicsSystem ApplyForces 21 行完全相同 |
| R4 | Buoyancy3DView 坐标轴 | 80 行 X/Y/Z 各复制一遍，改为循环 |
| R5 | Buoyancy3DView 矩阵 | bodyMatrix/thrusterMatrix/sensorMatrix 三个相同函数，合并为模板 |
| R6 | MenuPluginKeship 去重 | 忙等循环、索引校验、暂停/恢复各重复 2-5 次 |
| R7 | Buoyancy3DView paintGL | ~170 行，模型/浮力体/推进器/传感器/坐标轴渲染混在一起，拆分为 Layer |

### 次要清理

| # | 范围 | 说明 |
|---|------|------|
| C1 | 全局 | `std::cout` → `gzmsg`/`qDebug` |
| C2 | `MenuPluginKeship.cc:226` | `QRegExp` 废弃 → `QRegularExpression` |
| C3 | 全局 | `M_PI` → 自定义 `constexpr kPi` |
| C4 | `ModelSettingsDialog.cc:36-37` | Infinity → `std::numeric_limits<double>::lowest()/max()` |
| C5 | `MenuPluginKeship.hh:4` | include guard 名 `GZ_GUI_HELLOPLUGIN_HH_` → 更正 |
| C6 | `Buoyancy.hh:177` | 拼写错误 `fluideLevel_` + 死代码 |
| C7 | `ModelSettingsDialog.cc:257` | QMessageBox parent nullptr → this

---

## 相关专题文档

| 文档 | 内容 |
|------|------|
| [notes/keship_physics_design.md](notes/keship_physics_design.md) | 6-DOF Fossen 水动力学模型 + Wavefield–Buoyancy 集成方案 |
| [notes/wavefield_buoyancy_integration.md](notes/wavefield_buoyancy_integration.md) | Wavefield–Buoyancy Phase 1 实现细节（代码路径、线程安全、验证方法） |
| [notes/ui_refinement_session.md](notes/ui_refinement_session.md) | 2026-05-26~27 UI 交互优化记录（布局重构、KSM 置灰、信号泄漏修复） |
| [notes/virtual_experiment_framework.md](notes/virtual_experiment_framework.md) | 虚拟仿真实验框架：船舶原理/智能控制/结构与制图三门课程 × 三个层级，共 28 个实验及依赖链 |
| [notes/sp01_experiment_report_design.md](notes/sp01_experiment_report_design.md) | SP-01 漂浮圆柱体初稳性实验设计方案（修订）：GM 推导、临界曲线、三子实验、对话框设计 |
| [notes/sp01_experiment_guide.md](notes/sp01_experiment_guide.md) | SP-01 实验指导书：原理推导、分步操作、数据记录表、GM 快速计算表、思考题、材料密度参考 |
| [notes/sp02_experiment_guide.md](notes/sp02_experiment_guide.md) | SP-02 实验指导书：矩形驳船倾斜实验；$w·Δy$ vs $\tan\phi$ 线性拟合求 $GM_{exp}$；$I_x=LB^3/12$；船宽 $B^3$ 效应验证 |
| [notes/sp03_experiment_guide.md](notes/sp03_experiment_guide.md) | SP-03 实验指导书：数学船型稳性对比；Wigley/Lewis U/Lewis V + 矩形驳船对照；$C_{wp}$/$C_i$ 系数对 $GM$ 的影响 |
| [notes/water_area_ipc_refactor.md](notes/water_area_ipc_refactor.md) | **水域 IPC 架构重构** (2026-07-14~15)：TOML → JSON，修复 PreUpdate 轮询 bug，gz-transport 三次失败尝试记录 |
| [notes/fossen_physics_refactor.md](notes/fossen_physics_refactor.md) | **Fossen 物理模型重构** (2026-07-15)：惯性校验、阻尼项通用化、M_A 6×6 对称矩阵、D 矩阵彩色预览 |
| [notes/gz_transport_shutdown_fix.md](notes/gz_transport_shutdown_fix.md) | **gz‑transport 回调安全关闭** (2026-07-16)：`malloc_consolidate` 根因分析、13 个 Subscribe 点全扫描、两阶段关闭方案与实施 |
| [notes/ma_dart_removal_fix.md](notes/ma_dart_removal_fix.md) | **M_A 从 DART 移除方案** (2026-07-28)：Issue 2 根治 — `<fluid_added_mass>` 导致 CoG 偏移重力矩平衡破坏的根因分析、方案迭代、Gazebo 原生加速度 API 使用方法 |

---

### SP-03 数学船型稳性对比 实现 (2026-05-28) ✅

**底层：MathHullGenerator**（`src_cpp/utils/`，纯 C++，不依赖 Gazebo）：
- 3 种船型：Wigley（抛物线）、Lewis U（椭圆）、Lewis V（直线），通过 `sectionShape()` 区分
- `generateHalfHull`：结构化网格 (Nx×Nz) 离散半船体，镜像对称，输出 `MeshDataSimple`
- 梯形积分计算全部静水力：$A_w$, $C_{wp}$, $I_x$, $C_i$, $\nabla$, $KB$, $BM$
- `writeDAE`：导出标准 COLLADA 文件，含顶点索引查找
- 用法：`MathHullGenerator hull(MathShipType::Wigley, {6.0, 1.5, 1.0, 40, 20});` 构造即完成

**浮力插件扩展**（`KSPlugin_Buoyancy.cc`）：
- `LoadMultiBuoyancy` 新增 `<geometry><mesh><uri>` 支持
- mesh 经 `gz::common::findFile` 解析 → `MeshUtils::LoadMeshFromFile` → `Buoyancy::loadMeshData`
- 仅数学船型 SDF 走此路径，文件模型继续用 box/cylinder 代理

**SP-03 对话框**（`HullStabilityDialog`）：
- 船型选择 QComboBox + L/B/D spinbox → 实时显示静水力参数
- SDF 生成：MathHullGenerator 双分辨率（visual Nx=40/Nz=20，buoyancy Nx=20/Nz=10）→ DAE → mesh SDF
- 复用 SP-02 倾斜实验：压载滑块 + set_pose + 横倾监测 + 散点图
- 关键修正：质量用 `hydroProps().vol`（非包围盒）；GM_th 用 `spawnedHydro_` 快照

### SP-03 数学船型系统重构 — Phase A 完成 (2026-06-01) ✅

**最终方案**：新 ShipHullGenerator 直接在 Gazebo Z_UP 坐标生成网格 + 完整 OBJ（含顶点法线）+ 对齐工作代码的 SDF 格式。

**解决的全部 Bug**：

| Bug | 根因 | 修复 |
|-----|------|------|
| 三角绕序 + 坐标变换 | NED→Z_UP 变换链引入多轮错误 | 直接在 Gazebo Z_UP 生成，无变换 |
| DAE 顶点丢失 | Assimp COLLADA 解析异常 | 统一用 OBJ 格式 |
| DART mesh 碰撞 segfault | OBJ 无法线，ODE 碰撞检测器崩溃 | `generateMesh` Step 4：面法线面积加权平均 → `vn` 输出 |
| Box 碰撞 LCP NaN | Box + collision pose 组合在 DART 中产生病态约束矩阵 | 改用 `<mesh>` 碰撞（OBJ 有法线后 ODE 正确处理） |
| OBJ 索引查找崩溃 | 三角形存顶点副本，指针匹配失败 | 坐标字符串哈希映射 |
| 验证公式符号错误 | cross product y 分量用反 | `e1.Z()*e2.X() - e1.X()*e2.Z()` |

**与工作代码对齐的关键项**：
- SDF `<pose>0 0 0` 占位符 + EntityFactory.mutable_pose() 设实际 spawn 位置
- mesh URI 加 `file://` 前缀
- 惯性矩阵格式：`<ixx>v</ixx><ixy>0</ixy><ixz>0</ixz>` 同行
- 惯性体 `<pose>` 设 COM 偏移（均质船体 z=-D/2）

**新建文件**：`ShipHullGenerator.hh/.cc` (替代 MathHullGenerator)
**修改文件**：`HullStabilityDialog.hh/.cc`, `CMakeLists.txt`
**待清理**：删除旧 `MathHullGenerator.hh/.cc`

**测试结果**：船舶模型正常浮在水面 ✅，压载块实验正常 ✅

### 水面检测问题 — 待分析 (2026-07-21)

**问题背景**：迁移 M_A 到 DART `<fluid_added_mass>` 后，在 HydrodynamicsSystem::PreUpdate 中新增了水面检测逻辑，模型完全高于水面时跳过阻尼。

**现象的复杂性**（需要综合考虑）：

| 场景 | 水面检测需求 |
|------|-------------|
| 未接触水面 | 完全跳过水动力（M_A + C_A + D 全都不作用） |
| 漂浮（部分浸没） | M_A/C_A/D 全部按实际浸没体积比例作用 |
| 加载后吃水加深 | 同上，但浸没比例动态变化 |
| 潜航器（全浸没） | M_A/C_A/D 全部 100% 作用 |
| 水翼船（高速水翼支撑） | 船体脱离水面但水翼入水 → 复杂耦合 |

**当前方案的限制**：
1. `DART <fluid_added_mass>` 永久改变空间惯量，无法在空气中动态消除 M_A
2. 阻尼水检只能判断"全在水上"或"全在水中"，无法处理部分浸没
3. 未接触水时 M_A 仍在惯量中 → 自由落体加速度 = g × m_RB / (m_RB + M_A)，而非 g

**待分析方向**：
1. 浮力插件已有 `waterAreasConfigured_` / `inWater` 逻辑，能否复用？
2. DART 是否支持运行时动态修改 `fluid_added_mass`？
3. 是否需要退回到显式计算方案，但改用隐式积分？

---

## 2026-07-21 修复记录

### Bug: InjectFluidAddedMass 双重缩放 ✅

**问题**：`ModelManager.cc` 的 `InjectFluidAddedMass` 中 `* _mass` 导致附加质量值被放大 300 倍（`buildAddedMassMatrix` 已做过一次缩放，而 `_addedMass` 已是 UI 输入的 kg 物理值）。有效 Z 轴惯量 = 300 + 108000 = 108300 kg，重力加速度 g ≈ 9.81 m/s² 下净加速度仅 0.027 m/s² → 模型"缓慢下降 + 乱飘"。

**修复**：
1. 删除 `* _mass`
2. 函数签名移除 `_mass` 参数
3. 文件：`ModelManager.cc:2210`、`ModelManager.hh:346`

### 补全 21 元素映射 ✅

**问题**：kMap/kTag 只映射了 15 个上三角元素，漏了 6 个线性-旋转耦合项。

**修复**：补全全部 21 个上三角元素，输出时跳过零值。新增 `xq(0,4)、xr(0,5)、yq(1,4)、yr(1,5)、zq(2,4)、zr(2,5)`。其中 `yr=Y_rdot` 和 `zq=M_wdot` 在 12 参数 Fossen 模型中可为非零。

---

### SP-03 压载块消失 Bug 修复 (2026-06-01) ✅

**现象**：拖动压载 y 滑块后压载块消失，重新生成后恢复。

**根因**：`moveWeight` 使用硬编码 `deckZ=0.12`（世界坐标），船体上浮后甲板已不在 z=0，压载块被瞬移到甲板下方、穿入 mesh 内部。

**修复**：对齐 SP-02 `IncliningExperimentDialog` 模式，通过 PoseState 追踪船体世界 z，`deckZ = currentHullZ_ + wh/2 + 0.02`。`HullPoseState` 新增 `hullZ` 字段，pose 回调中提取 `position().z()`。

### SP-03 数学船型系统重构方案 (2026-06-01)

**设计文档**：[notes/math_hull_redesign_plan.md](notes/math_hull_redesign_plan.md)

**背景**：经过三轮修复后，SP-03 仍存在 3 个连锁 Bug（三角绕序、DAE 顶点丢失、DART mesh 碰撞体崩溃）。根因分析表明需要整体重构，而非局部修补。

**核心策略**：直接在 Gazebo Z_UP 坐标系中生成水密网格，消除所有坐标变换。

**分阶段**：
- **Phase A**：MathHullGenerator 重写为 ShipHullGenerator（Gazebo Z_UP 网格 + OBJ + collision Box）— ✅ 完成 (2026-06-01)
- **Phase B**：模型加载菜单集成 + 型线图三视图
- **Phase C**：不同吃水理论排水量 vs 仿真实测对比
- **Phase D**：学生 Python 编程计算排水量 + 安全沙箱

**技术决策**：Wigley ×4 变体 + Lewis U/V，工厂方法预设；学生编程用 Python；排水量实验无波浪；Catto totalVolume 与梯形积分互验证。

---

### 虚拟仿真实验框架 (2026-05-27)

**设计文档**：[notes/virtual_experiment_framework.md](notes/virtual_experiment_framework.md)

**课程覆盖**：船舶原理（12 实验）、船舶智能控制（9 实验）、船舶结构与制图（7 实验），共 28 个。

**层级体系**：
- **Tier 1 观察类**：自动运行，学生调节参数观察现象，验证理论
- **Tier 2 操作类**：学生动手配置（PID 参数、路径规划、结构参数），达到性能指标
- **Tier 3 设计类**：开放目标，学生从零设计方案并验证可行性

**依赖链设计**：实验间存在前置关系（如 SP-03 推力测量实验需要 SP-01 浮性数据），后续实验自然复用前置成果。

---

### SP-01 浮性与排水量实验实现 (2026-05-27) ✅

**首个虚拟仿真实验**：学生通过滑块调节配载质量（0~20000 kg），实时观察竖直圆柱体（D=2m, L=10m）吃水变化，记录多组数据，生成质量-吃水散点图，验证阿基米德原理。

**架构**：
```
GUI 滑块 → gz-transport Double → /keship/sp01/payload/{model}
  → KSPlugin_Controller PreUpdate → ExternalWorldWrenchCmd（累加）
  → 物理引擎 → 圆柱体下沉
  → /world/keship_world/pose/info → BuoyancyVariableLoadDialog（30Hz 轮询）
  → 吃水计算 + UI 更新 + 水线面 3D 渲染
```

**新建文件（2 个）**：

| 文件 | 说明 |
|------|------|
| `src_cpp/plugins_gui/menu_plugin_keship/BuoyancyVariableLoadDialog.hh` | 实验对话框头文件：MassDraftChartWidget（QPainter 图表）+ BuoyancyVariableLoadDialog（QDialog 非模态） |
| `src_cpp/plugins_gui/menu_plugin_keship/BuoyancyVariableLoadDialog.cc` | 完整实现 ~330 行：UI 布局（3D 预览 + 控制面板 + 数据表 + 图表）、transport pub/sub、理论吃水公式、数据记录/图表生成 |

**修改文件（7 个）**：

| 文件 | 变更 |
|------|------|
| `KSPlugin_Controller.hh` | 新增 `VariableLoad` 结构体（linkName, topicName, commandedPayloadMass, payloadMutex, enabled） |
| `KSPlugin_Controller.cc` | `LoadParameters()` 解析 `<variable_load>` SDF 子块 + transport 订阅；`PreUpdate()` 读-累加-写 ExternalWorldWrenchCmd |
| `ModelManager.hh` | 新增 `GetCachedModelSDF()` inline getter（SDF 注入需要） |
| `MenuPluginKeship.hh` | 新增 `OnBtn_SP01_BuoyancyDisplacement()` slot + `sp01Dialog_` 成员 |
| `MenuPluginKeship.cc` | ~60 行 slot：检测已有圆柱体、spawn（首次）、SDF 字符串注入 variable_load 块、RebuildModelWithAllPlugins、创建非模态 dialog |
| `MenuPluginKeship.qml` | 顶部 Row 新增"SP-01 浮性与排水量"按钮 |
| `Buoyancy3DView.hh/.cc` | 新增水线面渲染：`setWaterLevel`/`setShowWaterline`/`setModelZOffset` + `buildWaterlineGeo`（15m×15m 半透明蓝色水平面） |
| `CMakeLists.txt` | MenuPluginKeship 源文件列表新增 `BuoyancyVariableLoadDialog.cc` |

**设计要点**：
- **Wrench 累加协议**：控制器读取现有 `ExternalWorldWrenchCmd` → 叠加 payload 力 → 写回，与浮力/推力插件共存
- **线程安全**：`payloadMutex`（sim 侧 transport/sim 线程）、`poseMutex_`（GUI 侧 transport/Qt 线程）
- **理论吃水**：`draft = mass / (ρ_water × π × r²)`，圆柱体竖直（Z 轴），水线面积常数 ≈ 3.14 m²
- **SDF 注入**：在缓存的 SDF 字符串中查找控制器插件标记，插入 `<variable_load>` 子块后重建模型
- **生命周期**：dialog 非模态 `WA_DeleteOnClose`，关闭时发布 payload=0 归零，`destroyed` 信号清理指针

---

### SP-01 代码审查 (2026-05-27)

**审查范围**：11 个变更文件（3 新建 + 8 修改），逐文件审查。

**发现 Bug（2 个，均已修复）**：

| # | 严重度 | 文件:行 | 问题 | 修复 |
|---|--------|---------|------|------|
| B1 | 中 | `Buoyancy3DView.cc:652` | 水线面 IBO 用 `GLuint`（4B）填充但 `drawGeo()` 以 `GL_UNSIGNED_SHORT`（2B）渲染，little-endian 上索引错读为 [0,0,1,0,2,0]，三角形全部退化，水线面不可见 | `GLuint idx[]` → `uint16_t idx[]` |
| B2 | 低 | `MenuPluginKeship.cc:876` | SDF 注入 `variable_load` 时 marker 未找到静默继续，对话框打开但滑块无效且学生无提示 | 添加 `else { QMessageBox::warning(...) }` |

**编译错误修复（2 个）**：

| # | 文件:行 | 错误 | 修复 |
|---|---------|------|------|
| C1 | `KSPlugin_Controller.cc:74` | `SetComponentData` 第 2 参数不能传 Component 包装器，需传 `const gz::msgs::Wrench&` | `ExternalWorldWrenchCmd(wrenchMsg)` → `wrenchMsg` |
| C2 | `KSPlugin_Controller.cc:101` | `_sdf` 是 `const shared_ptr<const sdf::Element>`，`GetElement()` 非 const | 加 `const_cast<sdf::Element *>(_sdf.get())`（对齐 HydrodynamicsSystem/KSPlugin_Buoyancy 模式） |

**审查通过项**：
- 线程安全：mutex 范围正确，ECM 累加写入协议正确
- 数据流：transport topic pub/sub 名称一致
- 物理公式：理论吃水公式正确（圆柱体竖直，水线面积常数）
- 内存管理：`WA_DeleteOnClose` + `destroyed` 信号清理，无泄漏路径
- Signal/Slot：`blockSignals` 双向同步，无递归风险
- 生命周期：`transportNode_` 引用传递，ModelManager 生命周期覆盖 dialog

---

### SP-01 重构：初稳性观测实验 (2026-05-27) ✅

**动机**：旧版 SP-01"验证阿基米德原理"为中学物理范畴，不适合船舶专业教学。

**重构方向**：改为"漂浮圆柱体初稳性观测"，对应船舶原理·静力学初稳性章节。核心理论：$GM = KB + BM - KG$，临界条件 $D/L = \sqrt{8s(1-s)}$。

**新增文件**：
- `StabilityExperimentDialog.hh/.cc` — 初稳性实验对话框（参数输入、扰动控制、横倾监测、自动判定、稳定域图）

**删除文件**：
- `BuoyancyVariableLoadDialog.hh/.cc` — 旧配载-吃水对话框

**修改文件**：
- `MenuPluginKeship.hh/.cc` — 替换对话框类型和槽函数
- `MenuPluginKeship.qml` — 按钮文本 "SP-01 圆柱初稳性"
- `CMakeLists.txt` — 替换源文件

**设计要点**：
- 对话框自生成 SDF（含浮力插件），通过 gz-transport `/world/{name}/create` 服务直接 spawn，不依赖 ModelManager
- `StabilityPoseState` 共享状态解耦 transport 回调与对话框生命周期（复用前次 bug 修复的 shared_ptr 线程安全模式）
- 稳定/不稳定判定：施加初始扰动后 150 帧（~5s）观测横倾角变化；GM > 0 时 φ→0°，GM < 0 时 φ→大角度
- 稳定域图：横轴 s、纵轴 D/L，理论临界曲线 $y = \sqrt{8s(1-s)}$ + 实验数据点

**问题**：点击 SP-01 按钮弹出"无法在控制器插件中注入可变载荷配置"，实验滑块无法生效。

**根因**：两个连锁 bug：

**Bug 1 — 缓存 SDF 不含插件，标记找不到**：
- `SpawnCylinderInGazebo` 第 499 行 `sdfCache_[name] = baseSDF.str()` 只缓存了基础 SDF（`<model>...<link>...</link>`），不含插件段
- SP-01 代码 `GetCachedModelSDF` 拿到的是无插件的 baseSDF，搜索 `name="keship::KSPlugin_Controller">` 标记 → 找不到 → 报错

**Bug 2 — `RebuildModelWithAllPlugins` 丢弃 `<variable_load>`**：
- `RebuildModelWithAllPlugins` 第 2100 行硬编码 Controller 插件，不含 `<variable_load>`
- 即使 Bug 1 修复后注入成功，重建时 `<variable_load>` 仍会被丢弃

**修复 (ModelManager.cc)**：

| # | 位置 | 修改 |
|---|------|------|
| Fix 1 | `SpawnCylinderInGazebo` 成功 spawn 后 | `sdfCache_[name] = sdf.str()` 缓存完整 SDF（含插件），使标记可被找到 |
| Fix 2 | `RebuildModelWithAllPlugins` 切割 `</link>` 前 | 新增 `hasVarLoad = base.find("<variable_load>") != npos` |
| Fix 2 | Controller 插件输出段 | `if (hasVarLoad)` 条件输出 `<variable_load>` 块 |

**设计要点**：
- 所有 rebuild 方法均使用 `rfind("</link>")` 切割缓存 SDF 后重拼插件——缓存完整 SDF 对此无影响，因插件不含 `</link>` 标签
- `hasVarLoad` 检测必须在 `rfind` 切割**之前**执行（`<variable_load>` 在插件区，切割后丢失）

---

### SP-01 Bug 修复：关闭对话框时崩溃 (2026-05-27)

**问题**：关闭 SP-01 对话框时 Gazebo segfault 退出。

**崩溃栈**：
```
gz::transport::v13::NodeShared::RunReceptionTask()
  → TriggerCallbacks() → Pose_V::CopyFrom() → Pose_V::Clear()
  → Segmentation fault (Address not mapped to object [0x8])
```

**根因**：`initPoseSubscription` 的 lambda 捕获 `this`，订阅注册在 ModelManager 的长生命周期 `transportNode_` 上。对话框 `WA_DeleteOnClose` 被销毁后，transport 回调仍在后台线程触发，访问已释放的成员变量（`poseMutex_`, `latestPoses_`）→ segfault。

项目所有其他 subscriber 均在仿真插件内（长生命周期，从不销毁），此问题之前从未暴露。BuoyancyVariableLoadDialog 是第一个短生命周期订阅者。

**修复 (BuoyancyVariableLoadDialog.hh/.cc)**：

采用**共享状态 (PoseState)** 方案，将回调访问的数据从对话框成员中解耦：

```
Before:  callback → this → poseMutex_, latestPoses_  (随 dialog 销毁)
After:   callback → shared_ptr<PoseState> → mutex, latest, alive  (独立生命周期)
```

**头文件变更**：
- 新增 `PoseState` 结构体：`mutex`, `alive`, `latest` (Pose_V), `ready`, `active`, `initialZ`
- `latestPoses_` + `poseMutex_` → `std::shared_ptr<PoseState> poseState_`
- `experimentActive_` + `poseSubReady_` → 移入 `PoseState`
- 新增 `poseTopic_` 成员

**实现变更**：
- **构造函数**：`poseState_ = std::make_shared<PoseState>()`
- **`initPoseSubscription`**：lambda 按值捕获 `shared_ptr<PoseState>` + `cylinderInstanceName_`，不捕获 `this`；回调内先 `lock(mutex)` 再检查 `alive`
- **析构函数**：`lock → alive=false → unlock → Unsubscribe(poseTopic_) → cleanup`
- **`onUpdatePose` / `updateDraftDisplay`**：通过 `poseState_->` 访问共享数据

**线程安全保证**：
- `PoseState` 通过 `shared_ptr` 在 lambda 中持有引用，对话框销毁后数据仍存活
- 回调先 lock mutex 再检查 alive，析构先设 alive=false（under lock）再 unsubscribe
- 即使 Unsubscribe 后仍有残余回调触发，也能安全检查 alive 并提前返回

---

### UI 重构：Tab 布局已知问题 (2026-05-28)

**背景**：将 MenuPluginKeship.qml 从单页布局重构为 TabBar + 两个 Tab（模型管理/虚拟实验），以适应后续 28 个实验的入口。

**QML 类型兼容性发现**：
- 系统中 `import QtQuick.Controls 2.0` 仅解析到 2.0 API，`StackLayout`（2.1+）和 `ScrollView`（2.2+）不可用
- `Flickable` 使用时触发了 segfault（Gazebo 自定义 QML 环境不支持该类型）
- 最终方案：`TabBar`/`TabButton`（Controls 2.0）+ `Item`+`visible` 切换 + `ListView` 滚动（QtQuick 基础类型）

**问题 B1：滚动时 TypeError (已接受，不影响功能)**：
- `section.delegate` 和 `delegate` 内 `anchors.left/right: parent.left/right` 在 ListView 回收 delegate 瞬间 `parent` 未就绪
- 已尝试 `anchors`、`ListView.view`、fallback 等多种方案，均无法消除
- Qt Quick ListView delegate 生命周期固有行为，仅瞬态单帧，不积累、不崩溃、不卡顿
- **决定**：接受此无害警告

**问题 B2：WaveVisual GPU 卡顿 (已定位，暂缓)**：
- 注释 WaveVisual 插件 → 水变白色无外观；保留插件 → 水面正常但有卡顿
- 根因：Gerstner 片段着色器（立方体贴图 + 法线贴图 + Fresnel）GPU 开销较大
- 优化方向（后续）：降波数组件数 / 简化片段着色器 / 降网格精度
- **决定**：暂缓处理，卡顿体感不严重，优先推进功能开发

**问题 B3：传感器位置悬空 (已修复)**：
- **现象**：传感器 GroupBox 停在面板中间高度，其下方有大片空白区
- **修复**：Tab 1 从 `Column` 堆叠 → `Item` + `anchors` 定位。模型 GroupBox 撑满按钮行与传感器之间，传感器 `anchors.bottom` 贴底

**问题 B4：界面配色 (已修复)**：
- **现象**：深色背景看不清文字；实验列表绿色背景刺眼；层级标签浅绿在浅底不可见
- **修复**：全局改为浅色系（面板 `#d0d0d8c0`、列表行 `#e8e8ec`、深色文字）；TabButton 自绘 contentItem；实验列表用绿色细竖条替代全绿背景；tier 文字改为 `#338833`

**问题 B5：SP-01 对话框关闭后残留圆柱体 (已修复)**：
- **现象**：关闭 SP-01 对话框后圆柱体仍存在于 Gazebo 中，且模型列表中无此模型
- **修复**：`StabilityExperimentDialog` 析构函数末尾加 `doDeleteCylinder()`

**问题 B6：实验数据行管理 (已修复)**：
- **需求**：记录数据后无法删除单行，只能清除全部
- **修复**：新增"删除选中"按钮；表格启用 `ExtendedSelection`；选中行删除后图表数据自动重建

**问题 B7：稳定域图可读性 (已修复)**：
- **现象**：仅有理论曲线 + 文字标签，稳定/倾倒区域不直观
- **修复**：曲线上下分别填充绿色/红色半透明区域（`QPainterPath`）；标签改为带色块指示器

---

## 2026-07-22: Buoyancy3DView 旋转顺序对齐修复

**问题根源**：`Buoyancy3DView.cc` 中 `bodyMatrix` / `thrusterMatrix` / `sensorMatrix` 的 Qt矩阵旋转顺序为 `T * Rx(roll) * Ry(pitch) * Rz(yaw)`，等效于向顶点应用 yaw→pitch→roll。而 Gazebo 物理引擎中 `gz::math::Pose3d(px,py,pz,roll,pitch,yaw)` 使用 `q_z(yaw)*q_y(pitch)*q_x(roll)`，等效于 roll→pitch→yaw（ZYX 内禀欧拉角）。两者旋转顺序相反。

**影响**：
- 浮力体/推进器/传感器在 3D 预览中的朝向 ≠ 物理引擎实际朝向
- 复合旋转（多轴非零）时差异明显：如 pitch=90° + yaw=10°，显示 yaw 不可见，物理中 yaw 生效 → 浮心偏离 → 排水体积和浮力矩错误 → 船体异常晃动/倾覆
- 仅在非零 roll+pitch、pitch+yaw 或 roll+roll+... 复合旋转时暴露，纯平移或单轴旋转不受影响

**数值验证**：`r=30°, p=45°, y=60°` 作用于顶点 `(0,0,1)`：
- 显示 `Rx(30)·Ry(45)·Rz(60) = (0.7071, -0.3536, 0.6124)`
- 物理 `Rz(60)·Ry(45)·Rx(30) = (0.7392, 0.2804, 0.6124)`
- 两旋转矩阵不相等（数值验证）

**修复**：三处矩阵函数旋转顺序从 `Rx→Ry→Rz` 改为 `Rz→Ry→Rx`（yaw 最先生效，roll 最后生效），与 `gz::math::Pose3d` 的 ZYX 内禀欧拉角一致。

**涉及文件**
- `src_cpp/gui/keship/shared/Buoyancy3DView.cc:678-704`

**文件修改**
- `bodyMatrix` — `m.rotate` 顺序改为 `yaw → pitch → roll`
- `thrusterMatrix` — 同上
- `sensorMatrix` — 同上

---

## 2026-07-28: Issue 2 根治 — M_A 从 DART 移除 + Gazebo 原生加速度 API

**问题现象**: 配置了 CoG 偏移 + M_A 的模型在自由落体中自行旋转

**根因**（详见 [`notes/ma_dart_removal_fix.md`](notes/ma_dart_removal_fix.md)）：
- `<fluid_added_mass>` 注入 DART 后，M_A 的非对角块（如 `m15=m51=-405`）使合成空间惯量的逆矩阵产生线→角耦合
- 纯重力 `f_g = [m·g, 0]^T` 通过 `(I_RB + I_MA)^{-1}` 求解加速度，出现虚假角加速度
- CoG ≠ origin 时问题暴露（`I_RB` 自身已有 CoG 偏移耦合，M_A 叠加后放大）

**修正反思**（对比 2026-07-27 错误诊断）：
- Munk 力矩符号**不是 bug**（`_tau = -ca_nu` 配 `ca_nu = v×a` 得到 `τ = -v×a`，正确）
- 2026-07-27 的 Fix 2（M_A 注入 DART）+ Fix 3（从 FossenModel 移除 Munk）已被回退
- 2026-07-27 的 Fix 1（`inWater` 守卫）保留为正确修复

**最终方案**：
- 移除 `InjectFluidAddedMass` 的 `<fluid_added_mass>` 注入（保持 SDF 版本升级）
- FossenModel 使用 Gazebo 原生 `WorldLinearAcceleration` / `WorldAngularAcceleration` 获取 ν̇
- 显式计算 `τ_hydro = -M_A·ν̇ - C_A(ν)·ν - D(ν)·ν`，仅 `inWater=true` 时施加

**涉及文件（6 个）**:

| 文件 | 改动 |
|------|------|
| `program/notes/ma_dart_removal_fix.md` | 新建方案文档 |
| `program/keship_issues_log.md` | 更新本条目 |
| `ModelManager.cc:2218-2275` | 空化 `InjectFluidAddedMass` |
| `KSPlugin_HydroBuoyancy.cc:112` | 加 `EnableAccelerationChecks` |
| `KSPlugin_HydroBuoyancy.cc:419-449` | 读取加速度、传递 ν̇ |
| `FossenModel.hh:88-91` | 新增 `computeHydroForces(nu, nuDot, tau)` |
| `FossenModel.cc:159-170` | 实现 `-M_A·ν̇ - C_A·ν - D·ν` |

**验证**: `ke0728_3_neworigin_damp.ksm`（CoG=1.8,0.99,0.1）+ 无水区 → 自由落体零旋转 ✅
## 2026-08-05: 推力旋转调查 + 当前浮力/推力施加方式记录（待理论验证）

**现象**: 双推 200N 时模型旋转而非直线前进（用户多次实测）

**调查过程摘要**:
- 20:02 会话（KSM 原值 thrust 位置 y=-1.68/-0.3）: 单推 totalTorque.z=-335.88（吻合"关于 link 原点"计算），模型旋转
- 20:14 会话（用户实验 thrust 位置 y=±0.68）: 单推 totalTorque.z=-136.05，模型仍旋转
- 21:11 前后两插件（KSPlugin_Thrust / KSPlugin_HydroBuoyancy）被改为"力矩关于质心（linkCoMWorldPos）"计算
- 21:21 会话: 浮力按质心计算 → 模型被顶飞 2.57m、侧翻 94°（roll=1.645 rad）— 浮力异常
- 21:52 会话（本次修复后）: 浮力恢复正常（roll≈0.0003、pz≈-0.168、subVol=0.30）; 推力单推转圈、双推 totalTorque.z≈0.264（≈0）

**当前代码事实（2026-08-05 修复后）**:

| 插件 | 力合成 | 力矩计算 | 施加方式 |
|------|--------|----------|----------|
| `KSPlugin_Thrust.cc` | `totalForce += thrustForce`（各推进器推力合成） | `totalTorque += (P_i - linkCoMWorldPos) × F_i`（**关于质心**） | `ExternalWorldWrenchCmd` 组件（force+torque 累加写入） |
| `KSPlugin_HydroBuoyancy.cc` | `totalForce += buoyancyForce`（浮力竖直向上） | `totalTorque += (cobWorld - linkWorldPos) × F_buoy`（**关于 link 原点**） | 同一 `ExternalWorldWrenchCmd` 组件 |

**修复动作（2026-08-05）**:
- `MenuPluginKeship.cc` 恢复 .bak 基线（撤销"自动按 mesh 几何中心算质心"改动）
- `KSPlugin_HydroBuoyancy.cc` 浮力力矩参考点从 linkCoMWorldPos 改回 linkWorldPos（恢复 21:21 崩溃前状态）
- `KSPlugin_Thrust.cc` 保留"关于质心"计算（21:10 改动，非本次调查所改），另加实际惯性/质心打印与 diag 限制解除

**开放问题（待理论推导验证，勿当定论）**:
1. gz-sim `ExternalWorldWrenchCmd` 的 force 实际施加点：link 原点 or 质心？（源码显示 AddExternalForce 默认 position=link 原点，但实测浮力/推力行为与"力矩参考点=force施加点"自洽性矛盾，需独立实验裁决）
2. 两插件力矩参考点不一致（推力=质心、浮力=link原点）是否合理？
3. 双推直线是否达成：需在浮力正常基线上重新实测双推持续 20s 确认 yaw 稳定

### 2026-08-07 �������زο���þ����޸����������� link ԭ�㣩
**����**��`ExternalWorldWrenchCmd` �� force ʵ��ʩ���� **link ԭ��**��gz-sim8 Link.cc `AddWorldForce` + DART `BodyNode::addExtForce` Դ�붨������torque Ϊ�����ء����������� = t_cmd + (O-CoM)xF������ (O-CoM)xF Ϊ������вв�޷��رա�

**���֤��**��mylogs 20260806_222718 / 20260807_082646 / 20260807_084146����
1. linkCoMWorldPos��WorldInertialPose����ȷ = ���� (1.6,-0.99,0.15)����"��������ԭ��"����SystemAddress��
2. ���� 200N д�� totalTz = +137.95 = (P-CoM)xF=200x0.69��д���뼸����ȫ�Ǻϡ�
3. �Գ�˫��д�� totalTz Լ 0.26���������㣩���� yaw �Ե��������� 1.18~1.2 rad ���� ƫ������ȫ�����Բв� (O-CoM)xF = +396 N.m��ת�� 0.09 -> 0.003 rad/s ����˥����
4. ��ʹ�� dual_thrust.py ͬ֡ͬʱ˫����20260807_084146���޵��ƶΣ���yaw �Դ� 0 �ǵ� 1.2 rad ���� ֤�������ʱ���޹أ��ǵ�ǰ����д�����Ĳο����¡�

**�û��������**�����ƣ����� +336/+60��������ת��˫�ƺ��������ʣ�� 396 ��ѹ������ת�٣�"����ֱ��" ��˥����Ľ��ƣ�����ֱ�ߣ���ʱ���Ի����ۻ���������

**�޸�������2026-08-07��**��
- KSPlugin_Thrust.cc�����زο��� linkCoMWorldPos�����ģ��Ļ� linkWorldPos��link ԭ�㣩��(P_i-O)xF_i ������в�ͬ������������� = (P_i-CoM)xF_i���Գ�˫�ƺ�Ϊ 0 -> ֱ�ߣ����� = (P-CoM)*F ���֡�ע��ͬ����
- KSPlugin_HydroBuoyancy.cc���������ָ�����ע�ͣ���ʽ����ȷ����
- �����־���� roll/pitch/yaw ��̬�У�linkEuler����ֱ�ӹ۲� yaw ��֤��

**Ԥ�ڸ���**��˫�� total* = -396��yaw ȫ��Լ 0�����������԰� (P-CoM)*F ת��
*����2026-08-08 �������£�*

| **2026-08-12** | **KSW ������ϵ + ����洢���죨V5/V6��** | �� ����Ŀ¼��׺����`<������>.ksw/` �ļ��� + `keship_world.kswf` �嵥 + ������� `.ksw` ���ļ��У�`world1.ksw/boat.ksm`����JSON file ���·������ �����ռ�㼶�������� ksw ���������ռ䣨�� zoo����Ϊ `subWorlds.zoo` �㼶��ģ��/ˮ����ʾ�û�������spawn ƴ�� `zoo_m1`��mergeSegs ȥ�ط��ظ�ǰ׺������ ������ϵ������ ksm/dae ������m1��������ˮ��������water1����Ϊ�����/�ļ�������Դ�����û��������ǣ��� ������޸�������������Ⱦ��delegate ���ȸ� modelListView.width + buildRows ����� JS ���� + �۵�����ʼ̬������������������������۵������� sub ����飩���� ���ؽ������½� SelectWorldDialog��˫�� `.ksw` �ļ���ֱ�Ӽ��أ��� ǰ׺��Ⱦ���Σ����� SDF name=ʵ������spawn/�ؽ����ݣ�+ SaveModelToKSM ���滹ԭԭ����SDF �ļ���֧ name �滻�����ݵ�/˫���ţ���ʵ�����ַ�У�飨��ĸ�����»��ߣ����� ���Ǳ��棺ͬ����ʾ + �ݹ���գ�ˮ��ѡ��=��/ԭ��������ͬ���ɵ�����ѡ�������Է���Ԥ�ڡ������ĵ���program/plans/ksw_world_storage_v5_plan.md + ksw_three_names_plan.md |

