# 多模型运行期性能优化方案

- 日期：2026-08-18（v2：按评审反馈修订 P1/P2，取消 P3）
- 状态：方案定稿，待实施
- 范围：`keship_ws/src/keship`（C++ / Python 桥接 / GUI）

## 1. 背景与问题

- 现象：加载多个 `models_all/ks_model/ke0804_2_newaxis_addDBSIMU.ksm` 模型后，**运行期**性能很差（加载过程卡顿可接受）。
- 约束：模型当前不重叠；**未来场景需要模型间/与环境发生碰撞**。
- 目标：多模型（N≈5）运行期保持实时因子 ≈1.0，且为碰撞场景预留物理余量。

## 2. 模型事实

| 项目 | 数值 |
|------|------|
| KSM 文件 | 1.9 KB 配置（引用 `robot_model/keship3/keship3.dae`） |
| 源网格 | `keship3.dae` 13 MB，78,337 三角形 / 200,390 顶点 |
| 规范化缓存 | `normalized/keship3_83322452.obj` 20.4 MB（已存在） |
| 纹理 | `ship.png` 2.2 MB |
| 每实例物理 | 2 个圆柱浮力体（r=0.26, L=2.6, 32 段）+ 2 推进器 + Fossen 阻尼 |
| 每实例传感器 | Camera 640×480@30Hz、NavSat@10Hz、IMU@100Hz |
| 世界物理 | `keship_world.sdf`：DART，max_step_size=4ms，real_time_factor=1.0 |

## 3. 热点定位（运行期，成本随模型数 N 增长）

| 热点 | 位置 | 成本 | 量级 | 处理 |
|------|------|------|------|------|
| 相机持续渲染 | `scripts/keship_world_bridge:94` 无条件桥接 image → gz 相机传感器持续 30Hz 全场景离屏渲染（每模型一个相机，渲染整场景：全部船模+水面网格） | 每模型 30Hz 全场景离屏渲染 | ★★★★★ | **P1** |
| 78k 三角形碰撞网格 | KSM baseSDF `<collision>` 与视觉共用 `keship3.dae`；spawn 时 DART 构建碰撞结构（assxml 25MB/实例）；**未来碰撞场景 mesh-mesh 窄相每步求解** | 接触时窄相开销极大（GJK/BVH 逐三角形） | ★★★★（未来） | **P2** |
| 水面网格 | `WaterAreaVisualManager.cc:274-387` `GeneratePolygonMesh`：自适应步长 λ_min/3 下限 0.5m、上限 400×400 网格 → 最多 ~160k 顶点 / ~320k 三角形 | 主视图 + 每相机每帧绘制（恒定成本，被 N 相机重复） | ★★★ | 暂不处理（评审决定） |
| 浮力管线 | `KSPlugin_HydroBuoyancy.cc:305-338`：每 body 3 次波浪采样（h0/hx/hy），各持一次 mutex；圆柱体裁剪廉价 | 小（µs 级） | ★ | P4 |
| GUI 订阅 | `MenuPluginKeship.cc:178-194` pose/info 每消息 O(N) 重建缓存 | 小 | ★ | 暂不处理 |

已确认事实：`CameraViewerDialog.cc:37` 通过 **ROS** 订阅图像（依赖 `keship_world_bridge` 的 image 桥）；而桥接节点对每个模型**无条件**订阅 gz 图像 topic → 相机传感器始终渲染（gz-sim 相机在存在订阅者时按 update_rate 渲染），无人查看也在渲染。

## 4. 修复方案

### P1 相机按需渲染（最高优先）

**目标**：相机只在"有人查看"时渲染——GUI 查看器打开时、或 ROS 侧存在 image topic 订阅者时；其余时间不渲染。不区分实验模式：GUI 查看与 ROS2 学生订阅都可能随时发生。

1. **`CameraViewerDialog.cc`**：订阅源从 ROS 改为 **gz-transport 直连** gz image topic（`/world/keship_world/model/{model}/link/{link}/sensor/{name}/image`），复用 GUI 已有的 gz node；打开对话框才订阅 → 相机仅在查看时渲染。解码 gz `Image`（rgb8）填充 QLabel。

2. **`scripts/keship_world_bridge`（Python）— ROS image 桥按订阅者动态启停**：
   - 每个相机不再常驻 image 桥；桥接节点周期性（如 1s）用 `count_subscribers(ros_image_topic)` 检测 ROS 侧订阅者数量；
   - 数量 >0 → 启动该相机的 `parameter_bridge` image 子进程（创建 gz 订阅 → 相机开始渲染并转发 ROS）；数量 =0 → 停止对应子进程；
   - 学生随时可订阅 ROS image topic（最迟 ~1s 后出图），随时取消订阅后渲染自动停止；
   - 推进器 / IMU / NavSat / pose 桥接保持常驻不变。

3. **`ModelManager::MakeSensorSDF`**（`ModelManager.cc:2939-2951`）相机分支：显式 `<always_on>0</always_on>`（相机仅在存在 gz 订阅者时渲染；分辨率/频率维持 640×480@30Hz 不变）。

验证：`gz topic -i` 观察 image 订阅数（无人查看应为 0）；打开/关闭 GUI 查看器与 ROS 订阅时相机渲染随之启停；加载 5 模型对比 WorldStats 实时因子。
风险：ROS 订阅者出现到出图有 ~1s 延迟（可接受）；若 gz-sim 相机对短暂订阅启停有抖动，可加 2s 保活延迟再停。

### P2 碰撞模式开关（模型属性，默认包围盒）

**目标**：模型属性中可切换碰撞方式，默认**包围盒**，规避 78k 三角形 mesh-mesh 窄相开销，为未来碰撞场景提供低成本碰撞。

1. **UI**：`ModelSettingsDialog`（Full 模式，`editors/ModelSettingsDialog.hh:27` Mode 枚举）新增"碰撞模式"下拉，两档：`包围盒`（默认）/ `模型网格`；Ksm 模式只读显示当前值。
2. **持久化**：KSM 新增 `===COLLISION===` 段，格式 `mode=mesh|bbox`（`SaveModelToKSM` `ModelManager.cc:3377`、`ParseKSMFile` :3954 各加一段）；**无该段时按 bbox（新默认）**——现有 KSM 加载后碰撞即变为包围盒，可通过模型属性改回。
3. **包围盒尺寸**：源网格 Z-UP 包围盒。现状 `ComputeBBoxFromVertices`（`ModelManager.cc:2058-2071`）只有 XY（w/h/xMin/yMin），需扩展出 **zMin/zMax/深度**，并存入 `ModelFootprint`（`CacheModelFootprint` :2338 已有网格加载上下文，顺带计算，无需重复加载）。
4. **应用**：`ModelManager` 新增 `ApplyCollisionMode(sdf, mode, name)`：把 `<collision>` 段几何替换为 `<box><size>W H D</size></box>`（必要时带 `<pose>` 居中偏移，Z-UP 规范化缓存坐标系）；接入 spawn / 重建入口：`SpawnModelWithBuoyancy`(:2458)、`SpawnModelWithThrust`(:2736)、`SpawnModelInGazeboFromSDFString`(:1449)、`SpawnModelInGazeboFromFile`(:608)、`MakeSDFStringFromValidFileType`(:961)、`RebuildModel*` 系列。mesh 模式保持现状（78k 网格）。
5. **开关状态缓存**：`ModelManager` 增 `collisionModeCache_`（name→mode），编辑器修改后 `RebuildModelWithAllPlugins` 生效。

验证：5 模型实时因子；两船互碰（bbox 模式）窄相耗时对比；切换开关后 spawn/rebuild 生效。
风险：包围盒碰撞外形明显粗于原网格（用户已接受默认）；mesh 模式保留可回退。

### P3 水面网格密度调整 — 已取消（评审决定，暂不处理）

### P4 浮力管线微优化（顺带）

- **`KSPlugin_HydroBuoyancy.cc:313-326`**：所有 body 的 3 点波浪采样合并为**一次**锁内完成；采样前用 `h0` 与 body 半径快速跳过（圆柱体特征长 < 波长时点采样足够，可省 hx/hy 两次采样）。

### P5（暂缓）

主视图 78k 三角形 ×N 的视觉降面 LOD——gz-sim 无内置 mesh LOD，工程量大，视 P1-P2 效果再定。

## 5. 实施顺序与验证

1. **第 0 步（基线测量，需用户配合）**：加载 5 个该 KSM，记录 WorldStats 实时因子/迭代率；暂停仿真拖拽 3D 视图对比流畅度（区分渲染/物理瓶颈）；`nvidia-smi` 观察 GPU 占用。
2. **P1 → 复测**（预期解决大头：相机渲染）。
3. **P2**（含两船互碰场景验证）。
4. **P4**。

## 6. 风险与回退汇总

| 项 | 风险 | 兜底/回退 |
|----|------|-----------|
| P1 | ROS 订阅者出图延迟 ~1s | 轮询周期可调（0.5s）；相机启停抖动可加保活延迟 |
| P1 | 学生订阅期间相机渲染仍在（N 模型全开时） | 属预期行为（有人看才渲染）；如需进一步压缩可下调分辨率/频率（另行评估） |
| P2 | 包围盒碰撞外形粗、与视觉不符 | 属性开关随时改回"模型网格"；旧 KSM 无段时按 bbox 属新默认，需知悉 |
| P4 | 波浪采样近似变化 | 仅减少锁竞争与冗余采样，物理结果不变 |

## 7. 评审结论（2026-08-18）

1. 学生编程实验需要 ROS image topic，且 GUI/ROS 查看随时可能发生 → P1 采用"订阅者检测动态启停 image 桥"，不设实验模式。
2. 碰撞方式做成模型属性开关（模型网格 / 包围盒），**默认包围盒**。
3. 水面网格密度暂不调整（P3 取消）。
4. 待实施确认后按 P1 → P2 → P4 顺序执行。