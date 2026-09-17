# 浮力网格设计 — 从 Catto 算法到水密网格

> KeShip 项目 | 2026-05-28

---

## 〇、坐标系

**船体坐标系**（船舶静力学，z 向下为正）：

| 坐标 | 含义 | 范围 |
|------|------|------|
| $x$ | 纵向 | $[0, L]$ |
| $y$ | 横向 | $[-B/2, B/2]$ |
| $z$ | 垂向（水线 $0$ → 龙骨 $D$） | $[0, D]$ |

船体曲面：$y(x,z) = \pm \frac{B}{2} \cdot h(x_{norm}) \cdot s(z_{norm})$

其中 $h(x_{norm}) = 1 - x_{norm}^2$（Wigley 水线），$s(z_{norm})$ 为剖面形状。

**三种船型在 $z=D$（龙骨）处 $s(1)=0$**，底宽为零，尖底自然闭合。  
**在 $z=0$（水线）处 $s(0)=1$**，船宽最大，需要封盖。

---

## 一、Catto 浮力算法

### 1.1 基本思路（你设计的方案）

```
预计算阶段 (loadMeshData, 执行一次):
  遍历所有三角形 → 计算每个面的 V_k = A·(n·c)/3
  totalVolume = Σ V_k（要求网格水密 + 法线朝外）

每帧阶段 (calculateTotalVolumeContribution, 每步执行):
  对每个三角形，判断三个顶点相对水面的位置:
    · 全部在水下 → 贡献 V_k
    · 全部在水上 → 贡献 0
    · 与水面相交 → 裁剪，部分贡献 + 水线面多边形补偿
  submergedVolume = Σ 贡献
  F_buoyancy = ρ·g·submergedVolume
```

### 1.2 预计算的数学保证（散度定理）

取 $\mathbf{F}(\mathbf{x}) = \frac{1}{3}\mathbf{x}$，$\nabla \cdot \mathbf{F} = 1$。散度定理：

$$\iiint_\Omega dV = \frac{1}{3} \oiint_{\partial\Omega} \mathbf{x} \cdot \mathbf{n} \, dS$$

离散（三角形面）：

$$V = \frac{1}{3} \sum_{k=1}^N A_k (\mathbf{c}_k \cdot \mathbf{n}_k)$$

**前提**：$\partial\Omega$ 为封闭曲面，所有 $\mathbf{n}_k$ 一致朝外。满足时 $V$ 等于真实封闭体积。

### 1.3 每帧分类的数学保证（叠加原理）

水面将 $\mathbb{R}^3$ 分为水上区域 $\Omega_{above}$ 和水下区域 $\Omega_{below}$。浸没体积为 $\Omega \cap \Omega_{below}$。

Catto 算法对每个三角形 $T_k$ 单独处理——因为体积积分是线性的，可以按面叠加。**全水下**的面贡献全部体积，**相交**的面通过裁剪只贡献水下部分，**全水上**的面贡献为零。所有面贡献之和等于正确浸没体积。

---

## 二、网格构成

### 2.1 四个面

| 面 | 三角形来源 | 法线方向 | 何时生成 |
|----|----------|---------|---------|
| 右舷 | 结构化网格 | $+y$（朝外） | `generateHalfHull` |
| 左舷 | 镜像 | $-y$（朝外） | `generateHalfHull` |
| 龙骨 ($z=D$) | 无需 | — | $s(D)=0$，底宽为零，尖底自然闭合 |
| **甲板 ($z=0$)** | **封盖三角形** | **$-z$（船体坐标中朝上 = 朝外）** | **`generateHalfHull` 末尾** |

### 2.2 甲板封盖

甲板在船体坐标 $z=0$ 处。连接右舷顶边 $\{v_s^{(i)}\}$ 和左舷顶边 $\{v_p^{(i)}\}$：

```
船体横剖面 (x 固定, 从船尾向船首看)

        甲板面 (z=0)
  v_p ●───────────────● v_s     ← 封盖三角形连接左右舷
      ╲             ╱
       ╲   船体    ╱
        ╲  内部   ╱
         ╲       ╱
          ╲     ╱
           ╲   ╱
            ╲ ╱
             ● 龙骨 (z=D, 尖底, 自然闭合)
```

封盖三角形：
- $(v_s^{(i)}, v_s^{(i+1)}, v_p^{(i)})$：法线 $\propto (\Delta x, 0, 0) \times (0, -2y, 0) = (0, 0, -\Delta x \cdot 2y)$ → $-z$ 方向（船体坐标中朝上）
- $(v_p^{(i)}, v_s^{(i+1)}, v_p^{(i+1)})$：同上

共 $2 \times N_x$ 个甲板三角形。

### 2.3 封闭性验证

- 右舷 + 左舷：覆盖 $x \in [0, L], z \in [0, D]$ 的所有船体侧面
- 龙骨 ($z=D$)：$s(D)=0$，左右舷在此交汇为一条线（尖底），自然封闭
- 甲板 ($z=0$)：$2N_x$ 个三角形覆盖顶面开口

**四者构成完整封闭曲面 $\partial\Omega$。** 散度定理前提满足。

---

## 三、甲板在浮力计算中的行为

### 3.1 平衡浮态（$\phi = 0$）

均质船体密度 $\rho_{ship} = 0.5 \rho_w$，吃水 $d \approx 0.5D$。

在 Gazebo 坐标中：水面 $z_{water} = 0$，甲板 $z_{deck} = -D/2 < 0$。

**甲板始终在水面以上。** 每帧 Catto 分类：甲板所有三角形的三个顶点都在水面上 → 贡献 $0$。甲板不参与浸没体积计算。

### 3.2 小角度倾斜（$\phi < 10°$）

倾斜时水面相对船体旋转。甲板低侧可能触及水面。

Catto 算法逐三角形处理：
- 甲板高侧（$z < z_{water}$）：全水上，贡献 $0$
- 甲板低侧触及水面：与水面相交的三角形 → 裁剪 → 部分贡献，与其他面的水线段一同组装水线面多边形

**甲板三角形的裁剪逻辑与船体侧面三角形完全一致，Catto 统一处理，无需特殊对待。**

### 3.3 大角度倾斜（$\phi > 30°$，极端情况）

甲板低侧大量三角形入水。Catto 仍正确裁剪——甲板面、船体侧面、龙骨（尖底）的三角形全部参与分类和裁剪，水线面多边形闭合。无第二水线面干扰。

---

## 四、与 SP-02 矩形驳船的对比

SP-02 矩形驳船用 `<box>` 几何体——PrimitiveMeshGenerator 生成 12 个三角形（6 面 × 2），天然水密。

SP-03 数学船型是生成的水密网格：右舷 + 左舷 + 甲板封盖。龙骨处尖底自然闭合。

**两者的 Catto 算法流程完全一致：loadMeshData → 预计算 totalVolume → 每帧三角形分类 → 浸没体积求和。** 区别仅在于三角形的形状和数量，算法不变。

---

## 五、静水力参数计算

### 5.1 水线面属性（梯形积分）

水线面 $z=0$ 处 $s(0)=1$，半宽 $y_i = \frac{B}{2} \cdot h(x_i)$：

$$A_w = 2 \sum_{i=0}^{N_x} w_i y_i \Delta x, \quad I_x = \frac{2}{3} \sum_{i=0}^{N_x} w_i y_i^3 \Delta x$$

$$C_{wp} = \frac{A_w}{LB}, \quad C_i = \frac{I_x}{LB^3/12}$$

### 5.2 排水体积与浮心（梯形积分）

$$A_s(x_i) = 2 \sum_{j=0}^{N_z} w_j y_{ij} \Delta z, \quad \nabla = \sum_{i=0}^{N_x} w_i A_s(x_i) \Delta x$$

$$KB = \frac{\sum_i w_i (2 \sum_j w_j y_{ij} z_j \Delta z) \Delta x}{\nabla}$$

### 5.3 与 Catto totalVolume 的互验证

梯形积分 $\nabla$ 和 Catto `totalVolume` 应为同一值——两者都是封闭船体的体积，一个用连续积分近似、一个用离散面求和。差异应在 $1\%$ 以内。

---

---

## 七、三角形绕序与法线方向 — 实测 Bug 分析

### 7.1 实测现象（2026-05-31）

```
subVol = -0.9925  (负数！)
totalVol = 3.98   (正数，因为用了 std::abs)
totalSubVol = 0   (subVol<0 → continue → 零浮力 → 沉没)
```

### 7.2 根因推导

#### 右舷 (y > 0)，原始绕序 $(v_{00}, v_{10}, v_{01})$

$$\vec{e}_1 = v_{10} - v_{00} \approx (dx, 0, 0)$$

$$\vec{e}_2 = v_{01} - v_{00} = (0, y_{i,j+1} - y_{i,j}, dz)$$

船体从水线 (z=0) 向龙骨 (z=D) 收窄，$y_{i,j+1} - y_{i,j} < 0$。令 $\Delta y = |y_{i,j+1} - y_{i,j}|$：

$$\vec{e}_2 = (0, -\Delta y, dz)$$

$$\vec{n} = \vec{e}_1 \times \vec{e}_2 = \begin{vmatrix} \mathbf{i} & \mathbf{j} & \mathbf{k} \\ dx & 0 & 0 \\ 0 & -\Delta y & dz \end{vmatrix} = (0,\; \underbrace{-dx \cdot dz}_{n_y < 0},\; -dx \cdot \Delta y)$$

**$n_y = -dx \cdot dz < 0$。** 右舷法线指向 $-y$（朝内）❌。

#### 体积贡献的符号

单三角形 $V_k = \frac{1}{3} A_k (\mathbf{n} \cdot \mathbf{c})$。

右舷：$c_y > 0$，$n_y < 0$ → $n_y \cdot c_y < 0$ → $V_k < 0$。  
左舷：镜像推导，同样 $V_k < 0$。  
甲板：$n_z < 0$，符号不定但量级小。

**全体三角的 $V_k$ 均为负。**

### 7.3 为什么 totalVol 是正的但 subVol 是负的

```cpp
// loadMeshData — 预计算 totalVolume
body.totalVolume += std::abs(tri.volume_);  // abs!
// → totalVol = 3.98 ✓

// calculateTotalVolumeContribution — 每帧浸没体积
subVol += tri.volume_;  // 带符号，不 abs
// → 全部负 → subVol < 0 → continue → 零浮力
```

### 7.4 修复

翻转绕序使 $\vec{n}$ 朝外：

| | 原始 | 修正 |
|---|------|------|
| 右舷 | $(v_{00}, v_{10}, v_{01})$ | $(v_{00}, v_{01}, v_{10})$ |
| 左舷 | $(p_{00}, p_{01}, p_{10})$ | $(p_{00}, p_{10}, p_{01})$ |

修正右舷 $\vec{n} = \vec{e}_1 \times \vec{e}_2 = (0, -\Delta y, dz) \times (dx, 0, 0) = (0,\; \underbrace{dx \cdot dz}_{n_y > 0},\; \cdots)$ ✅

---

## 八、通用坐标系变换 `ConvertToZUp`

### 8.1 动机

不同建模工具的"朝上"约定不同：

| 来源 | 朝上轴 | 方向 |
|------|--------|------|
| Gazebo / Blender | Z | +Z |
| 船舶 NED | Z | -Z（向下） |
| SolidWorks / Maya | Y | +Y |

`ConvertToZUp` 统一处理所有情况，输出 Gazebo Z_UP 网格。

### 8.2 接口

```cpp
enum class UpAxis { X_UP, X_DOWN, Y_UP, Y_DOWN, Z_UP, Z_DOWN };

MeshDataSimple ConvertToZUp(const MeshDataSimple &mesh, UpAxis source);
```

6 种源约定 → 目标 Z_UP。变换同时作用于顶点坐标、三角形顶点、顶点法线。

### 8.3 变换规则

### 8.3 变换规则

| source | 操作 | 绕序调整 |
|--------|------|---------|
| `Z_UP` | 不变 | 无 |
| `Z_DOWN` | z → -z | $v_1 \leftrightarrow v_2$ |
| `Y_UP` | y ↔ z | 无 |
| `Y_DOWN` | y ↔ z, z → -z | $v_1 \leftrightarrow v_2$ |
| `X_UP` | x ↔ z | 无 |
| `X_DOWN` | x ↔ z, z → -z | $v_1 \leftrightarrow v_2$ |

**为什么需要绕序调整**：$z \to -z$ 是反射变换。$v_{01}-v_{00} = (0, -\Delta y, dz)$ 在反射后 $z$ 分量取反为 $-dz$，但 $y$ 分量 $-\Delta y$ 不变。叉积 $n_y = v_{01z} \cdot v_{10x} - v_{01x} \cdot v_{10z} = (-dz) \cdot dx - 0 \cdot 0 = -dz \cdot dx$。符号从 $+$ 翻为 $-$。交换 $v_1 \leftrightarrow v_2$ 将叉积再翻转一次，恢复正确符号。

### 8.4 完整管线

```
generateHalfHull (船舶 NED, z↓)
  → 三角绕序保证法线朝外
  → mesh_ (NED 坐标)
        │
ConvertToZUp(mesh_, Z_DOWN)
  ├ 顶点 z → -z        (坐标变换)
  ├ 法线 z → -z        (跟随)
  ├ 三角坐标 z → -z    (顶点跟随)
  └ 三角 v₁↔v₂         (绕序补偿)
        │
  → gzMesh (Z_UP, 法线朝外)
        │
writeDAE(gzMesh) → DAE (<up_axis>Z_UP</up_axis>)
        │
Buoyancy::loadMeshData → V_k > 0 → subVol > 0 → 正常浮力

### 8.4 在 MathHullGenerator 中的使用

```cpp
// writeDAE 中：
MeshDataSimple gzMesh = ConvertToZUp(mesh_, UpAxis::Z_DOWN);
```

生成 mesh 始终使用船舶 NED 坐标（$z \in [0, D]$，$z$ 向下），导出时一行调用完成变换。DAE 固定写 `<up_axis>Z_UP</up_axis>`。

---

## 九、总结

```
┌──────────────────────────────────────────────────────┐
│                    Catto 浮力计算                      │
│                                                      │
│  预计算 (loadMeshData, 一次):                          │
│    遍历所有三角形 (两侧 + 甲板)                          │
│    totalVolume = Σ V_k  ← 网格必须水密                  │
│                                                      │
│  每帧 (calculateTotalVolumeContribution):              │
│    三角顶点 vs 水面分类 → 全水下/水上/相交               │
│    submergedVolume = Σ 贡献                             │
│    F_b = ρ·g·submergedVolume                          │
│                                                      │
├──────────────────────────────────────────────────────┤
│                    网格结构                            │
│                                                      │
│    右舷 (结构化, Nx×Nz)    左舷 (镜像)                  │
│    龙骨 (z=D, s(D)=0)  — 尖底，自然闭合                 │
│    甲板 (z=0)           — 2Nx 个封盖三角形              │
│                                                      │
│    甲板作用:                                           │
│      · 完成水密 → totalVolume 正确                      │
│      · 正常浮态在水上 → 每帧贡献 0                       │
│      · 倾斜时 Catto 正常裁剪                            │
└──────────────────────────────────────────────────────┘
```
