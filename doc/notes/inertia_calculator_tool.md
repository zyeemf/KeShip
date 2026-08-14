# 惯性计算工具 (InertiaCalculatorDialog)

## 定位
纯离线独立工具，加载任意 mesh 文件（DAE/STL/OBJ），通过四面体分解法自动计算惯量参数，支持多种前置算法。

## 架构位置

```
keship/
├── src_cpp/utils/
│   ├── MeshInertiaCalculator.hh    ← 惯性计算引擎（算法枚举 + 入口 + Quickhull 3D）
│   └── MeshInertiaCalculator.cc    ← 四面体分解 + Quickhull 3D + 算法分支
└── src_cpp/gui/keship/editors/
    ├── InertiaCalculatorDialog.hh   ← 对话框接口
    └── InertiaCalculatorDialog.cc   ← UI + 算法下拉 + Buoyancy3DView 预览
```

## 三种计算方法

| 算法 | 适用场景 | 精度 | 依赖 | 实现 |
|------|---------|------|------|------|
| **Direct** | 已水密的封闭 mesh（圆柱体、长方体、数学船型） | 对封闭 mesh 精确 | 无 | 直接四面体分解 |
| **Convex Hull** | 非水密 mesh（船体壳体无甲板封盖） | 物理合理估计，略偏大 | 无 | Quickhull 3D + 四面体分解 |
| **V-HACD** | 高度非凸形状（平台结构、机械部件） | 最精确 | V-HACD 库 | Phase 2 预留 |

### Direct — 直接四面体分解
```
mesh 三角形 → 每个三角形(v₀,v₁,v₂) + 原点 → 四面体
   → 有符号体积 V = (v₀·(v₁×v₂))/6
   → 累加 10 个几何积分(vol, ∫x, ∫y, ∫z, ∫x², ∫y², ∫z², ∫xy, ∫xz, ∫yz)
   → 导出 CoM + 惯量张量
   → 平行轴定理至 CoM
   → 乘以密度 → InertiaParams
```

### Convex Hull — Quickhull 3D + 四面体分解
```
mesh 顶点 → Quickhull 3D → 封闭凸包三角网格
   → 对凸包做四面体分解（同上）
   → InertiaParams
```

Quickhull 内部结构：
```
QFace:    v[3] + normal + dist + outside[] + deleted
EdgeKey:  (minV, maxV) → {faceA, faceB}

1. 从 6 个极值点构初始四面体
2. 所有点按相对 4 个面的位置分类到 outside[]
3. while 有 face.outside 非空:
   a. 选最远点为 "眼点"
   b. BFS 找 horizon 边集
   c. 眼点 + 每条 horizon 边 → 新三角面
   d. 删除旧可见面 → 重分配 outside[]
4. 收集所有活跃面 → MeshDataSimple
```

### V-HACD — 凸分解合并（Phase 2）
```
VHACD::IVHACD::Create() → 分解为多凸包子块
   → 每子块单独四面体分解
   → 并行轴定理合并所有子块
   → InertiaParams
```

## 算法分支逻辑（ComputeInertiaFromMesh 内部）

```
Input: mesh, density, method
         │
         ▼
    switch(method)
         │
    ┌────┼──────────┐
    ▼    ▼          ▼
 Direct ConvexHull VHACD
    │    │          │
    │  Quickhull3D  │
    │    │          │
    └────┴──────────┘
         │
    workMesh (实际计算网格)
         │
    TetraAccumulator 累加
         │
    InertiaCalcResult
```

## 对话框 UI

```
┌─ 惯性计算工具 ─────────────────────────────────┐
│ 模型文件: [________________] [浏览]              │
│ 网格信息: 顶点 N | 三角面 M | 包围盒 L×B×H      │
│                                                  │
│ 密度: [▾ 钢(7850) 铝(2700) ...] [___ kg/m³]    │
│ 方法: [▾ Direct | ConvexHull ⬅默认 | V-HACD]   │
│                                                  │
│ ┌─ 3D 预览 ─────────────────────────────────┐  │
│ │  ● 原始 mesh (半透明)                       │  │
│ │  ● 质心标记 (橙色球)                        │  │
│ └────────────────────────────────────────────┘  │
│                                                  │
│ [计算惯性]  [复制 SDF]                           │
│ ─────────────────────────────────────────────── │
│ 体积 = xxx   质量 = xxx                          │
│ cogX cogY cogZ                                    │
│ Ixx Ixy ... Izz                                   │
│ ✅ 方法: ConvexHull | 凸包 128顶点 256面          │
└──────────────────────────────────────────────────┘
```

## 修改文件清单

| # | 文件 | 改动 |
|---|------|------|
| 1 | `src_cpp/utils/MeshInertiaCalculator.hh` | 新增 `InertiaCalcMethod` 枚举、`ComputeConvexHull3D()`、修改 `ComputeInertiaFromMesh` 签名 |
| 2 | `src_cpp/utils/MeshInertiaCalculator.cc` | 新增 ~300 行 Quickhull 3D 实现 + 算法分支 + `ComputeConvexHull3D` 公开函数 |
| 3 | `src_cpp/gui/keship/editors/InertiaCalculatorDialog.hh` | 新增 `methodCombo_`、`onMethodChanged()` |
| 4 | `src_cpp/gui/keship/editors/InertiaCalculatorDialog.cc` | UI 新增算法选择行 + 参数传递 + 结果显示方法信息 |

**不涉及**其他 7 个文件（MenuPluginKeship 三文件、CMakeLists.txt、Buoyancy3DView、ModelManager、MeshUtils）

## 零耦合验证

| 外部组件 | 影响 | 理由 |
|---------|------|------|
| `ModelManager` | ❌ 无 | InertiaParams 结构体未修改，仅引用 |
| `MenuPluginKeship` | ❌ 无 | 入口 slot 不变，仍调 InertiaCalculatorDialog |
| `Buoyancy3DView` | ❌ 无 | 仅复用 setModel/setCoGPosition/setShowCoG |
| `CMakeLists.txt` | ❌ 无 | 无新源文件、无新依赖 |
| `MeshUtils.hh` | ❌ 无 | 仅引用 MeshDataSimple，无修改 |

## Phase 2: V-HACD 集成

```cmake
option(ENABLE_VHACD "Enable V-HACD convex decomposition" OFF)
if(ENABLE_VHACD)
    find_package(VHACD REQUIRED)
    target_compile_definitions(keship_utils PRIVATE HAS_VHACD)
    target_link_libraries(keship_utils PUBLIC VHACD::VHACD)
endif()
```

MeshInertialCalculator.cc 内条件编译：
```cpp
#ifdef HAS_VHACD
#include "VHACD/VHACD.hh"
// ... computeWithVHACD() 实现
#endif
```
