#!/bin/bash
# MapWidget 底部文字布局验证探针 —— 独立编译运行，不依赖主构建
#
# 用法：bash tools/map_layout_probe/run.sh
# 判定：绘图区下方的「X 轴刻度 / 操作提示 / 范围信息」必须**分层不重叠**。
#       对比指标：底部 60px 内的文字行带个数（重叠时会合并成一个宽带）。
#       2026-09-18 曾因提示与刻度同区间（ph+2~16 vs ph+4~16）产生"拖拽移25"式叠字。
export LANG=C.UTF-8
WS=/home/keship/keship_ws
GUI=$WS/src/keship/src_cpp/gui/keship
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE" || exit 1

echo "=== 生成 moc（MapWidget 含 Q_OBJECT）==="
MOC=/usr/lib/qt5/bin/moc
[ -x "$MOC" ] || MOC=$(command -v moc)
echo "moc = $MOC"
"$MOC" "$GUI/tasks/MapWidget.hh" \
  -I"$GUI/tasks" -I"$WS/src/keship/include" \
  -o moc_MapWidget.cpp || exit 1
echo "[OK] moc_MapWidget.cpp"

echo
echo "=== 编译探针 ==="
g++ -fPIC -std=c++17 probe.cpp \
  "$GUI/tasks/MapWidget.cc" moc_MapWidget.cpp \
  -I"$GUI/tasks" \
  -I"$WS/src/keship/include" \
  $(pkg-config --cflags --libs Qt5Widgets) \
  -o map_layout_probe
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
./map_layout_probe
RC=$?
echo "探针退出码=$RC"

echo
echo "=== 逐行像素分析：底部文字行带是否分离 ==="
python3 analyze.py /tmp/map_hint.png /tmp/map_nohint.png
echo
ls -l /tmp/map_hint.png /tmp/map_nohint.png /tmp/map_hint_bottom.png 2>/dev/null
