/**
 * dsh-subagent-effort-router
 *
 * Routes reasoning effort per agent. Each `tool-subagent` instance marks its
 * desired effort through an `agentOptions.reasoningEffort` field:
 *
 *   - `subagent_read` -> agentOptions.reasoningEffort: "off"   (read-only tier)
 *   - `subagent`      -> no marker                             (deployment default)
 *   - `subagent_pro`  -> agentOptions.reasoningEffort: "high"  (flagship tier)
 *
 * Why `agent/created` instead of `registerContinuableSetup`:
 *
 *   `registerContinuableSetup` only reaches CONTINUABLE children. One-shot
 *   children (foreground `run_in_background: false`, and `subagent_fork`)
 *   never pass through it, so their effort would silently stay at the
 *   deployment default. The `agent/created` event is emitted by the agent
 *   registry (`ctx.agents.announce`) for EVERY live agent — root, one-shot,
 *   and continuable alike — with the agent's scope carrier. `dsh-schedule`
 *   and `dsh-goal-round-driver` use the same hook for per-agent setup.
 *
 * The marker travels like this:
 *
 *   tool-subagent config `agentOptions` (schemastery keeps unknown fields)
 *     -> `resolveChildAgentOptions(parent, request.agentOptions, depth)`
 *        spreads the marker into the child's `agent.options`
 *     -> this plugin reads `agent.options.reasoningEffort` at `agent/created`
 *     -> `installModelSelection` (exported by @deepseek-ai/dsh-agent) listens
 *        on the agent/request waterfall and injects provider/model/
 *        reasoningEffort per request, exactly like the web api-proxy does for
 *        top-level sessions.
 *
 * Lifecycle: `installModelSelection` returns a disposer; we bind it to the
 * agent's own scope with `agent.ctx.effect`, so it is torn down with the agent.
 * We only act when a marker is present — unmarked agents keep the deployment
 * default and we install nothing (no overhead).
 */
import { installModelSelection } from "@deepseek-ai/dsh-agent";

export const name = "subagent-effort-router";
export const inject = ["agents"];

export function apply(ctx) {
  ctx.on("agent/created", ({ agent }) => {
    const markedEffort = agent.options?.reasoningEffort;
    if (markedEffort === undefined) return;

    const options = agent.options;
    const selection = {
      get current() {
        return {
          provider: options.provider,
          model: options.model,
          reasoningEffort: markedEffort,
        };
      },
      assembled: void 0,
    };

    agent.ctx.effect(() => installModelSelection(agent.ctx, selection));
  });
}
