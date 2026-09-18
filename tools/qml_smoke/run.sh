#!/usr/bin/env bash
# 编译并运行 QML 创建期冒烟测试（首次自动编译，之后复用二进制）
# 用法：bash tools/qml_smoke/run.sh <file.qml>
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/smoke.cpp"
BIN="$HERE/qml_smoke"
QML="${1:-}"

if [ -z "$QML" ]; then
  echo "[ERROR] 用法: bash $0 <file.qml>"
  exit 64
fi

export QT_SELECT=qt5
export QT_QPA_PLATFORM=offscreen

# 编译（缺 pkg-config 时退回手写路径）
if [ ! -x "$BIN" ] || [ "$SRC" -nt "$BIN" ]; then
  if pkg-config --exists Qt5Quick 2>/dev/null; then
    g++ -fPIC -std=c++14 "$SRC" -o "$BIN" \
      $(pkg-config --cflags Qt5Quick Qt5Qml Qt5Gui Qt5Core) \
      $(pkg-config --libs Qt5Quick Qt5Qml Qt5Gui Qt5Core) || { echo "[ERROR] 编译失败"; exit 65; }
  else
    QTINC=/usr/include/x86_64-linux-gnu/qt5
    g++ -fPIC -std=c++14 "$SRC" -o "$BIN" \
      -I$QTINC -I$QTINC/QtCore -I$QTINC/QtGui -I$QTINC/QtQml -I$QTINC/QtQuick \
      -lQt5Quick -lQt5Qml -lQt5Gui -lQt5Core || { echo "[ERROR] 编译失败"; exit 65; }
  fi
  echo "[INFO] 已编译 $BIN"
fi

SHOT="${2:-}"
IDX="${3:-0}"
timeout 30 "$BIN" "$QML" "$SHOT" "$IDX"
code=$?
case $code in
  0)   echo "[PASS] QML 创建期无异常（退出码 0）" ;;
  2)   echo "[FAIL] QML 编译错误" ;;
  3)   echo "[FAIL] create() 失败" ;;
  4)   echo "[FAIL] 行为断言未通过（见 [行为] FAIL 行）" ;;
  124) echo "[FAIL] 超时（可能卡在渲染/事件循环）" ;;
  139) echo "[FAIL] SIGSEGV —— 即 2026-09-17 的崩溃模式，禁止入库" ;;
  *)   echo "[FAIL] 退出码 $code" ;;
esac
exit $code
