#!/usr/bin/env bash
# KeShip GUI 测试台 —— 公共函数
# 依赖：Xvfb / xdotool / imagemagick

log() { printf '[%s] %s\n' "$(date +%H:%M:%S)" "$*"; }

# 等待窗口出现；成功时输出 window id
wait_window() {
  local timeout="${1:-180}" i w
  for ((i = 0; i < timeout; i++)); do
    w=$(xdotool search --name "$WINDOW_TITLE" 2>/dev/null | tail -1)
    [ -n "$w" ] && { echo "$w"; return 0; }
    w=$(xdotool search --name "Gazebo" 2>/dev/null | tail -1)
    [ -n "$w" ] && { echo "$w"; return 0; }
    sleep 1
  done
  return 1
}

# 截屏：优先窗口，失败退回整屏（display 由本 run 独占，root 不会有别人的窗口）
# 三项体检：尺寸、亮度、颜色数
shot_window() {
  local win="$1" out="$2" err="${2%.png}.err"
  # GL 窗口用 XGetImage 读不出（Resource temporarily unavailable），
  # 而本 run 独占该 display，直接抓 root 等价于抓本窗口，且更稳。
  import -display "$DISPLAY" -window root "$out" 2>"$err"
  [ -s "$out" ] || { echo "  [shot] 截图仍失败：$(head -3 "$err" | tr '\n' ' ')" >&2; return 1; }

  local size mean colors
  size=$(identify -format '%wx%h' "$out" 2>/dev/null || echo 0x0)
  mean=$(convert "$out" -format '%[fx:mean]' info: 2>/dev/null || echo 0)
  colors=$(identify -format '%k' "$out" 2>/dev/null || echo 0)
  echo "  [shot] $out size=$size mean=$mean colors=$colors"
  awk -v m="$mean" 'BEGIN { exit (m > 0.04) ? 0 : 1 }' || return 1
  [ "${colors:-0}" -gt 16 ] || return 1
  return 0
}

# 两张图的差异（RMSE，0~1；越大差越多）
img_diff() {
  compare -metric RMSE "$1" "$2" null: 2>&1 | sed 's/.*(\(.*\))/\1/' | cut -d' ' -f1
}

# 窗口相对坐标点击（先聚焦，Xvfb 无 WM 也能收到键鼠）
click_win() {
  local win="$1" x="$2" y="$3"
  xdotool windowfocus "$win" 2>/dev/null
  xdotool mousemove --window "$win" "$x" "$y" click 1
  sleep "${CLICK_WAIT:-1.2}"
}

# 本 run 的进程组里是否还有活着的进程
pg_alive() {
  kill -0 -"$1" 2>/dev/null
}

# 屏幕绝对坐标点击（本 run 独占 display，生成模型按钮可能落在主窗口下沿之外）
click_screen() {
  local x="$1" y="$2"
  xdotool mousemove "$x" "$y" click 1
  sleep "${CLICK_WAIT:-1.2}"
}
