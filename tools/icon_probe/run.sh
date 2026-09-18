#!/bin/bash
# 自绘 SVG 图标渲染验证探针 —— 独立编译，不依赖主构建
#
# 说明：独立编译的探针**不含 qrc 资源**，故先用 rcc 把 icons.qrc 编成二进制
#       .rcc，再在运行时注册，这样才能看到真实图标。
# 判定：① 资源注册成功 ② 每个按钮的 icon().isNull() 必须为 false（否则图标没加载上）
#      ③ 截图肉眼核对（另存 2 倍放大图）
export LANG=C.UTF-8
WS=/home/keship/keship_ws
GUI=$WS/src/keship/src_cpp/gui/keship
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1

echo "=== 1. 把 icons.qrc 编译为二进制资源 ==="
RCC=/usr/lib/qt5/bin/rcc
[ -x "$RCC" ] || RCC=$(command -v rcc)
echo "rcc = $RCC"
(cd "$GUI" && "$RCC" --binary icons.qrc -o /tmp/icons.rcc) || exit 1
ls -l /tmp/icons.rcc

echo
echo "=== 2. 编译探针 ==="
g++ -fPIC -std=c++17 icon_probe.cpp \
  "$GUI/shared/UiTheme.cc" \
  -I"$GUI/shared" \
  $(pkg-config --cflags --libs Qt5Widgets) \
  -o icon_probe
RC=$?
if [ $RC -ne 0 ]; then
  echo "[FAIL] 编译失败（退出码 $RC）"
  exit $RC
fi
echo "[OK] 编译通过"

echo
echo "=== 3. 运行（offscreen）==="
export QT_SELECT=qt5
export QT_QPA_PLATFORM=offscreen
./icon_probe
echo "探针退出码=$?"
echo
ls -l /tmp/icon_probe.png /tmp/icon_probe_2x.png 2>/dev/null
