#!/usr/bin/env bash
# ============================================================
# 核对本地 / 华为云 / GitHub 三处的 main 分支是否一致
#
# 用途：双远端推送后确认"两边都到位" —— 避免"一个成功一个失败"的静默不一致。
#       （项目既有纪律：提交后必须核对远端 hash，见 RULES.md）
#
# 用法: bash check_remotes.sh
# ============================================================
set -u
# 禁用交互式凭据提示：避免某端无凭据/仓库不存在时挂起等待输入
export GIT_TERMINAL_PROMPT=0
export GIT_ASKPASS=/bin/true

cd "$HOME/keship_ws" || { echo "[FAIL] 找不到仓库"; exit 2; }

LOCAL="$(git rev-parse HEAD 2>/dev/null)"
HW="$(timeout 20 git ls-remote origin refs/heads/main 2>/dev/null | cut -f1)"
GH="$(timeout 20 git ls-remote github refs/heads/main 2>/dev/null | cut -f1)"

echo "本地      : ${LOCAL:-（取不到）}"
echo "华为云    : ${HW:-（取不到）}"
echo "GitHub    : ${GH:-（取不到）}"
echo ""

if [ -z "$HW" ] || [ -z "$GH" ]; then
    echo "[WARN] 有一端取不到 —— 检查网络/远端配置/凭据"
    echo "       华为云: git ls-remote origin refs/heads/main"
    echo "       GitHub: git ls-remote github refs/heads/main"
    exit 1
fi

if [ "$LOCAL" = "$HW" ] && [ "$LOCAL" = "$GH" ]; then
    echo "[OK] 三处一致（$LOCAL）"
    exit 0
fi

echo "[FAIL] 不一致，需排查："
[ "$LOCAL" != "$HW" ] && echo "  - 华为云与本地不同"
[ "$LOCAL" != "$GH" ] && echo "  - GitHub 与本地不同"
echo "  修复：git push origin main   /   git push github main"
exit 1
