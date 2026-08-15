# KeShip 动力学模型代码与理论检查报告（2026-08-14）

> 检查方式：4 个子代理并行精读（理论基线 8 篇文档 / 浮力+阻尼核心 / 浮力水动力插件 / 推进器+质量+波浪），
> 再经本机交叉验证（代码 ↔ 理论 ↔ 文档 ↔ KSM/SDF 配置 ↔ gz-sim8/gz-physics/DART 安装头文件与二进制反汇编）。
> 行号基于检查当日代码。

## 计划状态（2026-08-14 更新：全部修改已回退，本报告为未来修复计划）

> 按用户指示：本次检查期间对代码/文档所做的修改**已全部回退**（`git checkout` 恢复），
> 下述修复方案转为**未来计划**，待用户确认后分步实施。已实施的唯一修改：
> `start_keship.sh` colcon 参数修复（`--log-base` 移至 `build` 前，2026-08-12 提交 69990d2 引入的启动 bug）。

| 项 | 状态 | 方案摘要（实施时参考） |
|---|---|---|
| A1 封盖体积累加 | 📋 计划中 | `Buoyancy.cc` `calculateTotalVolumeContribution` 中 `volume_ += waterPolyVol`（一行）；浮力/浮心归一化同时修正 |
| A3 推进器力矩参考点 | 📋 计划中（**维持现状**） | 已确认原代码（质心参考）正确：用户实测证据（`keship_issues_log.md` 2026-08-05~07）表明 force 实际施加于质心，link 原点方案使对称双推产生 396 N·m 残差旋转。**不修改代码**，仅建议在注释中补充依据说明 |
| A4 推力符号 | 📋 计划中（**维持现状**） | 已确认设计意图：GUI 箭头=桨轴朝向、推力=箭头反方向（代码正确）；仅需更新 `keship_physics_design.md` 公式为 `F_t = -level·F_max·x̂_body` 并注明箭头语义 |
| C1 wrench 参考点文档 | 📋 计划中 | `keship_physics_design.md` "各力源施加方式"按实测更正：推力=质心参考、浮力=link 原点参考；注明 gz-sim8 头文件注释与 dartsim 实际行为不符 |
| 其余 A2/A5/B1-B15/C2-C3 | 📋 计划中 | 见下文分级清单 |

---

## 一、结论摘要

动力学**主体架构正确**（浮力主链路、wrench 参考点、Fossen 阻尼符号/量纲、M_A 经 DART 隐式注入、多水区与波浪场弱引用自愈），
但存在 **2 个确定性物理错误（A1 封盖体积、A3 推进器力矩参考点）**、1 个文档-实现矛盾待决（A4 推力符号）、
1 个倾斜波浪工况错误（A5 裁剪面锚点）、1 个幽灵方法注释（A7），以及一批边界/线程/死代码隐患。

| 级别 | 数量 | 说明 |
|---|---|---|
| A 明确错误 | 7 | 产生错误物理结果 |
| B 隐患 | 15 | 边界/数值/线程/时序 |
| C 不一致 | 15+ | 文档滞后、死代码、命名误导 |

---

## 二、A 级：明确错误（产生错误物理结果）

### A1. 水线面封盖多边形体积未累加进排开体积 → 浮力与浮心都错 【最高优先】📋 计划中（修复已回退）
- **位置**：`src_cpp/utils/Buoyancy.cc:315-339`（`calculateTotalVolumeContribution`）+ `:1018-1045`（`calculateWaterPlanePolygonVolumeContribution`）
- **现状**：封盖项 `polyVol` 只累加进 `centerOfBuoyancy_`，**从不写回成员 `volume_`**；浮心归一化分母与返回值都缺封盖项。
- **理论**：浸没体积 = 湿船体面贡献 + 水线面封盖贡献，`V_sub = V_wet + V_cap`（散度定理/Catto）。
- **影响**：`F_b = ρgV_sub` 系统性低估，浮心分母错误 → 恢复力矩力臂与力幅都错。**隐蔽性**：网格原点恰在水线面时封盖贡献≈0（静态平衡测试看不出）；垂荡/横倾/纵倾/来浪使 `d_local≠0` 时误差立现（圆柱侧躺算例可达数个百分点）。
- **修复**：`volume_ += waterPolyVol`，归一化与返回均用含封盖的总体积。

### A2. 浮心用"面重心"加权而非"四面体重心" → 系统性偏差
- **位置**：`Buoyancy.cc:113/207`（`tri.centroid_=(v0+v1+v2)/3`）+ `:894/941/979/1009/1040`（`CoB += V_i·c_i`）
- **现状**：体积 `V_i=A·(n·c)/3` 精确，但浮心矩臂用面重心 `/3`。
- **理论**：精确一阶矩应取四面体重心 `(v0+v1+v2+O)/4 = 3c_i/4`。反例：标准四面体，代码 CoB=(1/3,1/3,1/3)，精确 (1/4,1/4,1/4)，**偏 33%**。
- **影响**：CoB 偏向远离原点一侧，KB/GM 失真；粗网格（12 面 box、~64 面圆柱）不可忽略。项目现成 `Tetrahedron` 未接线。

### A3. 推进器力矩参考点错误（质心 vs link 原点）【与 A1 同级】📋 维持现状（原代码正确，勿改）

> **2026-08-14 回退说明**：初判"力矩应以 link 原点为参考"并修改了代码；但用户实测证据（`keship_issues_log.md` 2026-08-05~08-07 推力旋转调查）表明：
> - `ExternalWorldWrenchCmd` 的 force **实际施加于质心 CoM**（gz-sim8 头文件注释 "force is applied at the link origin" 与 dartsim 实际行为不符）；
> - 推力力矩按质心参考（原代码）→ 对称双推力矩抵消、直线前进 ✅（用户多次实测）；
> - 改为 link 原点参考（本次初改）→ 对称双推产生 `(O-CoM)×F = 396 N·m` 残差旋转 ❌（2026-08-07 曾实测失败，与用户所述"双推力不对称"一致）。
> **已恢复原代码**（`KSPlugin_Thrust.cc:191` 质心参考），并保留说明注释。
> 附带结论：浮力力矩参考 link 原点（`KSPlugin_HydroBuoyancy.cc:385`）同样是实测验证的正确方案（曾按质心计算导致模型顶飞侧翻）。两插件参考点不同但各自实测正确，机理待理论验证，勿随意改动。

- **位置**：`physics/KSPlugin_Thrust.cc:191`（配合 `:158-166`、`:204-205`）
- **现状**：`totalTorque += (bodyWorldPose.Pos() - linkCoMWorldPos).Cross(thrustForce)` —— 力矩参考 **质心**；但 `ExternalWorldWrenchCmd` 力作用点是 **link 原点**（`/usr/include/gz/sim8/.../ExternalWorldWrenchCmd.hh:39` 明文：force_offset 被忽略，力施加于 link 原点）。
- **理论**：应为 `(P_i - linkWorldPos) × F_i`。
- **影响**：CoM≠link 原点（项目支持自定义重心偏移）时每个推进器产生虚假力矩 `(O−CoM)×F`。**与浮力插件 `KSPlugin_HydroBuoyancy.cc:385`（link 原点，正确）直接矛盾**——同一 wrench 组件两个参考点。

### A4. 推力方向符号与设计文档相反 📋 维持现状（代码正确，仅文档待更正）
- **位置**：`KSPlugin_Thrust.cc:184-186`
- **现状**：`thrustForce = thrustDir * (-thrustMag)`（取负），注释"箭头（螺旋桨朝向）的反方向"。
- **理论**：`keship_physics_design.md` 公式速查 `F_t = level·F_max·x̂_body`（正指令 → local +X）。
- **影响**：正指令推船倒退（若 +X 为艏向）。可能是**有意约定**（螺旋桨朝艉则推力朝艏），但无文档定义——至少是指令极性未定义/文档矛盾，需与设计意图确认。

### A5. 倾斜水面裁剪面锚定在世界原点，而非浮力体位置
- **位置**：`KSPlugin_HydroBuoyancy.cc:330-336`
- **现状**：`gz::math::Plane<double> waterPlane(waveNormal, h0 + effectiveFluidLevel)` —— gz-math 语义为 `n·p = offset`，等于把斜面锚在 `(0,0,h0+level)`。
- **理论**：斜面应过采样点 `c=(cx,cy,h0+level)`：`offset = n·c`。
- **影响**：船在 (200,100)、波斜率 0.02 时有效水位误差可达 6 m；倾斜波浪（Phase 2）下吃水/横摇/纵摇全错，随船距原点放大。静水（斜率=0）退化正确，回归测不出。

### A6. `ComputeDepthDirectly` 残差缺 q 因子（Gerstner 精确解错误）
- **位置**：`Wavefield.cc:834-835`（残差）vs `:828-832`（Jacobian 含 q）vs shader `GerstnerWaves_vs_330.glsl:133-135`（水平位移含 q）
- **现状**：`F(0) += a*dx*s` 缺 `q`；Newton 迭代求解错误不动点；`steepness=0` 时也会引入不存在的水平位移。
- **影响**：**当前无调用者（死代码）**，不影响运行；但属确定性数学错误，未来误用即错。建议补 q 或删除。

### A7. 幽灵方法 `FossenModel::computeAirCorrection`（注释引用不存在的方法）
- **位置**：`ModelManager.cc:2708` 注释"空中由 FossenModel::computeAirCorrection 抵消"（指空中抵消 M_A 惯性）
- **现状**：全工程**无此方法**。
- **影响**：空中 `(M_RB+M_A)·ν̇ = τ_gravity`，垂向加速度 ≠ g（圆柱算例影响约 2.8%，λ<1 不发散，当前接受此近似）。注释应删除或补实现。

---

## 三、B 级：隐患

| # | 位置 | 问题 | 触发/影响 |
|---|---|---|---|
| B1 | `Buoyancy.cc:920-942`（及并行版 `:698-723`） | 三顶点全落在水线面 ±ε 带内 → `signSum==0 && signMulti==0` 走 `case 0`，`vertices_[-1]` **数组越界 UB** | 平顶/平底船完全沉没再上浮瞬间；崩溃或读垃圾 |
| B2 | `KSPlugin_HydroBuoyancy.cc:395-436` | Fossen 阻尼仅由 `inWater`（XY 水区判定）门控，**不检查浸没体积**；`aboveWater` 只进日志 | 船腾空/倾覆弹跳时空中仍施加满幅水阻尼（空气阻尼小 ~800 倍），与 pose_based 分支（有 `subVol<=1e-6 return`）语义不一致 |
| B3 | `KSPlugin_HydroBuoyancy.cc:184-187` | `enable_hydro && !pose_based && (!fossen || !valid())` → 整个 PreUpdate **早退，浮力一起被关** | 用户配 `enable_hydro=true` 但漏配 `<damping>` → 船直接下沉，无告警 |
| B4 | `KSPlugin_VariableMass.cc:39-47` vs `:105-111` | `PreUpdate` 读 `dirty_/payloadMass_` 不加锁，transport 回调写侧加锁 → **数据竞争 UB**（非 atomic） | 运行中改 payload 偶发撕裂读 |
| B5 | `KSPlugin_HydroBuoyancy.cc:547-548` 解析 → `:947-948` 硬编码 | **`damping_ratio` 解析后从未使用**，pose_based 用硬编码 `zeta_heave=0.15, zeta_rot=0.10` | 用户配 damping_ratio 无效（死参数） |
| B6 | `Wavefield.cc:330-343`（`bandwidthFor`） | PMS 对数网格正交化**边界点权重 ×2**（取全区间），带宽和过计 50%（number=3）~23%（number=6） | 有效波高 `H_s=4√m₀` 系统性高估 ~1.2-1.5 倍；与 `wind_wave_current_design.md` §9 标定表对不上 |
| B7 | `Wavefield.cc:347` | 默认 `number=6`（偶数）时 PMS `n∈{-2.5,...,2.5}`，**无分量落在谱峰 ωp**，主浪向分量缺失；CWR 用整数除则 n=0 存在 | 频谱不以谱峰为中心，波面特征偏离设计 |
| B8 | `Wavefield.cc:775` | `tau=0` 且 `t==t0` 时 `exp(-0/0)=NaN`；`t<t0` 时包络 `(1-exp)>0` 变负（波反向） | 首帧 NaN 污染浮力；当前调用方固定传 0 故未触发 |
| B9 | `KSPlugin_HydroBuoyancy.hh:105` | 重力硬编码 `(0,0,-9.81)`，不读世界/SDF | 非 9.81 重力场景（月球等）浮力/阻尼全错 |
| B10 | `FossenModel.cc:96-98` | `computeDampingForces` 无 dof 边界检查，SDF `<factor dof>` 越界 → UB | 手写 SDF 配置错误时崩溃；插件解析无校验 |
| B11 | `Wavefield.cc:265` vs `WaveVisual.cc:330-333` | `number` 无上限（渲染钳 8），物理用全部 N | GUI 设 >8 时视觉与物理波高/相位失配 |
| B12 | `KSPlugin_WavesModel.cc:117-121` | mtime 分辨率（1s）导致 <1s 内两次 JSON 写入漏读 | 快速连续改参数丢失 |
| B13 | `KSPlugin_Thrust.cc:181` | 推进器位姿用 `modelPose * poseOffset` 而 wrench 作用于 link；多连杆/link 偏移时作用点错 | 单连杆默认配置无影响 |
| B14 | `KSPlugin_VariableMass.cc:47-48` | `newMass<=0` 防不住 **NaN**（NaN 直通 SetMass） | 异常客户端 → DART 质量 NaN → 发散 |
| B15 | `KSPlugin_WavesModel.cc:101/117-119` | 每帧 `last_write_time` stat（250Hz） | 极小持续系统调用开销 |

---

## 四、C 级：不一致（文档滞后 / 死代码 / 命名误导）

### C1. 设计文档与 gz-sim8 实际语义三方矛盾（关键，文档结论反了）
- `keship_physics_design.md:176-186` 声称"ExternalWorldWrenchCmd 将力施加在 COM、恢复力矩参考点应改为 CoG（2026-07-15 修复）"。
- **事实**：gz-sim8 头文件明文 **力施加于 link 原点**（force_offset 忽略）。代码 `:385` 用 link 原点是**正确**的；文档的"修复"方向相反，按文档改会引入 `(O−CoM)×F_b` 新错误。
- 文档第三部分架构（三插件）也与现状（合并插件 + DART 隐式 M_A、无 C(ν)）不符。

### C2. `fluid_added_mass_research.md` 表格索引标注错误（本次交叉验证新发现）
- 文档 §1.3 表格：`M_A(1,1)=360 → X_u_dot(surge)`、`M_A(2,2)=270 → Y_v_dot(sway)`、`M_A(1,6)=-405 → X_r_dot(surge-yaw)`。
- **实际 3 个 KSM 配置**（0-indexed 行主序）：`m00=15`(surge)、`m11=360`(sway)、`m22=270`(heave)、`m33=132.5`(roll)、`m44=135`(pitch)、`m55=480`(yaw)、`m15=m51=-405`(sway-yaw 耦合)。
- `ma_dart_removal_fix.md:95-96` 的 sway-yaw 块 `[360,-405;-405,480]` 与 KSM **完全一致** → 判定 research 文档表格物理含义标注错误（360 实为 sway、270 实为 heave、-405 实为 sway-yaw 耦合）。

### C3. 其他文档滞后/死代码清单
- `setDampingCoeffs` 头注释"8 个一阶**对角** term"——实际含 Yr/Nv 两个**非对角**交叉项（FossenModel.hh:54-55）。
- `keship_physics_design.md` 的 D_n 负号写法与耗能方程矛盾（实现 `-=` 正确，文档记号误导）。
- `Wavefield.hh:89` 默认 number=3 vs `Wavefield.cc:129` number(6) vs `WaterArea.hh:37` number=6。
- `gain` 默认 1.0（Wavefield）vs 0.05（WaterArea.hh:36），两条默认路径不一致。
- `ComputeDepthSimply/Directly` 命名"depth"实为"波面高度"（Wavefield.hh:314-326 vs 实现 :888）。
- 波向约定：代码 θ 从世界 +X 起算；`wind_wave_current_design.md §4.1` 风 direction 0 = +Y(北)。GUI 若"浪向跟随风向"直接拷贝会有 **90° 系统性偏差**（需 GUI 侧确认）。
- 死代码：`Buoyancy::calculateWetSurfaceVolumeContribution_parallel`（从未调用，且含 1e-6 容差丢失 bug）、`CalculateBuoyancy` 声明未定义、`loadMeshData(string)` 半成品（cube 分支 0 三角形）、`intersectionPointsMap_`/`PointHash`、`Tetrahedron::Volume()`（无调用点，`abs` 丢失有向符号）、`ComputeDepthDirectly`（无调用点）、`WaveVisual::OnWavefield` 变更检测漏 gain/tau/phase（只改 gain 不重传 shader）、`lastJsonCheckTime_/simTime_/alive_/ecm_` 等死成员、`KSPlugin_WavesModel` 析构空 lock_guard。
- `body.totalVolume += std::abs(tri.volume_)`（插件侧 :612/:724）：abs 掩盖法线朝内错误（历史上正是沉没 bug 根因），仅能当诊断量。

---

## 五、DART 机制核实（二进制反汇编证据，本次检查新增）

对 `libgz-physics7-dartsim-plugin.so`（7.6.0）反汇编确认：

1. **`AddedMassFeatures::SetLinkAddedMass` 内部调用 `dart::dynamics::BodyNode::setGravityMode(bool)`**（唯一调用点，函数 +0x4ac 处），调用后紧跟 `getSpatialTensor → setSpatialTensor → setInertia`——确认 **M_total = M_RB + M_A 合并**机制，且 **API 路径（运行时 SetLinkAddedMass）会修改重力模式**。
2. **KeShip 走的是 SDF `<fluid_added_mass>` 路径**：`sdformat Link::Load → ECM Inertial → gz-physics SDFFeatures::ConstructSdfLink`。gz-physics 官方 PR #384（"Support fluid added mass"）显示 ConstructSdfLink 直接做 Featherstone 块重排并合并进空间惯量，**不经 SetLinkAddedMass**。
3. 因此 **KeShip 场景下 DART 内置重力大概率仍生效**（与 SP-01 排水量验证实验可用的事实一致）；`[调研]` 文档记载的 setGravityMode(false) 行为针对 API 路径，文档未区分两条路径。
4. **待运行时验证项**：加载带 `<fluid_added_mass>` 的模型后，将船抛至空中观察是否正常下落（重力正常）——建议列入回归测试。

---

## 六、数值核对结果

| 核对项 | 结论 |
|---|---|
| KSM M_A（3 文件一致：15/360/270/132.5/135/480/-405） | 与 `ma_dart_removal_fix.md` 一致 ✅；与 `fluid_added_mass_research.md` 表格标注矛盾（见 C2） |
| `InjectFluidAddedMass` 无 `*_mass` 双重缩放 | ✅ 已修复（历史上曾放大 300 倍，见 fluid_added_mass_migration.md） |
| M_A 对称性 | KSM m15=m51=-405 对称 ✅（DART 要求对称，否则 LINK_INERTIA_INVALID） |
| 阻尼链路 `dampt=target,coeff,dof,order` → DampingTerm → SDF → FossenModel | ✅ 一致；8 系数映射 Xu~Nv 与头注释一致 |
| 阻尼系数语义 | 不乘 mass（绝对 SI 值），与 `[方案A]` §八一致 ✅；`fossen_physics_refactor.md` 表格仍写 `c·mass`（文档滞后） |
| 惯性三角不等式 `ValidateInertia` | ✅ 三条不等式正确 |
| 浮力公式 `F_b=ρgV_sub`、裁剪插值、平面变换 | ✅ 正确（gz-math Plane::Distance 语义已核对） |

---

## 七、修复优先级建议

| 优先级 | 事项 | 工作量 |
|---|---|---|
| **P0** | A1 封盖体积累加（一行 + 归一化/返回值） | 小 |
| **P0** | A3 推进器力矩参考点改 link 原点（并同步 A4 推力符号与设计文档/控制器约定） | 小 |
| **P1** | A5 倾斜裁剪面锚点修正为过采样点 | 小 |
| **P1** | A2 浮心改四面体重心 `/4`（评估对 GM 影响） | 小 |
| **P1** | B1 全共面退化防护（case 0 分支加顶点计数校验） | 小 |
| **P1** | B3 早退吞浮力：改为仅跳过阻尼、保留浮力，并加告警 | 小 |
| **P2** | B2 出水阻尼衰减（对齐 pose_based 的 subVol 门控） | 中 |
| **P2** | B4 VariableMass 加锁/atomic；B14 NaN 防护 | 小 |
| **P2** | B5 damping_ratio 接线或删除；B6/B7 PMS 带宽与采样修正 | 中 |
| **P2** | A6 ComputeDepthDirectly 补 q 或删除；A7 注释修正/实现空中抵消 | 小 |
| **P3** | C1/C2 文档修正（wrench 参考点、M_A 表格、阻尼×mass 表、架构现状、波向约定）；死代码清理 | 中 |

---

## 八、已确认正确（平衡判断）

- 浮力恢复力矩参考点 **link 原点**（`KSPlugin_HydroBuoyancy.cc:385`）——**实测验证正确**（2026-08-05：按质心计算导致模型顶飞侧翻；注意 gz-sim8 头文件注释"force at link origin"与 dartsim 实际行为不符，此处以实测为准）✅
- 推进器力矩参考点 **质心**（`KSPlugin_Thrust.cc:191`）——**实测验证正确**（2026-08-07：link 原点方案导致双推 396 N·m 残差旋转）✅
- Fossen 阻尼符号（`−c·ν`、`−c·|ν|ν`、交叉项）、8 系数映射、量纲（N / N·m）✅
- 裁剪算法各分支（一顶点在上/在下/在面）的部分体积分解 ✅
- 水线面平面局部变换（`n=R⁻¹n_w, d=d_w−n_w·pos`）✅
- Wrench 累加协议（插件侧 `+=` 累加，物理系统每步 Clear）✅
- M_A 注入无双重缩放；SDF 版本 1.6→1.10 提升 ✅
- 波浪场 ECM 弱引用自愈（缺引用每 30 帧重试、静水降级）✅
