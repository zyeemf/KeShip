#!/bin/bash
# KeShip 对话框主题（阶段①）验证探针 —— 独立编译运行，不依赖主构建
#
# 用法：bash tools/ui_theme_probe/run.sh
# 判定：应用 UiTheme 前后，全部控件尺寸指纹必须**逐项一致**
#       （阶段① 只改颜色，不得触及布局盒模型）；
#       同时截图像素应有大面积变化，证明配色确实生效。
export LANG=C.UTF-8
WS=/home/keship/keship_ws
SHARED=$WS/src/keship/src_cpp/gui/keship/shared
cd "$(dirname "$0")" || exit 1

echo "=== 编译探针 ==="
g++ -fPIC -std=c++17 probe.cpp \
  "$SHARED/UiTheme.cc" \
  -I"$SHARED" \
  $(pkg-config --cflags --libs Qt5Widgets) \
  -o ui_theme_probe
RC=$?
if [ $RC -ne 0 ]; then
  echo "[FAIL] 编译失败（退出码 $RC）"
  exit $RC
fi
echo "[OK] 编译通过"

echo
echo "=== 运行（offscreen）==="
export QT_SELECT=qt5
export QT_QPA_PLATFORM=offscreen
./ui_theme_probe
RC=$?
echo "探针退出码=$RC"
echo
ls -l /tmp/theme_probe_before.png /tmp/theme_probe_after.png 2>/dev/null
