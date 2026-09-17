#!/bin/bash
# ============================================================
#  QML 版本切换（崩溃排查用，2026-09-17）
#
#  用法：
#    bash ~/keship_ws/tools/switch_qml.sh v0     # 切到提交版（HEAD，无 ⋯ 菜单、无今日改版）
#    bash ~/keship_ws/tools/switch_qml.sh cur    # 切回今日改版
#
#  切换后需重新编译：colcon build --packages-select keship --merge-install
#  目的：二分定位崩溃来源——若 v0 不崩而 cur 崩，则问题在本地未提交改动内。
# ============================================================
set -e

WS="$HOME/keship_ws"
QML="$WS/src/keship/src_cpp/gui/keship/MenuPluginKeship.qml"
BK="$WS/tools/qml_backup"
HEAD_VER="$BK/MenuPluginKeship.qml.HEAD_310779b"
CUR_VER="$BK/MenuPluginKeship.qml.current_20260917"

case "$1" in
    v0)
        cp "$HEAD_VER" "$QML"
        echo "[已切换] 提交版（HEAD 310779b：按钮平铺、无 ⋯ 菜单、无今日改版）"
        ;;
    cur)
        cp "$CUR_VER" "$QML"
        echo "[已切换] 今日改版（0818 结构树改造 + 09-17 布局改版，import 已回退 2.0）"
        ;;
    *)
        echo "用法: bash ~/keship_ws/tools/switch_qml.sh v0|cur"
        exit 1
        ;;
esac
echo "当前 QML: $(wc -l < "$QML") 行"
echo "下一步：cd ~/keship_ws && colcon --log-base data/log build --packages-select keship --merge-install"
