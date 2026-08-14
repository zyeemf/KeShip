# gz‑transport 回调安全关闭问题

> 日期：2026-07-16
> 触发问题：关闭 Gazebo 窗口后程序未完全退出，控制台持续输出 `malloc_consolidate(): unaligned fastbin chunk detected`

---

## 一、问题根因

### 1.1 现象

```
关闭 Gazebo 窗口
  → [gazebo-1] malloc_consolidate(): unaligned fastbin chunk detected
  → [gazebo-1] [GUI] Shutting down gz-sim-gui
  → 进程未完全退出，`gz topic -l` 仍显示残留 topic
```

### 1.2 原因

Gazebo 关闭时模型被批量删除，`KSPlugin_*` 等系统插件的析构函数为默认实现（`= default`），**没有显式取消 gz‑transport 订阅**。transport 线程独立于主线程运行，回调 lambda 中捕获的裸指针（`this`、`bodyPtr` 等）在析构后变为悬空指针。当 transport 线程仍在尝试执行回调时，访问已释放的堆内存 → 堆元数据损坏 → `malloc_consolidate` 报错。

### 1.3 典型崩溃链

```
Gazebo 关闭 → 批量删除模型
  → KSPlugin_Thrust::~KSPlugin_Thrust() = default
    → dataPtr 析构 → Implementation 析构
      → bodies 析构 → ThrusterBody 析构 → bodyPtr 悬空
        ↓ 同时
      transport 线程: callback → bodyPtr->currentLevel.store(...) → 访问已释放内存
        ↓
      malloc_consolidate(): unaligned fastbin chunk detected
```

---

## 二、修复方案：两阶段关闭

### 2.1 核心模式

```
第一阶段（持有锁）:   设置 alive_ = false → 通知所有在飞的回调"别再碰成员变量"
第二阶段（释放锁后）:  Unsubscribe(topic) → 等待 pending callback 执行完毕 → 安全析构成员
```

### 2.2 时序保证

```
主线程 (析构)                          Transport 线程
=============                          ==============
lock(mutex)                            callback 正在运行 ← 阻塞在 lock(mutex)
alive_ = false                         
unlock(mutex)                          
                                       lock(mutex) 成功
                                       if (!alive_) return; ← 安全退出
node.Unsubscribe(topic)                (等待所有 pending callback 结束)
← 返回: 保证无 callback 在运行
~members...                            ← 安全析构
```

### 2.3 为什么仅 Unsubscribe 不够

`Unsubscribe` 只从订阅表中移除条目，**不保证没有正在执行的回调实例**。若先析构成员再 Unsubscribe（或析构中无 Unsubscribe），会出现：

```
Unsubscribe(topic) → 注册解除
                    ← 但 callback 还在飞!
delete dataPtr      ← 释放 mutex / wavefield / ...
callback 访问       ← SEGFAULT / heap corruption
```

---

## 三、受影响组件

### 3.1 全扫描结果

| # | 文件 | Subscribe 点 | 进程 | 回调捕获 | 修复方式 |
|---|------|------------|------|---------|---------|
| 1 | `KSPlugin_Buoyancy.cc` | wavefield topic | Server | `this->dataPtr.get()` | 两阶段关闭 |
| 2 | `WavefieldPublisher.cc` | `_set` control topic | Server | `this->dataPtr.get()` | 两阶段关闭 |
| 3 | `WaveVisual.cc` | wavefield topic | Server | `this->dataPtr.get()` | 两阶段关闭 |
| 4 | `KSPlugin_Thrust.cc` | 每个推进器 1 个 topic | Server | `[bodyPtr]` 裸指针 | 析构 Unsubscribe |
| 5 | `KSPlugin_Controller.cc` | variable_load topic | Server | `[this]` 裸指针 | 两阶段关闭 |
| 6 | `MenuPluginKeship.cc` | pose info | GUI | `[this]` 裸指针 | 析构 Unsubscribe |
| 7 | `TaskMonitorDialog.cc` | pose + thrust topics | GUI | `shared_ptr<State>` | 已有 alive ✅ |
| 8 | `DisplacementVerifyDialog.cc` | pose topic | GUI | `shared_ptr<State>` | 已有 alive ✅ |
| 9 | `HullStabilityDialog.cc` | pose topic | GUI | `shared_ptr<State>` | 已有 alive ✅ |
| 10 | `IncliningExperimentDialog.cc` | pose topic | GUI | `shared_ptr<State>` | 已有 alive ✅ |
| 11 | `StabilityExperimentDialog.cc` | pose topic | GUI | `shared_ptr<State>` | 已有 alive ✅ |
| 12 | `SensorOverlayPlugin.cc` | sensor_view topic | GUI | 成员函数指针 | 低风险（GUI 进程长生命周期） |
| 13 | `KSPlugin_Thrust.cc` | 每个推进器 1 个 topic | Server | 同 #4 | — |

### 3.2 各组件具体改动

#### KSPlugin_Buoyancy::Implementation

```cpp
// .hh 新增:
~Implementation();
bool alive_ = true;

// .cc 回调:
void OnWavefield(const gz::msgs::Param &_msg) {
    std::lock_guard<std::mutex> lock(this->mutex);
    if (!this->alive_) return;        // ← 新增
    this->wavefield_.Load(_msg);
}

// .cc 析构:
~Implementation() {
    { std::lock_guard<std::mutex> lock(mutex); alive_ = false; }
    if (!wavefield_.Topic().empty())
        node.Unsubscribe(wavefield_.Topic());
}
```

#### WavefieldPublisherPrivate

```cpp
// 新增:
std::mutex mutex_;
bool alive_ = true;

// 回调:
void OnWavefield(const msgs::Param &_msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!alive_) return;
    wavefield.Load(_msg);
    republishRequested_ = true;
}

// 析构:
~WavefieldPublisherPrivate() {
    { std::lock_guard<std::mutex> lock(mutex_); alive_ = false; }
    node.Unsubscribe(topic + "_set");
    pub = {};
}
```

#### WaveVisualPrivate

```cpp
// 与 Buoyancy 相同模式，额外清理 rendering connection:
~WaveVisualPrivate() {
    { std::lock_guard<std::mutex> lock(mutex); alive_ = false; }
    if (!wavefield.Topic().empty()) node.Unsubscribe(wavefield.Topic());
    if (connection) connection.reset();
    if (teardownConnection) teardownConnection.reset();
}
```

#### KSPlugin_Thrust::Implementation

回调直接捕获 `bodyPtr`（物理数据指针），无法加 `alive_` 检查。采用纯 Unsubscribe 保护：

```cpp
// .hh Implementation 新增:
std::vector<std::string> thrusterTopics;
~Implementation();

// .cc Configure 中 新增:
this->dataPtr->thrusterTopics.push_back(topic);

// .cc 析构:
~Implementation() {
    for (const auto &t : thrusterTopics)
        node.Unsubscribe(t);
}
```

> 关键：`Unsubscribe` 会等待该 topic 的 pending callback 执行完毕再返回，因此析构函数体返回后 `bodies` 可安全销毁。

#### KSPlugin_Controller::Implementation

与 Buoyancy 完全相同的模式：

```cpp
// .hh 新增:
~Implementation();
bool alive_ = true;

// .cc 回调 新增:
if (!this->dataPtr->alive_) return;

// .cc 析构:
~Implementation() {
    { std::lock_guard<std::mutex> lock(variableLoad.payloadMutex); alive_ = false; }
    if (!variableLoad.topicName.empty())
        node.Unsubscribe(variableLoad.topicName);
}
```

#### MenuPluginKeship

```cpp
~MenuPluginKeship() {
    modelManager->GetNode().Unsubscribe(
        "/world/" + modelManager->GetWorldName() + "/pose/info");  // ← 新增
    rclcpp::shutdown();
    if (spinThread_.joinable()) spinThread_.join();
}
```

---

## 四、注意事项

### 4.1 `bool alive_` 的线程安全性

- 有 `std::mutex` 保护的组件（Buoyancy/Publisher/WaveVisual/Controller）：`alive_` 读写均在锁内执行 → 线程安全
- `KSPlugin_Thrust` 不需要 `alive_`（回调不捕获 Implementation），使用纯 Unsubscribe 模式
- `MenuPluginKeship` 的 lambda 捕获 `this` 但无 alive 保护：依赖 `modelManager` 生命期覆盖析构体且 Unsubscribe 在 `rclcpp::shutdown` 之前调用

### 4.2 `Unsubscribe` 在析构函数体中的有效性

C++ 析构顺序：**析构函数体先执行 → 成员按声明逆序析构**。因此在析构函数体中所有成员仍然有效，可以安全访问 `node.Unsubscribe()`、`wavefield_.Topic()` 等。

### 4.3 `malloc_consolidate` 的其他可能来源

- WSLg D3D12/NVIDIA GPU 驱动退出时的资源释放顺序问题（与启动 segfault 同源，`start_keship.sh` 已有预热和重试保护启动阶段，退出阶段无法干预）
- 非本次范围

---

## 五、文件改动汇总

| commit | 内容 | 文件数 |
|--------|------|:---:|
| `e910cb6` | Buoyancy + Publisher + WaveVisual 两阶段关闭 | 4 |
| `a8a107a` | WavefieldPublisher 新增 mutex_ 线程安全 | 1 |
| `4098be9` | Thrust + Controller + MenuPluginKeship 关闭 | 5 |

---

## 六、相关专题文档

| 文档 | 内容 |
|------|------|
| `wind_wave_current_design.md` | 风浪流完整设计 |
| `water_area_ipc_refactor.md` | 水域 IPC 重构记录 |
| `fossen_physics_refactor.md` | Fossen 物理重构记录 |
