# WaveVisual SDF Schema 警告分析

**日期**: 2026-05-25  
**状态**: 已关闭（接受为无害警告）

## 警告信息

```
[Wrn] [Utils.cc:132] XML Element[plugin], child of element[sdf], not defined in SDF. Copying[plugin] as children of [sdf]
```

## 原因

WaveVisual 插件位于 `<visual>` 元素内部：

```xml
<visual name="wave_visual">
  <plugin filename="libWaveVisual.so" name="keship::WaveVisual">
    ...
  </plugin>
</visual>
```

Gazebo 在加载时会将 visual 内的 `<plugin>` 重包装为 `<sdf><plugin>...</plugin></sdf>` 结构进行内部解析。SDF schema 校验器检查重包装后的结构时，发现 `<plugin>` 不是 `<sdf>` 的有效子元素，从而发出警告。

这是 Gazebo Harmonic (gz-sim8) 框架的内部行为，不是代码错误。

## 尝试过的解决方案

### 方案 1：将 plugin 移到 `<model>` 层（失败）

**思路**：`<plugin>` 是 `<model>` 的有效子元素，移到 model 层可消除警告。

**问题**：Configure 收到的是 model entity ID，而渲染 BFS 需要 visual entity ID 来匹配场景图中的渲染节点。场景图中只有 visual 实体对应的渲染节点带有 `gazebo-entity` 用户数据——model 和 link 实体没有。

**尝试的子方案**：
| 子方案 | 时机 | 结果 |
|--------|------|------|
| ECM 查找（Configure 阶段） | Configure 中 `LinkByName` + `_ecm.Each` | 失败 — 子实体可能未创建 |
| ECM 查找（PreUpdate 阶段） | PreUpdate 中延迟查找 + `visualResolved` 标志 | 失败 — 原因未明 |

### 方案 2：`SceneManager::VisualById(Entity)`（不可行）

Gazebo 提供了 `SceneManager::VisualById(Entity _id)` 方法（O(1) 从 sim entity ID 查找渲染 Visual），但 SceneManager 只能通过 `RenderUtil`（GUI 侧基础设施）访问，服务端 System 插件无法获取。

### 方案 3：保持现状（采用）

Plugin 仍在 `<visual>` 内，接受无害的 SDF schema 警告。

## 渲染管线

```
Configure(_entity=visual实体)
  → this->entity = visual_entity_id
  → 连接 SceneUpdate 事件

PreUpdate(simTime)
  → 存储仿真时间

SceneUpdate 事件触发 OnUpdate() [渲染线程]
  → BFS 遍历场景图，查找 gazebo-entity == this->entity
  → 找到 VisualGeometry 节点
  → 绑定自定义 shader 材质和参数
```

## 关键发现

1. `gazebo-entity` 用户数据仅设置在 visual 实体对应的渲染节点上
2. model/link 实体在场景图中没有对应的渲染节点（或有但无 `gazebo-entity`）
3. BFS 需要在 OnUpdate 首次运行时执行，成功后缓存 `this->visual` 指针
4. `SceneUpdate` 事件在渲染线程触发，与 PreUpdate（服务端线程）存在线程分离
5. 这是 VRX 项目（Gazebo 专家开发）也采用的模式
