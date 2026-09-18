#!/usr/bin/env bash
# QML 改动的完整离线核查（红线 grep + qmllint + 创建期冒烟测试 + 弹出截图）
# 用法：bash check_qml.sh <qml文件> [截图路径] [菜单序号]
set -u
F="${1:?用法: bash check_qml.sh <qml文件> [截图路径] [菜单序号]}"
SHOT="${2:-/tmp/menu_shot.png}"
IDX="${3:-0}"
TOOLS="$(cd "$(dirname "$0")" && pwd)"
LINT=/usr/lib/x86_64-linux-gnu/qt5/bin/qmllint

echo "===== 1. 红线检查（样本数必须为 0 才算通过）====="
echo -n "  delegate: MenuItem 次数 = "
grep -c "delegate: *MenuItem" "$F" || true
echo -n "  原生 Menu { 使用次数 = "
grep -c "^ *Menu {" "$F" || true
echo -n "  import QtQuick.Controls 版本 = "
grep -m1 "import QtQuick.Controls" "$F"
echo -n "  onTriggered 总数 / 其中先 close() 的条数 = "
echo "$(grep -c 'onTriggered:' "$F") / $(grep -c 'onTriggered: { [A-Za-z_]*\.close();' "$F")"

echo "===== 2. qmllint 语法检查 ====="
"$LINT" "$F" && echo "  [PASS] qmllint 无警告" || echo "  [FAIL] qmllint 有警告/错误"

echo "===== 3. 创建期冒烟测试 + 弹出截图 ====="
bash "$TOOLS/run.sh" "$F" "$SHOT" "$IDX"
