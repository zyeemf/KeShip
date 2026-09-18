#!/bin/bash
# ============================================================
#  check_qml_versions.sh — QML 导入版本一致性检查
#
#  用途：核对 QML 声明的 `import QtQuick.Controls X.Y` 是否满足
#        该文件实际使用的全部 Controls 类型的最低引入版本。
#  背景：2026-09-17 实测故障——文件 import 2.0 却使用了 MenuSeparator（2.1 引入），
#        导致插件加载失败："MenuSeparator is not a type"。
#
#  ⚠️ 2026-09-18 修复（Skill 演练时发现）：
#     类型抓取原为「行首声明式」正则（^\s*(Menu|MenuItem|...)\s*\{），
#     但本文件已改用内联组件（`component StyledMenu: Menu {` 形式），
#     类型名不再位于行首 → Menu / MenuItem / MenuSeparator 三者**全部漏检**，
#     样本数由 8 掉到 5，而脚本**仍输出 [OK]**（真实漏检被"全绿"掩盖）。
#     现改为「词边界式」抓取（类型名前不得是字母/数字/下划线），
#     并新增「覆盖自检」：内联组件的基类型若属 Controls 类却未被计入样本，一律 [!!] 报警。
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

# 候选 Controls 类型：逐个用词边界匹配（避免子串误判，例如 Menu 不应匹配 MenuBar）
CANDIDATES="Menu MenuItem MenuSeparator Button GroupBox TextField ComboBox ScrollBar ScrollIndicator CheckBox Switch Slider Dialog Drawer SpinBox ProgressBar ToolBar MenuBar TabBar StackView Page Pane Frame Label ItemDelegate CheckDelegate RadioButton RadioDelegate SwitchDelegate RangeSlider SplitView SwipeView BusyIndicator DelayButton Dial Tumbler ToolSeparator Action ActionGroup ButtonGroup ScrollView Container Popup ApplicationWindow"

TYPES=""
for T in $CANDIDATES; do
    if grep -qE "(^|[^A-Za-z0-9_])$T[[:space:]]*\{" "$QML"; then
        TYPES="$TYPES $T"
    fi
done

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

# ---- 覆盖自检（2026-09-18 新增）：内联组件基类型若属 Controls 类，必须已计入样本 ----
BASES=$(grep -oE "^[[:space:]]*component[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*:[[:space:]]*[A-Za-z_][A-Za-z0-9_]*" "$QML" \
        | sed -E 's/.*:[[:space:]]*//' | sort -u)
UNCOVERED=""
NB=0
for B in $BASES; do
    grep -qE "name: \"QtQuick\.Controls/$B [0-9]+\.[0-9]+\"" "$QMLTYPES" || continue   # 非 Controls 类型（如 Item）不计
    NB=$((NB+1))
    case " $TYPES " in
        *" $B "*) ;;
        *) UNCOVERED="$UNCOVERED $B" ;;
    esac
done

echo "检查文件: $(basename "$QML")"
echo "  声明版本: $DECL"
echo "  检查样本数: $N 个 Controls 类型（类型 → 引入版本）"
echo -e "  $DETAIL"
echo "  覆盖自检: 内联组件基类型中属 Controls 的 $NB 个"
if [ -n "$UNCOVERED" ]; then
    echo "  [!!] 覆盖自检失败：下列内联组件基类型属 Controls 类却未被计入样本 →$UNCOVERED"
    echo "       （类型抓取又漏了，请修 CANDIDATES 或抓取方式，不要当成通过）"
    exit 1
fi
echo "  所需最低版本: $REQUIRED"

if awk -v a="$DECL" -v b="$REQUIRED" 'BEGIN{exit !(a<b)}'; then
    echo "  [!!] 版本不足：声明 $DECL < 所需 $REQUIRED —— 加载时会报 \"... is not a type\""
    exit 1
else
    echo "  [OK] 声明版本满足全部所用类型"
    exit 0
fi
