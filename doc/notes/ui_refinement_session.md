# 交互界面修改优化（2026-05-26 ~ 2026-05-27）

## 一、ModelSettingsDialog 布局重构

### 整体布局
- 从 QHBoxLayout（左表单 | 右预览）改为 QVBoxLayout（上表单 | 中预览 | 下按钮）
- 窗口尺寸：650 × 600

### 位置/姿态
- X/Y/Z 位置合并为一行，prefix 为 `x ` `y ` `z `，suffix `m`，decimals=3
- Roll/Pitch/Yaw 合并为一行，prefix 为 `Roll(绕X) ` 等，suffix `°`，decimals=2

### 设置行（3 个 checkbox 并行）
- "静态模型" / "添加浮力项" / "惯性参数" 三个 checkbox 放在同一行，form label "设置"
- 勾选"添加浮力项"自动勾选并禁用"惯性参数"（保留 isEnabled 守卫）

### 惯性质量
- 独立 form row "惯性质量"，decimals=3，suffix `kg`
- 随"惯性参数" checkbox 启用/禁用

### 惯性张量 3×3 矩阵
- QGridLayout 排列，每格 prefix 为 `Ixx: ` 等，suffix `kg·m²`，decimals=4
- 随"惯性参数" checkbox 启用/禁用

### 重心偏移
- X/Y/Z 同一行，prefix 为 `x ` `y ` `z `，suffix `m`，decimals=3
- 随"惯性参数" checkbox 启用/禁用

### CoG 3D 预览（Buoyancy3DView）
- 对话框中间嵌入，默认隐藏
- OnlyPose 模式显示，橙色半透明小球标记重心位置
- CoG spinbox 值变化时实时更新

## 二、KSM 模式行为调整

### 去掉隐藏，改为置灰
- KSM 模式下三 checkbox 置灰 + tooltip 说明"由 KSM 文件配置"
- 浮力/惯性/CoG spinbox 保持禁用
- 用户可见全部配置但不可编辑，仅允许改模型名称和位姿

### 删除 ksmInfoLabel_
- 去掉"浮力体/推进器配置已在文件中"提示标签
- 从 .hh 和 .cc 完全移除

### 惯性/CoG 数据回填
- 新增 `ModelManager::ParseInertiaFromSDF` / `ModelManager::ParseStaticFromSDF` 静态方法
- KSM 加载流程调用上述方法从 baseSDF 提取惯性参数填入对话框
- `GetCachedModelInertia` / `GetCachedModelStatic` 重构复用静态方法

## 三、信号泄漏修复

### 问题
KSM 模式下 checkbox 被 `setEnabled(false)`，但 `setChecked(true)` 仍触发 `stateChanged` 信号，级联启用本应禁用的 spinbox。

### 修复
4 处信号连接 lambda 增加 `isEnabled()` 守卫：

| 信号源 | 守卫 |
|--------|------|
| `addBuoyancyCheckBox::stateChanged` | `if (!addBuoyancyCheckBox->isEnabled()) return;` |
| `setInertiaCheckBox::stateChanged` → 惯性质量 | `&& setInertiaCheckBox->isEnabled()` |
| `setInertiaCheckBox::stateChanged` → 惯性张量 | `&& setInertiaCheckBox->isEnabled()` |
| `setInertiaCheckBox::stateChanged` → CoG | `&& setInertiaCheckBox->isEnabled()` |

## 四、BuoyancyEditorDialog 删除按钮修复

### 问题
删除第一个浮力体后无法删除剩余最后一个：`updateList()` 内部静默恢复 currentRow 与 `onDeleteBody()` 的 `setCurrentRow(0)` 冲突，导致 `currentBodyIdx_` 未更新。

### 修复
1. 删除 `updateList()` 内部的 `setCurrentRow(oldRow)` 恢复逻辑
2. `onSaveBody()` 补上显式 `bodyList_->setCurrentRow(currentBodyIdx_)` 恢复选中

## 五、涉及文件清单

| 文件 | 修改内容 |
|------|----------|
| `ModelSettingsDialog.hh` | 新增 setBuoyancy / CoG 预览成员；删除 ksmInfoLabel_ / QLabel include |
| `ModelSettingsDialog.cc` | 布局重构；KSM 置灰；CoG 预览；信号守卫；删除 ksmInfoLabel_ |
| `ModelSettingsDialog.cc`（构造函数） | Full/OnlyPose/Ksm 三种模式行为 |
| `Buoyancy3DView.hh` | 新增 setCoGPosition / setShowCoG / CoG 球成员 |
| `Buoyancy3DView.cc` | CoG 球绘制 + 接口实现 |
| `ModelManager.hh` | 新增 ParseInertiaFromSDF / ParseStaticFromSDF 静态方法；InertiaParams 结构 |
| `ModelManager.cc` | 实现静态解析方法；重构 GetCachedModelInertia/Static |
| `MenuPluginKeship.cc` | KSM 加载流程调用静态方法回填对话框数据 |
| `BuoyancyEditorDialog.cc` | updateList 清理；onSaveBody 选中恢复；删除按钮 bug 修复 |

## 六、三种模式行为矩阵

| 字段 | Full | OnlyPose | KSM |
|------|------|----------|-----|
| 模型名称 | 可编辑 | 只读 | 可编辑 |
| 位置/姿态 | 可编辑 | 可编辑 | 可编辑 |
| 静态模型 | 可勾选 | 置灰 | 置灰 |
| 添加浮力项 | 可勾选 | 隐藏 | 置灰 |
| 惯性参数 | 可勾选 | 可勾选 | 置灰 |
| 惯性质量/张量/CoG | 随勾选启用 | 随勾选启用 | 禁用 |
| CoG 预览 | 隐藏 | 显示 | 隐藏 |
