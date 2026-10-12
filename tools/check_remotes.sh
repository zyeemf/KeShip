#!/usr/bin/env bash
# ============================================================
# 核对本地 / 华为云 / GitHub 三处的**指定分支**是否一致
#
# 用途：双远端推送后确认"两边都到位" —— 避免"一个成功一个失败"的静默不一致。
#       （项目既有纪律：提交后必须核对远端 hash，见 RULES.md）
#
# 用法: bash check_remotes.sh [branch]      # branch 默认 main
#       bash check_remotes.sh dev
# ============================================================
set -u
# 禁用交互式凭据提示：避免某端无凭据/仓库不存在时挂起等待输入
export GIT_TERMINAL_PROMPT=0
export GIT_ASKPASS=/bin/true

BRANCH="${1:-main}"

cd "$HOME/keship_ws" || { echo "[FAIL] 找不到仓库"; exit 2; }

LOCAL="$(git rev-parse "$BRANCH" 2>/dev/null)"
HW="$(timeout 45 git ls-remote origin "refs/heads/$BRANCH" 2>/dev/null | cut -f1)"
GH="$(timeout 45 git ls-remote github "refs/heads/$BRANCH" 2>/dev/null | cut -f1)"

echo "分支      : $BRANCH"
echo "本地      : ${LOCAL:-（取不到）}"
echo "华为云    : ${HW:-（取不到）}"
echo "GitHub    : ${GH:-（取不到）}"
echo ""

if [ -z "$HW" ] || [ -z "$GH" ]; then
    echo "[WARN] 有一端取不到 —— 检查网络/远端配置/凭据"
    echo "       华为云: git ls-remote origin refs/heads/$BRANCH"
    echo "       GitHub: git ls-remote github refs/heads/$BRANCH"
    exit 1
fi

if [ "$LOCAL" = "$HW" ] && [ "$LOCAL" = "$GH" ]; then
    echo "[OK] 三处一致（$LOCAL）"
    exit 0
fi

echo "[FAIL] 不一致，需排查："
[ "$LOCAL" != "$HW" ] && echo "  - 华为云与本地不同"
[ "$LOCAL" != "$GH" ] && echo "  - GitHub 与本地不同"
echo "  修复：git push origin $BRANCH   /   git push github $BRANCH"
exit 1
