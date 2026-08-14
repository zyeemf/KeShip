# Phase D 详细方案：学生编程模块

> KeShip 项目 | 2026-06-02

---

## 一、教学目标

学生根据型值表，用梯形积分法编程计算排水体积，与系统标准答案对比。培养学生将数值方法转化为代码的能力。

---

## 二、系统架构

```
CodeRunnerDialog (C++ Qt)
    │ 学生编写代码
    ├─ [Python] → QProcess → keship_code_runner.py → subprocess python -c
    └─ [C]      → QProcess → keship_code_runner.py → gcc 编译 + 执行
    │
    ▼ 捕获 stdout（期望一个浮点数）
    与 ShipHullGenerator::volumeAtDraft() 标准答案对比
    → 显示结果 + 误差 + 判定
```

---

## 三、数据提供方式

系统从 `ShipHullGenerator::lines()` 获取型值数据，注入到学生代码中。

| 难度 | 提供数据 | 学生需要做什么 |
|------|---------|---------------|
| 初级 | 各站位横剖面面积 `areas[N]` + 站距 `dx` | 一维梯形积分：`V = Σ (A[i]+A[i+1])/2 * dx` |
| 中级 | 型值表 `halfBreadths[M][N]` + 吃水 `draft` | 二维梯形积分：先算剖面面积再积体积 |
| 高级 | 船型参数 (L, B, D) + 曲面公式 | 自行推导积分公式 + 数值实现 |

**数据注入机制**：系统生成代码前缀（声明变量 + 赋值），拼接到学生代码前面，组成完整可执行程序。

---

## 四、代码模板

### Python 模板（初级难度）

```python
# ===== 给定数据（系统自动注入，请勿修改）=====
# stations = [0.0, 0.15, 0.30, ...]   # 站位 x 坐标 (m)
# areas    = [0.0, 0.12, 0.23, ...]   # 各站位横剖面面积 (m²)
# N = 41                               # 数据点个数
# dx = 0.15                            # 站距 (m)

def calculate_displacement(stations, areas, N, dx):
    """用梯形积分法计算排水体积"""
    V = 0.0
    # TODO: 实现梯形积分 V = Σ (A[i] + A[i+1]) / 2.0 * dx
    # 提示：写一个 for 循环，从 i=0 到 i=N-2

    return V

# ===== 以下代码请勿修改 =====
result = calculate_displacement(stations, areas, N, dx)
print(f"{result:.6f}")
```

### C 模板（初级难度）

```c
/* ===== 给定数据（系统自动注入，请勿修改）===== */
#include <stdio.h>
#define N 41

double stations[N] = {0.0, 0.15, 0.30, ...};  /* 站位 x (m) */
double areas[N]    = {0.0, 0.12, 0.23, ...};  /* 剖面面积 (m²) */
double dx = 0.15;                              /* 站距 (m) */

/* ===== 请在此处编写你的代码 ===== */
/* 目标：用梯形积分法计算排水体积 V            */
/* 公式：V = Σ (A[i] + A[i+1]) / 2.0 * dx     */
/* 提示：写一个 for 循环，从 i=0 到 i<N-1      */

int main() {
    double V = 0.0;
    /* TODO: 实现梯形积分 */


    printf("%.6f\n", V);
    return 0;
}
/* ===== 代码结束 ===== */
```

---

## 五、沙箱实现

### Python 沙箱

```
QProcess 调用 → keship_code_runner.py → subprocess.run(python -c)
  ├── 数据注入为代码前缀
  ├── cwd=/tmp（隔离文件系统）
  ├── 环境变量最小化
  ├── timeout=10s
  └── 返回 json: {ok, stdout, stderr}
```

### C 沙箱

```
QProcess 调用 → keship_code_runner.py
  ├── Step 1: 生成完整 .c 源文件（preamble + 学生代码）
  ├── Step 2: gcc 编译 (-Wall, timeout=10s)
  ├── Step 3: 执行编译产物 (timeout=5s)
  └── 返回 json: {ok, stdout, stderr} 或 {error: "编译失败"}
```

### 安全措施

| 风险 | 防护 |
|------|------|
| 死循环 | Python 超时 10s，C 运行超时 5s |
| 内存越界 | C `-Wall` 编译检查，固定数组大小 |
| 文件写入 | 执行在 `/tmp`，环境变量 `HOME=/tmp` |
| 恶意代码 | 代码长度限制 < 5000 字符 |

---

## 六、执行流程

```
1. 学生选择语言（Python/C）+ 难度（初级/中级/高级）
2. 系统根据难度生成代码模板 + 注入数据
3. 学生填充 TODO 区域
4. 点击 [运行代码]
5. QProcess 调用 keship_code_runner.py
6. 沙箱执行，捕获 stdout
7. 解析输出浮点数 → 与标准答案对比
8. 显示结果、误差、判定
```

**标准答案**：`ShipHullGenerator::volumeAtDraft(draft)`。

---

## 七、评分系统

| 误差 | 判定 | 颜色 |
|------|------|------|
| < 5% | ✅ 通过 | 绿色 |
| 5% ~ 10% | ⚠️ 接近 | 橙色 |
| ≥ 10% | ❌ 不通过 | 红色 |

---

## 八、UI 布局

```
┌──────────────────────────────────────────────────────────┐
│  编程练习：梯形积分法计算排水体积                           │
│                                                            │
│  船型: [Wigley经典 ▼]  L:6.0  B:1.5  D:1.0  ρ:500       │
│  吃水: 0.500 m              标准答案 ∇ = 2.01 m³          │
│                                                            │
│  难度: ○ 初级  ○ 中级  ○ 高级    语言: ○ Python  ○ C      │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │                                                      │ │
│  │   (QPlainTextEdit 代码编辑区，monospace 字体)         │ │
│  │                                                      │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  [运行代码]  [查看提示]  [重置模板]                        │
│                                                            │
│  ┌──────────────────────────────────────────────────────┐ │
│  │  运行输出 / 编译错误信息                               │ │
│  └──────────────────────────────────────────────────────┘ │
│                                                            │
│  结果: ___ m³    误差: ___%    判定: ✅ 通过               │
└──────────────────────────────────────────────────────────┘
```

**查看提示**：点击显示梯形积分公式、伪代码或示例，不直接给出答案。

---

## 九、实现文件清单

| 文件 | 说明 |
|------|------|
| `CodeRunnerDialog.hh` | Qt 对话框头文件 |
| `CodeRunnerDialog.cc` | 完整实现：模板生成 + QProcess 调用 + 结果展示 |
| `scripts/keship_code_runner.py` | Python/C 沙箱执行器 |
| `MenuPluginKeship.hh/.cc` | 加 `OnBtn_CodeRunner()` 槽 |
| `MenuPluginKeship.qml` | Tab 2 实验列表加条目 |
| `CMakeLists.txt` | 加 CodeRunnerDialog.cc，安装 keship_code_runner.py |

---

## 十、潜在风险与对策

| 风险 | 对策 |
|------|------|
| WSL 中无 gcc | C 模式启动时检测 gcc 是否可用，不可用则置灰 |
| Python 版本差异 | 使用系统 `python3`，不做版本假设 |
| 中文输出乱码 | stdout 统一 UTF-8 编码 |
| 学生代码语法错误 | 完整捕获 stderr，展示给用户 |

---

## 十一、待讨论

1. 是否需要"查看提示"功能？提示内容如何设计才能引导思考而不直接给出答案？
2. 中级难度（二维梯形积分）是否首版就实现？还是先只做初级？
3. CodeRunnerDialog 入口位置：主界面 Tab 2 实验列表？还是独立按钮？

---

## 十三、Phase 13 代码审查 Bugs (2026-06-02)

| # | 严重度 | 位置 | 问题 | 修复 |
|---|--------|------|------|------|
| 1 | 🔴 | `MapWidget.cc` `toX/toY` | `pw ≤ 0` 时除以零崩溃 | guard `pw > 0` |
| 2 | 🔴 | `TaskSetDialog::onSave` | 模型列表为空时 `controlled_model = ""` | 保存前检查 `modelCombo_->count() > 0` |
| 3 | 🔴 | `TaskSetDialog` `onTaskTypeChanged` | tracking 显示 waypoint 的 `arrival_radius` 面板，缺少 `deviation_tolerance` | tracking 单独面板或不显示 radius |
| 4 | 🟡 | `TaskSetDialog::onSave` | DP heading 检查 `> -900`：-900~-998 之间的值会误写 | 改为 `!= -999` |
| 5 | 🟡 | `MapWidget.cc:14` | `setMouseTracking(true)` 不必要 | 移除或改为 `false` |
| 6 | 🟡 | `MapWidget.cc:141` | 拖拽可移出可视范围 | 后续加边界约束 |
| 7 | 🟡 | `TaskSetDialog` | `toggled` 信号双触发导致面板闪烁 | 用 `clicked` 替代或加防抖 |
| 8 | 🟡 | `TaskSetDialog` | 关闭不提示未保存 | 后续加 `closeEvent` |
| 9 | 🟡 | `MapWidget.cc:53` | 船标硬编码像素大小 | 后续改为比例缩放 |
| 10 | 🟡 | `TaskSetDialog` | 保存后无信号通知 | 后续加 `taskSaved` 信号 |

### 修复计划

**立即修复**（#1~4，崩溃/数据错误/逻辑错误）
**本 Phase 后续修复**（#7，面板闪烁）
**Phase 13 整体完成后再修**（#5,6,8,9,10，不影响基本功能）
