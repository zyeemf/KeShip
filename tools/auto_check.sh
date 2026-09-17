#!/bin/bash
# ============================================================
#  auto_check.sh — 从 WSL 侧触发 KeShip 文档自动检查
#
#  用途：被 start_keship.sh 与 git post-commit hook 调用（也可手动执行）
#  原理：代码在 WSL、文档在 Windows 工作空间，本脚本调用 Windows 侧
#        doc/tools/auto_check.py（其内部已做双环境路径适配）
#
#  参数透传：--quick（仅漂移检查，启动时用）/ --quiet
#  非阻塞：任何失败都返回 0，绝不影响启动或提交
#
#  ⚠️ 两个 WSL 侧隐患（2026-09-17 演习前检查发现），本脚本已处理：
#     1) /usr/bin/python3 可能不存在（本机仅有 python3.12）→ 探测式查找
#     2) WSL 未设置 locale（LANG 为空 → C locale），而路径含中文
#        （/mnt/e/AI_ws/KeShip开发）→ Python 文件系统编码可能失败 → 强制 UTF-8
# ============================================================
set +e

DOC_WIN="/mnt/e/AI_ws/KeShip开发"
AUTO="$DOC_WIN/doc/tools/auto_check.py"

if [ ! -f "$AUTO" ]; then
    echo "[文档] 跳过自动检查：Windows 工作空间未挂载（$DOC_WIN 不可达）"
    exit 0
fi

# 隐患 1：探测可用的 python 解释器
PY=""
for c in python3 python3.12 python; do
    if command -v "$c" >/dev/null 2>&1; then PY="$c"; break; fi
done
if [ -z "$PY" ]; then
    echo "[文档] 跳过自动检查：WSL 内未找到 python3（已尝试 python3 / python3.12 / python）"
    exit 0
fi

# 隐患 2：中文路径 + 缺失 locale → 强制 UTF-8 模式
export PYTHONUTF8=1
export LANG="${LANG:-C.UTF-8}"
export LC_ALL="${LC_ALL:-C.UTF-8}"

"$PY" "$AUTO" "$@"
RC=$?
if [ $RC -ne 0 ]; then
    echo "[文档] 检测到代码改动或文档问题（详见上方输出；日志：doc/audit/auto_check_log.md）"
fi
exit 0
