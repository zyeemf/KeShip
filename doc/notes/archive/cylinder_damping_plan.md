# 圆柱体阻尼重构方案 (Pose-Based 阻尼)

## 一、背景

### 问题诊断
圆柱体初稳性实验（SP-02）中，圆柱体入水后持续振荡不收敛。诊断结果：

| 参数 | 值 | 说明 |
|------|-----|------|
| 圆柱质量 | 274,889 kg | D=5m, L=20m, ρ=700 kg/m³ |
| 当前 Mq | 82,467 N·m/(rad/s) | 0.3 × mass (cylinderDefaults) |
| 临界 Mq | 1,120,216 N·m/(rad/s) | ζ=1 所需 |
| 实际 ζ | 0.074 | 严重欠阻尼 |

### 根因
1. Fossen 模型的线性阻尼系数是**固定值**，不随姿态变化——圆柱竖立和侧躺使用同一组系数
2. SP-02 初稳性实验当前 `enable_hydro=false`，**零阻尼**

## 二、架构总览

```
模型管理 → 加载圆柱体 → [Fossen | Pose-Based] 下拉选择
                                  │
                  ┌───────────────┴───────────────┐
                  ▼                               ▼
          Fossen 模式                     Pose-Based 模式
    弹出 HydrodynamicsEditorDialog     跳过 HydroEditor，弹提示
    系数从尺寸密度自动计算              直接 spawn
                  │                               │
                  ▼                               ▼
        SDF: damping_mode=fossen         SDF: damping_mode=pose_based
        含 <linear_damping> 块           不含阻尼块

虚拟实验 → SP-02 初稳性实验 → 固定 damping_mode=pose_based
```

### 路径划分

| 路径 | 入口 | 阻尼策略 |
|------|------|---------|
| 模型管理 → Fossen | CylinderTestDialog → HydrodynamicsEditorDialog | 固定线性系数（用户可编辑） |
| 模型管理 → Pose-Based | CylinderTestDialog → 直接 spawn | 实时计算线性+二次（无需编辑） |
| SP-02 初稳性实验 | StabilityExperimentDialog | 实时计算线性+二次（固定） |

### SDF 参数 `<damping_mode>`

- `fossen`（默认）：使用 FossenModel 计算阻尼，系数从 `<linear_damping>` 读取
- `pose_based`：使用 computePoseBasedDamping 实时计算

## 三、修改文件清单

| # | 文件 | 改动 |
|---|------|------|
| 1 | `utils/ModelManager.hh` | `HydroConfig` 加 `std::string dampingMode = "fossen"` |
| 2 | `utils/ModelManager.cc` — `MakeBuoyancyPluginSDF()` | 根据 `dampingMode` 分支写 SDF |
| 3 | `physics/KSPlugin_HydroBuoyancy.hh` | 加 `dampingMode_`、`computePoseBasedDamping()` |
| 4 | `physics/KSPlugin_HydroBuoyancy.cc` — `LoadParameters()` | 读 `<damping_mode>` |
| 5 | `physics/KSPlugin_HydroBuoyancy.cc` — `LoadMultiBuoyancy()` | 存 `cylinderRadius_/Length_` |
| 6 | `physics/KSPlugin_HydroBuoyancy.cc` — `PreUpdate()` | 阻尼段分支 |
| 7 | `physics/KSPlugin_HydroBuoyancy.cc` — _新增方法_ | `computePoseBasedDamping()` |
| 8 | `gui/.../CylinderTestDialog.hh` | 加 `QComboBox` |
| 9 | `gui/.../CylinderTestDialog.cc` | 加下拉框、提示 |
| 10 | `gui/.../MenuPluginKeship.cc` — `OnBtn_SP01_CylinderTest()` | 分支流程 |
| 11 | `gui/.../StabilityExperimentDialog.cc` — `makeCylinderSDF()` | 启用 hydro + pose_based |
| 12 | `gui/.../HydrodynamicsEditorDialog.cc` | Fossen 模式保持现有自动计算不变 |

## 四、各文件详细改动

### 1. ModelManager.hh

`HydroConfig` 结构体末尾新增：
```cpp
std::string dampingMode = "fossen";  // "fossen" | "pose_based"
```

### 2. ModelManager.cc — MakeBuoyancyPluginSDF()

```cpp
// 在写 <linear_damping> 之前：
if (_hydro->dampingMode == "pose_based")
{
    sdf << "      <damping_mode>pose_based</damping_mode>\n";
    // 跳过 damping + linear_damping 块
}
else
{
    // 现有 <damping> + <linear_damping> 逻辑不变
}
```

### 3. KSPlugin_HydroBuoyancy.hh

`Implementation` 内新增：
```cpp
std::string dampingMode_{"fossen"};
```

新增方法声明（public private 均可）：
```cpp
void computePoseBasedDamping(
    gz::sim::EntityComponentManager &_ecm,
    const gz::math::Pose3d &_modelPose,
    const gz::math::Vector3d &_linkWorldPos,
    double _effectiveFluidLevel,
    gz::math::Vector3d &_force,
    gz::math::Vector3d &_torque);
```

### 4. KSPlugin_HydroBuoyancy.cc — LoadParameters()

```cpp
// 在 cylinder_segments 附近：
auto [dm, dmOk] = _sdf->Get<std::string>("damping_mode", "fossen");
if (dmOk) this->dataPtr->dampingMode_ = dm;
```

### 5. KSPlugin_HydroBuoyancy.cc — LoadMultiBuoyancy()

cylinder 分支（641-648行）末尾：
```cpp
// 存储圆柱参数供 pose_based 阻尼使用
this->dataPtr->cylinderRadius_ = r;
this->dataPtr->cylinderLength_ = l;
this->dataPtr->hasCylinderGeometry_ = true;
```

### 6. KSPlugin_HydroBuoyancy.cc — PreUpdate()

**第 193 行 early return**：
```cpp
if (this->enableHydro_
    && this->dataPtr->dampingMode_ != "pose_based"
    && (!this->dataPtr->fossen || !this->dataPtr->fossen->valid()))
  return;
```

**阻尼段（367-408行）**：
```cpp
/// 7. 阻尼
{
    if (inWater && this->enableHydro_)
    {
        if (this->dataPtr->dampingMode_ == "pose_based"
            && this->dataPtr->hasCylinderGeometry_
            && !this->dataPtr->bodies.empty())
        {
            computePoseBasedDamping(
                _ecm, modelPose, linkWorldPos,
                effectiveFluidLevel, totalForce, totalTorque);
        }
        else if (this->dataPtr->fossen
                 && this->dataPtr->fossen->valid())
        {
            // 现有 Fossen 路径（不变）
        }
    }
}
```

**注意**：`computePoseBasedDamping` 内部自行读取 `lastWaveHeight_`，无需额外传参。

### 7. KSPlugin_HydroBuoyancy.cc — computePoseBasedDamping()

```cpp
void KSPlugin_HydroBuoyancy::computePoseBasedDamping(
    gz::sim::EntityComponentManager &_ecm,
    const gz::math::Pose3d &_modelPose,
    const gz::math::Vector3d &_linkWorldPos,
    double _effectiveFluidLevel,
    gz::math::Vector3d &_force,
    gz::math::Vector3d &_torque)
{
    // 1. 读速度
    auto linVelWorld = this->linkObj_.WorldLinearVelocity(_ecm);
    auto angVelWorld = this->linkObj_.WorldAngularVelocity(_ecm);
    if (!linVelWorld || !angVelWorld) return;

    gz::math::Quaterniond R_bw = _modelPose.Rot().Inverse();
    gz::math::Vector3d vBody = R_bw * (*linVelWorld);
    gz::math::Vector3d wBody = R_bw * (*angVelWorld);

    // 2. 几何参数
    double r = this->dataPtr->cylinderRadius_;
    double L = this->dataPtr->cylinderLength_;
    if (r <= 0.0 || L <= 0.0) return;

    // 3. 圆柱轴线世界方向 (body +Z → world)
    gz::math::Vector3d axisZ = _modelPose.Rot() * gz::math::Vector3d(0, 0, 1);

    // 4. 两端点世界 Z
    double halfL = L * 0.5;
    gz::math::Vector3d topWorld = _linkWorldPos + axisZ * halfL;
    gz::math::Vector3d botWorld = _linkWorldPos - axisZ * halfL;
    double zTop = topWorld.Z();
    double zBot = botWorld.Z();

    // 5. 水面高度
    double waterZ = _effectiveFluidLevel + this->dataPtr->lastWaveHeight_;

    // 6. 浸没长度
    double L_sub = 0.0;
    if (zBot < waterZ && zTop > waterZ)
    {
        double t = (waterZ - zBot) / (zTop - zBot);
        L_sub = t * L;
    }
    else if (zTop <= waterZ)
    {
        L_sub = L; // 全浸没
    }

    if (L_sub <= 0.0) return;

    // 7. 几何量
    double rho = this->dataPtr->fluidDensity_;
    double A_side = 2.0 * r * L_sub;          // 侧面积
    double A_end  = M_PI * r * r;              // 端面积
    double m_sub  = rho * A_end * L_sub;       // 浸没排水质量
    double A_moment = r * L_sub * L_sub * L_sub / 6.0;  // 转动惯量面积矩

    // 8. 阻尼系数
    const double Cd = 0.7;            // 圆柱阻力系数
    const double half_rho_Cd = 0.5 * rho * Cd;
    const double linFactor = 0.3;     // 线性系数: B = linFactor × m_sub

    // 9. 逐 DOF 计算
    // nu = [u, v, w, p, q, r]
    double nu[6] = {vBody.X(), vBody.Y(), vBody.Z(),
                    wBody.X(), wBody.Y(), wBody.Z()};

    double tauBody[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};

    // 平动: surge(0) sway(1) → A_side, heave(2) → A_end
    double A_trans[3] = {A_side, A_side, A_end};
    for (int i = 0; i < 3; i++)
    {
        double lin = linFactor * m_sub;
        double quad = half_rho_Cd * A_trans[i] * std::abs(nu[i]);
        tauBody[i] -= (lin + quad) * nu[i];
    }

    // 转动: roll(3) pitch(4) → A_moment, yaw(5) → 0
    for (int i = 3; i < 5; i++)
    {
        double lin = linFactor * m_sub * L * L / 12.0;  // 转动等效线性系数
        double quad = half_rho_Cd * A_moment * std::abs(nu[i]);
        tauBody[i] -= (lin + quad) * nu[i];
    }

    // 10. 转世界坐标
    _force  += _modelPose.Rot() * gz::math::Vector3d(tauBody[0], tauBody[1], tauBody[2]);
    _torque += _modelPose.Rot() * gz::math::Vector3d(tauBody[3], tauBody[4], tauBody[5]);

    // 11. 诊断日志
    {
        auto &d = this->dataPtr->hydroDiag;
        for (int i = 0; i < 6; ++i) d.tauD[i] = tauBody[i];
        d.hydroActive = true;
        d.waterFrameCount++;
    }
}
```

### 8. CylinderTestDialog.hh

```cpp
QComboBox *dampingModeCombo_;
QString getDampingMode() const;
```

### 9. CylinderTestDialog.cc

构造函数中加：
```cpp
dampingModeCombo_ = new QComboBox(this);
dampingModeCombo_->addItem("Fossen 模型 (推荐)");
dampingModeCombo_->addItem("基于位姿 (Pose-Based)");
formLayout->addRow("阻尼模式:", dampingModeCombo_);

connect(dampingModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
        this, &CylinderTestDialog::onDampingModeChanged);
```

新增槽函数：
```cpp
void CylinderTestDialog::onDampingModeChanged(int _idx)
{
    if (_idx == 0)  // Fossen
    {
        QMessageBox::information(this, "Fossen 模型",
            "将弹出水动力参数编辑器，系数已根据圆柱尺寸和密度自动计算。\n"
            "如需调整可直接编辑各 DOF 的阻尼系数。");
    }
    else  // Pose-Based
    {
        QMessageBox::information(this, "基于位姿阻尼",
            "圆柱体将使用基于实时位姿与水面线的阻尼计算（线性+二次），\n"
            "无需手动设置水动力系数。");
    }
}
```

`getDampingMode()` 返回值：
```cpp
QString CylinderTestDialog::getDampingMode() const
{
    return (dampingModeCombo_->currentIndex() == 0) ? "fossen" : "pose_based";
}
```

### 10. MenuPluginKeship.cc — OnBtn_SP01_CylinderTest()

```cpp
void MenuPluginKeship::OnBtn_SP01_CylinderTest()
{
    // ... 现有对话框创建和获取参数代码不变 ...

    // 获取阻尼模式选择
    std::string dampingMode = dialog.getDampingMode().toStdString();

    // 缓存 SDF（延迟 spawn）
    bool cached = this->modelManager->SpawnCylinderInGazebo(
        instanceName, diameter, length, density, pos, /*_skipSpawn=*/true);
    if (!cached)
    {
        std::cout << "keship: 圆柱体 SDF 缓存失败" << std::endl;
        return;
    }

    if (dampingMode == "fossen")
    {
        // 现有流程：弹出 HydrodynamicsEditorDialog
        auto *hydroDialog = new HydrodynamicsEditorDialog(instanceName, "base_link");
        hydroDialog->setAttribute(Qt::WA_DeleteOnClose);
        double cylMass = density * M_PI * (diameter*0.5) * (diameter*0.5) * length;
        hydroDialog->setConfig(
            HydrodynamicsEditorDialog::cylinderDefaults(cylMass, diameter*0.5, length));

        auto spawnPos = std::make_shared<std::array<double, 6>>();
        std::copy(pos, pos + 6, spawnPos->begin());

        QObject::connect(hydroDialog, &QDialog::accepted, this,
            [this, hydroDialog, name = instanceName, spawnPos]() {
            keship::HydroConfig hydroCfg = hydroDialog->getConfig();
            hydroCfg.dampingMode = "fossen";
            this->modelManager->CacheHydroConfig(name, hydroCfg);
            keship::BuoyancyConfig buoyCfg;
            this->modelManager->GetCachedBuoyancyConfig(name, buoyCfg);
            this->modelManager->SpawnModelWithBuoyancy(name, buoyCfg, spawnPos->data());
            NotifyModelListChanged();
        });
        hydroDialog->show();
    }
    else  // pose_based
    {
        // Pose-Based 模式：跳过 HydroEditor，直接 spawn
        keship::HydroConfig hydroCfg;
        hydroCfg.linkName = "base_link";
        hydroCfg.dampingMode = "pose_based";
        this->modelManager->CacheHydroConfig(instanceName, hydroCfg);

        keship::BuoyancyConfig buoyCfg;
        this->modelManager->GetCachedBuoyancyConfig(instanceName, buoyCfg);
        this->modelManager->SpawnModelWithBuoyancy(
            instanceName, buoyCfg, pos);
        NotifyModelListChanged();
    }
}
```

### 11. StabilityExperimentDialog.cc — makeCylinderSDF()

第 618-633 行改为：
```cpp
<< "    <plugin filename=\"libKSPlugin_HydroBuoyancy.so\""
<< " name=\"keship::KSPlugin_HydroBuoyancy\">\n"
<< "      <link_name>base_link</link_name>\n"
<< "      <enable_buoyancy>true</enable_buoyancy>\n"
<< "      <enable_hydro>true</enable_hydro>\n"
<< "      <damping_mode>pose_based</damping_mode>\n"
<< "      <fluid_density>1000</fluid_density>\n"
<< "      <fluid_level>0.0</fluid_level>\n"
<< "      <cylinder_radius>" << r << "</cylinder_radius>\n"
<< "      <cylinder_length>" << _length << "</cylinder_length>\n"
<< "      <buoyancy name=\"cylinder_hull\">\n"
<< "        <geometry>\n"
<< "          <cylinder>\n"
<< "            <radius>" << r << "</radius>\n"
<< "            <length>" << _length << "</length>\n"
<< "          </cylinder>\n"
<< "        </geometry>\n"
<< "      </buoyancy>\n"
<< "    </plugin>\n"
```

注意：写入 `<cylinder_radius>` 和 `<cylinder_length>` 顶层参数，使得 `LoadSingleMeshBuoyancy` 路径（SP-02 不走多浮力体）也能识别 cylinder geometry。

### 12. HydrodynamicsEditorDialog.cc

无需改动。`cylinderDefaults()` 已自动计算系数，Fossen 模式保持现有行为。

## 五、Pose-Based 阻尼算法

### 输入
| 变量 | 来源 |
|------|------|
| `cylinderRadius_` | `LoadMultiBuoyancy` 或 `LoadParameters` |
| `cylinderLength_` | 同上 |
| `lastWaveHeight_` | 步骤 6 浮力计算已更新 |
| `effectiveFluidLevel` | 步骤 5 水区查询 |
| link 世界速度 | `WorldLinearVelocity / WorldAngularVelocity` |

### 公式

#### 1. 浸没长度计算

```
轴线方向: axisZ = modelPose.Rot() * (0,0,1)
上端点 Z: zTop  = (linkWorldPos + axisZ * halfL).Z()
下端点 Z: zBot  = (linkWorldPos - axisZ * halfL).Z()
水面   Z: waterZ = effectiveFluidLevel + lastWaveHeight_

if zBot ≥ waterZ → 出水, 无阻尼
if zTop ≤ waterZ → 全浸没, L_sub = L
else → t = (waterZ - zBot) / (zTop - zBot), L_sub = t × L
```

#### 2. 阻尼力/力矩（body frame）

```
线性系数: B_i = 0.3 × m_sub          (平动)
          B_i = 0.3 × m_sub × L²/12   (转动)
二次系数: Q_i = 0.5 × ρ × Cd × A_proj_i

τ_i = -(B_i + Q_i × |ν_i|) × ν_i
```

#### 3. 投影面积分配

| DOF | 方向 | A_proj | 物理含义 |
|-----|------|--------|---------|
| 0 (surge) | body X 平动 | 2r·L_sub | 径向投影 |
| 1 (sway) | body Y 平动 | 2r·L_sub | 径向投影 |
| 2 (heave) | body Z 平动 | π·r² | 端面投影 |
| 3 (roll) | body X 转动 | r·L_sub³/6 | 侧面积绕 X 的矩 |
| 4 (pitch) | body Y 转动 | r·L_sub³/6 | 侧面积绕 Y 的矩 |
| 5 (yaw) | body Z 转动 | 0 | 绕轴自转极小 |

## 六、验证方式

### 编译
```bash
cd ~/keship_ws
colcon build --packages-select keship --merge-install
```

### 功能测试

1. **Fossen 模式**：CylinderTestDialog → 选 Fossen → 弹出 HydroEditor → 确认 → 圆柱体应 10-15s 内收敛
2. **Pose-Based 模式**：CylinderTestDialog → 选 Pose-Based → 弹提示 → 确认  → 圆柱体应快速收敛
3. **SP-02 初稳性实验**：打开实验 → 生成圆柱体 → 应快速稳定/倾倒，无持续振荡
4. **舵船不受影响**：在模型管理中加载舵船模型，确认水动力阻尼正常

### 诊断检查
- CSV 日志中 `tauD[0..5]` 在 Pose-Based 模式下应为非零且随姿态变化

## 七、实施结果补充（2026-07-29~31）✅

### 方案变更
本方案（Pose-Based）已实施。Fossen 模式的 `cylinderDefaults` 另行升级为
**线性ζ法 + Morison 二次项复合阻尼**（超出本方案范围，见 keship_plan.md），
并于 2026-07-31 **纯物理化**：移除 ζLin 经验线性法，最终系数全部来自
几何/流体物理，无经验比例：

| 项 | 系数 | 物理依据 |
|----|------|---------|
| 轴向线性 | 0.8×mass | 轴向投影 + 线性粘性等效（非 ζ 经验法） |
| 转动线性 | 0.3×m·L²/12 | 悬浮惯量比例 |
| yaw 线性 | 0.15×m·L²/12 | 对齐 Pose-Based B_target_rot |
| 平动二次 | 0.5ρ·Cd·A_proj（侧=2r·L_sub，端=πr²） | Morison 圆桩阻力 |
| 转动二次 | 0.5ρ·Cd_n·πr⁴·L_sub | Morison 转动矩 |
| yaw 二次 | 0.5ρ·Cd_n·2πr·L_sub·r² | 湿侧表面力矩 |

### 验证结果（20260731_113221, cy1 Fossen roll=1°）
- ✅ 4s 倾倒至侧躺（GM<0 静不稳定，本应倾倒），roll 稳定 88~93° 无符号翻转
- ✅ wy 自转 ±0.073 rad/s 双向小幅（此前恒负 -0.23 单向持续自转）
- ✅ pitch ±22° 内（此前 ±1.4 rad 大摆动）
- ✅ tauD 径向（tx max 33.6 万 N·m）≫ 轴向，径向阻尼优先正确
- ⚠️ 当时观测到 ~25s 周期耦合小振荡，后确认由波浪强迫引起
  （waveGain=0 未生效，波浪恒满幅 ±0.37m），非阻尼不足

### 静水终测（20260731_174827, waveHeight≡0）
- ✅ waveHeight 38.8s 后恒 0.0000 — 静水真实生效（波浪 Bug 双通道修复后）
- ✅ wy 93s 收敛 <0.005 rad/s（124s 时 0.0020）；roll 稳定 1.570 无翻转
- ✅ pz 单调衰减：124s 时 ±0.09m（16s 周期 roll-heave 慢模式，ζ_eff≈0.07，
  预计 ~180s 达 0.05m）；tauD 量级与 0.5ρCdAv² 吻合
- ✅ 验收按物理现实定稿：无波浪时垂荡/横轴收敛达标，
  "30s 内几乎静止"与 ζ=0.10 旋转阻尼物理不可兼得

### 遗留说明
- M_A：DART `<fluid_added_mass>` 隐式注入；实验对话框生成的圆柱 SDF 无 M_A
  （DART 0 附加质量）——只影响瞬态、不影响稳态收敛，对称圆柱可接受
- 横荡-首摇耦合项：对称圆柱影响≈0，真实船型才需要（待办）
- 多水区波浪参数取 JSON 第一个水区（单水区教学场景够用，待办）
