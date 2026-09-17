# 水面多边形网格修复

**日期**：2026-06-23 | **状态**：✅ 已完成 | **关联 Issue**：#5

## 问题

`WaterAreaVisualManager::MakeSurfaceSdf()` 始终使用 `<plane>` 几何体，即使用户在 `WaterAreaDrawDialog` 中绘制了非矩形多边形，Gazebo 中仍显示为矩形水面。

## 方案

仅改 `WaterAreaVisualManager.{hh,cc}`，新增 5 个工具方法 + 分支逻辑：

### 分支逻辑

```
MakeSurfaceSdf(area):
  if area.vertices.size() >= 3 && bboxW > 1 && bboxH > 1:
    mesh = GeneratePolygonMesh(area)
    if mesh 非空 && OBJ 写入成功:
      SDF 输出 <mesh> 几何体
    else:
      回退 <plane>
  else:
    原 <plane> 几何体（向后兼容）
```

### 算法

| 方法 | 说明 |
|------|------|
| `PointInPolygon` | 射线法（crossing number），判断点是否在多边形内 |
| `GeneratePolygonMesh` | 网格采样：包围盒内 min 50×50 ~ max 200×200 等距格点，保留多边形内部点，规则网格三角化（4 角全内→2 三角，3 角→退化三角） |
| `WritePolygonObjFile` | 含 `v` / `vt` / `vn` / `f v/vt/vn` 完整 OBJ，目录自动创建 |
| `SanitizeName` | 只保留 `[a-zA-Z0-9_-]` |
| `GetTempMeshPath` | 输出 `/tmp/keship/waves/polygon_<name>.obj` |
| `DeleteAll` 扩展 | 删除 Gazebo 模型后清理 `.obj` 文件 |

### 坐标系

Mesh 顶点使用局部坐标（相对水区中心 `(cx, cy)`），模型 `<pose>` 为 `(cx, cy, baseHeight, 0, 0, 0)`，与 plane 分支一致。

### Shader 兼容性

Gerstner vertex shader (`GerstnerWaves_vs_330.glsl`) 使用 `in vec4 vertex`（位置）和 `in vec4 uv0`（纹理坐标）。OGRE 从 OBJ 自动映射 `v` → POSITION、`vt` → TEXCOORD0。

## 涉及文件

| 文件 | 改动 |
|------|------|
| `src_cpp/utils/WaterAreaVisualManager.hh` | +`#include "MeshUtils.hh"`, +5 私有方法声明, +`tempDir_` 成员 |
| `src_cpp/utils/WaterAreaVisualManager.cc` | 重写 `MakeSurfaceSdf`, +`PointInPolygon`/+`GeneratePolygonMesh`/+`WritePolygonObjFile`/+`SanitizeName`/+`GetTempMeshPath`, `DeleteAll` 扩展 |

## 已知局限

1. 网格边界 stair-step 误差（≤ 半个格间距），Gerstner 位移可部分平滑
2. TOML 不持久化多边形顶点（待单独修复，见 water_area_toml_persistence（已于 2026-09-17 移出仓库））
3. 临时 OBJ 留存 `/tmp/keship/waves/`，进程结束后不自动清理（非 WSL 重启则自动回收）
4. OGRE MeshManager 以文件路径为 key 全局缓存网格，同名 OBJ 只加载一次。通过 `GetTempMeshPath` 全局递增 ID 突破
5. OBJ 缺 `o` 对象名导致 OBJLoader 材质警告，已加 `o water_surface` 声明

## 关联文档

- 水域 TOML 持久化（已于 2026-09-17 移出仓库）
- Step 2 方案：per-area Plane Waves（已于 2026-09-17 移出仓库）
