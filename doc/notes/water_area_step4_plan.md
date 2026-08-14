# Step 4.1：多边形绘制 UX 增强

**日期**：2026-06-18 | **状态**：✅ 已完成

---

## 改动 1：名称输入框

**`WaterAreaDrawDialog`** 顶部加 `QLineEdit` 名称输入框，默认值由外部传入。

### `WaterAreaDrawDialog.hh`

```diff
+ void setDefaultName(const QString &_name);
+ QLineEdit *nameEdit_;
```

### `WaterAreaDrawDialog.cc`

构造函数中在 hint 和 map_ 之间插入：

```cpp
nameEdit_ = new QLineEdit(this);
nameEdit_->setPlaceholderText("水域名称");
layout->addWidget(nameEdit_);
```

`getWaterArea()` 新增返回名称：

```diff
  keship::WaterArea getWaterArea() const { return result_; }
+ QString name() const { return nameEdit_->text().trimmed(); }
```

`onPolygonFinished` 中填入名称：

```cpp
result_.name = nameEdit_->text().trimmed().toStdString();
if (result_.name.empty())
    result_.name = "水域";
```

### `MenuPluginKeship.cc` — `OnBtn_AddWaterArea`

```diff
  WaterAreaDrawDialog dlg;
+ dlg.setDefaultName(
+     QString::fromStdString("水域" + std::to_string(customWaterAreas_.size() + 1)));
  if (dlg.exec() != QDialog::Accepted) return;

  keship::WaterArea area = dlg.getWaterArea();
- if (area.name.empty())
-   area.name = "水域" + std::to_string(customWaterAreas_.size() + 1);
```

---

## 改动 2：鼠标悬停显示顶点坐标

**`MapWidget::mouseMoveEvent`** — 在多边形拖拽检查之后、`editable_` 检查之前插入：

```cpp
  // ── 多边形悬停提示 ──
  if (polygonMode_ && dragIdx_ < 0) {
    int px = (int)_event->pos().x(), py = (int)_event->pos().y();
    double minDist = 10.0 * (viewXMax_ - viewXMin_) / plotW();
    for (size_t i = 0; i < polygonVertices_.size(); ++i) {
      int vx = toX(polygonVertices_[i].first);
      int vy = toY(polygonVertices_[i].second);
      double d = std::hypot(px - vx, py - vy);
      if (d < 8) {
        QToolTip::showText(_event->globalPos(),
            QString("顶点 %1  X=%2  Y=%3")
                .arg(i + 1)
                .arg(polygonVertices_[i].first, 0, 'f', 1)
                .arg(polygonVertices_[i].second, 0, 'f', 1));
        return;
      }
    }
    QToolTip::hideText();
  }
```

已有 include `<QToolTip>`（`MapWidget.cc:11`），无需新增。

---

## 改动 3：双击顶点编辑坐标

**`MapWidget::mouseDoubleClickEvent`** — 在 `polygonFinished` 检查**之前**插入顶点编辑：

```cpp
void MapWidget::mouseDoubleClickEvent(QMouseEvent *_event) {
  // ── 多边形模式：双击顶点编辑坐标 ──
  if (polygonMode_) {
    int pw = plotW(), ph = plotH();
    if (pw > 0 && ph > 0) {
      double wx = fromX((int)_event->pos().x());
      double wy = fromY((int)_event->pos().y());
      double minDist = 8.0 * (viewXMax_ - viewXMin_) / pw;
      int hitIdx = -1; double best = minDist;
      for (size_t i = 0; i < polygonVertices_.size(); ++i) {
        double d = std::hypot(polygonVertices_[i].first - wx,
                               polygonVertices_[i].second - wy);
        if (d < best) { best = d; hitIdx = (int)i; }
      }
      if (hitIdx >= 0) {
        bool ok;
        double newX = QInputDialog::getDouble(this, "编辑顶点 X",
            QString("顶点 %1  X 坐标").arg(hitIdx + 1),
            polygonVertices_[hitIdx].first, -9999, 9999, 1, &ok);
        if (!ok) return;
        double newY = QInputDialog::getDouble(this, "编辑顶点 Y",
            QString("顶点 %1  Y 坐标").arg(hitIdx + 1),
            polygonVertices_[hitIdx].second, -9999, 9999, 1, &ok);
        if (!ok) return;
        polygonVertices_[hitIdx].first  = newX;
        polygonVertices_[hitIdx].second = newY;
        update();
        return;
      }
    }

    // 双击空白 + ≥3 顶点 → 闭合
    if (polygonVertices_.size() >= 3) {
      emit polygonFinished();
      return;
    }
  }

  // 仅多边形模式时返回（阻止 waypoint 双击逻辑）
  if (polygonMode_) return;

  // ... 现有 waypoint 双击逻辑 ...
```

---

## 文件改动汇总

| 文件 | 改动 |
|------|------|
| `WaterAreaDrawDialog.hh` | +`nameEdit_` + `setDefaultName()` + `name()` |
| `WaterAreaDrawDialog.cc` | 构造函数加名称输入框；`onPolygonFinished` 填入名称 |
| `MapWidget.cc` | `mouseMoveEvent` +悬停提示；`mouseDoubleClickEvent` +顶点编辑 |
| `MenuPluginKeship.cc` | `OnBtn_AddWaterArea` 传入默认名称；删除空名检查 |

---

## 交互总结

```
[添加水域] → DrawDialog
  ├─ 名称框: "水域1"（可编辑）
  ├─ 2D 俯视地图
  │   ├─ 左键放置顶点
  │   ├─ 拖拽移动顶点
  │   ├─ 右键删除顶点
  │   ├─ 悬停显示 X=... Y=...
  │   ├─ 双击顶点 → 弹框编辑坐标
  │   ├─ 双击空白（≥3顶点）→ 完成
  │   └─ 滚轮缩放 / 中键平移
  ├─ [完成] [手动输入] [取消]
```
