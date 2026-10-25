#!/usr/bin/env bash
# 跑"改质量→重建"回归探针（需要运行中的世界；本脚本负责起隔离实例）
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
WS="$(cd "$HERE/../.." && pwd)"
REAL_HOME="$HOME"
PROBE="$WS/build/keship/mathhull_rebuild_probe"
CTL="$WS/install/lib/keship/keship_ctl"

[ -x "$PROBE" ] || { echo "缺少探针：$PROBE（用 -DKESHIP_BUILD_TESTS=ON 编译）"; exit 1; }

OUT="$WS/data/test/gui_test/probe_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$OUT"
TEST_HOME="$OUT/home"; mkdir -p "$TEST_HOME/.gz"
ln -sfn "$WS" "$TEST_HOME/keship_ws"
[ -d "$REAL_HOME/keship-gz" ] && ln -sfn "$REAL_HOME/keship-gz" "$TEST_HOME/keship-gz"
[ -d "$REAL_HOME/.gz/fuel" ] && ln -sfn "$REAL_HOME/.gz/fuel" "$TEST_HOME/.gz/fuel"

Xvfb :94 -screen 0 1280x800x24 -nolisten tcp > "$OUT/xvfb.log" 2>&1 &
XVFB_PID=$!; sleep 2
export DISPLAY=:94 HOME="$TEST_HOME" KESHIP_DATA_DIR="$OUT/data"
export GZ_PARTITION="keship_probe" ROS_DOMAIN_ID="69"
export LIBGL_ALWAYS_SOFTWARE=1 GALLIUM_DRIVER=llvmpipe
mkdir -p "$KESHIP_DATA_DIR"

setsid bash "$WS/start_keship.sh" --no-build --test-isolated --no-d3d12 --no-gpu-warm --gz-verbose 3 \
  > "$OUT/keship.log" 2>&1 &
KSHIP_PID=$!
cleanup() { kill -TERM -"$KSHIP_PID" 2>/dev/null; sleep 2; kill -KILL -"$KSHIP_PID" 2>/dev/null
            kill "$XVFB_PID" 2>/dev/null; rm -f "$WS/data/keship_test.lock"; }
trap cleanup EXIT

for _ in $(seq 1 150); do xdotool search --name "Gazebo" >/dev/null 2>&1 && break; sleep 1; done
sleep 3

cat > "$OUT/water.json" <<'JSON'
{"version":2,"areas":[{"name":"测试水域","xMin":-200,"xMax":200,"yMin":-200,"yMax":200,"baseHeight":0,
"vertices":[[-200,-200],[200,-200],[200,200],[-200,200]],"waveModel":"PMS","wavePeriod":5,"waveNumber":6,
"waveScale":1.1,"waveGain":0,"waveDirection":0,"waveAngle":0.4,"waveSteepness":0,"waveAmplitude":0,
"waveTau":2,"windSpeed":0,"windDirection":0,"currentSpeed":0,"currentDirection":0,
"waveDirectionAuto":"true"}]}
JSON
"$CTL" --force water set "$OUT/water.json" > "$OUT/water.log" 2>&1

echo "=== 探针输出"
"$PROBE" 2>&1 | tee "$OUT/probe.log"
RC=${PIPESTATUS[0]}
echo "=== rc=$RC（0=PASS）"
exit "$RC"
