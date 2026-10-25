#!/usr/bin/env bash
# KeShip 自动化质检总入口（2026-10-25）
#
# 一条命令跑完：离屏回归(ctest) → GUI 基线 → 参数矩阵 → GUI 操作矩阵 → 重建回归探针
# 退出码 0 = 全绿；任何一项 FAIL 则非 0。
#
# 用法：bash tools/gui_test/run_all.sh
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$HERE/../.." && pwd)"
CTEST_DIR="$WS/build/keship"
PROBE="$WS/build/keship/mathhull_rebuild_probe"

FAIL=0
declare -a SUMMARY

run_step() {
  local name="$1"; shift
  echo
  echo "════════ $name ════════"
  if "$@"; then
    SUMMARY+=("PASS  $name")
  else
    SUMMARY+=("FAIL  $name")
    FAIL=$((FAIL + 1))
  fi
}

# 1) 离屏回归（需要 -DKESHIP_BUILD_TESTS=ON 的构建）
if [ -x "$CTEST_DIR/sl02_hull_dialog_test" ]; then
  run_step "离屏回归 ctest" bash -c "cd '$CTEST_DIR' && ctest -L regression --output-on-failure >/dev/null"
else
  SUMMARY+=("SKIP  离屏回归 ctest（未用 -DKESHIP_BUILD_TESTS=ON 构建）")
fi

# 2) GUI 基线（真 GUI：启动/插件/菜单/对话框/生成/浮稳/退出）
run_step "GUI 基线" bash "$HERE/run.sh"

# 3) 参数/船型矩阵（6 船型 + 水动力两档 + 极端尺寸 + 位姿）
run_step "参数矩阵" bash "$HERE/mathhull_matrix.sh"

# 4) GUI 操作矩阵（改船型/改名/改位姿/重名拦截）
run_step "GUI 操作矩阵" bash "$HERE/mathhull_gui_matrix.sh"

# 5) 重建回归（改质量→重建→仍浮）
if [ -x "$PROBE" ]; then
  run_step "重建回归探针" bash "$HERE/run_rebuild_probe.sh"
else
  SUMMARY+=("SKIP  重建回归探针（未构建 mathhull_rebuild_probe）")
fi

echo
echo "════════ 汇总 ════════"
for s in "${SUMMARY[@]}"; do echo "  $s"; done
echo
if [ "$FAIL" -eq 0 ]; then
  echo "RESULT: 全绿"
  exit 0
else
  echo "RESULT: $FAIL 项失败"
  exit 1
fi
