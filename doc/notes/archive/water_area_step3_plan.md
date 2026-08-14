# Step 3：GUI 水区编辑器 + 默认/自定义双模式

**日期**：2026-06-18 | **状态**：✅ 完成 | **依赖**：Step 1 + Step 2

---

## 目标

主界面勾选框改为模式开关：勾选 → 内置默认 500m×500m 大水域；取消 → 自由添加/编辑/删除自定义小水域。自定义水区与模型同列表显示。

---

## 核心简化：移除 `waterAreasEnabled_` 机制

当前 Buoyancy 通过两步控制浮力：

```
① 读 /tmp/keship_water_enabled → waterAreasEnabled_
② if (configured) → 水区查找
```

Step 3 简化为一步——浮力纯粹由是否存在水区决定：

```
① 读 /tmp/keship_water_areas.toml → waterAreaMgr_.LoadFromToml
② if (configured) → 水区查找 → 在区内=浮力，在区外=无浮力
③ else (无任何水区) → inWater = false（无浮力）
```

### 移除清单

| 移除项 | 位置 |
|--------|------|
| `waterAreasEnabled_` 成员 | `KSPlugin_Buoyancy.hh` |
| `/tmp/keship_water_enabled` 文件读写 | `KSPlugin_Buoyancy.cc` PreUpdate + `MenuPluginKeship.cc` LoadConfig/OnToggleWater |
| 所有 `std::ofstream("/tmp/keship_water_enabled")` | 两处 |

---

## 勾选框行为矩阵

| 自定义水区数 | 勾选框状态 | 勾选行为 | "添加水域"按钮 |
|-------------|-----------|---------|--------------|
| 0 | 可选 | √=默认500×500, ×=无水 | ×时显示 |
| ≥1 | **置灰不可选** | N/A | 显示 |

---

## 新建文件

### `WaterAreaEditorDialog.hh/.cc`

字段：
- 名称（QLineEdit）
- XY 范围（xMin/xMax/yMin/yMax，QDoubleSpinBox）
- 水面高度 baseHeight（QDoubleSpinBox）
- 波浪参数区（折叠面板）：model、period、amplitude、gain、direction、angle、steepness、tau

两种模式：
- **创建模式**：空白表单 → 点击确定 → 返回 WaterArea
- **编辑模式**：从已有 WaterArea 填充 → 点击确定 → 返回修改后 WaterArea

---

## 修改文件

### `KSPlugin_Buoyancy.hh`

```diff
- bool waterAreasEnabled_ = true;
+ std::string lastWaterAreasMtime_;   // TOML 文件修改时间，用于检测变化
```

### `KSPlugin_Buoyancy.cc` — PreUpdate

```cpp
// 检测 TOML 变化并重载（每 250 帧约 1 秒）
static int reloadCounter = 0;
if (++reloadCounter >= 250)
{
    reloadCounter = 0;
    // 检查文件修改时间
    struct stat st;
    if (stat("/tmp/keship_water_areas.toml", &st) == 0)
    {
        std::string mtime = std::to_string(st.st_mtime);
        if (mtime != this->dataPtr->lastWaterAreasMtime_)
        {
            this->dataPtr->lastWaterAreasMtime_ = mtime;
            this->dataPtr->waterAreasConfigured_ =
                this->dataPtr->waterAreaMgr_.LoadFromToml(
                    "/tmp/keship_water_areas.toml");
        }
    }
    else
    {
        // 文件不存在 → 无水区
        this->dataPtr->waterAreasConfigured_ = false;
    }
}

// 简化的浮力控制（无 waterAreasEnabled_）
double effectiveFluidLevel = this->dataPtr->fluidLevel_;
bool inWater = true;

if (this->dataPtr->waterAreasConfigured_)
{
    const WaterArea *area = this->dataPtr->waterAreaMgr_.Lookup(
        linkWorldPos.X(), linkWorldPos.Y());
    if (!area)
        inWater = false;
    else
    {
        effectiveFluidLevel = area->baseHeight;
        // ... 水区切换时更新 wavefield ...
    }
}
else
{
    inWater = false;  // 无水区 = 无浮力
}
```

### `KSPlugin_Buoyancy.cc` — Configure

```diff
- // 初始加载 TOML（如果有 <water_areas_config>）
+ // 尝试从 /tmp 加载（MenuPluginKeship 会写入）
+ this->dataPtr->waterAreasConfigured_ =
+     this->dataPtr->waterAreaMgr_.LoadFromToml(
+         "/tmp/keship_water_areas.toml");
```

### `MenuPluginKeship.hh`

```cpp
// 新增
private:
    std::vector<keship::WaterArea> customWaterAreas_;
    
    void RebuildWaterAreas();           // 写 TOML + SpawnAll
    void UpdateWaterAreaMode();         // 更新勾选框状态
    
protected slots:
    void OnBtn_AddWaterArea();
    void OnBtn_EditWaterArea(int _index);
    void OnBtn_DeleteWaterArea(int _index);
```

### `MenuPluginKeship.cc` — LoadConfig

```cpp
// 启动时默认勾选 → 使用默认 500×500 水区
if (showWater_)
{
    waterAreaMgr_.Clear();
    WaterArea defaultArea;
    defaultArea.name = "默认水域";
    defaultArea.xMin = -250; defaultArea.xMax = 250;
    defaultArea.yMin = -250; defaultArea.yMax = 250;
    defaultArea.baseHeight = 0.0;
    waterAreaMgr_.AddArea(defaultArea);
    
    WriteWaterAreasToml();  // 写 /tmp/keship_water_areas.toml
    waterVisualMgr_.SetShaderDir(...);
    waterVisualMgr_.SpawnAll(waterAreaMgr_.Areas(), ...);
}
```

### `MenuPluginKeship.cc` — OnToggleWater

```cpp
void OnToggleWater(bool _checked)
{
    showWater_ = _checked;
    
    if (_checked)
    {
        // 进入默认模式：清除自定义，加载默认 500×500
        customWaterAreas_.clear();
        waterAreaMgr_.Clear();
        waterAreaMgr_.AddArea(defaultArea);
        WriteWaterAreasToml();
        waterVisualMgr_.SpawnAll(...);
    }
    else
    {
        // 进入自定义模式：清除默认
        waterVisualMgr_.DeleteAll(...);
        waterAreaMgr_.Clear();
        WriteWaterAreasToml(/*empty*/);
        // 用户通过"添加水域"按钮自行添加
    }
    
    emit ShowWaterChanged();
}
```

### `MenuPluginKeship.cc` — 自定义水区管理

```cpp
void OnBtn_AddWaterArea()
{
    WaterAreaEditorDialog dlg;
    if (dlg.exec() != Accepted) return;
    
    WaterArea area = dlg.getWaterArea();
    customWaterAreas_.push_back(area);
    waterAreaMgr_.AddArea(area);
    
    WriteWaterAreasToml();          // 更新 Buoyancy
    waterVisualMgr_.SpawnAll(...);  // 生成 Waves
    UpdateWaterAreaMode();          // 置灰勾选框
    NotifyModelListChanged();
}

void OnBtn_EditWaterArea(int _index);
void OnBtn_DeleteWaterArea(int _index);
// 类似逻辑，操作后调用 WriteWaterAreasToml() + SpawnAll + UpdateWaterAreaMode
```

### `MenuPluginKeship.cc` — WriteWaterAreasToml

```cpp
void MenuPluginKeship::WriteWaterAreasToml()
{
    std::ofstream f("/tmp/keship_water_areas.toml");
    for (const auto &area : waterAreaMgr_.Areas())
    {
        f << "[[water_areas]]\n"
          << "name = \"" << area.name << "\"\n"
          << "x_min = " << area.xMin << "\n"
          << "x_max = " << area.xMax << "\n"
          << "y_min = " << area.yMin << "\n"
          << "y_max = " << area.yMax << "\n"
          << "base_height = " << area.baseHeight << "\n\n"
          << "[water_areas.wave]\n"
          << "model = \"" << area.waveParams.model << "\"\n"
          << "period = " << area.waveParams.period << "\n"
          // ... 其他 wave params
          << "\n";
    }
}
```

### `MenuPluginKeship.qml`

```qml
CheckBox {
    text: "默认水域"
    checked: MenuPluginKeship.showWater
    enabled: MenuPluginKeship.showWater || MenuPluginKeship.customWaterAreaCount === 0
    onClicked: MenuPluginKeship.OnToggleWater(checked)
}

CustomButton {
    text: "添加水域"
    visible: !MenuPluginKeship.showWater
    onClicked: MenuPluginKeship.OnBtn_AddWaterArea()
}
```

水区条目在模型列表中以 `[水区] 池塘` 格式显示，点击属性弹出 `WaterAreaEditorDialog`，点击删除移除水区。

---

### `WaterAreaManager.hh`

```cpp
void Clear() { areas_.clear(); }
void AddArea(const WaterArea &_a) { areas_.push_back(_a); }
```

---

### `CMakeLists.txt`

```diff
  add_library(MenuPluginKeship SHARED
    ...
    src_cpp/plugins_gui/menu_plugin_keship/TaskMonitorDialog.cc
+   src_cpp/plugins_gui/menu_plugin_keship/WaterAreaEditorDialog.cc
    ...
  )
```

---

## 文件改动汇总

| 文件 | 改动 |
|------|------|
| `WaterAreaEditorDialog.hh` | **新建**：水区编辑对话框 |
| `WaterAreaEditorDialog.cc` | **新建**：对话框实现 |
| `KSPlugin_Buoyancy.hh` | 删 `waterAreasEnabled_`，加 `lastWaterAreasMtime_` |
| `KSPlugin_Buoyancy.cc` | PreUpdate 简化 + TOML 重载；Configure 读 /tmp TOML |
| `MenuPluginKeship.hh` | 新增自定义水区容器 + 槽函数 + RebuildWaterAreas |
| `MenuPluginKeship.cc` | 水区增删改 + TOML 写 + 勾选框模式逻辑 |
| `MenuPluginKeship.qml` | "添加水域"按钮 + 勾选框置灰 + 水区列表条目 |
| `WaterAreaManager.hh` | `Clear()` / `AddArea()` |
| `CMakeLists.txt` | 新增 WaterAreaEditorDialog 源文件 |

---

## 删除项

| 删除项 | 说明 |
|--------|------|
| `waterAreasEnabled_` | 不再需要独立启用标志 |
| `/tmp/keship_water_enabled` 所有读写 | 被 TOML 文件取代 |
| `LoadConfig` / `OnToggleWater` 中的 `ofstream enabled` | 移除 |

---

## 浮力行为总表

| 模式 | 水区 | inWater | buoyancy |
|------|------|---------|----------|
| 默认（勾选） | 500×500 at Z=0 | 按查找 | 区内=Z=0，区外=false |
| 自定义（取消） | 用户添加的 | 按查找 | 区内=baseHeight，区外=false |
| 无任何水区 | 无 | false | **无浮力** |
