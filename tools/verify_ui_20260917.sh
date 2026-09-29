#!/bin/bash
# ============================================================
#  KeShip GUI 改版验证脚本（2026-09-17 布局评审改版专用）
#
#  用法：bash ~/keship_ws/tools/verify_ui_20260917.sh
#
#  只做「编译 + 静态校验」，不启动 GUI。
#  界面观感验证请随后执行：~/keship_ws/start_keship.sh
# ============================================================
set +e

WS="$HOME/keship_ws"
PKG="$WS/src/keship"
QML="$PKG/src_cpp/gui/keship/MenuPluginKeship.qml"
CC="$PKG/src_cpp/gui/keship/MenuPluginKeship.cc"
PASS=0
FAILN=0

check() {  # 描述 期望 实际
    if [ "$3" = "$2" ]; then
        echo "  [OK] $1（$3/$2）"; PASS=$((PASS+1))
    else
        echo "  [!!] $1（实际 $3 / 期望 $2 不符）"; FAILN=$((FAILN+1))
    fi
}

echo "============================================================"
echo " 1. 编译（对 C++ 与 QML 接口的最强验证）"
echo "============================================================"
# shellcheck disable=SC1091
source /opt/ros/jazzy/setup.bash 2>/dev/null
cd "$WS" || exit 2
colcon --log-base "$WS/data/log" build --packages-select keship --merge-install
BUILD_RC=$?
if [ $BUILD_RC -eq 0 ]; then
    echo "  [OK] 编译通过（退出码 0）"
else
    echo "  [!!] 编译失败（退出码 $BUILD_RC）——请查看上方报错定位"
fi

echo
echo "============================================================"
echo " 2. 静态校验：本次改动的关键点是否在位"
echo "============================================================"
check "删除模型确认（byName + byIndex）" 2 "$(grep -c '确定删除模型「' "$CC")"
check "删除全部模型确认（2026-09-25 delete-all 新语义后文案为「确定清空场景吗」）" 1 "$(grep -c '确定清空场景吗' "$CC")"
check "删除水域确认" 1 "$(grep -c '确定删除水域「' "$CC")"
check "删除子世界确认（原有，未改动）" 1 "$(grep -c '将删除其下' "$CC")"
check "确认框总数（原有3 + 新增4）" 7 "$(grep -c 'QMessageBox::question' "$CC")"
check "QML「加载」下拉入口项" 6 "$(awk '/id: loadMenu/,/^                    \}/' "$QML" | grep -c 'MenuItem {')"
check "QML Tab 新名称（自由任务+教学实验）" 2 "$(grep -cE 'qsTr\("(自由任务|教学实验)"\)' "$QML")"
check "QML 行内菜单拆分（模型/水域/子世界）" 3 "$(grep -cE 'id: (modelRowMenu|waterRowMenu|subRowMenu)' "$QML")"
check "StyledMenu 统一样式组件" 1 "$(grep -c 'component StyledMenu:' "$QML")"
check "StyledMenuItem 条目组件" 1 "$(grep -c 'component StyledMenuItem:' "$QML")"
check "菜单项全部使用 StyledMenuItem" 24 "$(grep -c '^ *StyledMenuItem {' "$QML")"
check "旧 rowMenu 已移除" 0 "$(grep -c 'id: rowMenu' "$QML")"
# 崩溃红线：Qt 5.15 下 Menu 覆盖 delegate 会导致 QML 创建期 segfault（2026-09-17 实测）
check "Menu 未覆盖 delegate（红线，必须 0）" 0 "$(grep -c 'delegate: MenuItem' "$QML")"

echo
echo "============================================================"
echo " 3. QML 导入版本一致性（MenuSeparator 需 2.1，2026-09-17 故障点）"
echo "============================================================"
if bash "$WS/tools/check_qml_versions.sh" "$QML"; then
    PASS=$((PASS+1))
else
    FAILN=$((FAILN+1))
fi

echo
echo "============================================================"
echo " 4. QML 语法静态检查"
echo "============================================================"
if command -v qmllint >/dev/null 2>&1; then
    if qmllint "$QML" 2>&1 | grep -q .; then
        echo "  [!!] qmllint 有输出（见下方），请核对"
        qmllint "$QML" 2>&1 | head -20
        FAILN=$((FAILN+1))
    else
        echo "  [OK] qmllint 无输出（通过）"; PASS=$((PASS+1))
    fi
else
    echo "  [--] 未安装 qmllint，跳过（编译已覆盖大部分语法错误）"
fi

echo
echo "============================================================"
echo " 5. 结论"
echo "============================================================"
echo "  静态校验：$PASS 项通过 / $FAILN 项不符"
if [ $BUILD_RC -eq 0 ] && [ $FAILN -eq 0 ]; then
    echo "  → 编译与静态校验全部通过。下一步执行："
    echo "     ~/keship_ws/start_keship.sh --no-build"
    echo "     然后按验证清单检查界面（加载▾下拉 / ⋯三菜单 / 五处删除确认 / Tab 切换）"
else
    echo "  → 存在未通过项，先按上方提示处理，再启动界面。"
fi
