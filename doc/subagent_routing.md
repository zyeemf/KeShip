# KeShip 子代理分层模型路由方案

> 目标：在 DSH 的两个模型 **deepseek-v4-flash**（便宜快，默认）与 **deepseek-v4-pro**（强、贵）之间，
> 让每一类工作落在成本/能力最匹配的层级：质量关键处用 pro 兜底，其余用 flash 或零成本路径，
> 并靠上下文隔离压低长期 token 消耗。

## 1. 省 token 的三个杠杆

成本 ≈ **模型单价 × 上下文规模 × 往返次数**。

| 杠杆 | 机制 |
|---|---|
| A. 模型档位 | pro 单价高，只用于能力敏感任务 |
| B. 执行位置 | 主代理自己做零固定开销，但结果长期驻留主上下文、每轮重算；子代理有固定开销，但工作上下文结束后丢弃、只回结论 |
| C. 上下文规模 | 主上下文越小，后续每轮越便宜，越晚触发压缩 |

### 缓存修正（关键认知）

DeepSeek 使用**自动前缀缓存**：命中部分按 cache-hit 价（打折、非免费）计费，有长度窗口上限，
超时不活跃过期。因此：

- 子代理**不是零缓存**：公共头部（系统提示 + 工具目录）跨会话高命中、被大量摊销；
  子代理内部多轮循环命中率高；只有"任务前缀"是一次性 miss。
- 隔离的真实收益不是"避免无缓存"，而是把"每轮按 hit 价重算的驻留内容"变成"一次性支付后丢弃"。

## 2. 分层模型与推理等级（四档）

| 档位 | 工具 | 模型 + effort | 适用任务 |
|---|---|---|---|
| T0 主代理 | （主会话） | flash + max（settings.yaml 控制） | 交互、单步读取、快速查找、结果马上要用 |
| T1 普通 | `subagent` | flash + 部署默认（high） | 批量扫描、探索检索、机械转换、摘要 |
| T1r 只读 | `subagent_read` | flash + **off**（唯一 off 档） | 纯阅读/检索类，只求结论，省 reasoning token |
| T2 旗舰 | `subagent_pro` | pro + high | 架构设计、方案权衡、根因分析、深度调试、代码审查、安全审计、写文件/改代码 |

### 推理等级分档（reasoning effort）

由自定义插件 `dsh-subagent-effort-router` 实现（源码 `tools/dsh-subagent-effort-router/`，经 `dsh plugin` 安装到 profile）。
插件监听 **`agent/created` 事件**（DSH 的 agent 注册表对每个 live agent 广播，覆盖根、one-shot、continuable 三类）+ `installModelSelection`（`@deepseek-ai/dsh-agent` 公开导出，api-proxy 给主会话注入模型选择用的同一函数）工作：

- 每个 `tool-subagent` 实例在 `agentOptions.reasoningEffort` 里**显式标记**目标 effort；
- 标记经 `resolveChildAgentOptions` 透传到子代理 `agent.options`，插件在 `agent/created` 时读取并安装选择；
- **无标记的实例（如默认 `subagent`）不安装选择，继承部署默认**——只有明确标记 off 的只读档才关推理。

| 工具实例 | agentOptions 标记 | 效果 |
|---|---|---|
| `subagent`（默认） | 无 | 不覆盖，继承默认 |
| `subagent_read` | `reasoningEffort: "off"` | 关推理，省 token |
| `subagent_pro` | `reasoningEffort: "high"` | 深度推理 |

新增档位 = patch 里加一个带标记的 `tool-subagent` 实例即可，无需改插件。

## 3. 决策规则（按序判断）

1. 一步完成且结果马上用 → T0 主代理，不派子代理；
2. 大量读取只求结论、纯检索 → **T1r `subagent_read`**（flash + off，最省）；
3. 机械重复 / 格式化 / 简单替换 / 摘要 / 标题 / 提交信息 → T1 `subagent`；
4. 强推理（设计 / 权衡 / 根因）→ T2 `subagent_pro`；
5. 写文件 / 改代码（错了要返工）→ T2 `subagent_pro`；
6. 代码审查 / 安全审计 → T2 `subagent_pro`；
7. 拿不准 → 先 T1，质量不达标再 T2 重试（失败升级，不赌）。

## 4. 反模式（明确禁止）

1. 单步小读取也派子代理 —— 固定开销 > 任务本身；
2. 链式嵌套委托（主 → 子 → 孙）—— 每层重付固定开销、信息逐层丢失，委托深度限一层；
3. 用 `subagent_fork` 做无关任务 —— fork 会复制父对话整个历史，极贵，只用于真正需要父上下文的后续分析；
4. 主上下文已有的信息，再派子代理去重读。

## 5. 顶层模型开关

- 任务**整体**难 → 主会话切 pro（GUI Models 页），内部琐碎子任务仍派 `subagent`（flash）；
- 任务整体简单、只有个别子任务难 → 主会话保持 flash + 按需派 `subagent_pro`。

## 6. 配置实现

flash 是默认模型，子代理默认继承父模型，因此 T0/T1 零配置，只补 pro 通道和只读档。

**默认模型有两处入口，优先级：settings 层 > bundle 层**：
- bundle 层（随包升级）`agent-default-model` 默认就是 `deepseek-v4-flash`；
- settings 层 `~/.dsh/settings.yaml` 的 `agent-default-model` 会**覆盖** bundle 层。
  当前主会话默认 `deepseek-v4-flash + reasoningEffort: max`（用户在 GUI 所选）。

位置：`~/.dsh/profiles/web/cordis.patch.yml`，insert 两个 `tool-subagent` 实例 + 插件行：

```yaml
- insert:
    - id: tool-subagent-pro
      name: '@deepseek-ai/dsh-tool-subagent'
      config:
        provider: spawn
        toolName: subagent_pro
        backgroundMode: continuable
        agentOptions:
          provider: deepseek-official
          model: deepseek-v4-pro
          reasoningEffort: high

    - id: tool-subagent-read
      name: '@deepseek-ai/dsh-tool-subagent'
      config:
        provider: spawn
        toolName: subagent_read
        backgroundMode: continuable
        agentOptions:
          provider: deepseek-official
          model: deepseek-v4-flash
          reasoningEffort: off

    - id: subagent-effort-router
      name: dsh-subagent-effort-router
```

**安装插件（官方方式）**：

```sh
dsh plugin --profile web add file:/home/keship/keship_ws/tools/dsh-subagent-effort-router
```

- 同一插件多实例有现成先例（`tool-subagent` / `tool-subagent-fork`）；
- `agentOptions` 把 pro/read 钉死，不依赖父会话当前模型；
- patch 的 config 修改由 `watchUserPatches` 热重载；**host 代码变化（插件 lib/index.js）需重启 `dsh web`**。

## 7. 缓存与上下文策略

1. 主上下文保持精简：大扫描结果只回结论，不进主上下文；
2. 独立任务用 background / continuable 并行，减少主代理串行往返；
3. 依赖已配置的 `compaction-basic` + `tool-result-pruner`（8KB 截断）兜底；
4. 保持前缀稳定 → 提升后续轮 KV 缓存命中率、更便宜。

## 8. Workflow 集成（大规模 fan-out）

大规模并行任务用 `workflow`，把路由写死在脚本里，比父代理逐个判断更可控、更省往返：

```js
await parallel([
  () => agent("遍历所有源文件并汇总结构", { model: "deepseek-v4-flash" }),
  () => agent("设计模块间接口方案", { model: "deepseek-v4-pro" }),
]);
```

`phase(title, { model })` 可对整个阶段生效。

## 9. 验证步骤

1. 写 patch 后，观察热重载日志确认 `tool-subagent-pro` 挂载；
2. 新会话工具目录出现 `subagent_pro`；
3. 四探测：
   - T0 任务（"看一下这个文件"）→ 主代理直接读、不派子代理；
   - T1r 任务（"扫全库汇总 XX"）→ 调用 `subagent_read`（flash + off）；
   - T1 任务（"格式化某文件"）→ 调用 `subagent`（flash + 默认）；
   - T2 任务（"设计 XX 架构"）→ 调用 `subagent_pro`（pro + high）。

## 10. 风险与回退

| 风险 | 缓解 |
|---|---|
| 父代理选错档 | 工具名自解释 + 本规则固化，父代理读取遵守 |
| pro 成本失控 | 默认 flash 兜底；pro 只在显式 T2 路径；委托一层限制 |
| 默认模型被改导致 flash 路径变贵 | 假设默认模型为 flash；settings.yaml 的 `agent-default-model` 若被 GUI 改为 pro，需重新评估并改回 flash |
| 配置冲突 / 升级不兼容 | 只动用户 patch 层，可即时回退；bundle 升级不覆盖用户层 |
