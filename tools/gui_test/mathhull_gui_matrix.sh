#!/usr/bin/env bash
# 数学船型 —— GUI 操作矩阵（真 GUI：菜单→对话框→改参→生成）
#
# G1 改船型(Lewis V)+改名(HullX)+位姿(2,-1,1.2)+yaw30° → 生成 → 断言名称/位姿/浮稳
# G2 用同名再生成 → 对话框拦截（场景模型数不变）
# 字段坐标来自 1600x1000 下对话框的固定位置（截图量取）。
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$HERE/../.." && pwd)"
REAL_HOME="$HOME"
CTL="$WS/install/lib/keship/keship_ctl"

OUT="$WS/data/test/gui_test/gui_matrix_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
RESULT="$OUT/result.txt"; : > "$RESULT"
FAILS=0
pass() { echo "PASS  $*" | tee -a "$RESULT"; }
fail() { echo "FAIL  $*" | tee -a "$RESULT"; FAILS=$((FAILS + 1)); }
log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

TEST_HOME="$OUT/home"; mkdir -p "$TEST_HOME/.gz"
ln -sfn "$WS" "$TEST_HOME/keship_ws"
[ -d "$REAL_HOME/keship-gz" ] && ln -sfn "$REAL_HOME/keship-gz" "$TEST_HOME/keship-gz"
[ -d "$REAL_HOME/.gz/fuel" ] && ln -sfn "$REAL_HOME/.gz/fuel" "$TEST_HOME/.gz/fuel"

Xvfb :92 -screen 0 1600x1000x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
XVFB_PID=$!; sleep 2
export DISPLAY=:92 HOME="$TEST_HOME" KESHIP_DATA_DIR="$OUT/data"
export GZ_PARTITION="keship_matrix" ROS_DOMAIN_ID="67"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe QT_QPA_PLATFORM=xcb
mkdir -p "$KESHIP_DATA_DIR"

setsid bash "$WS/start_keship.sh" --no-build --test-isolated --no-d3d12 --no-gpu-warm --gz-verbose 2 \
  > "$OUT/keship.log" 2>&1 &
KSHIP_PID=$!
cleanup() {
  kill -TERM -"$KSHIP_PID" 2>/dev/null; sleep 2; kill -KILL -"$KSHIP_PID" 2>/dev/null
  kill "$XVFB_PID" 2>/dev/null; rm -f "$WS/data/keship_test.lock"
}
trap cleanup EXIT

for _ in $(seq 1 150); do xdotool search --name "Gazebo" >/dev/null 2>&1 && break; sleep 1; done
sleep 4

shot() { import -display "$DISPLAY" -window root "$1" 2>/dev/null; }
click_at() { xdotool mousemove "$1" "$2" click 1; sleep "${3:-0.6}"; }
key_at() { xdotool key "$@"; sleep 0.3; }

# 字段坐标（1600x1000，对话框固定在左侧面板之上）
NAME_X=800 NAME_Y=436
POS_X=586  POS_Y=470
POS_Yx=767 POS_Yy=470
POS_Zx=942 POS_Zy=470
YAW_X=957 YAW_Y=507
TYPE_X=700 TYPE_Y=134
SPAWN_X=790 SPAWN_Y=810

set_field() {  # x y text
  click_at "$1" "$2" 0.4
  key_at ctrl+a
  xdotool type --delay 40 -- "$3"
  sleep 0.3
}

DLG=""
open_dialog() {
  click_at 53 108 1.0
  click_at 150 220 1.5
  DLG=$(xdotool search --name "新建数学船型" | tail -1)
  [ -n "$DLG" ] || { fail "对话框未出现"; return 1; }
  xdotool windowfocus "$DLG" 2>/dev/null; sleep 0.3
  return 0
}
close_dialog() {
  [ -n "$DLG" ] && xdotool windowfocus "$DLG" 2>/dev/null
  key_at Escape; sleep 0.4; key_at Escape; sleep 0.3
  DLG=""
}

cat > "$OUT/water.json" <<'JSON'
{"version":2,"areas":[{"name":"测试水域","xMin":-200,"xMax":200,"yMin":-200,"yMax":200,"baseHeight":0,
"vertices":[[-200,-200],[200,-200],[200,200],[-200,200]],"waveModel":"PMS","wavePeriod":5,"waveNumber":6,
"waveScale":1.1,"waveGain":0,"waveDirection":0,"waveAngle":0.4,"waveSteepness":0,"waveAmplitude":0,
"waveTau":2,"windSpeed":0,"windDirection":0,"currentSpeed":0,"currentDirection":0,
"waveDirectionAuto":"true"}]}
JSON
"$CTL" --force water set "$OUT/water.json" > "$OUT/water.log" 2>&1 \
  && pass "水区已注入" || fail "水区注入失败"

model_count() { "$CTL" --force model list 2>/dev/null | grep -c . || true; }

# ── G1 ──
open_dialog || exit 1
click_at "$TYPE_X" "$TYPE_Y" 0.5
key_at Home
for _ in 1 2 3 4 5; do key_at Down; done
key_at Return
set_field "$NAME_X" "$NAME_Y" "HullX"
set_field "$POS_X" "$POS_Y" "2"
set_field "$POS_Yx" "$POS_Yy" "-1"
set_field "$POS_Zx" "$POS_Zy" "1.2"
set_field "$YAW_X" "$YAW_Y" "30"
key_at Return
shot "$OUT/g1_filled.png"
click_at "$SPAWN_X" "$SPAWN_Y" 2.0
shot "$OUT/g1_after.png"

INFO=$("$CTL" --force --json model info HullX 2>/dev/null)
echo "$INFO" | grep -q '"inScene":true' && pass "G1 改名生成成功（HullX 在场景）" \
  || { fail "G1 未生成 HullX"; tail -3 "$OUT/keship.log" | tee -a "$RESULT"; }

POSE=$("$CTL" --force --json model pose HullX 2>/dev/null \
  | python3 -c 'import json,sys; d=json.load(sys.stdin); print(*d["pos"], *d["rpy"])' 2>/dev/null) || true
if [ -n "${POSE:-}" ]; then
  echo "      G1 位姿: $POSE" | tee -a "$RESULT"
  read -r X Y Z R P YW <<< "$POSE"
  awk -v x="$X" -v y="$Y" 'BEGIN { dx=x-2; dy=y+1; exit (dx*dx+dy*dy < 0.25) ? 0 : 1 }' \
    && pass "G1 生成位置 = 设定值 (2,-1)" || fail "G1 位置不符（$X,$Y）"
  awk -v w="$YW" 'BEGIN { d=w-0.5236; if (d<0) d=-d; exit (d < 0.05) ? 0 : 1 }' \
    && pass "G1 偏航角 = 30°（$YW rad）" || fail "G1 偏航角不符（$YW）"
  awk -v z="$Z" 'BEGIN { exit (z > -1 && z < 1) ? 0 : 1 }' \
    && pass "G1 浮在水面附近（z=$Z）" || fail "G1 z=$Z 异常"
fi

# ── G2 重名 ──
close_dialog
BEFORE=$(model_count)
open_dialog || exit 1
set_field "$NAME_X" "$NAME_Y" "HullX"
click_at "$SPAWN_X" "$SPAWN_Y" 1.5
shot "$OUT/g2_duplicate.png"
WARN=$(xdotool search --name "警告" | tail -1)
[ -n "$WARN" ] && { xdotool windowfocus "$WARN"; xdotool key Return; sleep 1; }
AFTER=$(model_count)
[ "$BEFORE" -eq "$AFTER" ] && pass "G2 重名被拦截（模型数 $BEFORE 不变）" \
  || fail "G2 重名未被拦截（$BEFORE → $AFTER）"
close_dialog

echo
if [ "$FAILS" -eq 0 ]; then echo "RESULT: PASS（产物：$OUT）" | tee -a "$RESULT"; exit 0
else echo "RESULT: FAIL ${FAILS} 项（产物：$OUT）" | tee -a "$RESULT"; exit 1; fi
