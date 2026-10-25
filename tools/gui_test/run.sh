#!/usr/bin/env bash
# KeShip GUI 测试台入口（阶段 1 冒烟，2026-10-25）
#
# 与生产启动链完全一致：调用 start_keship.sh（同环境/同渲染开关/同启动重试），
# 只额外加 --test-isolated（独立锁、跳过全局 gz 检查、跳过文档自检）
# 与 --no-d3d12 --no-gpu-warm（Xvfb 下走 llvmpipe，不碰 D3D12）。
# 隔离：独立 HOME（~/.gz 全是自己的）、KESHIP_DATA_DIR（data/ 是自己的）、
#       GZ_PARTITION + ROS_DOMAIN_ID、动态分配的空闲 X display。
#
# 用法：bash tools/gui_test/run.sh
# 产物：~/keship_ws/data/test/gui_test/<时间戳>/
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$HERE/../.." && pwd)"
REAL_HOME="${HOME}"
# shellcheck disable=SC1091
. "$HERE/config.env"
# shellcheck disable=SC1091
. "$HERE/lib.sh"

STAMP=$(date +%Y%m%d_%H%M%S)
OUT="$WS/data/test/gui_test/$STAMP"
mkdir -p "$OUT"
RESULT="$OUT/result.txt"
: > "$RESULT"

FAILS=0
pass() { echo "PASS  $*" | tee -a "$RESULT"; }
fail() { echo "FAIL  $*" | tee -a "$RESULT"; FAILS=$((FAILS + 1)); }
skip() { echo "SKIP  $*" | tee -a "$RESULT"; }

XVFB_PID=""
KSHIP_PID=""
cleanup() {
  log "收尾"
  if [ -n "$KSHIP_PID" ]; then
    kill -TERM -"$KSHIP_PID" 2>/dev/null
    for _ in $(seq 1 15); do pg_alive "$KSHIP_PID" || break; sleep 1; done
    pg_alive "$KSHIP_PID" && kill -KILL -"$KSHIP_PID" 2>/dev/null
  fi
  [ -n "$XVFB_PID" ] && kill "$XVFB_PID" 2>/dev/null
  rm -f "$WS/data/keship_test.lock"
  sleep 1
}
trap cleanup EXIT

# ── 独立 HOME（~/.gz 的日志/窗口布局/渲染配置全在本次目录内）──
TEST_HOME="$OUT/home"
mkdir -p "$TEST_HOME/.gz"
ln -sfn "$WS" "$TEST_HOME/keship_ws"
[ -d "$REAL_HOME/keship-gz" ] && ln -sfn "$REAL_HOME/keship-gz" "$TEST_HOME/keship-gz"
[ -d "$REAL_HOME/.gz/fuel" ] && ln -sfn "$REAL_HOME/.gz/fuel" "$TEST_HOME/.gz/fuel"

# ── T1 动态分配 X display（扫描空闲号；flock 串行避免并发抢号）──
# 说明：不用 Xvfb -displayfd —— 它从 :0 试起，会一直撞 WSLg 的 :0 并刷屏。
exec 8>"/tmp/keship_gui_display.lock"
flock -w 60 8 || { fail "T1 显示号分配锁超时"; exit 1; }
DISPLAY_NUM=""
for n in $(seq 90 130); do
  [ -e "/tmp/.X${n}-lock" ] && continue
  Xvfb ":$n" -screen 0 "${SCREEN_W}x${SCREEN_H}x24" -nolisten tcp \
    > "$OUT/xvfb.log" 2>&1 &
  XVFB_PID=$!
  for _ in $(seq 1 30); do
    DISPLAY=":$n" xdpyinfo >/dev/null 2>&1 && break
    kill -0 "$XVFB_PID" 2>/dev/null || break
    sleep 0.1
  done
  if DISPLAY=":$n" xdpyinfo >/dev/null 2>&1 && kill -0 "$XVFB_PID" 2>/dev/null; then
    DISPLAY_NUM=":$n"
    break
  fi
  kill "$XVFB_PID" 2>/dev/null
  XVFB_PID=""
done
flock -u 8
if [ -n "$DISPLAY_NUM" ]; then
  export DISPLAY="$DISPLAY_NUM"
  pass "T1 Xvfb 就绪：DISPLAY=$DISPLAY（本 run 启动，pid=$XVFB_PID）"
else
  fail "T1 找不到空闲 display（90~130 全被占用）"
  exit 1
fi

# ── 环境：与生产一致 + 隔离与软件渲染 ──
export HOME="$TEST_HOME"
export KESHIP_DATA_DIR="$OUT/data"
export GZ_PARTITION="keship_guitest"
export ROS_DOMAIN_ID="66"
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
mkdir -p "$KESHIP_DATA_DIR"

# ── T2 产物新鲜度闸门（--no-build 不能静默跑旧二进制）──
REF="$WS/install/lib/libKSPlugin_HydroBuoyancy.so"
if [ ! -f "$REF" ]; then
  fail "T2 缺少构建产物 $REF（请先正常编译一次）"
  exit 1
fi
NEWER=$(find "$WS/src/keship/src_cpp" "$WS/src/keship/src_py" "$WS/src/keship/include" \
  -type f \( -name '*.cc' -o -name '*.hh' -o -name '*.py' -o -name '*.qml' \) \
  -newer "$REF" 2>/dev/null | wc -l)
if [ "$NEWER" -eq 0 ]; then
  pass "T2 二进制与源码同步（无更新的源文件）"
else
  fail "T2 有 $NEWER 个源文件比产物新 —— 拒绝用旧二进制测（请先 colcon build）"
  exit 1
fi

# ── T3 启动（与 bat 同链）──
log "启动：start_keship.sh --no-build --test-isolated --no-d3d12 --no-gpu-warm --gz-verbose 2"
setsid bash "$WS/start_keship.sh" --no-build --test-isolated --no-d3d12 --no-gpu-warm \
  --gz-verbose 2 > "$OUT/keship.log" 2>&1 &
KSHIP_PID=$!

WIN=$(wait_window "$START_TIMEOUT")
if [ -n "$WIN" ]; then
  pass "T3 窗口出现（id=$WIN）"
else
  fail "T3 ${START_TIMEOUT}s 内未出现窗口"
  tail -40 "$OUT/keship.log" | tee -a "$RESULT"
  exit 1
fi

# ── T4 只启动了一次（崩溃重试不能掩盖问题）──
LAUNCH_N=$(grep -c "正在启动 KeShip 仿真" "$OUT/keship.log" || true)
RETRY_N=$(grep -c "次尝试\|检测到崩溃" "$OUT/keship.log" || true)
if [ "$LAUNCH_N" -eq 1 ] && [ "$RETRY_N" -eq 0 ]; then
  pass "T4 单次启动（无崩溃重试）"
else
  fail "T4 启动次数=$LAUNCH_N 重试标志=$RETRY_N（应用一次启动）"
fi

sleep 5
# ── T5 主界面截图（窗口级）──
if shot_window "$WIN" "$OUT/01_start.png"; then
  pass "T5 主界面截图 01_start.png"
else
  fail "T5 主界面截图失败（尺寸/亮度/颜色数不达标）"
fi

# ── T6 日志：插件加载（日志已在独立 HOME，不会被生产污染）──
GZLOG="$TEST_HOME/.gz/auto_default.log"
if [ -f "$GZLOG" ]; then
  cp "$GZLOG" "$OUT/gz.log"
  if grep -q "Failed to instantiate" "$GZLOG"; then
    fail "T6 存在 Failed to instantiate"
    grep -n "Failed to instantiate" "$GZLOG" | head -3 | tee -a "$RESULT"
  else
    pass "T6 无 Failed to instantiate"
  fi
  if grep -q "Added plugin \[MenuPluginKeship\]" "$GZLOG"; then
    pass "T7 MenuPluginKeship 已加载"
  else
    fail "T7 未加载 KeShip 插件"
  fi
else
  fail "T6/T7 未找到 gz 日志 $GZLOG"
fi

# ── T8/T9 交互（需要标定坐标）──
if [ -n "${LOAD_BTN_X:-}" ] && [ -n "${LOAD_BTN_Y:-}" ]; then
  BEFORE="$OUT/01_start.png"
  click_win "$WIN" "$LOAD_BTN_X" "$LOAD_BTN_Y"
  if shot_window "$WIN" "$OUT/02_menu.png"; then
    D=$(img_diff "$BEFORE" "$OUT/02_menu.png")
    echo "  菜单前后差异 RMSE=$D" | tee -a "$RESULT"
    awk -v d="$D" 'BEGIN { exit (d > 0.002) ? 0 : 1 }' \
      && pass "T8 点击「加载」后画面变化（菜单弹出）" \
      || fail "T8 点击后画面几乎没变（$D）—— 坐标可能偏了"
    if [ -n "${MATHHULL_X:-}" ] && [ -n "${MATHHULL_Y:-}" ]; then
      click_win "$WIN" "$MATHHULL_X" "$MATHHULL_Y"
      sleep 2
      if shot_window "$WIN" "$OUT/03_dialog.png"; then
        D2=$(img_diff "$OUT/02_menu.png" "$OUT/03_dialog.png")
        echo "  菜单→对话框 差异 RMSE=$D2" | tee -a "$RESULT"
        awk -v d="$D2" 'BEGIN { exit (d > 0.002) ? 0 : 1 }' \
          && pass "T9 打开数学船型对话框（画面变化）" \
          || fail "T9 对话框未打开（$D2）"
        xdotool key --window "$WIN" Escape 2>/dev/null
        sleep 1
      else
        fail "T9 对话框截图失败"
      fi
    else
      skip "T9 坐标未标定（MATHHULL_X/Y）"
    fi
  else
    fail "T8 菜单截图失败"
  fi
else
  skip "T8/T9 坐标未标定 —— 先看 01_start.png 再填 tools/gui_test/config.env"
fi

# ── T10 退出：干净退出（exit 0），不是崩溃重试 ──
log "请求退出"
kill -TERM -"$KSHIP_PID" 2>/dev/null
GONE=false
for _ in $(seq 1 30); do pg_alive "$KSHIP_PID" || { GONE=true; break; }; sleep 1; done
[ "$GONE" = true ] && pass "T10 进程组已退出" || fail "T10 30s 内未退出"

RETRIES_AFTER=$(grep -c "次尝试\|检测到崩溃" "$OUT/keship.log" || true)
[ "$RETRIES_AFTER" -eq 0 ] && pass "T11 退出前未触发崩溃重试" \
  || fail "T11 运行期间出现重试标志 $RETRIES_AFTER 次"

# ── T12 生产数据未被触碰（KESHIP_DATA_DIR 隔离应保证这一点）──
PROD_JSON="$WS/data/keship_water_areas.json"
if [ -f "$PROD_JSON" ]; then
  PROD_MTIME=$(stat -c %Y "$PROD_JSON")
  sleep 1
  [ "$(stat -c %Y "$PROD_JSON")" = "$PROD_MTIME" ] \
    && pass "T12 生产水域 JSON 未被改写" \
    || fail "T12 生产水域 JSON 被改写（隔离失效）"
fi

echo
if [ "$FAILS" -eq 0 ]; then
  echo "RESULT: PASS（产物：$OUT）" | tee -a "$RESULT"
  exit 0
else
  echo "RESULT: FAIL ${FAILS} 项（产物：$OUT）" | tee -a "$RESULT"
  exit 1
fi
