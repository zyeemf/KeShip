/* 菜单插件： keship */
/// Zhangyu 创建 2026.1.16
/// 2026.5.28 重构：Tab 布局（模型管理 / 虚拟实验）

import QtQuick 2.0
/// 版本要求可核查，勿随意升降！
/// 本文件用到的 QtQuick.Controls 类型引入版本（依据 Controls.2/plugins.qmltypes）：
///   Menu / MenuItem / Button / GroupBox / TextField / ComboBox / ScrollBar / ScrollIndicator 均为 2.0；
///   **MenuSeparator（本文件 5 处）为 2.1** ← 决定了本文件的最低要求
/// 2.0 下会报 "MenuSeparator is not a type" 导致插件加载失败（2026-09-17 实测）。
/// 若日后加入更高版本的类型/属性，须同步提升此版本号；
/// 可用 tools/check_qml_versions.sh 自动核对。
import QtQuick.Controls 2.1

Rectangle {
    id: root
    anchors.fill: parent
    color: theme.panelBg

    /// 当前激活 Tab（0=模型管理, 1=虚拟实验）
    property int tabBarCurrent: 0
    /// 面板收起状态（双击标题切换，收起时仅显示标题栏）
    property bool collapsed: false

    /// ═══════════════════════════════════════════════════════════════
    /// 设计令牌（2026-09-18 建立｜颜色与尺寸的**唯一来源**）
    /// 规则：本文件任何控件不得再写十六进制色值，一律引用 theme.*
    /// 本轮为**纯令牌化重构**：所有色值与建立前**逐字节相同**，零视觉变化。
    /// 归并相近色 / 调整配色属后续批次（见 doc/plans/ui_improvement_plan.md）。
    /// ═══════════════════════════════════════════════════════════════
    QtObject {
        id: theme

        // ---- 品牌 / 主色 ----
        readonly property color primary:        "#7384e4"   // 选中、激活、强调
        readonly property color primaryHover:   "#dde4f7"   // 条目悬停底
        readonly property color accent:         "#66c4e9"   // 普通按钮底
        readonly property color accentLoad:     "#8fd0a8"   // 载入类按钮底
        readonly property color accentLoadDown: "#5eb07e"   // 载入类按钮按下
        readonly property color warn:           "#f2c14e"   // 警示 / 维护类操作
        readonly property color warnDown:       "#d9a62e"   // 警示类按下
        readonly property color danger:         "#d9534f"   // 危险操作（删除类）
        readonly property color dangerDown:     "#bf4440"   // 危险操作按下
        readonly property color ok:             "#448844"   // 通过 / 启用

        // ---- 面板 / 容器 ----
        readonly property color panelBg:        "#d0d0d8c0" // 插件面板底（含 alpha，原值保留）
        readonly property color sectionBg:      "#c0d8f0"   // 按钮区行容器
        readonly property color headerBg:       "#b8b8c8"   // 实验列表课程分组头
        readonly property color surface:        "#ffffff"   // 弹层 / 条目底
        readonly property color surfaceIdle:    "#e8e8ee"   // 标题按钮未激活底
        readonly property color rowEven:        "#f8f8f8"   // 树行斑马纹（偶）
        readonly property color rowOdd:         "#ececec"   // 树行斑马纹（奇）
        readonly property color rowSelected:    "#b8d0f0"   // 树行选中
        readonly property color expRowBg:       "#e8e8ec"   // 实验行底
        readonly property color disabledBg:     "#f4f4f7"   // 条目禁用底

        // ---- 文字 ----
        readonly property color text:           "#20202a"   // 主文字
        readonly property color textStrong:     "#101010"   // 组行文字
        readonly property color textOnDark:     "#ffffff"   // 深底上的文字
        readonly property color textOnBlack:    "#000000"   // 按钮常态文字
        readonly property color textDark:       "#202020"   // 实验行文字
        readonly property color textLabel:      "#303030"   // 紧凑控件 / 分组头文字
        readonly property color textMid:        "#404040"   // 标题按钮未激活文字
        readonly property color textSub:        "#555555"   // 次要说明
        readonly property color textSub2:       "#666666"   // 次要说明（2）
        readonly property color textDim:        "#888888"   // 空态提示 / 按钮禁用文字
        readonly property color textMuted:      "#999999"   // 实验行未启用文字
        readonly property color textFaint:      "#a8a8b0"   // 条目禁用文字
        readonly property color textDisabled:   "#b0b0c0"   // 控件禁用文字
        readonly property color infoText:       "#0055aa"   // 海况等提示信息
        readonly property color okText:         "#338833"   // 启用标记

        // ---- 边框 / 分隔 ----
        readonly property color border:         "#c8c8d8"   // 通用浅边框 / 分隔线
        readonly property color borderStrong:   "#444444"   // 强调边框（按钮悬停 / 实验行）
        readonly property color borderMenu:     "#9aa0b4"   // 弹层描边
        readonly property color borderRow:      "#cccccc"   // 树叶行描边
        readonly property color borderRowGroup: "#8a8a8a"   // 树组行描边
        readonly property color treeLine:       "#b8b8b8"   // 树层级连接线

        // ---- 树 / 分组专用 ----
        readonly property color groupSubBg:     "#e8d8c8"
        readonly property color groupWaterBg:   "#c8e8d8"
        readonly property color groupModelBg:   "#c8d8e8"
        readonly property color groupSubBar:    "#c07840"
        readonly property color groupWaterBar:  "#40a070"
        readonly property color groupModelBar:  "#4070b0"

        // ---- 行内紧凑控件（⋯ 按钮）----
        readonly property color miniBg:         "#e0e0e8"
        readonly property color miniBgHover:    "#b8c8e0"
        readonly property color miniBorder:     "#909098"

        // ---- 尺寸阶梯 ----
        readonly property int hPrimary:         40          // 主操作按钮高
        readonly property int hStandard:        32          // 标准控件高
        readonly property int hCompact:         24          // 行内紧凑控件高
        readonly property int radiusCtl:        8           // 按钮圆角
        readonly property int radiusPanel:      4           // 面板 / 弹层圆角
        readonly property int radiusMenu:       3           // 菜单条目圆角
        readonly property int radiusMini:       2           // 类型色块圆角
        readonly property int fontTitle:        14
        readonly property int fontBody:         12
        readonly property int fontSmall:        11
        readonly property int fontTiny:         10
    }

    /// 收起/展开卡片本体（优先复用 gz-gui 原生折叠动画，结构异常时直接改高度兜底）
    function toggleCollapsed() {
        var card = root.parent ? root.parent.parent.parent : null;
        if (!card || !card.state) card = root.parent ? root.parent.parent : null;
        if (!card) return;
        if (root.collapsed) {
            if (card.state === "floating_collapsed") {
                card.lastHeight = 600;
                card.state = "floating";
            } else {
                card.height = 600;
            }
            root.collapsed = false;
        } else {
            if (card.state === "floating") {
                card.lastHeight = Math.max(card.height, 50.5);
                card.state = "floating_collapsed";
            } else {
                card.height = 50;
            }
            root.collapsed = true;
        }
    }

    /// 自定义按钮组件
    component CustomButton: Button {
        property color bk_normalColor: theme.accent
        property color bk_pressedColor: theme.primary
        property color bk_disabledColor: theme.textDisabled
        property color borderColor: theme.borderStrong
        property int btnRadius: theme.radiusCtl
        property int btnWidth: 80
        property int btnHeight: theme.hPrimary
        property color text_normalColor: theme.textOnBlack
        property color text_pressedColor: theme.textOnDark
        property color text_disabledColor: theme.textDim
        property int textSize: theme.fontTitle

        implicitWidth: btnWidth
        implicitHeight: btnHeight

        background: Rectangle {
            radius: btnRadius
            color: !parent.enabled ? bk_disabledColor :
                   (parent.down ? bk_pressedColor : bk_normalColor)
            border.color: parent.hovered ? borderColor : "transparent"
            border.width: 1
        }

        contentItem: Text {
            text: parent.text
            color: !parent.enabled ? text_disabledColor :
                   (parent.down ? text_pressedColor : text_normalColor)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: textSize
            font.bold: true
        }
    }

    /// 标题按钮（单击切换 Tab，双击收起/展开面板）
    component TitleButton: Item {
        property bool active: false
        property string title: ""
        signal clicked()
        signal doubleClicked()
        width: 100
        height: theme.hStandard

        Rectangle {
            anchors.fill: parent
            radius: theme.radiusPanel
            color: parent.active ? theme.primary : theme.surfaceIdle
            border.color: parent.active ? theme.primary : theme.textDisabled
            border.width: 1
        }
        Text {
            anchors.centerIn: parent
            text: parent.title
            color: parent.active ? theme.textOnDark : theme.textMid
            font.pixelSize: 13
            font.bold: true
        }
        MouseArea {
            anchors.fill: parent
            onClicked: parent.clicked()
            onDoubleClicked: parent.doubleClicked()
        }
    }

    // [2026-09-17 菜单外观统一]
    // 红线：禁止用 Menu.delegate 定制样式 —— 与 Menu 内部 contentItem: ListView 的实例化
    // 机制冲突，曾导致 QML 创建期 SIGSEGV（见 modules/gui.md）。
    // 正确做法：样式走各 Control 自身属性（background / contentItem / padding / font）。
    // 验证方式：bash tools/qml_smoke/run.sh <本文件>（创建期冒烟测试 + 弹出截图）。
    component StyledMenu: Menu {
        topPadding: 4
        bottomPadding: 4
        // 隐式尺寸必须写在 background 上：Menu 的宽度由背景 implicitWidth 推导
        // （原生背景自带 200；不写则弹窗宽度退化为 0，条目宽度也随之为 0）
        background: Rectangle {
            implicitWidth: 200
            implicitHeight: theme.hStandard
            color: theme.surface
            border.color: theme.borderMenu
            border.width: 1
            radius: theme.radiusPanel
        }
    }

    component StyledMenuItem: MenuItem {
        id: mi
        leftPadding: 12
        rightPadding: 12
        topPadding: 6
        bottomPadding: 6
        font.pixelSize: 12
        // 最小尺寸写在 background 上（经 background 与 contentItem 取大者推导）；
        // 直接写 implicitWidth 会覆盖 Qt 的隐式尺寸推导，使 Menu 弹窗宽度退化为 0
        background: Rectangle {
            implicitWidth: 150
            implicitHeight: 30
            radius: theme.radiusMenu
            color: !mi.enabled   ? theme.disabledBg :
                   mi.highlighted ? theme.primary :
                   mi.hovered     ? theme.primaryHover : theme.surface
        }
        contentItem: Text {
            text: mi.text
            font: mi.font
            color: !mi.enabled     ? theme.textFaint :
                   mi.highlighted   ? theme.textOnDark : theme.text
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    component StyledSeparator: MenuSeparator {
        leftPadding: 8
        rightPadding: 8
        topPadding: 4
        bottomPadding: 4
        background: Rectangle { color: theme.surface }
        contentItem: Rectangle {
            implicitWidth: 150
            implicitHeight: 1
            color: theme.border
        }
    }

    /// 实验列表数据模型
    ListModel {
        id: experimentModel
        ListElement { expId: "SP-01";  name: "排水量验证实验";         course: "船舶原理";     tier: "观察类"; enabled: true }
        ListElement { expId: "SP-02";  name: "漂浮圆柱体初稳性";       course: "船舶原理";     tier: "观察类"; enabled: true }
        ListElement { expId: "SP-03";  name: "初稳性倾斜实验";         course: "船舶原理";     tier: "观察类"; enabled: true }
        ListElement { expId: "SP-04";  name: "数学船型稳性对比";       course: "船舶原理";     tier: "观察类"; enabled: true }
        ListElement { expId: "PR-01";  name: "编程计算排水量";         course: "船舶原理";     tier: "设计类"; enabled: true }
        ListElement { expId: "SP-05";  name: "回转性观测";             course: "船舶原理";     tier: "观察类"; enabled: false }
        ListElement { expId: "SP-06";  name: "大倾角稳性";             course: "船舶原理";     tier: "操作类"; enabled: false }
        ListElement { expId: "SP-07";  name: "阻力曲线测定";           course: "船舶原理";     tier: "操作类"; enabled: false }
        ListElement { expId: "SP-08";  name: "推进性能匹配";           course: "船舶原理";     tier: "操作类"; enabled: false }
        ListElement { expId: "SP-09";  name: "横摇RAO测量";            course: "船舶原理";     tier: "操作类"; enabled: false }
        ListElement { expId: "SP-10";  name: "浮体布局设计";           course: "船舶原理";     tier: "设计类"; enabled: false }
        ListElement { expId: "SP-11";  name: "完整稳性综合校核";       course: "船舶原理";     tier: "设计类"; enabled: false }
        ListElement { expId: "SP-12";  name: "船型阻力优化";           course: "船舶原理";     tier: "设计类"; enabled: false }
        ListElement { expId: "SC-01";  name: "开环vs闭环响应";         course: "船舶智能控制"; tier: "观察类"; enabled: false }
        ListElement { expId: "SC-02";  name: "PID参数效应";            course: "船舶智能控制"; tier: "观察类"; enabled: false }
        ListElement { expId: "SC-03";  name: "环境扰动影响";           course: "船舶智能控制"; tier: "观察类"; enabled: false }
        ListElement { expId: "SC-04";  name: "航向控制";               course: "船舶智能控制"; tier: "操作类"; enabled: false }
        ListElement { expId: "SC-05";  name: "航速控制";               course: "船舶智能控制"; tier: "操作类"; enabled: false }
        ListElement { expId: "SC-06";  name: "LOS循迹跟踪";            course: "船舶智能控制"; tier: "操作类"; enabled: false }
        ListElement { expId: "SC-07";  name: "动力定位";               course: "船舶智能控制"; tier: "操作类"; enabled: false }
        ListElement { expId: "SC-08";  name: "PID自整定";              course: "船舶智能控制"; tier: "设计类"; enabled: false }
        ListElement { expId: "SC-09";  name: "航路点路径跟随";         course: "船舶智能控制"; tier: "设计类"; enabled: false }
        ListElement { expId: "SC-10";  name: "COLREGs避碰";            course: "船舶智能控制"; tier: "设计类"; enabled: false }
        ListElement { expId: "SS-01";  name: "船体结构认知";           course: "船舶结构与制图"; tier: "观察类"; enabled: false }
        ListElement { expId: "SS-02";  name: "型线图识读";             course: "船舶结构与制图"; tier: "观察类"; enabled: false }
        ListElement { expId: "SS-03";  name: "横剖面结构";             course: "船舶结构与制图"; tier: "观察类"; enabled: false }
        ListElement { expId: "SS-04";  name: "舱室划分";               course: "船舶结构与制图"; tier: "操作类"; enabled: false }
        ListElement { expId: "SS-05";  name: "破舱稳性仿真";           course: "船舶结构与制图"; tier: "操作类"; enabled: false }
        ListElement { expId: "SS-06";  name: "参数化型线设计";         course: "船舶结构与制图"; tier: "设计类"; enabled: false }
        ListElement { expId: "SS-07";  name: "综合结构设计";           course: "船舶结构与制图"; tier: "设计类"; enabled: false }
    }

    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 0

        /// 标题栏（单击切换 Tab，双击收起/展开面板）
        Row {
            id: titleBar
            width: parent.width
            spacing: 6

            TitleButton {
                title: qsTr("自由任务")
                active: root.tabBarCurrent == 0
                onClicked: {
                    // 单击只切 Tab；折叠/展开交给双击
                    // 若单击也展开，双击会先展开再折叠、互相抵消，表现为双击折叠失效
                    root.tabBarCurrent = 0;
                }
                onDoubleClicked: root.toggleCollapsed()
            }
            TitleButton {
                title: qsTr("教学实验")
                active: root.tabBarCurrent == 1
                onClicked: {
                    // 单击只切 Tab；折叠/展开交给双击
                    // 若单击也展开，双击会先展开再折叠、互相抵消，表现为双击折叠失效
                    root.tabBarCurrent = 1;
                }
                onDoubleClicked: root.toggleCollapsed()
            }
        }

        /// Tab 内容区（仅用 Item + visible，不用 StackLayout）
        Rectangle {
            width: parent.width
            /// 固定展开态高度（卡片 600 时 = 600-12-32-6），避免折叠动画期间
            /// 底部锚定控件（传感器查看等）随高度变化滑移；超出部分由卡片 content 裁切
            height: 550
            visible: !root.collapsed
            color: theme.panelBg
            radius: theme.radiusPanel

            /// Tab 1：模型管理
            Item {
                visible: root.tabBarCurrent == 0
                anchors.fill: parent

                Column {
                    id: tab1BtnCol
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 4

                    /// 行1：加载（下拉归拢全部载入/添加入口）+ 保存世界 + 世界设置 + 暂停
                    /// （2026-09-17 布局评审：载入世界/载入模型/数学船型/圆柱体/两种水域
                    ///   归入"加载"下拉，避免六个入口平铺两行）
                    Rectangle {
                        color: theme.sectionBg
                        radius: theme.radiusPanel
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                id: loadBtn
                                text: qsTr("加载 ▾")
                                bk_normalColor: theme.accentLoad
                                bk_pressedColor: theme.accentLoadDown
                                onClicked: loadMenu.popup()
                            }
                            CustomButton {
                                text: qsTr("保存世界")
                                onClicked: { MenuPluginKeship.OnBtn_SaveWorld(); }
                            }
                            CustomButton {
                                text: qsTr("世界设置")
                                onClicked: { MenuPluginKeship.OnBtn_WorldSettings(); }
                            }
                            CustomButton {
                                text: MenuPluginKeship.paused ? qsTr("继续") : qsTr("暂停")
                                bk_normalColor: theme.warn
                                bk_pressedColor: theme.warnDown
                                onClicked: { MenuPluginKeship.OnPauseToggle(); }
                            }
                        }
                    }

                    /// "加载"下拉：六种载入/添加入口（保留"添加默认水域"仅无水域时可用的约束）
                    StyledMenu {
                        id: loadMenu
                        StyledMenuItem { text: "载入世界…"
                            onTriggered: { loadMenu.close(); MenuPluginKeship.OnBtn_LoadWorld(); } }
                        StyledMenuItem { text: "载入模型（文件）…"
                            onTriggered: { loadMenu.close(); MenuPluginKeship.OnBtn_LoadModel(); } }
                        StyledMenuItem { text: "数学船型…"
                            onTriggered: { loadMenu.close(); MenuPluginKeship.OnBtn_NewMathHull(); } }
                        StyledMenuItem { text: "圆柱体（含浮力/水动力）…"
                            onTriggered: { loadMenu.close(); MenuPluginKeship.OnBtn_CylinderBuoyancyTest(); } }
                        StyledSeparator {}
                        StyledMenuItem { text: "添加默认水域"
                            enabled: MenuPluginKeship.customWaterAreaCount === 0
                            onTriggered: { loadMenu.close(); MenuPluginKeship.OnBtn_AddDefaultWaterArea(); } }
                        StyledMenuItem { text: "添加自定义水域…"
                            onTriggered: { loadMenu.close(); MenuPluginKeship.OnBtn_AddWaterArea(); } }
                    }

                    /// 行2：惯性计算（模型辅助工具）+ 任务（实验/考核基础设施）
                    Rectangle {
                        color: theme.sectionBg
                        radius: theme.radiusPanel
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                text: qsTr("惯性计算")
                                onClicked: { MenuPluginKeship.OnBtn_InertiaCalculator(); }
                            }
                            CustomButton {
                                text: qsTr("任务设定")
                                onClicked: { MenuPluginKeship.OnBtn_TaskSet(); }
                            }
                            CustomButton {
                                text: qsTr("任务执行")
                                onClicked: { MenuPluginKeship.OnBtn_TaskMonitor(); }
                            }
                        }
                    }

                    /// 行3：维护操作（警示色）
                    Rectangle {
                        color: theme.sectionBg
                        radius: theme.radiusPanel
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                text: qsTr("清理网格缓存")
                                width: 130
                                bk_normalColor: theme.warn
                                bk_pressedColor: theme.warnDown
                                onClicked: MenuPluginKeship.OnBtn_CleanMeshCache()
                            }
                        }
                    }
                }

                GroupBox {
                    id: tab1ModelBox
                    anchors.top: tab1BtnCol.bottom
                    anchors.topMargin: 8
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: tab1SensorBox.top
                    anchors.bottomMargin: 6

                    // 层次树（行 = worldHierarchy；过滤+折叠后显示）
                    // 声明式绑定：treeBase 绑 C++ 属性（NOTIFY: ModelNamesChanged），
                    // treeRows 经 buildRows() 随依赖自动重估，无需手动刷新
                    property var treeBase: MenuPluginKeship.worldHierarchy
                    property var treeExpanded: ({})
                    property string treeFilter: ""
                    property var treeRows: buildRows()

                    function buildRows() {
                        // ★ 绑定依赖收集铁律：函数内访问的全部响应式属性必须**无条件地**
                        //   在任何 return 之前读取。否则创建期（worldHierarchy 未就绪）
                        //   提前返回的那次求值，会让绑定永久失去对后续属性的依赖 ——
                        //   表现为 treeFilter 变化后列表不再重估，即"搜索无功能"的根因。
                        var base = treeBase
                        var f = treeFilter.trim()
                        var ex = treeExpanded
                        // 创建期 worldHierarchy 可能尚未就绪，返回空行避免 TypeError
                        if (!base || !base.length) return []
                        // 组键：chain + "/" + gtype（models/waters/sub）；未记录 = 默认展开
                        var parentSubKeyOf = function(r) {
                            if (!r.chain.length) return ""
                            var idx = r.chain.lastIndexOf("/")
                            return (idx < 0 ? "" : r.chain.substr(0, idx)) + "/sub"
                        }
                        // 搜索：命中行 + 其祖先链组行 + 全部分类组行保留
                        var kept = {}
                        if (f.length) {
                            var subIdx = {}
                            for (var i = 0; i < base.length; ++i)
                                if (base[i].kind === "group" && base[i].gtype === "sub")
                                    subIdx[base[i].chain] = i
                            // 2026-09-18：改为**大小写不敏感**匹配 —— 模型名多为全小写
                            //（如 aas_0），而用户习惯输入大写缩写（AAS），原先的严格
                            // indexOf 匹配会永远搜不到，表现为"搜索没反应"
                            var fl = f.toLowerCase()
                            for (i = 0; i < base.length; ++i) {
                                var r = base[i]
                                if ((r.text && r.text.toLowerCase().indexOf(fl) >= 0)
                                        || (r.chain && r.chain.toLowerCase().indexOf(fl) >= 0)) {
                                    kept[i] = true
                                    // r.chain 可能为 undefined（数据行缺字段），
                                    // text 命中而 chain 缺失时原先会抛 TypeError 使过滤中断
                                    if (r.chain && r.chain.length) {
                                        var segs = r.chain.split("/")
                                        var cur = ""
                                        for (var k = 0; k < segs.length - 1; ++k) {
                                            cur = cur.length ? cur + "/" + segs[k] : segs[k]
                                            if (cur in subIdx) kept[subIdx[cur]] = true
                                        }
                                    }
                                }
                            }
                            for (i = 0; i < base.length; ++i)
                                if (base[i].kind === "group" && base[i].gtype !== "sub")
                                    kept[i] = true
                        }
                        // 折叠判定 + 过滤（搜索模式下不折叠遮蔽，全部显示）
                        var out = []
                        for (i = 0; i < base.length; ++i) {
                            r = base[i]
                            if (f.length && !kept[i]) continue
                            if (!f.length && r.depth > 0) {
                                if (r.kind === "model" || r.kind === "water") {
                                    var gk = r.chain + "/"
                                            + (r.kind === "model" ? "models" : "waters")
                                    if (ex[gk] === false) continue
                                    // 祖先链任一 sub 组折叠 → 隐藏（沿 chain 逐级向上检查）
                                    var anc = r.chain
                                    var ancCollapsed = false
                                    while (anc.length && !ancCollapsed) {
                                        if (ex[anc + "/sub"] === false)
                                            ancCollapsed = true
                                        var idx = anc.lastIndexOf("/")
                                        anc = (idx < 0 ? "" : anc.substr(0, idx))
                                    }
                                    if (ancCollapsed) continue
                                } else if (r.kind === "group" && r.gtype === "sub") {
                                    var psk = parentSubKeyOf(r)
                                    if (psk.length && ex[psk] === false) continue
                                } else if (r.kind === "group") {
                                    // 模型/水域组行：受其所在链的 sub 组展开态控制（顶层链恒显示）
                                    if (ex[r.chain + "/sub"] === false) continue
                                }
                            }
                            // 输出纯 JS 对象（显式拷贝字段 + 预计算 label），
                            // 避免 QVariantMap 桥接对象动态属性在 model 传递中丢失
                            // 折叠箭头由 delegate 绘制，组行 label 不再拼 +/- 前缀
                            var label = r.text
                            out.push({ kind: r.kind, gtype: r.gtype, text: r.text,
                                       chain: r.chain, depth: r.depth,
                                       midx: r.midx, widx: r.widx,
                                       isStatic: r.isStatic, mathHull: r.mathHull,
                                       label: label })
                        }
                        return out
                    }

                    TextField {
                        id: searchField
                        anchors.top: parent.top
                        anchors.topMargin: 24
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        // 2026-09-18 修复"输入文本显示不全"：原写死 height: 30 与 TextField
                        // 内建上下 padding 叠加，文字被顶到上沿、下半截发虚。
                        // 改用 implicitHeight 由内容推导，并显式声明垂直居中。
                        implicitHeight: theme.hStandard
                        verticalAlignment: TextInput.AlignVCenter
                        placeholderText: qsTr("搜索模型 / 水域 / 子世界")
                        font.pixelSize: theme.fontSmall
                        // 搜索实际由 QML 侧完成（treeFilter → buildRows() 过滤），
                        // 原调用的 OnSearchModel 只是 C++ 侧一个打印空壳，误导性，已移除。
                        onTextEdited: treeFilter = searchField.text
                    }

                    // 搜索命中数：放在搜索框**正下方**的独立信息行。
                    // 2026-09-18 修正：原先为把它塞进框内而设 rightPadding:58，
                    // 输入较长文字时可见宽度被压缩、光标滚动后文字"看起来被吃掉"，
                    // 这正是"显示不全"的来源；移出后文本恢复全宽。
                    Label {
                        id: searchHint
                        anchors.top: searchField.bottom
                        anchors.topMargin: 2
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        height: searchField.text.length > 0 ? 14 : 0
                        visible: height > 0
                        text: searchField.text.length > 0
                              ? qsTr("命中 %1 项").arg(modelListView.count) : ""
                        font.pixelSize: theme.fontTiny
                        color: theme.textDim
                    }

                    ListView {
                        id: modelListView
                        anchors.top: searchHint.bottom
                        anchors.topMargin: 2
                        anchors.bottom: deleteAllBtn.top
                        anchors.bottomMargin: 6
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        model: tab1ModelBox.treeRows
                        clip: true

                        ScrollBar.vertical: ScrollBar {
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 10
                            policy: ScrollBar.AsNeeded
                        }

                        Label {
                            // 有搜索词时给"无匹配"提示，与空场景区分开
                            text: searchField.text.length > 0
                                  ? qsTr("没有匹配「%1」的项").arg(searchField.text)
                                  : qsTr("暂无模型 / 水域 / 子世界")
                            anchors.centerIn: parent
                            font.pixelSize: theme.fontBody
                            color: theme.textDim
                            visible: modelListView.count == 0
                        }

                        delegate: Rectangle {
                            width: modelListView.width
                            height: theme.hStandard
                            color: modelData.kind === "group"
                                   ? (modelData.gtype === "sub" ? theme.groupSubBg
                                      : modelData.gtype === "waters" ? theme.groupWaterBg
                                                                     : theme.groupModelBg)
                                   : (modelListView.currentIndex === index ? theme.rowSelected
                                      : (index % 2 == 0 ? theme.rowEven : theme.rowOdd))
                            border.color: modelData.kind === "group" ? theme.borderRowGroup : theme.borderRow
                            border.width: 1

                            // 左侧色条（组行按类型着色）
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 4
                                visible: modelData.kind === "group"
                                color: modelData.gtype === "sub" ? theme.groupSubBar
                                     : modelData.gtype === "waters" ? theme.groupWaterBar
                                                                    : theme.groupModelBar
                            }

                            // 缩进区 + 树连接竖线（叶行按层级逐级绘制）
                            Item {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 6 + modelData.depth * 14
                                Repeater {
                                    model: (modelData.kind === "group") ? 0 : modelData.depth
                                    Rectangle {
                                        x: 4 + index * 14
                                        width: 1
                                        height: parent.height
                                        color: theme.treeLine
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    modelListView.currentIndex = index
                                    if (modelData.kind === "group") {
                                        // JS 对象成员修改不触发绑定重估：整体替换
                                        var key = modelData.chain + "/" + modelData.gtype
                                        var e = {}
                                        var keys = Object.keys(tab1ModelBox.treeExpanded)
                                        for (var ki = 0; ki < keys.length; ++ki)
                                            e[keys[ki]] = tab1ModelBox.treeExpanded[keys[ki]]
                                        e[key] = !(tab1ModelBox.treeExpanded[key] === true)
                                        tab1ModelBox.treeExpanded = e
                                    }
                                }
                            }

                            Row {
                                width: modelListView.width
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 6 + modelData.depth * 14
                                anchors.rightMargin: 2
                                spacing: 4

                                // 类型色块（叶行：模型=蓝、水域=青）
                                Rectangle {
                                    width: 8; height: 8
                                    anchors.verticalCenter: parent.verticalCenter
                                    visible: modelData.kind !== "group"
                                    radius: 2
                                    color: modelData.kind === "model" ? theme.groupModelBar
                                                                      : theme.groupWaterBar
                                }

                                // 折叠箭头（组行）
                                Text {
                                    text: modelData.kind === "group"
                                          ? (tab1ModelBox.treeExpanded[
                                              modelData.chain + "/" + modelData.gtype] === false
                                             ? "▸ " : "▾ ")
                                          : ""
                                    width: 14
                                    height: parent.height
                                    verticalAlignment: Text.AlignVCenter
                                    font.pixelSize: theme.fontTiny
                                    font.bold: true
                                    color: theme.textMid
                                }

                                // 名称（组行加粗；水域行预留海况列宽）
                                Text {
                                    text: String(modelData.label || modelData.text || "")
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    font.pixelSize: 12
                                    font.bold: modelData.kind === "group"
                                    height: 30
                                    width: modelData.kind === "water"
                                           ? Math.max(40, modelListView.width - 130 - (modelData.depth || 0) * 14 - 46)
                                           : Math.max(40, modelListView.width - 130 - (modelData.depth || 0) * 14)
                                    color: modelData.kind === "group"
                                            ? theme.textStrong : theme.textOnBlack
                                }

                                // 海况（水域）
                                Text {
                                    text: modelData.widx >= 0
                                          ? MenuPluginKeship.waterAreaSeaStates[modelData.widx] : ""
                                    verticalAlignment: Text.AlignVCenter
                                    height: parent.height
                                    font.pixelSize: theme.fontSmall
                                    color: theme.infoText
                                    width: 42
                                    visible: modelData.kind === "water"
                                }

                                // ⋯ 下拉菜单（按实体类型选择对应菜单，popup 于按钮处）
                                Button {
                                    width: 28; height: 24
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "⋯"
                                    visible: modelData.kind !== "group"
                                             || (modelData.kind === "group"
                                                 && modelData.gtype === "sub")
                                    onClicked: {
                                        if (modelData.kind === "model")
                                            modelRowMenu.popup()
                                        else if (modelData.kind === "water")
                                            waterRowMenu.popup()
                                        else if (modelData.kind === "group"
                                                 && modelData.gtype === "sub")
                                            subRowMenu.popup()
                                    }

                                    background: Rectangle {
                                        radius: theme.radiusPanel
                                        color: parent.hovered ? theme.miniBgHover : theme.miniBg
                                        border.color: theme.miniBorder
                                        border.width: 1
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: theme.textLabel
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 11
                                        font.bold: true
                                    }
                                }

                                /// 模型操作菜单（仅模型行弹出）
                                StyledMenu {
                                    id: modelRowMenu
                                    StyledMenuItem { text: "属性"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_ShowModelSettingsDialogByIndex(modelData.midx); } }
                                    StyledMenuItem { text: "复制"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_DuplicateModelByIndex(modelData.midx); } }
                                    StyledMenuItem { text: "移动到子世界"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_MoveModelByIndex(modelData.midx); } }
                                    StyledSeparator {}
                                    /// ★ 2026-09-29 修：静态模型下**不能**用 `visible: false` 隐藏这三项 ——
                                    ///   Qt 5.15 的 Menu 内部是 ListView，`visible:false` 的条目**仍占高度**
                                    ///   ⇒ 会留下 3 行**空白行**（张老师 2026-09-29 反馈）。
                                    ///   改为在**委托创建时用 `Menu.removeItem()` 真正移出菜单**
                                    ///   （Qt 官方 API，专为动态增删菜单项设计；**不是** `Menu.delegate` ⇒ 不触红线）。
                                    ///   ⚠ 不要试图改条目尺寸（`height`/`implicitHeight`）来塌缩 ——
                                    ///     实测会破坏 Controls2 的隐式尺寸推导：整个菜单 `contentHeight`
                                    ///     塌成 9~18（只剩分隔线），与 `implicitWidth` 的历史坑同源。
                                    StyledMenuItem { id: miBuoy; text: "浮力体"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditBuoyancy(modelData.midx); } }
                                    StyledMenuItem { id: miThrust; text: "推进器"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditThrust(modelData.midx); } }
                                    StyledMenuItem { id: miHydro; text: "流体动力"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditHydro(modelData.midx); } }
                                    StyledMenuItem { text: "传感器"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditSensor(modelData.midx); } }
                                    Component.onCompleted: {
                                        // 与原先的 visible 条件等价：mathHull 只影响浮力体；静态则三项全去
                                        if (modelData.isStatic || modelData.mathHull)
                                            modelRowMenu.removeItem(miBuoy)
                                        if (modelData.isStatic) {
                                            modelRowMenu.removeItem(miThrust)
                                            modelRowMenu.removeItem(miHydro)
                                        }
                                    }
                                    StyledSeparator {}
                                    StyledMenuItem { text: "保存"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_SaveModel(modelData.midx); } }
                                    StyledMenuItem { text: "删除"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_DeleteModelByIndex(modelData.midx); } }
                                }

                                /// 水域操作菜单（仅水域行弹出）
                                StyledMenu {
                                    id: waterRowMenu
                                    StyledMenuItem { text: "环境"
                                        onTriggered: { waterRowMenu.close(); MenuPluginKeship.OnBtn_ShowWaterEnv(modelData.widx); } }
                                    StyledMenuItem { text: "编辑"
                                        onTriggered: { waterRowMenu.close(); MenuPluginKeship.OnBtn_EditWaterArea(modelData.widx); } }
                                    StyledMenuItem { text: "复制"
                                        onTriggered: { waterRowMenu.close(); MenuPluginKeship.OnBtn_DuplicateWaterArea(modelData.widx); } }
                                    StyledMenuItem { text: "移动到子世界"
                                        onTriggered: { waterRowMenu.close(); MenuPluginKeship.OnBtn_MoveWaterByIndex(modelData.widx); } }
                                    StyledSeparator {}
                                    StyledMenuItem { text: "删除"
                                        onTriggered: { waterRowMenu.close(); MenuPluginKeship.OnBtn_DeleteWaterArea(modelData.widx); } }
                                }

                                /// 子世界操作菜单（仅子世界链组行弹出）
                                StyledMenu {
                                    id: subRowMenu
                                    StyledMenuItem { text: "属性"
                                        onTriggered: { subRowMenu.close(); MenuPluginKeship.OnBtn_ShowSubWorldSettings(modelData.chain); } }
                                    StyledMenuItem { text: "保存子世界"
                                        onTriggered: { subRowMenu.close(); MenuPluginKeship.OnBtn_SaveSubWorld(modelData.chain); } }
                                    StyledMenuItem { text: "复制"
                                        onTriggered: { subRowMenu.close(); MenuPluginKeship.OnBtn_DuplicateSubWorld(modelData.chain); } }
                                    StyledSeparator {}
                                    StyledMenuItem { text: "删除"
                                        onTriggered: { subRowMenu.close(); MenuPluginKeship.OnBtn_DeleteSubWorld(modelData.chain); } }
                                }
                            }
                        }
                    }
                    // 2026-09-18（批次 1b）：原为裸 Button（零样式），与相邻 CustomButton 风格脱节；
                    // 改为统一组件 + 危险色（删除是不可逆操作，需要视觉警示）
                    CustomButton {
                        id: deleteAllBtn
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        height: theme.hStandard
                        bk_normalColor: theme.danger
                        bk_pressedColor: theme.dangerDown
                        text: qsTr("删除所有模型")
                        onClicked: { MenuPluginKeship.OnBtn_DeleteAllModels(); }
                    }
                }

                GroupBox {
                    id: tab1SensorBox
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    anchors.left: parent.left
                    anchors.right: parent.right
                    title: qsTr("传感器查看")
                    implicitHeight: 64
                    height: 64

                    ComboBox {
                        id: sensorCombo
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        height: 30
                        model: MenuPluginKeship.sensorList
                        currentIndex: -1
                        displayText: currentIndex < 0 ? qsTr("-- 选择传感器 --") : model[currentIndex]
                        onActivated: {
                            var text = model[index];
                            MenuPluginKeship.OnSensorSelected(text);
                            currentIndex = -1;
                        }
                    }
                }
            }

            /// Tab 2：虚拟实验
            Item {
                id: tab2Root
                visible: root.tabBarCurrent == 1
                anchors.fill: parent

                /// 课程折叠状态：course 名 → true 表示该课程已折叠
                /// （做法与 tab1 层次树的 treeExpanded 一致）
                property var collapsedCourses: ({})

                function isCourseCollapsed(c) {
                    return collapsedCourses[c] === true;
                }

                function toggleCourse(c) {
                    // JS 对象成员直接改不会触发绑定重估 → 整体替换
                    var e = {};
                    var ks = Object.keys(collapsedCourses);
                    for (var i = 0; i < ks.length; ++i)
                        e[ks[i]] = collapsedCourses[ks[i]];
                    e[c] = !(collapsedCourses[c] === true);
                    collapsedCourses = e;
                }

                // 实现说明：折叠**不**过滤 model —— 若把该课程的行从 model 里剔掉，
                // 分组头会随之消失，折叠后就再也点不开它了。改为 delegate 高度归零：
                // 行不可见、但 section 头仍在，随时可点击展开。
                ListView {
                    id: expListView
                    anchors.fill: parent
                    model: experimentModel
                    clip: true
                    spacing: 2

                    ScrollBar.vertical: ScrollBar {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 10
                        policy: ScrollBar.AsNeeded
                    }

                    /// 课程分组头部（2026-09-18 新增：单击折叠/展开该课程的全部实验）
                    section.property: "course"
                    section.criteria: ViewSection.FullString
                    section.delegate: Rectangle {
                        width: expListView.width
                        height: 28
                        // 悬停高亮，提示"此处可点击"
                        color: sectionMouse.containsMouse ? theme.surfaceIdle : theme.headerBg

                        MouseArea {
                            id: sectionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: tab2Root.toggleCourse(section)
                        }

                        Text {
                            // ▾ 展开 / ▸ 折叠
                            text: (tab2Root.isCourseCollapsed(section) ? "▸ " : "▾ ")
                                  + "▎" + section
                            color: theme.textLabel
                            font.pixelSize: 13
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                        }
                    }

                    delegate: Rectangle {
                        width: expListView.width
                        // 该课程折叠时高度归零（行隐藏，分组头仍在、可再次点击展开）
                        height: tab2Root.isCourseCollapsed(course) ? 0 : theme.hStandard
                        visible: height > 0
                        color: bgColor
                        border.color: theme.borderStrong
                        border.width: 1

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            color: leftBarColor
                        }

                        property color bgColor: theme.expRowBg
                        property color textColor: enabled ? theme.textDark : theme.textMuted
                        property color leftBarColor: enabled ? theme.ok : "transparent"

                        MouseArea {
                            anchors.fill: parent
                            enabled: model.enabled
                            onClicked: {
                                if (expId === "SP-01") {
                                    MenuPluginKeship.OnBtn_Displacement();
                                } else if (expId === "SP-02") {
                                    MenuPluginKeship.OnBtn_SP02_Stability();
                                } else if (expId === "SP-03") {
                                    MenuPluginKeship.OnBtn_SP03_Inclining();
                                } else if (expId === "SP-04") {
                                    MenuPluginKeship.OnBtn_SP04_HullStability();
                                } else if (expId === "PR-01") {
                                    MenuPluginKeship.OnBtn_PR01_CodeRunner();
                                }
                                // 后续实验在此添加分支
                            }
                        }

                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 8
                            spacing: 8

                            Text {
                                text: expId
                                color: parent.parent.textColor
                                font.pixelSize: 11
                                font.bold: true
                                width: 42
                            }
                            Text {
                                text: name
                                color: parent.parent.textColor
                                font.pixelSize: 11
                                width: parent ? parent.width - 150 : 80
                                elide: Text.ElideRight
                            }
                            Text {
                                text: tier
                                color: model.enabled ? theme.okText : theme.textSub2
                                font.pixelSize: 10
                                width: 36
                            }
                            Text {
                                text: model.enabled ? "" : "(待开发)"
                                color: theme.textSub
                                font.pixelSize: 10
                                width: 56
                            }
                        }
                    }
                }
            }
        }
    }
}