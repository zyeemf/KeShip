// ============================================================================
// 模型路由分工 Workflow 模板（model-routing）
// ----------------------------------------------------------------------------
// 用途：把一批任务按"档位"路由给 deepseek-v4-flash / deepseek-v4-pro 子代理
//       并行执行；可选：flash 分诊器自动分类 + pro 复核 pro 档产出（含返工循环）。
//
// 设计原则（来自工作流讨论结论）：
//   主代理（flash）自己干琐碎只读活，只有值得外包的任务才进入本 workflow；
//   分档核心判据是【错误代价】：错了要返工/有危害 → pro；错了无害/高频 → flash。
//
// 如何运行（DeepSeek Harness GUI 的 workflow 工具）：
//   1. 把本文件中 "=== 脚本主体 ===" 之后的内容复制到 workflow 工具的 script 参数；
//   2. meta 参数（name/description/whenToUse/phases）按文件底部注释填写；
//   3. args 参数示例：
//      {
//        "tasks": [
//          { "title": "搜索类任务", "prompt": "……", "tier": "auto" },   // auto=交给 flash 分诊器
//          { "title": "写代码任务", "prompt": "……", "tier": "pro" },     // 显式指定档位
//          "只有一句 prompt 也行"                                        // 裸字符串自动转成 auto 档任务
//        ],
//        "autoClassify": true,        // 是否用 flash 分诊器分类 auto 任务（默认 true）
//        "proReview": true,           // 是否用 pro 复核 pro 档产出（默认 true）
//        "maxReviewAttempts": 1,      // 评审不通过时最多返工次数（默认 1）
//        "batchSize": 4,              // 每批并行子代理数（默认 4）
//        "modelFlash": "deepseek-v4-flash",  // 可选覆盖
//        "modelPro": "deepseek-v4-pro"       // 可选覆盖
//      }
//
// 返回：{ ok, stats, results: [{ id, title, tier, output, review? }] }
//   - output 为 null 表示该子代理执行失败
//   - review.verdict: "pass" | "fix" | "failed"
// ============================================================================

// ============================ 参数与常量 =====================================
const cfg = args ?? {};
const FLASH = cfg.modelFlash ?? 'deepseek-v4-flash';
const PRO = cfg.modelPro ?? 'deepseek-v4-pro';
const provider = cfg.provider ?? 'deepseek-official';
const autoClassify = cfg.autoClassify !== false;
const proReview = cfg.proReview !== false;
const maxReviewAttempts = Math.max(1, cfg.maxReviewAttempts ?? 1);
const batchSize = Math.max(1, cfg.batchSize ?? 4);

const tasks = (cfg.tasks ?? [])
  .filter(t => (typeof t === 'string' ? t.trim() : t?.prompt?.trim()))
  .map((t, i) => {
    if (typeof t === 'string') return { id: `task-${i}`, title: t.slice(0, 40), prompt: t, tier: 'auto' };
    const tier = t.tier === 'pro' || t.tier === 'flash' ? t.tier : 'auto';
    return { id: t.id ?? `task-${i}`, title: (t.title ?? `task-${i}`).slice(0, 40), prompt: t.prompt, tier };
  });

if (tasks.length === 0) {
  return {
    ok: false,
    error: 'args.tasks 为空或全部无 prompt；请提供任务列表',
    usage: { tasks: [{ title: '任务名', prompt: '任务描述', tier: 'auto|flash|pro' }] },
  };
}

const TIER_RULES = `把每个任务分到两个档位之一，只输出 JSON。

【flash 档 —— 高频 × 低难度】
- 文件搜索、代码查找、快速问答
- 只读任务（结果错了无害）
- 机械转换、格式化、简单编辑
- 摘要、标题、提交信息
- 探索性、信息收集

【pro 档 —— 低频 × 高难度】
- 架构设计、方案权衡
- 会写文件/改代码（错了要返工）
- 根因分析、深度调试
- 代码审查、安全审计
- 复杂多步实现

核心判断标准：错误代价 —— 错了要返工/有危害 → pro；错了无害/高频 → flash。难度是次要信号。`;

// ============================ 阶段 1：分诊 ===================================
phase('triage');
const tierMap = new Map();
const pending = tasks.filter(t => t.tier === 'auto');
if (autoClassify && pending.length > 0) {
  log(`分诊 ${pending.length} 个任务 → ${FLASH}`);
  const classified = await agent(
    `你是任务分诊器。规则：\n${TIER_RULES}\n\n请给下列每个任务指定 tier（"flash" 或 "pro"），index 对应任务列表下标。只输出 JSON。\n任务列表：\n` +
      JSON.stringify(pending.map((t, i) => ({ index: i, title: t.title, prompt: t.prompt })), null, 2),
    {
      label: 'triage-classifier',
      phase: 'triage',
      provider,
      model: FLASH,
      schema: {
        type: 'object',
        required: ['assignments'],
        additionalProperties: false,
        properties: {
          assignments: {
            type: 'array',
            items: {
              type: 'object',
              required: ['index', 'tier'],
              additionalProperties: false,
              properties: {
                index: { type: 'integer' },
                tier: { type: 'string', enum: ['flash', 'pro'] },
              },
            },
          },
        },
      },
    },
  );
  if (classified && Array.isArray(classified.assignments)) {
    for (const a of classified.assignments) {
      const item = pending[a.index];
      if (item && (a.tier === 'flash' || a.tier === 'pro')) tierMap.set(item.id, a.tier);
    }
  } else {
    log('分诊失败（分诊器无有效输出），未分类任务按 flash 兜底');
  }
}

const jobs = tasks.map(t => ({ ...t, tier: t.tier !== 'auto' ? t.tier : (tierMap.get(t.id) ?? 'flash') }));
const flashJobs = jobs.filter(j => j.tier === 'flash');
const proJobs = jobs.filter(j => j.tier === 'pro');

// ============================ 阶段 2：执行 ===================================
phase('execute');

async function runJob(job, model, feedback) {
  const prompt =
    `任务标题：${job.title}\n\n${job.prompt}` +
    (feedback ? `\n\n【评审意见，请据此修正后再输出完整结果】\n${feedback}` : '');
  return agent(prompt, { label: job.title, phase: 'execute', provider, model });
}

async function runBatch(jobList, model) {
  const out = [];
  for (let i = 0; i < jobList.length; i += batchSize) {
    const chunk = jobList.slice(i, i + batchSize);
    const texts = await parallel(chunk.map(j => () => runJob(j, model)));
    out.push(...chunk.map((j, k) => ({ id: j.id, title: j.title, tier: j.tier, output: texts[k] ?? null })));
  }
  return out;
}

const [flashResults, proRaw] = await parallel([
  () => runBatch(flashJobs, FLASH),
  () => runBatch(proJobs, PRO),
]);

// ============================ 阶段 3：pro 复核 ===============================
let proResults = proRaw;
const proWithOutput = proResults.filter(r => r.output);
if (proReview && proWithOutput.length > 0) {
  phase('review');
  const jobById = new Map(jobs.map(j => [j.id, j]));
  const reviewed = [];
  for (const r of proResults) {
    if (!r.output) {
      reviewed.push({ ...r, review: { verdict: 'failed', issues: '子代理执行失败（无产出）' } });
      continue;
    }
    let cur = r.output;
    let lastReview = null;
    let reruns = 0;
    while (true) {
      lastReview = await agent(
        `你是质量评审员。判断下面的产出是否达到任务要求。只输出 JSON：verdict 为 "pass"（通过）或 "fix"（需修正）；issues 给出具体、可操作的问题清单（无问题时为空字符串）。\n\n【任务】\n${jobById.get(r.id).prompt}\n\n【产出】\n${cur}`,
        {
          label: `review:${r.title}`,
          phase: 'review',
          provider,
          model: PRO,
          schema: {
            type: 'object',
            required: ['verdict', 'issues'],
            additionalProperties: false,
            properties: {
              verdict: { type: 'string', enum: ['pass', 'fix'] },
              issues: { type: 'string' },
            },
          },
        },
      );
      if (!lastReview || lastReview.verdict === 'pass') break;
      if (reruns >= maxReviewAttempts) break;
      reruns++;
      const fixed = await runJob(jobById.get(r.id), PRO, lastReview.issues);
      if (!fixed) break;
      cur = fixed;
    }
    reviewed.push({ ...r, output: cur, review: lastReview ?? { verdict: 'failed', issues: '评审子代理执行失败' } });
  }
  proResults = reviewed;
}

// ============================ 阶段 4：报告 ===================================
phase('report');
const all = [...flashResults, ...proResults];
const stats = {
  total: jobs.length,
  flash: flashJobs.length,
  pro: proJobs.length,
  executed: all.filter(r => r.output).length,
  failed: all.filter(r => !r.output).length,
  reviewed: proResults.filter(r => r.review).length,
  reviewPassed: proResults.filter(r => r.review?.verdict === 'pass').length,
  reviewFixed: proResults.filter(r => r.review?.verdict === 'fix').length,
};

return { ok: true, config: { autoClassify, proReview, maxReviewAttempts, batchSize }, stats, results: all };

// ============================================================================
// meta 参数（复制到 workflow 工具的 meta 字段）：
// {
//   "name": "model-routing-workflow",
//   "description": "flash 主调度 + 按档位路由 pro/flash 子代理 + 可选 pro 复核的通用任务分工模板",
//   "whenToUse": "需要把一批任务按难度/错误代价分给 flash 或 pro 子代理并行执行时",
//   "phases": [
//     { "title": "triage",  "detail": "flash 分诊器为未指定档位的任务分类" },
//     { "title": "execute", "detail": "按档位并行执行子代理任务" },
//     { "title": "review",  "detail": "pro 复核 pro 档产出，可带返工循环" },
//     { "title": "report",  "detail": "汇总结果与统计" }
//   ]
// }
// ============================================================================

