# 2D 地图显示加载的地图模型 — 实施方案

**日期**：2026-06-22 | **状态**：✅ 已实施（Step 1~3 + 多算法渲染 + 双击修复）

> **代码验证日期**：2026-06-21，所有关键 API 签名已与源码核对。
> **实施日期**：2026-06-21，12 文件改动，编译通过。

---

## 问题

MapWidget 当前是空白网格，不渲染已加载的场景模型。在水区编辑器/任务编辑器中，用户无法对照地图模型（建筑、地形）的空间位置来放置水区或航路点。

---

## 总体架构

```
模型 spawn 时
  └─ ModelManager::CacheModelFootprint()
       └─ 计算局部包围盒 → 存入 modelBBoxCache_

MenuPluginKeship::LoadConfig()
  └─ 通过 modelManager->GetNode() 订阅 /world/{world}/pose/info (Pose_V)
       └─ 回调中更新 modelPoseCache_（MenuPluginKeship 成员）

打开对话框时
  └─ MenuPluginKeship::GatherSceneFootprints()
       ├─ 遍历 ModelManager::modelBBoxCache_（局部尺寸）
       ├─ 查询自身 modelPoseCache_（世界位姿）
       ├─ 组合为 vector<ModelFootprint>
       └─ 传入 MapWidget → paintEvent 渲染
```

**数据所有权**：
| 缓存 | 所有者 | 原因 |
|------|--------|------|
| `modelBBoxCache_` | `ModelManager` | 包围盒是模型数据，spawn 时计算，删除时清理 |
| `modelPoseCache_` | `MenuPluginKeship` | Pose_V 订阅在 MenuPluginKeship 中设置，位姿是实时 UI 数据 |
| `GatherSceneFootprints()` | `MenuPluginKeship` | 组合来自两个所有者的数据 |

---

## 数据结构

### ModelFootprint（定义在 `include/keship/utils/WaterArea.hh` 中）

```cpp
namespace keship {

/// 模型的 2D 足迹，供 MapWidget 渲染场景参考
struct ModelFootprint {
    std::string name;          // 模型名称（渲染时显示）
    double worldX = 0;         // 模型世界位姿 X
    double worldY = 0;         // 模型世界位姿 Y
    double worldYaw = 0;       // 模型世界朝向（弧度）
    double localW = 0;         // 模型局部 XY 包围盒宽度（dx）
    double localH = 0;         // 模型局部 XY 包围盒高度（dy）
    bool isStatic = false;     // 静态模型（不同渲染样式）
};

}  // namespace keship
```

**语义约定**：
- `localW / localH` 是**模型局部坐标系**下的包围盒尺寸（未旋转）
- `worldX / worldY / worldYaw` 是模型在世界系下的位姿
- MapWidget 渲染时：以 `(worldX, worldY)` 为中心，旋转 `worldYaw`，绘制 `localW × localH` 矩形

---

## 实施步骤

### Step 1：ModelManager — 包围盒缓存

#### 1.1 ModelManager.hh 新增声明

```cpp
// ModelManager.hh — 新增
// （ModelFootprint 结构体放在 include/keship/utils/WaterArea.hh）

public:
    /// spawn 后调用，计算并缓存模型的 2D 局部包围盒
    void CacheModelFootprint(const std::string &_name);

private:
    /// 模型局部包围盒缓存（spawn 时填充，删除时清理）
    std::map<std::string, keship::ModelFootprint> modelBBoxCache_;
```

#### 1.2 包围盒计算 — spawn 入口中调用

在以下方法中，模型创建成功后调用 `CacheModelFootprint(_instanceName)`：

- `SpawnModelInGazeboFromFile()` — 第 178 行（sdfCache_ 赋值之后）
- `SpawnModelInGazeboFromSDFString()` — 成功路径
- `SpawnCylinderInGazebo()` — 成功路径（`SpawModelInGazeboFromShape` 无实现，跳过）

```cpp
void ModelManager::CacheModelFootprint(const std::string &_name)
{
    ModelFootprint fp;
    fp.name = _name;

    // 1. 提取局部几何尺寸（复用已有 API）
    bool isBox;
    double dx, dy, dz, radius, length;
    std::string meshPath;
    if (!GetCachedModelGeometry(_name, isBox, dx, dy, dz, radius, length, meshPath))
        return;

    if (isBox) {
        fp.localW = dx;
        fp.localH = dy;
    } else if (radius > 0.0) {
        fp.localW = radius * 2.0;
        fp.localH = radius * 2.0;
    } else if (!meshPath.empty()) {
        // Mesh 模型：加载顶点 → 遍历 XY min/max
        MeshDataSimple mesh = MeshUtils::LoadMeshFromFile(meshPath);
        if (!mesh.vertices.empty()) {
            double xMin = INFINITY, xMax = -INFINITY;
            double yMin = INFINITY, yMax = -INFINITY;
            for (const auto &v : mesh.vertices) {
                if (v.X() < xMin) xMin = v.X();
                if (v.X() > xMax) xMax = v.X();
                if (v.Y() < yMin) yMin = v.Y();
                if (v.Y() > yMax) yMax = v.Y();
            }
            fp.localW = xMax - xMin;
            fp.localH = yMax - yMin;
        }
    }

    // 2. 静态标记
    GetCachedModelStatic(_name, fp.isStatic);

    modelBBoxCache_[_name] = fp;
}
```

> **注**：`GetCachedModelGeometry` 返回 `bool`（SDF 缓存命中）。`MeshUtils::LoadMeshFromFile` 返回 `MeshDataSimple`（非 bool，通过 `mesh.vertices.empty()` 判断成败）。

#### 1.3 缓存清理

在 `DeleteModelInGazeboByName()` 第 892 行（`modelList.erase()` 之后）增加：

```cpp
modelBBoxCache_.erase(_instanceName);
```

所有删除路径（`DeleteAllModelsInGazebo`、`DeleteModelInGazeboByIndex`）均委托到此方法，一处修改即可覆盖。

---

### Step 2：MenuPluginKeship — Pose_V 订阅 + 数据组合

#### 2.1 新增成员

```cpp
// MenuPluginKeship.hh — 新增

private:
    /// 模型世界位姿缓存（Pose_V 订阅回调更新）
    struct CachedPose { double x = 0, y = 0, yaw = 0; };
    std::map<std::string, CachedPose> modelPoseCache_;
    mutable std::mutex modelPoseCacheMutex_;

    /// 组合包围盒 + 位姿为完整的场景足迹列表
    std::vector<keship::ModelFootprint> GatherSceneFootprints() const;
```

#### 2.2 LoadConfig 中启动订阅

```cpp
// MenuPluginKeship::LoadConfig()

// 订阅 Pose_V 以实时获取所有模型位姿
std::string poseTopic = "/world/" + worldName_ + "/pose/info";
this->modelManager->GetNode().Subscribe(poseTopic,
    std::function<void(const gz::msgs::Pose_V&)>(
        [this](const gz::msgs::Pose_V &_msg) {
            std::lock_guard<std::mutex> lock(modelPoseCacheMutex_);
            modelPoseCache_.clear();
            for (int i = 0; i < _msg.pose_size(); ++i) {
                const auto &p = _msg.pose(i);
                const auto &ori = p.orientation();
                // gz::math::Quaterniond 构造函数参数顺序：w, x, y, z
                double yaw = gz::math::Quaterniond(
                    ori.w(), ori.x(), ori.y(), ori.z()).Yaw();
                modelPoseCache_[p.name()] = {
                    p.position().x(), p.position().y(), yaw
                };
            }
        }));
```

#### 2.3 GatherSceneFootprints 实现

```cpp
std::vector<keship::ModelFootprint> MenuPluginKeship::GatherSceneFootprints() const
{
    std::vector<keship::ModelFootprint> result;
    std::lock_guard<std::mutex> lock(modelPoseCacheMutex_);

    for (const auto &[name, bbox] : modelManager->modelBBoxCache_) {
        // 过滤系统实体
        if (name == "sun" || name == "ground_plane" ||
            name.rfind("waves_", 0) == 0)
            continue;

        ModelFootprint fp = bbox;  // 复制局部尺寸 + static 标记
        auto it = modelPoseCache_.find(name);
        if (it != modelPoseCache_.end()) {
            fp.worldX   = it->second.x;
            fp.worldY   = it->second.y;
            fp.worldYaw = it->second.yaw;
        }
        result.push_back(fp);
    }
    return result;
}
```

> **注**：`modelManager->modelBBoxCache_` 需要 ModelManager 新增公开 getter 或设为 friend，见下方"补充说明"。

#### 2.4 对话框打开时传入

```cpp
// OnBtn_AddWaterArea / OnBtn_EditWaterArea
WaterAreaDrawDialog dlg;
dlg.setSceneModels(GatherSceneFootprints());
// ...
dlg.exec();

// TaskSetDialog 构造函数
mapWidget_->setSceneModels(GatherSceneFootprints());
mapWidget_->fitToSceneModels();
```

---

### Step 3：MapWidget — 场景模型渲染层

#### 3.1 新增声明

```cpp
// MapWidget.hh — 新增

#include "keship/utils/WaterArea.hh"  // 复用 ModelFootprint

public:
    void setSceneModels(const std::vector<keship::ModelFootprint> &_models);
    void clearSceneModels() { sceneModels_.clear(); update(); }
    void fitToSceneModels();

private:
    std::vector<keship::ModelFootprint> sceneModels_;
```

#### 3.2 渲染代码（paintEvent 中，网格之后、船舶之前）

```cpp
// 场景模型层
if (!sceneModels_.empty()) {
    for (const auto &m : sceneModels_) {
        p.save();

        // 变换到模型世界位姿
        QPointF center(toX(m.worldX), toY(m.worldY));
        double angleDeg = -m.worldYaw * 180.0 / M_PI;  // QPainter 顺时针为正

        p.translate(center);
        if (std::abs(m.worldYaw) > 1e-6)
            p.rotate(angleDeg);

        double pw = m.localW * plotW() / (viewXMax_ - viewXMin_);
        double ph = m.localH * plotH() / (viewYMax_ - viewYMin_);

        QRectF rect(-pw / 2, -ph / 2, pw, ph);

        if (m.isStatic) {
            p.setBrush(QColor(160, 160, 160, 80));
            p.setPen(QPen(QColor(120, 120, 120), 1, Qt::DashLine));
        } else {
            p.setBrush(QColor(100, 150, 220, 60));
            p.setPen(QPen(QColor(80, 120, 200), 1));
        }
        p.drawRect(rect);

        // 名称标签（resetTransform 确保文字始终水平）
        p.resetTransform();
        p.setPen(QColor(60, 60, 60));
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.drawText(QRectF(toX(m.worldX) - 50,
                          toY(m.worldY) - ph / 2 - 14, 100, 14),
                   Qt::AlignCenter,
                   QString::fromStdString(m.name));

        p.restore();
    }
}
```

> **注**：`plotW()/plotH()/toX/toY/viewXMin_/viewXMax_/viewYMin_/viewYMax_` 均为 MapWidget 已有成员（已在源码中验证），签名完全匹配。

#### 3.3 视图自适应

```cpp
void MapWidget::fitToSceneModels()
{
    if (sceneModels_.empty()) return;
    double xMin = INFINITY, xMax = -INFINITY;
    double yMin = INFINITY, yMax = -INFINITY;
    for (const auto &m : sceneModels_) {
        double halfDiag = std::sqrt(m.localW * m.localW + m.localH * m.localH) * 0.5;
        if (m.worldX - halfDiag < xMin) xMin = m.worldX - halfDiag;
        if (m.worldX + halfDiag > xMax) xMax = m.worldX + halfDiag;
        if (m.worldY - halfDiag < yMin) yMin = m.worldY - halfDiag;
        if (m.worldY + halfDiag > yMax) yMax = m.worldY + halfDiag;
    }
    double margin = std::max(xMax - xMin, yMax - yMin) * 0.1;
    viewXMin_ = xMin - margin;
    viewXMax_ = xMax + margin;
    viewYMin_ = yMin - margin;
    viewYMax_ = yMax + margin;
    update();
}
```

---

### Step 4（可选）：动态模型位置实时刷新

仅对长时间打开的对话框（WaterAreaDrawDialog、TaskMonitorDialog）：

```
QTimer(500ms) → GatherSceneFootprints() → map_->setSceneModels()
```

- `GatherSceneFootprints()` 仅从内存缓存读取，**无阻塞调用**
- 静态模型的包围盒不变，动态模型的位姿由 Pose_V 订阅实时更新

---

## 涉及文件

| 文件 | 改动 |
|------|------|
| `include/keship/utils/WaterArea.hh` | 新增 `ModelFootprint` 结构体 |
| `ModelManager.hh` | 新增 `modelBBoxCache_` + `CacheModelFootprint()` + `GetBBoxCache()` getter |
| `ModelManager.cc` | 实现 `CacheModelFootprint`（三类分支）；spawn 入口调用；删除入口清理 |
| `MenuPluginKeship.hh` | 新增 `modelPoseCache_` + `CachedPose` + `GatherSceneFootprints()` |
| `MenuPluginKeship.cc` | `LoadConfig` 中订阅 Pose_V；`GatherSceneFootprints` 实现；对话框打开时传入 |
| `MapWidget.hh` | 新增 `sceneModels_` + `setSceneModels()` / `clearSceneModels()` / `fitToSceneModels()` |
| `MapWidget.cc` | `paintEvent` 新增场景模型渲染层 |
| `WaterAreaDrawDialog.hh/.cc` | 新增 `setSceneModels()`，转发到 `map_->setSceneModels()` |
| `TaskSetDialog.hh/.cc` | 暂未实施（低优先级） |
| `CMakeLists.txt` | 无新增源文件 |

---

## 遗留问题（2026-06-21 代码审查）

| # | 问题 | 严重度 |
|---|------|--------|
| 1 | `CacheModelFootprint` 仅从 `SpawnModelInGazeboFromFile` 调用，缺少 `SpawnModelInGazeboFromSDFString` / `SpawnCylinderInGazebo` / `SpawnMathHull` | 中 |
| 2 | `OnBtn_EditWaterArea` 中 `setPolygonVertices` 覆盖 `fitToSceneModels` 视图 | 低 |
| 3 | `WriteWaterAreasToml` 不写多边形顶点到 TOML（历史遗留） | 中 |

---

## 补充说明

### ModelManager 的 `modelBBoxCache_` 访问权

需要新增一个公开 getter 供 `GatherSceneFootprints()` 使用：

```cpp
// ModelManager.hh
public:
    const std::map<std::string, keship::ModelFootprint> &GetBBoxCache() const {
        return modelBBoxCache_;
    }
```

### 现有代码验证结果

以下假设已与源码核对，确认正确（2026-06-21）：

| 验证项 | 源码位置 | 状态 |
|--------|---------|------|
| `GetCachedModelGeometry(name, isBox, dx, dy, dz, radius, length, meshPath)` | `ModelManager.hh:399-401` | ✅ 签名匹配 |
| `GetModelList()` 返回 `map<string,uint32_t>` | `ModelManager.hh:378` | ✅ |
| `GetCachedModelStatic(name, isStatic)` | `ModelManager.hh:404` | ✅ |
| `DeleteModelInGazeboByName` 统一删除入口 | `ModelManager.cc:869-902` | ✅ |
| `MeshUtils::LoadMeshFromFile(path)` 返回 `MeshDataSimple` | `MeshUtils.hh:45` | ✅ |
| `MeshDataSimple.vertices` 为 `vector<gz::math::Vector3d>` | `MeshUtils.hh:27` | ✅ |
| MenuPluginKeship 无独立 gz::transport::Node（通过 `modelManager->GetNode()` 访问） | `MenuPluginKeship.hh` | ✅ 已修正方案 |
| `gz::math::Quaterniond(w, x, y, z)` 参数顺序为 w 在前 | `IncliningExperimentDialog.cc:494` | ✅ 已修正方案 |
| MapWidget 坐标转换方法 `toX/toY/fromX/fromY/plotW/plotH` | `MapWidget.hh:77-86` | ✅ |
| MapWidget 视图字段 `viewXMin_/viewXMax_/viewYMin_/viewYMax_` | `MapWidget.hh:122-123` | ✅ |
| WaterAreaDrawDialog 已有 `map_` 成员 | `WaterAreaDrawDialog.hh:34` | ✅ |
| SpawnModelInGazeboFromFile 中缓存赋值位置 | `ModelManager.cc:177-183` | ✅ |

---

## 设计要点

### 1. 为什么包围盒在 spawn 时计算而非懒加载？

- Mesh 包围盒计算需要遍历全部顶点，大模型（如 78K 三角形的嵙海三号）会卡顿
- spawn 发生在用户操作间隙，卡顿感知弱；打开对话框时操作，卡顿感知强
- 与现有缓存体系（6 个 map）风格一致

### 2. 为什么位姿走 Pose_V 订阅而非服务调用？

- `GetModelPoseByIDFromGazebo` 是阻塞 gz-transport 服务请求（`/world/{world}/state`）
- 20 个模型 = 20 次顺序阻塞，~1-2 秒 UI 冻结
- Pose_V 是 Gazebo 已广播的话题，每个物理步都发布，无需额外开销
- 内存缓存读取 O(1)，无阻塞

### 3. Pose 缓存在 MenuPluginKeship 而非 ModelManager

- ModelManager 是数据层，不应负责 UI 订阅管理
- Pose_V 订阅生命周期与 GUI 插件一致
- 保持 ModelManager 单纯的数据/服务角色

### 4. 过滤规则

| 实体名 | 原因 |
|--------|------|
| `sun` | Gazebo 内置光源 |
| `ground_plane` | Gazebo 内置地面 |
| `waves_*` | 水区水面可视化模型（非用户关注） |

### 5. 旋转矩形渲染

QPainter 不支持直接画旋转矩形，通过 `QPainter::translate` + `rotate` 变换坐标系实现。旋转后在 `resetTransform()` 后绘制文字，确保标签始终水平可读。

### 6. 后续优化方向

- 非矩形 Mesh 模型用多边形轮廓代替矩形包围盒（如 L 形建筑）
- 模型贴图作为底图背景（需要地图模型提供 overhead 截图）
- 3D 边界线框 Marker 可视化（原 water_area_implementation_plan 中的计划）
