#!/bin/bash
# ============================================================
#  check_qml_versions.sh — QML 导入版本一致性检查
#
#  用途：核对 QML 声明的 `import QtQuick.Controls X.Y` 是否满足
#        该文件实际使用的全部 Controls 类型的最低引入版本。
#  背景：2026-09-17 实测故障——文件 import 2.0 却使用了 MenuSeparator（2.1 引入），
#        导致插件加载失败："MenuSeparator is not a type"。
#
#  用法：bash ~/keship_ws/tools/check_qml_versions.sh [QML文件路径]
#        不带参数则检查 GUI 主插件 QML。
# ============================================================
set +e

QML="${1:-$HOME/keship_ws/src/keship/src_cpp/gui/keship/MenuPluginKeship.qml}"
# 双路径兼容：WSL 内用系统路径；从 Windows 侧（UNC）调用时回退到 \\wsl.localhost 形式
if [ ! -f "$QML" ]; then
    QML="//wsl.localhost/KeShipV0010/home/keship/keship_ws/src/keship/src_cpp/gui/keship/MenuPluginKeship.qml"
fi
QMLTYPES="/usr/lib/x86_64-linux-gnu/qt5/qml/QtQuick/Controls.2/plugins.qmltypes"
[ -f "$QMLTYPES" ] || QMLTYPES="//wsl.localhost/KeShipV0010/usr/lib/x86_64-linux-gnu/qt5/qml/QtQuick/Controls.2/plugins.qmltypes"

[ -f "$QML" ] || { echo "[!!] QML 文件不存在: $QML"; exit 2; }
[ -f "$QMLTYPES" ] || { echo "[??] 未能判定：未找到 $QMLTYPES（样本数 0）"; exit 0; }

DECL=$(grep -oE "import QtQuick\.Controls [0-9]+\.[0-9]+" "$QML" | head -1 | grep -oE "[0-9]+\.[0-9]+")
[ -n "$DECL" ] || { echo "[??] 未能判定：文件中未找到 import QtQuick.Controls（样本数 0）"; exit 0; }

# 本文件出现的 Controls 类型（行首类型声明形式）
TYPES=$(grep -oE "^\s*(Menu|MenuItem|MenuSeparator|Button|GroupBox|TextField|ComboBox|ScrollBar|ScrollIndicator|CheckBox|Switch|Slider|Dialog|Drawer|SpinBox|ProgressBar|ToolBar|MenuBar|TabBar|StackView|Page|Pane|Frame|Label|ItemDelegate|CheckDelegate|RadioButton|RadioDelegate|SwitchDelegate|RangeSlider|SplitView|SwipeView|BusyIndicator|DelayButton|Dial|Tumbler|ToolSeparator|Action|ActionGroup|ButtonGroup|ScrollView|Container|Popup|ApplicationWindow)\s*\{" "$QML" | sed 's/[{ \t]//g' | sort -u)

REQUIRED="2.0"
N=0
DETAIL=""
for T in $TYPES; do
    V=$(grep -oE "name: \"QtQuick\.Controls/$T [0-9]+\.[0-9]+\"" "$QMLTYPES" | head -1 | grep -oE "[0-9]+\.[0-9]+")
    [ -z "$V" ] && continue          # 自建类型不在 Controls 中，跳过
    N=$((N+1))
    DETAIL="$DETAIL\n    $T: 需 $V"
    # 浮点比较取大者
    if awk -v a="$V" -v b="$REQUIRED" 'BEGIN{exit !(a>b)}'; then REQUIRED="$V"; fi
done

echo "检查文件: $(basename "$QML")"
echo "  声明版本: $DECL"
echo "  检查样本数: $N 个 Controls 类型（类型 → 引入版本）"
echo -e "  $DETAIL"
echo "  所需最低版本: $REQUIRED"

if awk -v a="$DECL" -v b="$REQUIRED" 'BEGIN{exit !(a<b)}'; then
    echo "  [!!] 版本不足：声明 $DECL < 所需 $REQUIRED —— 加载时会报 \"... is not a type\""
    exit 1
else
    echo "  [OK] 声明版本满足全部所用类型"
    exit 0
fi
