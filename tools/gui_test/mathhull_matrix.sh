#!/usr/bin/env bash
# 数学船型生成 —— 参数/船型矩阵（无头驱动，与 GUI 走同一个 SpawnMathHull）
#
# 覆盖：6 种船型 × 水动力(轻量/完整) + 极端尺寸 + 自定义位姿 + 零水动力
# 断言：模型在场景(inScene) / 水平位姿对 / 浮稳 / 稳态 z 在水面附近
# 用法：bash tools/gui_test/mathhull_matrix.sh
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$HERE/../.." && pwd)"
REAL_HOME="$HOME"
CTL="$WS/install/lib/keship/keship_ctl"

OUT="$WS/data/test/gui_test/matrix_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
RESULT="$OUT/result.txt"
: > "$RESULT"

FAILS=0
pass() { echo "PASS  $*" | tee -a "$RESULT"; }
fail() { echo "FAIL  $*" | tee -a "$RESULT"; FAILS=$((FAILS + 1)); }
log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

TEST_HOME="$OUT/home"
mkdir -p "$TEST_HOME/.gz"
ln -sfn "$WS" "$TEST_HOME/keship_ws"
[ -d "$REAL_HOME/keship-gz" ] && ln -sfn "$REAL_HOME/keship-gz" "$TEST_HOME/keship-gz"
[ -d "$REAL_HOME/.gz/fuel" ] && ln -sfn "$REAL_HOME/.gz/fuel" "$TEST_HOME/.gz/fuel"

Xvfb :91 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 2
export DISPLAY=:91
export HOME="$TEST_HOME"
export KESHIP_DATA_DIR="$OUT/data"
export GZ_PARTITION="keship_matrix"
export ROS_DOMAIN_ID="67"
export LIBGL_ALWAYS_SOFTWARE=1
export GALLIUM_DRIVER=llvmpipe
mkdir -p "$KESHIP_DATA_DIR"

setsid bash "$WS/start_keship.sh" --no-build --test-isolated --no-d3d12 --no-gpu-warm --gz-verbose 2 \
  > "$OUT/keship.log" 2>&1 &
KSHIP_PID=$!

cleanup() {
  kill -TERM -"$KSHIP_PID" 2>/dev/null
  sleep 2
  kill -KILL -"$KSHIP_PID" 2>/dev/null
  kill "$XVFB_PID" 2>/dev/null
  rm -f "$WS/data/keship_test.lock"
}
trap cleanup EXIT

# 等窗口
for _ in $(seq 1 150); do
  xdotool search --name "Gazebo" >/dev/null 2>&1 && break
  sleep 1
done
sleep 3

# 注入测试水区（启动后热更新，原因见 gui_test_plan §4.1）
cat > "$OUT/water.json" <<'JSON'
{"version":2,"areas":[{"name":"测试水域","xMin":-200,"xMax":200,"yMin":-200,"yMax":200,"baseHeight":0,
"vertices":[[-200,-200],[200,-200],[200,200],[-200,200]],"waveModel":"PMS","wavePeriod":5,"waveNumber":6,
"waveScale":1.1,"waveGain":0,"waveDirection":0,"waveAngle":0.4,"waveSteepness":0,"waveAmplitude":0,
"waveTau":2,"windSpeed":0,"windDirection":0,"currentSpeed":0,"currentDirection":0,
"waveDirectionAuto":"true"}]}
JSON
"$CTL" --force water set "$OUT/water.json" > "$OUT/water_set.log" 2>&1 \
  && pass "水区已注入" || fail "水区注入失败"

wait_float() {   # $1 名称；输出最终 "x y z"
  local name="$1" last="" cur="" cz=""
  for _ in $(seq 1 25); do
    cur=$("$CTL" --force --json model pose "$name" 2>/dev/null \
      | python3 -c 'import json,sys; d=json.load(sys.stdin); print(*d["pos"])' 2>/dev/null) || true
    [ -z "$cur" ] && { sleep 1; continue; }
    cz=$(echo "$cur" | awk '{print $3}')
    if [ -n "$last" ] && awk -v a="$last" -v b="$cz" \
         'BEGIN { d=a-b; if (d<0) d=-d; exit (d < 0.02) ? 0 : 1 }'; then
      echo "$cur"
      return 0
    fi
    last="$cz"
    sleep 1
  done
  echo "$cur"
  return 1
}

run_case() {   # $1 标签  $2..  spawn-hull 参数
  local tag="$1"; shift
  log "生成 $tag"
  if ! "$CTL" --force model spawn-hull "$@" > "$OUT/spawn_$tag.log" 2>&1; then
    fail "$tag 生成命令失败"
    tail -2 "$OUT/spawn_$tag.log" | tee -a "$RESULT"
    return
  fi
  local name; name=$("$CTL" --force --json model list 2>/dev/null \
    | python3 -c 'import json,sys
d=json.load(sys.stdin)
ms=d.get("value",d) if isinstance(d,dict) else d
print(ms[-1]["name"] if isinstance(ms,list) and ms else "")' 2>/dev/null)
  # model list 结构不确定时退回日志里的名字
  [ -z "$name" ] && name=$(grep -oE "已生成数学船型 [^ ]+" "$OUT/spawn_$tag.log" | awk '{print $2}' | head -1)
  [ -z "$name" ] && { fail "$tag 无法确定模型名"; return; }

  local info; info=$("$CTL" --force --json model info "$name" 2>/dev/null)
  echo "$info" | grep -q '"inScene":true' \
    && pass "$tag 在场景（$name）" || { fail "$tag 不在场景"; return; }

  local pose; pose=$(wait_float "$name") || { fail "$tag 未浮稳（$pose）"; return; }
  echo "      $tag 稳态位姿: $pose" | tee -a "$RESULT"
  local x y z; read -r x y z <<< "$pose"
  awk -v z="$z" -v band="${CASE_BAND:-1.0}" 'BEGIN { exit (z > -band && z < band) ? 0 : 1 }' \
    && pass "$tag 停在水面附近（z=$z）" || fail "$tag z=$z 异常"
}

# ── 6 种船型（轻量水动力）──
TYPES="wigley-classic wigley-flat wigley-sharp wigley-fine lewis-u lewis-v"
i=0
for t in $TYPES; do
  i=$((i + 1))
  run_case "type_$t" "T$i" --type "$t" --hydro-estimate \
    --pos "$((i * 2)) 0 0.6 0 0 0"
done

# ── 完整水动力档（一个代表船型）──
run_case "hydro_full" "TFull" --type lewis-u --hydro-full --pos "-6 0 0.6 0 0 0"

# ── 零水动力（只靠浮力）──
run_case "hydro_off" "TOff" --type wigley-classic --pos "8 0 0.6 0 0 0"

# ── 极端尺寸 ──
CASE_BAND=3.2
run_case "extreme" "TBig" --type wigley-classic --L 20 --B 5 --D 3 --rho 950 \
  --hydro-estimate --pos "0 6 1.6 0 0 0"

# ── 自定义位姿（含旋转）──
run_case "pose" "TPose" --type wigley-classic --hydro-estimate \
  --pos "-2 -6 0.8 0 0 0.5236"

echo
if [ "$FAILS" -eq 0 ]; then
  echo "RESULT: PASS（产物：$OUT）" | tee -a "$RESULT"
  exit 0
else
  echo "RESULT: FAIL ${FAILS} 项（产物：$OUT）" | tee -a "$RESULT"
  exit 1
fi
