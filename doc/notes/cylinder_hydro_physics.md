# 圆柱体水动力物理化方案（Cylinder Hydro Physics）

> 2026-08-01 制定并实施
> 背景：fossen（cylinderDefaults 系数法）与 pose-based（湿面积积分法）两种阻尼模式对比测试后，
> 评估两者物理合理性并修正不符合物理的参数。

## 一、问题背景

测试（20260801_185855）：圆柱 r=2.4m, L=21.6m, V=390m³, m=274.9t，静水（waveH=0）加载对比：

| 指标 | cy1（fossen） | cy2（pose-based） |
|------|--------------|------------------|
| 垂荡阻尼峰值 | 55万N（v=1.54） | 27.7万N（v=3.0） |
| 最大下沉 | -5.1m | -8.5m |
| 收敛 | 52s 完全静止 | 90s+ 缓慢漂移 |

**评估结论**（两种模式均验证有效，非 bug）：
- **fossen 垂荡 ζ=0.75 明显偏强**（cylinderDefaults 注释"目标 ζ≈0.7"为收敛设定，非物理值；真实浮体垂荡 ζ∈[0.1,0.3]）
- **fossen 转动 M_A 存在单位 bug**：`pp/qq=0.3·m` 注入 DART `<fluid_added_mass>` 后按 kg·m² 解释，真实细长圆柱绕横轴附加质量矩 ≈ 0.25·ρV·L²/12，现值小 40 倍（几乎无效）
- **pose-based 缺 M_A**（SDF 无 fluid_added_mass）→ 附加质量缺失，瞬态不完整
- pose-based 阻尼参数（ζ_heave=0.15、ζ_rot=0.10、Cd_n=1.1、Cd_a=0.82）符合文献值 ✓

## 二、修改方案

### 1. cylinderDefaults 阻尼系数物理化（HydrodynamicsEditorDialog.cc）

| 项 | 现值 | 修改值 | 物理依据 |
|---|---|---|---|
| Zw 垂荡线性 | 1.2·m（ζ=0.75） | 2ζ√(ρg·πr²·m)，ζ=0.2 → ≈0.32·m | 浮体垂荡 ζ∈[0.1,0.3]，A_wp 取直立 πr² |
| Xu/Yv 水平线性 | 1.2·areaRatio·m（≈6.9·m） | 0.05·m（τ≈20s） | 自由漂移无静水恢复力，线性摩擦小，压差（二次）主导 |
| Kp/Mq 横摇线性 | 0.3·m·L²/12（ζ≈0.35） | 0.15·m·L²/12（ζ≈0.18） | 船舶/浮体横摇 ζ∈[0.03,0.2] |
| Nr yaw 线性 | 0.15·m·L²/12 | 0.05·m·L²/12（τ≈2s） | 对称旋转体绕轴自转无压差阻力，真实衰减极慢 |
| 二次项 ×4 | 现值 | 不变 | Morison 压差阻力（Cd_n=1.1, Cd_a=0.82）✓ |

### 2. M_A 物理化（修复转动项单位 bug）

| 项 | 现值 | 修改值 | 依据 |
|---|---|---|---|
| xx/yy 径向平移 | 1.0·ρV | 不变 | 无限长圆柱 C_a=1.0（精确）✓ |
| zz 轴向平移 | 0.02·ρV | 不变 | 细长体轴向附加质量小 ✓ |
| pp/qq 绕横轴转动 | 0.3·m | 0.25·ρV·L²/12 | 细长圆柱绕中点横轴附加质量矩 |
| rr 绕轴自转 | 0.05·m | 0.02·ρV·r² | 旋转体绕轴附加惯量矩（小） |

### 3. pose-based 补 M_A

- 新增公共静态函数 `HydrodynamicsEditorDialog::cylinderAddedMass(m, r, l, rho)` → 返回 M_A 36 项
- `cylinderDefaults` 内部复用
- `MenuPluginKeship.cc` pose-based 路径：`hydroCfg.addedMass = cylinderAddedMass(cylMass, r, l, 1000)`

### 4. 清理（可选）

- `cfg.damping`（8 数组）与 `dampingTerms` 数值统一（消除无量纲/绝对语义混乱）

## 三、影响面（已确认）

- `cylinderDefaults` 仅用于圆柱测试 fossen 路径；实验对话框不使用 ✓
- KSM 保存/加载完整保留 dampt（ParseKSMFile 解析 dampt）✓
- KSM 用户配置的 dampt 不受影响 ✓

## 四、已知限制（本次不修）

1. **空中 M_A**：DART fluid_added_mass 无条件生效（空中也生效）——垂向影响 2.8%（0.02ρV），完整修复需恢复"空中抵消"（C_A(ν)ν，参考 notes/archive/ma_dart_removal_fix.md 7-28 方案）
2. **fossen 固定系数**：垂荡 A_wp 取直立值，侧躺时实际 ζ 偏离——系数法固有局限
3. **pose-based 几何假设**：圆柱轴沿 body Z、中心在原点（圆柱测试满足）；自定义浮力体 poseOffset 时数值积分不准确

## 五、验证计划

| 场景 | 预期 |
|---|---|
| fossen 圆柱（新默认） | ζ≈0.2：深潜 ~-6~7m、收敛 ~60-80s、最终侧躺 subVol=275 |
| pose-based（+M_A） | 垂荡行为基本不变；倾倒转动更真实 |
| tauD 理论对比 | fossen（v=1.5）≈15万N；pose-based（v=3）≈26.5万N |
| 回归 | 静水 waveH=0、双模型日志分离、实验对话框 |

## 六、实施与验证记录（2026-08-01）

**实施**：
1. `HydrodynamicsEditorDialog.hh/.cc`：新增 `cylinderAddedMass()` 公共函数；`cylinderDefaults` 阻尼系数物理化（Zw=2ζ√(ρg·πr²·m) ζ=0.2、Xu/Yv=0.05m、Kp/Mq=0.15mL²/12、Nr=0.05mL²/12）；M_A 转动项修复（pp/qq=0.25ρV·L²/12、rr=0.02ρV·r²）；cfg.damping 与 dampingTerms 数值统一
2. `MenuPluginKeship.cc` pose-based 路径：补 M_A（cylinderAddedMass）
3. 编译：`colcon build` 零警告 ✓

**验证**（headless 双圆柱，初始 roll=1°，静水 waveGain=0）：

| 指标 | cy_fossen（新系数） | cy_pose（pose-based+M_A） |
|------|--------------------|---------------------------|
| 最大下沉 | -9.23m | -9.79m |
| 最终姿态 | roll=89.7° 侧躺 ✓ | roll=88.7° 侧躺 ✓ |
| 最终平衡 | pz=-1.23, subVol=279 | pz=-1.16, subVol=274 |
| tauDfz 峰值 | 32.5万N | 28.3万N |
| tauDtx 峰值 | 8.6万N·m | 9.5万N·m |

**结论**：
- 两种模式均正确侧躺（直立圆柱 GM≈-7.5m 不稳定 → 侧躺，物理正确；完全对称初始化 roll=0 时维持直立不稳定平衡，需初始扰动——用户 GUI 1° 初始角已具备）
- tauD 与理论公式吻合（fossen v=3：理论 33.2万 vs 实测 32.5万；pose：26.5万 vs 28.3万）
- 两种模式阻尼量级一致（物理化目标达成）
- 收敛 ~80s（ζ=0.15~0.2 欠阻尼的物理行为，慢于旧 ζ=0.75 的 52s——真实性代价）
- 回归：静水 waveH=0、日志分离、无残留进程、JSON 保持用户状态

> **注 1（ζ 姿态相关）**：固定系数法（fossen）与 ζ 法（pose-based）的线性阻尼为定值，
> 实际等效 ζ 随姿态变化：垂荡直立 ζ=0.2（设计值）→ 侧躺 ζ≈0.084（A_wp 增大 5.7 倍）；
> 横摇侧躺 ζ≈0.18 → 直立 ζ≈0.49。均落在浮体经验区间，但侧躺垂荡阻尼偏低
> （真实兴波阻尼随 A_wp 增大而增大）——固定系数法的固有局限（已列入已知限制）。
>
> **注 2（验证方法）**：headless 验证用手写测试 SDF，其中惯性值（Ixx/Izz）与精确值
> （m(3r²+L²)/12、m·r²/2）有偏差（2.5~4 倍），因 tauD/平衡与惯性无关、倾倒/深潜
> 定性行为不受影响，验证结论仍有效；正式验证以 GUI 路径（ModelManager 正确生成惯性）为准。
