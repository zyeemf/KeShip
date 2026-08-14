# 水域 TOML 持久化方案

**日期**：2026-06-23 | **状态**：✅ 已完成

## 目标

水域数据随世界（.ksw）保存和加载，支持多边形顶点持久化。

## 背景

`keship_water_areas.toml` 是 GUI 进程和 Gazebo 物理进程之间的通信介质：

```
GUI 进程（MenuPluginKeship）
    │  WriteWaterAreasToml()
    ▼
/tmp/keship_water_areas.toml
    │
    ▼ Read: KSPlugin_Buoyancy::Configure()
Gazebo Server 进程
```

水区数据不是 Gazebo 原生实体，无法通过 `EntityFactory` 服务传递，只能通过文件共享。

## 方案

### 保存（OnBtn_SaveWorld）

```
SaveWorldManifest(dirPath)
    → 生成 keship_world.ksw（JSON 清单）
    → 逐个模型保存为 .ksm 文件

WriteWaterAreasToml(dir/keship_water_areas.toml)   ← 新增
    → 将所有水区写入用户选择的保存目录
    → 含 vertices 字段（多边形顶点）
```

### 加载（OnBtn_LoadWorld）

```
LoadWorldManifest(.ksw) → 解析模型列表

如果 keship_water_areas.toml 存在:                ← 新增
    tempMgr.LoadFromToml(waterToml)
    逐条 AddArea 到主 waterAreaMgr_（追加合并）

WriteWaterAreasToml()                              ← 新增
    → 重写 /tmp/keship_water_areas.toml 供 Buoyancy Configure

循环 Spawn 模型（Buoyancy Configure 从 /tmp/ 读水区）
```

### 设计原则

- **加载不删已有**：水区追加合并，不影响场景中已存在的内容
- **加载不控制视觉**：水面显示/隐藏由复选框独立控制，加载只负责数据层
- **保存全量**：保存当前全部水区状态（不是差异）

## TOML 格式

```toml
[[water_areas]]
name = "水域1"
x_min = -20.0
x_max = 20.0
y_min = -20.0
y_max = 20.0
base_height = 5.0
vertices = [[-18.0, -20.0], [0.0, -25.0], [18.0, -20.0], [18.0, 20.0], [0.0, 25.0], [-18.0, 20.0]]

[water_areas.wave]
model = "PMS"
period = 5.0
...
```

### 解析逻辑

`LoadFromToml` 在 basic fields 后新增 vertices 解析：
```cpp
if (auto vertsArr = (*tbl)["vertices"].as_array())
{
    for (const auto &v : *vertsArr)
    {
        auto coordArr = v.as_array();
        if (coordArr && coordArr->size() >= 2)
        {
            auto vx = (*coordArr)[0].value<double>();
            auto vy = (*coordArr)[1].value<double>();
            if (vx && vy)
                area.vertices.emplace_back(*vx, *vy);
        }
    }
}
```

## 涉及文件

| 文件 | 改动 |
|------|------|
| `MenuPluginKeship.hh` | `WriteWaterAreasToml` 签名加路径参数 |
| `MenuPluginKeship.cc` | `WriteWaterAreasToml`（参数化 + vertices）；`OnBtn_SaveWorld`（+1行）；`OnBtn_LoadWorld`（水区合并加载） |
| `WaterAreaManager.cc` | `LoadFromToml` 解析 vertices 数组 |

## 关联文档

- [水面多边形网格修复](../water_area_polygon_mesh.md)
- [多水区场景实施方案](water_area_implementation_plan.md)
- [Step 2 方案：per-area Plane Waves](water_area_step2_plan.md)
