# SP-03 Bug 分析报告

> 2026-06-01

---

## 一、SP-03 涉及的三层几何体

| 层 | 用途 | 使用者 | 格式 |
|----|------|--------|------|
| 视觉 | 3D 渲染 | Ogre2 渲染引擎 | DAE / OBJ |
| 碰撞 | 物理碰撞检测 | DART | DAE / OBJ（❌ DART 不支持 mesh） |
| 浮力 | Catto 浸没体积 | KSPlugin_Buoyancy | 独立加载，不经过 DART |

SP-01（圆柱体）和 SP-02（矩形驳船）的碰撞体用的是 `<cylinder>` 和 `<box>`，DART 原生支持。SP-03 是第一个用了 `<mesh>` 碰撞体的实验。

---

## 二、已发现的 Bug 及修复

### Bug 1：三角形绕序导致 subVol 为负（✅ 已修复）

**现象**：模型下沉，`subVol = -1.99`，`totalSubVol = 0`。

**根因**：船舶 NED 坐标（z 向下）下正确的三角绕序，在 Gazebo Z_UP 坐标（z 向上）下法线朝内。

**数学证明**：对右舷三角 $(v_{00}, v_{10}, v_{01})$：

$$v_{10} - v_{00} \approx (dx, 0, 0), \quad v_{01} - v_{00} = (0, -\Delta y, -dz)$$

$$\vec{n} = (dx, 0, 0) \times (0, -\Delta y, -dz) = (0, -dx \cdot dz, -dx \cdot \Delta y)$$

$n_y < 0$ → 朝内。体积 $V_k = \frac{1}{3}A(\vec{n}\cdot\vec{c})$ 全为负。

**修复**：`ConvertToZUp` 中 $z \to -z$ 后交换 $v_1 \leftrightarrow v_2$，恢复法线朝外。`subVol` 变正（+1.99），浮力正常。

### Bug 2：DAE 格式导致右舷顶点丢失（✅ 已修复）

**现象**：浮力正常但模型侧翻。`COBlocal_y = -0.5`（全部浮力作用在左舷）。

**根因**：`writeDAE` 输出的 DAE 文件中确有正 y 值（0.1425, 0.27, …, 0.75），但经过 Assimp 加载后，421 个顶点 y 全 ≤ 0。具体是 Assimp 的哪个环节（`PreTransformVertices`、COLLADA 解析、还是 `<p>` 索引）导致丢失，尚未最终确定。

**修复**：浮力 mesh 改用 OBJ 格式（`writeOBJ`），绕过 DAE/Assimp。OBJ 加载后顶点完整保留：

```
Y顶点 pos=190 neg=231 range=[-0.75,0.75]
右舷 pos=399 sum=1.99  左舷 pos=399 sum=1.99
```

COB 恢复对称。

### Bug 3：DART 不支持 mesh 碰撞体（❌ 未修复）

**现象**：DART 崩溃，日志：

```
[CustomMeshShape.cc:144] One of the submeshes has normal count [0] that
does not match its vertex count [9840]. This submesh will be ignored!
Segmentation fault
```

**根因**：SDF 中的 `<collision>` 使用了 `<mesh>`。DART 不支持从 SDF 创建 mesh 碰撞体（`SDFFeatures.cc:332: Mesh construction from an SDF has not been implemented yet for dartsim`）。

**对比**：

| 实验 | 碰撞体 | DART 支持？ | 结果 |
|------|--------|-----------|------|
| SP-01 | `<cylinder>` | ✅ | 正常 |
| SP-02 | `<box>` | ✅ | 正常 |
| SP-03 | `<mesh>` | ❌ | 崩溃 |

碰撞体创建失败的后果：
- DART 无法建立碰撞几何 → 动力学积分缺少约束 → NaN → 崩溃
- 这个崩溃与浮力 mesh（OBJ，symmetry verified）无关

**修复方向**（未实施）：碰撞体改用 `<box>`，尺寸 $L \times B \times D$。视觉和浮力保持当前格式（视觉 OBJ + DAE，浮力 OBJ）。

---

## 三、当前代码状态

| 文件 | 状态 |
|------|------|
| `MathHullGenerator` | 三角绕序 ✅、`writeOBJ` ✅、`ConvertToZUp` ✅ |
| `ConvertToZUp` | Z_DOWN：$z \to -z$ + $v_1 \leftrightarrow v_2$ ✅ |
| `HullStabilityDialog` | 视觉 OBJ、浮力 OBJ |
| `KSPlugin_Buoyancy` | `<mesh>` 浮力体加载 ✅ |
| SDF collision | `<mesh>` ❌ → 需改为 `<box>` |

---

## 四、待解决问题

1. **Bug 3**：碰撞体从 `<mesh>` 改为 `<box>`
2. **验证**：修复 Bug 3 后，完整测试浮力 + 稳定性
