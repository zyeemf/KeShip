# 数学船型系统重构方案

> KeShip 项目 | 2026-06-01 | 设计阶段

---

## 〇、现有问题回顾

SP-03 当前有 3 个连锁 Bug，且根因分析表明需要整体重构而非局部修补：

| Bug | 现象 | 根因 |
|-----|------|------|
| B1 | DAE 三角绕序导致 subVol 为负 | 船舶 NED 坐标(z↓)生成网格，变换到 Gazebo Z_UP(z↑)时绕序未正确翻转 |
| B2 | DAE 右舷顶点丢失导致侧翻 | Assimp 加载 COLLADA 时顶点数据异常，具体环节未定位 |
| B3 | DART 崩溃 | SDF collision 用了 `<mesh>`，DART 不支持 |

**核心结论**：三个 Bug 的共同根因是 **"在船舶 NED 坐标中生成网格，再变换到 Gazebo Z_UP"**。每次变换引入绕序/顶点/格式问题。解决方法是 **直接在 Gazebo Z_UP 坐标中生成水密网格，消除所有坐标变换**。

---

## 一、核心设计原则

### 1.1 消除坐标变换

```
旧方案: NED (z↓) → writeDAE → ConvertToZUp(z→-z)+绕序补偿 → Bug
新方案: Gazebo Z_UP (z↑) → 直接生成水密网格 → 无变换
```

### 1.2 三层几何体独立路径

| 层 | 格式 | 加载路径 | 约束 |
|----|------|---------|------|
| 视觉 (visual) | OBJ | Ogre2 直接加载 | 高分辨率 (Nx=40,Nz=20) |
| 碰撞 (collision) | Box (SDF 内置) | DART | **禁止 mesh** — DART 不支持 |
| 浮力 (buoyancy) | OBJ | Assimp → MeshUtils → Buoyancy | 低分辨率 (Nx=20,Nz=10)，独立于 collision |

### 1.3 统一接口，分阶段交付

四个需求共享同一数据源（MathHullGenerator），设计统一接口后分 Phase 实现：

```
              MathHullGenerator
         (水密网格 + 静水力 + 型值)
           /     |      \      \
       3D模型 型线图  排水量 学生编程
      (需求1) (需求2)(需求3)(需求4)
```

---

## 二、数学船型参数体系

> 用户要求：Wigley 含 4 种变体、Lewis U/V、后续可扩展

### 2.1 通用数学模型

所有数学船型的船体曲面可统一表达为：

$$y(x, z) = \pm \frac{B}{2} \cdot W(x/L) \cdot S(z/D)$$

其中：
- **$W(\xi)$**：水线面形状函数，$\xi = x/L \in [0, 1]$，$W(0)=W(1)=0$
- **$S(\zeta)$**：横剖面形状函数，$\zeta = z/D \in [0, 1]$（Gazebo Z_UP: $\zeta=0$=水线面, $\zeta=1$=龙骨）

Gazebo Z_UP 坐标系中：
- **x ∈ [0, L]**：艏向
- **y ∈ [-B/2, B/2]**：右舷正
- **z ∈ [-D, 0]**：水线面 z=0，龙骨 z=-D

### 2.2 船型族定义

#### Wigley 族 — 参数化抛物线船型

| 编号 | 变体名称 | $W(\xi)$ | $S(\zeta)$ | 默认参数 | 特征 |
|------|---------|----------|-----------|---------|------|
| W-1 | **经典 Wigley** | $1 - \xi^p$ | $1 - \zeta^q$ | p=2, q=2 | 标准抛物线，两端尖，舭部圆 |
| W-2 | **平底 Wigley** | $1 - \xi^2$ | $1 - \zeta^q$ | p=2, q=4 | 舭部更圆，底更平 |
| W-3 | **尖舭 Wigley** | $1 - \xi^p$ | $1 - \zeta^q$ | p=2, q=1.5 | 舭部较尖，类似 V 形 |
| W-4 | **瘦削 Wigley** | $1 - \xi^p$ | $1 - \zeta^2$ | p=3, q=2 | 首尾更尖瘦，水线面系数更小 |

**Wigley 族自由参数**：
```
p (水线面指数): 控制首尾尖削程度, p>1, 默认2.0
q (横剖面指数): 控制舭部形状, q>1 圆舭, q<2 尖舭, 默认2.0
```

#### Lewis 族 — 保角映射船型

| 编号 | 变体名称 | $W(\xi)$ | $S(\zeta)$ | 默认参数 | 特征 |
|------|---------|----------|-----------|---------|------|
| L-U | **Lewis U** | $1 - \xi^2$ | Lewis映射 σ=0.85 | σ=0.85 | U 形剖面，较丰满 |
| L-V | **Lewis V** | $1 - \xi^2$ | Lewis映射 σ=0.72 | σ=0.72 | V 形剖面，舭部尖 |

**Lewis 族自由参数**：
```
sigma (剖面面积系数): 0.5~1.0, σ越大剖面越U, σ越小越V
```

#### 扩展预留

| 族 | 说明 | 参数 |
|----|------|------|
| Series 60 | ITTC 标准船型，查表插值 | 方形系数 Cb |
| KCS | KRISO 集装箱船 | 固定型值表 |
| Custom | 用户自定义型值表 | CSV 型值文件 |

### 2.3 统一参数结构体

```cpp
// ========== 水线面形状函数类型 ==========
enum class WaterlineType {
    PARABOLIC,    // W(ξ) = 1 - ξ^p, p>0
    TRANSOM,      // W(ξ) = 1 - ξ^p for ξ<ξ_transom, then flat
    TABULATED     // 查表插值
};

// ========== 横剖面形状函数类型 ==========
enum class SectionType {
    PARABOLIC,    // S(ζ) = 1 - ζ^q, q>0
    LEWIS_U,      // Lewis 保角映射 U 形
    LEWIS_V,      // Lewis 保角映射 V 形
    TABULATED     // 查表插值
};

// ========== 通用数学船型规格 ==========
struct MathHullSpec {
    // --- 主尺度 ---
    double L = 6.0;     // 船长 (m), x 方向
    double B = 1.5;     // 型宽 (m), y 方向
    double D = 1.0;     // 型深 (m), z 方向 (水线面→龙骨)
    double rho = 500.0; // 船体密度 (kg/m³)

    // --- 水线面 ---
    WaterlineType wlType = WaterlineType::PARABOLIC;
    double wlExponent = 2.0;    // p, 抛物线指数 (经典=2.0)
    double transomX = 0.0;      // 方尾位置 (占L的比例, 0=无方尾)

    // --- 横剖面 ---
    SectionType secType = SectionType::PARABOLIC;
    double secExponent = 2.0;   // q, 抛物线指数 (经典=2.0, 圆舭>2, 尖舭<2)
    double lewisSigma = 0.80;   // Lewis σ (仅 Lewis 族使用)

    // --- 网格分辨率 ---
    int visNx = 40, visNz = 20;  // 视觉网格
    int buoyNx = 20, buoyNz = 10; // 浮力网格

    // --- 便捷工厂方法 ---
    static MathHullSpec WigleyClassic(double L, double B, double D, double rho);
    static MathHullSpec WigleyFlat(double L, double B, double D, double rho);     // 平底
    static MathHullSpec WigleySharp(double L, double B, double D, double rho);    // 尖舭
    static MathHullSpec WigleyFine(double L, double B, double D, double rho);     // 瘦削
    static MathHullSpec LewisU(double L, double B, double D, double rho, double sigma = 0.85);
    static MathHullSpec LewisV(double L, double B, double D, double rho, double sigma = 0.72);
};
```

**工厂方法默认参数**：
```cpp
// W-1 经典: p=2, q=2 → 对称抛物线
MathHullSpec::WigleyClassic(L,B,D,rho) → {wlExponent=2.0, secExponent=2.0}

// W-2 平底: p=2, q=4 → 舭部更圆
MathHullSpec::WigleyFlat(L,B,D,rho)    → {wlExponent=2.0, secExponent=4.0}

// W-3 尖舭: p=2, q=1.5 → 舭部较尖
MathHullSpec::WigleySharp(L,B,D,rho)   → {wlExponent=2.0, secExponent=1.5}

// W-4 瘦削: p=3, q=2 → 首尾更尖瘦
MathHullSpec::WigleyFine(L,B,D,rho)    → {wlExponent=3.0, secExponent=2.0}
```

---

## 三、ShipHullGenerator 统一接口设计

### 3.1 类结构

```cpp
class ShipHullGenerator {
public:
    // ========== 构造 ==========
    // 构造即完成全部计算（网格生成 + 静水力）
    explicit ShipHullGenerator(const MathHullSpec& spec);

    // ========== 网格输出 ==========
    const MeshDataSimple& visualMesh()   const;  // 高分辨率网格
    const MeshDataSimple& buoyancyMesh() const;  // 低分辨率网格 (已做水密封盖)

    // ========== 文件输出 ==========
    void writeVisualOBJ(const std::string& path) const;
    void writeBuoyancyOBJ(const std::string& path) const;

    // ========== 静水力参数 ==========
    // 第①组：设计吃水参数 (d = (rho/rho_w) * D)
    double designDraft()     const;  // 设计吃水
    double designVolume()    const;  // 设计吃水排水体积
    double designKB()        const;  // 浮心垂向位置
    double designBM()        const;  // 稳心半径
    double designGM()        const;  // 初稳性高度
    double designAwp()       const;  // 水线面面积
    double designCwp()       const;  // 水线面面积系数
    double designIx()        const;  // 水线面惯性矩
    double designCi()        const;  // 水线面惯性矩系数

    // 第②组：型值表 (供型线图和学生编程)
    const std::vector<double>& stations()       const; // 站位 x 坐标 [Nx+1]
    const std::vector<double>& waterlines()     const; // 水线 z 坐标 [Nz+1] (z≤0)
    double halfBreadth(int iStation, int jWaterline) const; // 半宽 [Nx+1][Nz+1]

    // 第③组：排水量曲线 (不同吃水 → 排水体积)
    int                             draftCurveCount() const; // 曲线采样点数
    const std::vector<double>&      draftCurveValues() const;      // 吃水值
    const std::vector<double>&      draftCurveVolumes() const;     // 对应排水体积
    double                          volumeAtDraft(double draft) const; // 任意吃水插值

    // ========== 验证 ==========
    double cattoTotalVolume()  const;  // Catto 预计算的 totalVolume (用于互验证)
    double cattoCOBx() const, cattoCOBy() const, cattoCOBz() const;

private:
    // 生成核心
    void generateMesh();       // 在 Gazebo Z_UP 中生成水密网格
    void computeHydroProps();  // 梯形积分计算全部静水力参数
    void computeVolumeCurve(); // 计算排水量曲线
    void sealDeck();           // 甲板封盖 (z=0)
};
```

### 3.2 静水力参数计算公式

**梯形积分**（对水密网格，Gazebo Z_UP 坐标）：

水线面面积 (z=0)：
$$A_w = 2 \sum_{i=0}^{N_x} w_i \cdot y_i \cdot \Delta x, \quad y_i = \frac{B}{2} \cdot W(x_i/L)$$

$$C_{wp} = \frac{A_w}{L \cdot B}$$

水线面惯性矩：
$$I_x = \frac{2}{3} \sum_{i=0}^{N_x} w_i \cdot y_i^3 \cdot \Delta x$$

$$C_i = \frac{I_x}{L \cdot B^3 / 12}$$

排水体积 (z=0 为水面)：
$$\nabla = \sum_{i=0}^{N_x} w_i \cdot A_s(x_i) \cdot \Delta x, \quad A_s(x_i) = 2 \sum_{j=0}^{N_z} w_j \cdot y_{ij} \cdot \Delta z$$

浮心垂向位置：
$$KB = \frac{\sum_i w_i \left(2 \sum_j w_j \cdot y_{ij} \cdot z_j \cdot \Delta z\right) \Delta x}{\nabla}$$

稳心半径与初稳性高度：
$$BM = I_x / \nabla$$
$$GM = KB + BM - KG, \quad KG = D/2 \quad (\text{均质船体})$$

**互验证**：
- Catto `totalVolume`（基于三角形面的散度定理）
- 梯形积分 $\nabla$（基于横剖面积叠积）
- 两者差异 $< 1\%$ 则网格正确

### 3.3 网格生成算法（Gazebo Z_UP 直接生成）

```
Algorithm: generateMesh()

Input:  spec (L, B, D, Nx, Nz)

// Step 1: 顶点生成
for i = 0..Nx:
    xi = i * L / Nx
    xi_norm = xi / L
    w_i = W(xi_norm)

    for j = 0..Nz:
        zj = -j * D / Nz          // Gazebo Z_UP: 水线=0, 龙骨=-D
        zj_norm = j / Nz          // 归一化深度 [0, 1]
        s_j = S(zj_norm)
        half_y = (B/2) * w_i * s_j

        vs(i,j) = (xi, +half_y, zj)  // 右舷
        vp(i,j) = (xi, -half_y, zj)  // 左舷

// Step 2: 三角形生成 (绕序保证法线朝外)
// 右舷: 法线 = e1×e2, e1=(dx,0,0), e2=(0,-Δy,-dz) → n_y = -dx*(-dz) = +dx*dz > 0 ✓
// 绕序: (vs00, vs01, vs10)
//
// 左舷: 法线应朝 -y
// 绕序: (vp00, vp10, vp01)  → n_y = -dx*(-dz)*(-1)?
//
// 需要严格推导，确保法线指向船体外侧。

// Step 3: 甲板封盖 (z=0)
// 连接右舷顶边 vs(i,0) 和左舷顶边 vp(i,0)
// 绕序: (vs_i, vs_{i+1}, vp_i) 和 (vp_i, vs_{i+1}, vp_{i+1}) → 法线朝 +z ✓

// Step 4: 龙骨 (z=-D)
// 尖底: s(D)=0 → half_y=0 → 左右舷交汇为一条线 → 自动闭合
// 平底 (如果 S(ζ) 在 ζ=1 时 ≠ 0): 需额外封盖
```

### 3.4 三角绕序严格推导（Gazebo Z_UP 中）

**右舷** (y > 0)，期望法线朝 +y（船体外侧向右）：

顶点：$v_{00} = (x, +y, z)$，$v_{10} = (x+dx, +y', z)$，$v_{01} = (x, +y'', z+dz)$

$$\vec{e}_1 = v_{10} - v_{00} = (dx, 0, 0)$$

$$\vec{e}_2 = v_{01} - v_{00} = (0, -\Delta y, dz) \quad (\Delta y = |y'' - y| > 0 \text{ 船从水线向龙骨收窄})$$

$$\vec{n} = \vec{e}_1 \times \vec{e}_2 = (0, \underbrace{dx \cdot dz}_{>0}, \underbrace{-dx \cdot (-\Delta y)}_{>0})$$

**$n_y > 0$ ✓** 法线朝 +y。绕序：**(vs00, vs01, vs10)**

**左舷** (y < 0)，期望法线朝 -y（船体外侧向左）：

选择绕序 **(vp00, vp10, vp01)** 并验证：

$v_{00} = (x, -y, z)$，$v_{10} = (x+dx, -y', z)$，$v_{01} = (x, -y'', z+dz)$

$$\vec{e}_1 = (dx, 0, 0)$$
$$\vec{e}_2 = (0, +\Delta y, dz) \quad (\text{左舷 y 也向龙骨收窄}, y'' \text{ 绝对值更小})$$

$$\vec{n} = (0, -dx \cdot dz, dx \cdot \Delta y)$$

**$n_y < 0$ ✓** 法线朝 -y。绕序：**(vp00, vp10, vp01)**

**甲板封盖 (z=0)**，期望法线朝 +z（船体外侧向上）：

右舷侧封盖三角 $(vs_i, vs_{i+1}, vp_i)$：
$$\vec{e}_1 = (dx, 0, 0), \quad \vec{e}_2 = (0, -2y_i, 0)$$
$$\vec{n} = (0, 0, \underbrace{-dx \cdot 2y_i}_{<0})$$
→ **$n_z < 0$**，法线朝下 ✗

需要调整：$(vs_i, vp_i, vs_{i+1})$：
$$\vec{e}_1 = (0, -2y_i, 0), \quad \vec{e}_2 = (dx, 0, 0)$$
$$\vec{n} = (0, 0, \underbrace{2y_i \cdot dx}_{>0})$$
**$n_z > 0$** ✓ 绕序：**(vs_i, vp_i, vs_{i+1})**

左舷侧封盖三角 $(vp_i, vs_{i+1}, vp_{i+1})$→ 绕序：**(vp_i, vp_{i+1}, vs_{i+1})**（对称推导）

**结论** — 三角绕序表（已通过编译 + 法线方向验证）：

| 面 | Triangle 1 | Triangle 2 |
|----|-----------|-----------|
| 右舷 | `(vs_{i,j}, vs_{i+1,j}, vs_{i,j+1})` | `(vs_{i,j+1}, vs_{i+1,j}, vs_{i+1,j+1})` |
| 左舷 | `(vp_{i,j}, vp_{i,j+1}, vp_{i+1,j})` | `(vp_{i,j+1}, vp_{i+1,j+1}, vp_{i+1,j})` |
| 甲板右 | `(vs_{i,0}, vp_{i,0}, vs_{i+1,0})` | — |
| 甲板左 | `(vp_{i,0}, vp_{i+1,0}, vs_{i+1,0})` | — |
| 龙骨 | 尖底自动闭合 (y=0)，无需封盖 | — |

---

## 八、Phase A 实施中发现的问题与修复

### 8.1 OBJ 无法线 → DART segfault

DART 用 ODE 做 mesh 碰撞检测，OdeMesh 加载 OBJ 时必须读取顶点法线。

**修复**：`generateMesh` Step 4 — 面法线面积加权平均计算顶点法线，OBJ 输出 `vn` + `f v//vn` 格式。

### 8.2 碰撞体 mismatch → LCP NaN / segfault

| 尝试 | 结果 | 原因 |
|------|------|------|
| `<box>` + `<pose>` 偏移 | BoxedLcpConstraintSolver NaN | DART box collision + COM offset 产生病态约束矩阵 |
| `<mesh>` 无顶点法线 | OdeMesh segfault | ODE 需要法线 |
| `<mesh>` 有顶点法线 | ✅ 正常 | 对齐工作代码 MakeSDFStringFromValidFileType |

### 8.3 moveWeight 硬编码 deckZ

参考 SP-02 `IncliningExperimentDialog::moveWeight`：

```cpp
// SP-02: deckZ = 甲板高度 + 压载半高 + 间隙
double deckZ = H/2.0 + wh/2.0 + 0.02;

// SP-03 (旧): 硬编码, 缺少甲板位置
double deckZ = wh/2.0 + 0.02;  // = 0.12, 船浮起后这个值就不对了
```

船体上浮后甲板在 z≈0.5，`set_pose` 瞬移到 z=0.12 把压载块放进 ODE trimesh 内部（trimesh 碰撞是单向表面检测，无法处理内部穿透）→ 压载块消失。

**修复**：在 PoseState 中追踪船体世界 z，`deckZ = currentHullZ + wh/2.0 + 0.02`。

### Phase A：MathHullGenerator 重构 + SP-03 Bug 修复

**目标**：解决全部 3 个 Bug，SP-03 实验正常运行。

**新建文件**：

| 文件 | 说明 | 行数估计 |
|------|------|---------|
| `src_cpp/utils/ShipHullGenerator.hh` | 统一船型生成器接口 | ~80 |
| `src_cpp/utils/ShipHullGenerator.cc` | 实现：网格生成 + 静水力 + OBJ 输出 | ~300 |

**修改文件**：

| 文件 | 变更 |
|------|------|
| `src_cpp/utils/MathHullGenerator.hh/.cc` | **删除**，被 ShipHullGenerator 取代 |
| `src_cpp/utils/MeshUtils.hh/.cc` | `ConvertToZUp` 标记 deprecated（不再需要但保留兼容） |
| `src_cpp/plugins_gui/.../HullStabilityDialog.hh/.cc` | 改用 ShipHullGenerator；SDF collision 改用 `<box>` |
| `CMakeLists.txt` | 替换源文件名 |

**关键实现点**：
- 网格在 Gazebo Z_UP 中直接生成（无坐标变换）
- 三角绕序严格按第三节表格
- 视觉 OBJ (Nx=40, Nz=20) + 浮力 OBJ (Nx=20, Nz=10)
- 碰撞体统一用 Box `<size>L B D</size>`
- 静水力参数在构造时预计算
- 排水量曲线预计算（20 个吃水采样点）
- Catto totalVolume 与梯形积分互验证

---

### Phase B：模型加载集成 + 型线图

**目标**：主界面可以通过"新建数学船型"加载，并查看型线图。

#### B.1 模型加载入口

修改 `MenuPluginKeship`：
- 在"载入模型"按钮旁增加"新建船型"按钮（或载入类型下拉增加"数学船型"项）
- 弹出 `ShipHullDialog`：船型族选择 → 参数输入 → 3D 预览 → 确认 spawn
- `ShipHullGenerator` 集成到 `ModelManager`：新增 `SpawnMathHull` 方法

#### B.2 型线图对话框

新建 `LinesPlanDialog`：

```
┌──────────────────────────────────────────────┐
│  型线图 — Wigley 经典 L=6.0 B=1.5 D=1.0     │
│                                                │
│  ┌─────────────────┐ ┌──────────────────────┐ │
│  │   横剖线图        │ │   半宽水线图           │ │
│  │   (y vs z)       │ │   (x vs y)           │ │
│  │   z↑             │ │   y↑                 │ │
│  │   ├──→ y         │ │   ├──→ x            │ │
│  │                  │ │                      │ │
│  └─────────────────┘ └──────────────────────┘ │
│  ┌──────────────────────────────────────────┐ │
│  │   纵剖线图 (x vs z)                        │ │
│  │   z↑                                      │ │
│  │   ├──→ x                                  │ │
│  └──────────────────────────────────────────┘ │
└──────────────────────────────────────────────┘
```

**数据来源**：`ShipHullGenerator::stations()` / `waterlines()` / `halfBreadth()`。
**渲染方式**：QPainter 在 QWidget 上绘制（纯 2D，与 SP-02 散点图一致）。

---

### Phase C：排水量验证

**目标**：理论排水量曲线 vs 仿真实测排水量对比。

#### C.1 排水量曲线图

```
吃水 d (m)
  ↑
  │  ╭────────────── ∇_sim (实测点, 每500kg配载)
  │ ╱
  │╱    ───── ∇_th (理论曲线)
  │
  └──────────────→ ∇ (m³)
```

- **理论曲线**：`ShipHullGenerator::draftCurveValues/Volumes`（梯形积分，构造时预计算，无仿真依赖）
- **仿真实测**：spawn 模型 → 逐级增加配载 → 每级测量 `subVol`（来自 Catto 算法实时输出）

#### C.2 波浪控制

> **用户特别要求**：排水量实验必须保证无波浪，否则吃水测量不准确。

排水量实验的 SDF 配置中 **不加 `<wavefield>` 子块**。`KSPlugin_Buoyancy` 在不配波浪时自动退化为固定 `fluidLevel_`（水平面），`ComputeDepthSimply` 返回 0。

实现方式：
- `ShipHullGenerator` 生成的 SDF 默认不带波浪配置
- SP-03 倾斜实验如需波浪，由 `HullStabilityDialog` 额外添加
- 排水量验证对话框 `DisplacementVerifyDialog` 的 SDF 明确不含波浪

#### C.3 实测流程

```
学生选择船型参数 → 生成模型 (配载=0)
    → 记录 配载=0 时的吃水 d₀
    → 添加配载 500kg → 等待稳定 → 记录 d₁
    → 添加配载 500kg → 等待稳定 → 记录 d₂
    → ...
    → 散点图叠在理论曲线上
    → 误差分析
```

---

### Phase D：学生编程模块

**目标**：学生用 Python 或 C 语言编写梯形积分代码，计算排水体积，与系统标准答案对比。

> **支持 Python + C**：大一学生先学 C 语言（课程：C 程序设计），大二学 Python。两种语言都支持，学生自选。

#### D.0 语言适配总览

```
                    ┌─────────────────────────┐
                    │  CodeRunnerDialog (C++)  │
                    │                          │
                    │  [Python] [C]  语言选择   │
                    │                          │
                    │  ┌────────────────────┐  │
                    │  │ 代码编辑区          │  │
                    │  │ (按语言切换模板)    │  │
                    │  └────────────────────┘  │
                    │                          │
                    │  [ 运行代码 ]  [提示]     │
                    │                          │
                    │  输出 / 编译错误 / 结果   │
                    └─────────────────────────┘
                              │
              ┌───────────────┴───────────────┐
              ▼                               ▼
     keship_code_runner.py          gcc 编译 + 执行
     (Python 子进程)                 (C 沙箱)
```

#### D.1 编程对话框设计

```
┌──────────────────────────────────────────────────────────┐
│  编程练习：梯形积分法计算排水体积                           │
│                                                            │
│  船型参数:                   语言: ◉ Python  ○ C          │
│    船型: Wigley 经典   L=6.0m  B=1.5m  D=1.0m            │
│    ρ=500 kg/m³  吃水=0.5m                                │
│  标准答案: ∇ = 2.01 m³                                    │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │ /* 给定数据：                                         │ │
│  │  *   stations[N] = {0.0, 0.15, ...}  // 站位x(m)     │ │
│  │  *   areas[N]    = {0.0, 0.15, ...}  // 剖面面积(m²) │ │
│  │  *   N = 41   dx = 0.15                              │ │
│  │  * 提示: V ≈ Σ (A[i] + A[i+1]) / 2.0 * dx           │ │
│  │  */                                                   │ │
│  │                                                       │ │
│  │ #include <stdio.h>                                   │ │
│  │                                                       │ │
│  │ int main() {                                         │ │
│  │     double V = 0.0;                                  │ │
│  │     // TODO: 用梯形积分计算排水体积                      │ │
│  │                                                       │ │
│  │     printf("%.6f\n", V);                              │ │
│  │     return 0;                                         │ │
│  │ }                                                     │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  [ 运行代码 ]  [ 查看提示 ]             结果: ___ m³       │
│  误差: ___ %    判定: ✅ 通过 (<5%) / ❌ 不通过            │
└──────────────────────────────────────────────────────────┘
```

#### D.2 Python 执行沙箱

```python
# keship_code_runner.py — Python 代码执行
def run_python(code: str, hull_data: dict, timeout: int = 10):
    preamble = f"""
import sys, json
stations = {hull_data['stations']}
areas = {hull_data['areas']}
N = {hull_data['N']}
dx = {hull_data['dx']}
"""
    full_code = preamble + code
    result = subprocess.run(
        [sys.executable, '-c', full_code],
        capture_output=True, text=True, timeout=timeout,
        cwd='/tmp',
        env={'HOME': '/tmp', 'PATH': '/usr/bin'}
    )
    return {
        'stdout': result.stdout.strip(),
        'stderr': result.stderr.strip(),
        'returncode': result.returncode
    }
```

#### D.3 C 语言执行沙箱

C 语言的执行流程比 Python 多一步：**编译 + 运行**。学生代码写的是 `main()` 函数体，系统将其嵌入完整可编译的 C 程序。

**C 代码模板**（系统自动生成 preamble）：

```c
/* === 系统生成：数据声明 === */
#include <stdio.h>
#define N 41

double stations[N] = {0.00, 0.15, 0.30, 0.45, /* ...共41个 */ };
double areas[N]    = {0.00, 0.12, 0.23, 0.33, /* ...共41个 */ };
double dx = 0.15;

/* === 学生填写：main 函数 === */
int main() {
    double V = 0.0;

    // ===== 学生代码开始 =====

    for (int i = 0; i < N - 1; i++) {
        V += (areas[i] + areas[i+1]) / 2.0 * dx;
    }

    // ===== 学生代码结束 =====

    printf("%.6f\n", V);
    return 0;
}
```

**编译+执行流程**：

```python
# keship_code_runner.py — C 代码编译 + 执行
def run_c(student_code: str, hull_data: dict, timeout: int = 15):
    """
    C 代码执行：
    1. 将学生代码嵌入完整 C 程序
    2. gcc 编译（超时 10s）
    3. 运行二进制（超时 5s）
    """
    import tempfile, os

    # Step 1: 生成完整 C 源文件
    preamble = generate_c_preamble(hull_data)
    full_source = preamble + student_code

    # Step 2: 编译
    src_path = '/tmp/student_code.c'
    bin_path = '/tmp/student_prog'
    with open(src_path, 'w') as f:
        f.write(full_source)

    compile_result = subprocess.run(
        ['gcc', '-o', bin_path, src_path, '-lm', '-Wall', '-Werror'],
        capture_output=True, text=True, timeout=10
    )

    if compile_result.returncode != 0:
        return {
            'success': False,
            'stage': 'compile',
            'error': compile_result.stderr
        }

    # Step 3: 运行
    try:
        run_result = subprocess.run(
            [bin_path],
            capture_output=True, text=True, timeout=5,
            cwd='/tmp',
            env={'HOME': '/tmp', 'PATH': '/usr/bin'}
        )
        os.unlink(src_path)
        os.unlink(bin_path)
        return {
            'success': True,
            'stdout': run_result.stdout.strip(),
            'stderr': run_result.stderr.strip()
        }
    except subprocess.TimeoutExpired:
        return {
            'success': False,
            'stage': 'runtime',
            'error': '程序运行超时（可能死循环）'
        }
```

**C 语言安全考虑**：

| 风险 | 防护措施 |
|------|---------|
| 死循环 | 编译超时 10s + 运行超时 5s |
| 内存越界 | `-Wall -Werror` 编译检查；固定数组大小 |
| 写文件 | 运行在 `/tmp`，不挂载项目目录 |
| 系统调用 | gcc 编译纯计算代码，学生不接触系统头文件 |
| 恶意代码 | 代码长度限制（< 5000 字符）；禁用 `#include` 以外的预处理指令 |

#### D.4 两种语言的代码模板对照

| 特性 | Python | C |
|------|--------|---|
| 难度 | 语法简单，无编译 | 需理解编译、类型声明 |
| 适用学生 | 大二及以上 | 大一起可用 |
| 执行方式 | 解释执行 | GCC 编译 + 执行 |
| 错误反馈 | 运行时 traceback | 编译错误 + 运行时错误 |
| 典型代码行数 | ~8 行 | ~12 行 |
| 性能 | 足够（计算量极小） | 极快 |
| 精度 | float64 默认 | `double` |

#### D.5 船型数据级别（两种语言通用）

| 实验级别 | 提供数据 | 难度 |
|---------|---------|------|
| 初级 | 各站位横剖面面积 `areas[]` + 站距 `dx` | 1D 梯形积分 |
| 中级 | 型值表 `halfBreadths[][]` + 吃水 `draft` | 2D 梯形积分（先算剖面面积再积体积） |
| 高级 | 船型参数 (L, B, D) + 曲面公式 | 积分公式推导 + 数值实现 |

#### D.6 标准答案与评分

系统使用 `ShipHullGenerator::volumeAtDraft()` 作为标准答案：
- `|student - reference| / reference < 5%` → ✅ 通过
- `5% ≤ error < 10%` → ⚠️ 接近
- `error ≥ 10%` → ❌ 不通过，给出提示

---

## 五、文件变更汇总

### 全部阶段汇总

| 阶段 | 新建文件 | 修改文件 | 删除文件 |
|------|---------|---------|---------|
| **Phase A** | `ShipHullGenerator.hh/.cc` | `HullStabilityDialog.hh/.cc`, `CMakeLists.txt` | `MathHullGenerator.hh/.cc` |
| **Phase B** | `ShipHullDialog.hh/.cc`, `LinesPlanDialog.hh/.cc` | `MenuPluginKeship.hh/.cc/.qml`, `ModelManager.hh/.cc`, `CMakeLists.txt` | — |
| **Phase C** | `DisplacementVerifyDialog.hh/.cc` | `MenuPluginKeship`, `CMakeLists.txt` | — |
| **Phase D** | `CodeRunnerDialog.hh/.cc`, `keship_code_runner.py` | `CMakeLists.txt` | — |

---

## 六、关键设计决策记录

| # | 决策 | 理由 |
|---|------|------|
| 1 | 直接在 Gazebo Z_UP 中生成网格 | 消除所有坐标变换，根除绕序/顶点丢失问题 |
| 2 | 碰撞体用 Box 包围盒 | DART 不支持 mesh 碰撞体，Box 对倾斜实验精度足够 |
| 3 | 输出格式统一用 OBJ | OBJ 纯文本无歧义，Assimp 解析可靠（已验证） |
| 4 | 学生编程用 Python | 船舶专业通用、易沙箱、已有 Python 环境 |
| 5 | 排水量实验不加波浪 | 波浪导致吃水动态变化，无法准确测量 |
| 6 | 船型参数用 MathHullSpec 统一结构体 | 工厂方法预设 Wigley×4 + Lewis×2，开放参数支持任意组合 |
| 7 | 静水力在构造时预计算 | 梯形积分 O(Nx·Nz)，一次计算，后续零开销 |
| 8 | Catto totalVolume 与梯形积分互验证 | 两个独立路径算同一值，差异<1%则网格正确 |

---

## 七、Phase A 实施检查清单

- [ ] 删除旧 `MathHullGenerator.hh/.cc`
- [ ] 新建 `ShipHullGenerator.hh`（含 `MathHullSpec`, `WaterlineType`, `SectionType`）
- [ ] 新建 `ShipHullGenerator.cc`
  - [ ] `generateMesh()`：Gazebo Z_UP 顶点生成
  - [ ] 三角绕序严格按表格
  - [ ] `sealDeck()`：甲板封盖
  - [ ] `writeVisualOBJ()` / `writeBuoyancyOBJ()`
  - [ ] `computeHydroProps()`：梯形积分静水力
  - [ ] `computeVolumeCurve()`：排水量曲线
  - [ ] Catto totalVolume vs 梯形积分互验证日志
- [ ] 更新 `HullStabilityDialog`
  - [ ] 改用 `ShipHullGenerator`
  - [ ] collision 改为 `<box>`
  - [ ] 检查生成的 OBJ 路径
- [ ] 更新 `CMakeLists.txt`
  - [ ] 删除 `MathHullGenerator` 引用
  - [ ] 添加 `ShipHullGenerator` 编译
- [ ] 编译通过
- [ ] 测试：4 种 Wigley + 2 种 Lewis 加载正常
- [ ] 验证：模型不侧翻、不崩溃、浮力正常
- [ ] 回归：SP-01 / SP-02 不受影响

---

## 八、Phase A 实施记录 (2026-06-01) ✅

### 8.1 新建/修改文件

| 文件 | 变更 | 说明 |
|------|------|------|
| `src_cpp/utils/ShipHullGenerator.hh` | **新建** | Gazebo Z_UP 直接生成，6 种船型工厂方法 |
| `src_cpp/utils/ShipHullGenerator.cc` | **新建** | 网格生成 + 顶点法线 + 静水力 + 排水量曲线 + 型值表 |
| `src_cpp/utils/MathHullGenerator.hh/.cc` | **待删除** | 被 ShipHullGenerator 取代 |
| `HullStabilityDialog.hh/.cc` | 修改 | 使用 ShipHullGenerator，6 种船型，动态 deckZ |
| `StabilityExperimentDialog.cc` | 修改 | 波浪控制（构造=CWR静水，析构=恢复PMS） |
| `IncliningExperimentDialog.cc` | 修改 | 波浪控制（同上） |
| `CMakeLists.txt` | 修改 | MathHullGenerator → ShipHullGenerator |

### 8.2 解决的 Bug

| Bug | 根因 | 修复 |
|-----|------|------|
| DAE 三角绕序 + 坐标变换 | NED→Z_UP 变换链引入错误 | ShipHullGenerator 直接 Gazebo Z_UP 生成，无变换 |
| DAE 顶点丢失 | Assimp COLLADA 解析异常 | 统一用 OBJ 格式 |
| OBJ 无法线 → DART segfault | ODE 碰撞检测需要顶点法线 | generateMesh Step 4：面法线面积加权平均 → OBJ 含 vn |
| OBJ 索引查找崩溃 | 三角形存顶点副本，指针匹配失败 | 坐标字符串哈希映射 |
| 验证公式符号错误 | cross product y 分量反了 | e1.Z()*e2.X() - e1.X()*e2.Z() |
| 压载块拖动后消失 | moveWeight 硬编码 deckZ，船体上浮后甲板位置已变 | deckZ = currentHullZ_ + wh/2 + 0.02 |
| SP-01/02/03 视觉波浪干扰 | WaveVisual 全局渲染波浪 | 对话框构造/析构发布 CWR/PMS 参数 |

### 8.3 测试结果

- ✅ 6 种船型加载显示正常
- ✅ 浮力 + 波浪响应正常
- ✅ 静水力参数正确（Catto 与梯形积分互验证）
- ✅ 压载块 spawn + 拖动正常
- ✅ 三个实验视觉波浪控制正常

---

## 九、Phase B 实施记录 (2026-06-02) ✅

### 9.1 新建/修改文件

| 文件 | 动作 | 说明 |
|------|------|------|
| `ShipHullDialog.hh/.cc` | **新建** | 数学船型参数输入对话框（6 种船型 + 静水力预览 + 型线图入口） |
| `LinesPlanDialog.hh/.cc` | **新建** | 型线图三视图：横剖线图 + 半宽水线图 + 纵剖线图（QPainter） |
| `MenuPluginKeship.qml` | 修改 | Tab 1 按钮行加"数学船型"按钮 |
| `MenuPluginKeship.hh` | 修改 | 加 `OnBtn_NewMathHull` 槽 |
| `MenuPluginKeship.cc` | 修改 | 实现：ShipHullDialog → SpawnMathHull |
| `ModelManager.hh` | 修改 | 加 `SpawnMathHull` 声明 + `ShipHullGenerator.hh` include |
| `ModelManager.cc` | 修改 | 完整 spawn：OBJ 生成→SDF 组装→EntityFactory spawn→缓存→通知 |
| `CMakeLists.txt` | 修改 | 加 ShipHullDialog.cc, LinesPlanDialog.cc |

### 9.2 用户流程

```
主界面 Tab 1 → [数学船型] → ShipHullDialog
  ├── 选择船型(6种) + 输入 L/B/D/ρ
  ├── [型线图] → LinesPlanDialog (三视图)
  └── [生成模型] → ModelManager::SpawnMathHull → Gazebo
```

### 9.3 发现并修复的问题

| 问题 | 根因 | 修复 |
|------|------|------|
| 模型名含中文导致 Gazebo 异常 | SDF parser 不接受非 ASCII 模型名 | modelName() 改为 ASCII（Wigley, LewisU 等） |
| dialog exec() + WA_DeleteOnClose 访问已销毁 widget | 模态关闭后访问 combo 索引越界 | 手动 delete，exec() 后立即取值 |
| LinesPlanDialog 窗口卡住 | 模态对话框阻塞子窗口事件 | 加 Qt::Window flag + 关闭按钮 |
