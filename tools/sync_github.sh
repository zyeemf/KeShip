#!/usr/bin/env bash
# ============================================================
# 网络恢复后一键补推 GitHub（并核对三处一致）
#
# 背景：本机 `ssh.github.com:443` **时通时断**（`github.com` 网页长期不通），
#       网络波动时 GitHub 会落后于华为云。本脚本在可达时补推，不可达时如实报告。
#
# 用法: bash tools/sync_github.sh
# ============================================================
set -u
export GIT_TERMINAL_PROMPT=0     # 不弹交互式凭据提示
export GIT_ASKPASS=/bin/true

cd "$HOME/keship_ws" || { echo "[FAIL] 找不到仓库目录"; exit 2; }

echo "=== 1) 当前三处状态 ==="
bash tools/check_remotes.sh || true

echo
echo "=== 2) 连通性预检（ssh.github.com:443）==="
if timeout 10 bash -c 'cat < /dev/null > /dev/tcp/ssh.github.com/443' 2>/dev/null; then
    echo "  可达 → 开始补推"
else
    echo "  ⚠ 不可达（网络波动）→ 本次跳过，等网络恢复再跑本脚本"
    exit 3
fi

echo
echo "=== 3) 补推 GitHub ==="
timeout 120 git push github main 2>&1 | tail -4

echo
echo "=== 4) 补推后核对 ==="
bash tools/check_remotes.sh
