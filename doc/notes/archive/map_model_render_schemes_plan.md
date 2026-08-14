# 2D 地图模型渲染方案 — 多算法架构

**日期**：2026-06-22 | **状态**：✅ 已实施 | **依赖**：[map_model_2d_preview_plan.md](map_model_2d_preview_plan.md)

---

## 已实施的 4 种方案

| 方案 | 枚举值 | 算法 | 计算时机 | 渲染函数 |
|------|--------|------|---------|---------|
| Rect | `ModelRenderScheme::Rect` | min/max 包围盒 | spawn 时 | `paintModelRect`（兜底） |
| ConvexHull | `ModelRenderScheme::ConvexHull` | Graham Scan | spawn 时 | `paintModelPolygon` |
| AlphaShape | `ModelRenderScheme::AlphaShape` | 凸包 + 去长边 | `EnsureFootprintOutlines` 懒加载 | `paintModelPolygon` |
| SubMeshHulls | `ModelRenderScheme::SubMeshHulls` | Assimp sub-mesh 分解 + 各自凸包 | `EnsureFootprintOutlines` 懒加载 | `paintModelSubPolygons` |

### 默认方案：`ConvexHull`

---

## 数据结构

### `ModelRenderScheme` 枚举（`WaterArea.hh`）

```cpp
enum class ModelRenderScheme : int {
    Rect = 0, ConvexHull, AlphaShape, SubMeshHulls
};
```

### `ModelFootprint` 扩展字段

```cpp
std::vector<std::pair<double,double>> convexOutline;   // 凸包
std::vector<std::pair<double,double>> alphaOutline;    // 凹包
std::vector<std::vector<std::pair<double,double>>> subOutlines;  // 分组件
```

---

## 计算函数（`ModelManager.cc`）

| 函数 | 描述 |
|------|------|
| `ComputeBBoxFromVertices` | 顶点→min/max→(w,h) |
| `ComputeConvexHull` | Graham Scan 2D 凸包，纯 C++ 内嵌 |
| `ComputeAlphaShape` | 简化版：凸包+去长边过滤 |
| `ComputeSubMeshHulls` | Assimp 分 mesh 加载+各自凸包 |

---

## 渲染函数（`MapWidget.cc`）

| 函数 | 描述 |
|------|------|
| `paintModelRect` | 旋转矩形（兜底方案） |
| `paintModelPolygon` | 旋转多边形轮廓（凸包/凹包共用） |
| `paintModelSubPolygons` | 多组件多边形轮廓 |

### paintEvent 分支调度

```cpp
switch (renderScheme_) {
case ConvexHull:   if (!convexOutline.empty()) paintModelPolygon; break;
case AlphaShape:   if (!alphaOutline.empty())  paintModelPolygon; break;
case SubMeshHulls: if (!subOutlines.empty())   paintModelSubPolygons; break;
}
if (!rendered) paintModelRect(p, m, pw, ph);  // 兜底
```

---

## 方案切换

```cpp
map_->setModelRenderScheme(ModelRenderScheme::ConvexHull);
```

---

## 涉及文件

| 文件 | 改动 |
|------|------|
| `include/keship/utils/WaterArea.hh` | 新增 `ModelRenderScheme` 枚举 + `ModelFootprint` 扩展 3 个 outline 字段 |
| `ModelManager.hh` | 新增 `EnsureFootprintOutlines()` |
| `ModelManager.cc` | 4 个计算函数 + `CacheModelFootprint` 集成 + 懒加载实现 |
| `MapWidget.hh` | 新增 `renderScheme_` + `setModelRenderScheme` + 3 个私有渲染函数 |
| `MapWidget.cc` | `paintModelRect` / `paintModelPolygon` / `paintModelSubPolygons` + `paintEvent` 分支调度 |
