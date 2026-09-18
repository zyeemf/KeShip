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
    color: "#d0d0d8c0"

    /// 当前激活 Tab（0=模型管理, 1=虚拟实验）
    property int tabBarCurrent: 0
    /// 面板收起状态（双击标题切换，收起时仅显示标题栏）
    property bool collapsed: false

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
        property color bk_normalColor: '#66c4e9'
        property color bk_pressedColor: '#7384e4'
        property color bk_disabledColor: '#b0b0c0'
        property color borderColor: "#444444"
        property int btnRadius: 8
        property int btnWidth: 80
        property int btnHeight: 40
        property color text_normalColor: "black"
        property color text_pressedColor: "white"
        property color text_disabledColor: "#888888"
        property int textSize: 14

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
        height: 32

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: parent.active ? "#7384e4" : "#e8e8ee"
            border.color: parent.active ? "#7384e4" : "#b0b0c0"
            border.width: 1
        }
        Text {
            anchors.centerIn: parent
            text: parent.title
            color: parent.active ? "white" : "#404040"
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
            implicitHeight: 32
            color: "#ffffff"
            border.color: "#9aa0b4"
            border.width: 1
            radius: 4
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
            radius: 3
            color: !mi.enabled   ? "#f4f4f7" :
                   mi.highlighted ? "#7384e4" :
                   mi.hovered     ? "#dde4f7" : "#ffffff"
        }
        contentItem: Text {
            text: mi.text
            font: mi.font
            color: !mi.enabled     ? "#a8a8b0" :
                   mi.highlighted   ? "#ffffff" : "#20202a"
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
        background: Rectangle { color: "#ffffff" }
        contentItem: Rectangle {
            implicitWidth: 150
            implicitHeight: 1
            color: "#c8c8d8"
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
            color: "#d0d0d8c0"
            radius: 4

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
                        color: "#c0d8f0"
                        radius: 4
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                id: loadBtn
                                text: qsTr("加载 ▾")
                                bk_normalColor: "#8fd0a8"
                                bk_pressedColor: "#5eb07e"
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
                                bk_normalColor: "#f2c14e"
                                bk_pressedColor: "#d9a62e"
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
                        color: "#c0d8f0"
                        radius: 4
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
                        color: "#c0d8f0"
                        radius: 4
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                text: qsTr("清理网格缓存")
                                width: 130
                                bk_normalColor: "#f2c14e"
                                bk_pressedColor: "#d9a62e"
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
                        var base = treeBase
                        var f = treeFilter.trim()
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
                            for (i = 0; i < base.length; ++i) {
                                var r = base[i]
                                if ((r.text && r.text.indexOf(f) >= 0)
                                        || (r.chain && r.chain.indexOf(f) >= 0)) {
                                    kept[i] = true
                                    if (r.chain.length) {
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
                                    if (treeExpanded[gk] === false) continue
                                    // 祖先链任一 sub 组折叠 → 隐藏（沿 chain 逐级向上检查）
                                    var anc = r.chain
                                    var ancCollapsed = false
                                    while (anc.length && !ancCollapsed) {
                                        if (treeExpanded[anc + "/sub"] === false)
                                            ancCollapsed = true
                                        var idx = anc.lastIndexOf("/")
                                        anc = (idx < 0 ? "" : anc.substr(0, idx))
                                    }
                                    if (ancCollapsed) continue
                                } else if (r.kind === "group" && r.gtype === "sub") {
                                    var psk = parentSubKeyOf(r)
                                    if (psk.length && treeExpanded[psk] === false) continue
                                } else if (r.kind === "group") {
                                    // 模型/水域组行：受其所在链的 sub 组展开态控制（顶层链恒显示）
                                    if (treeExpanded[r.chain + "/sub"] === false) continue
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
                        height: 30
                        placeholderText: qsTr("搜索模型 / 水域 / 子世界")
                        font.pixelSize: 11
                        onTextEdited: {
                            MenuPluginKeship.OnSearchModel(searchField.text)
                            treeFilter = searchField.text
                        }
                    }

                    ListView {
                        id: modelListView
                        anchors.top: searchField.bottom
                        anchors.topMargin: 6
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
                            text: qsTr("暂无模型 / 水域 / 子世界")
                            anchors.centerIn: parent
                            font.pixelSize: 12
                            color: "#888888"
                            visible: modelListView.count == 0
                        }

                        delegate: Rectangle {
                            width: modelListView.width
                            height: 32
                            color: modelData.kind === "group"
                                   ? (modelData.gtype === "sub" ? "#e8d8c8"
                                      : modelData.gtype === "waters" ? "#c8e8d8"
                                                                     : "#c8d8e8")
                                   : (modelListView.currentIndex === index ? "#b8d0f0"
                                      : (index % 2 == 0 ? "#f8f8f8" : "#ececec"))
                            border.color: modelData.kind === "group" ? "#8a8a8a" : "#cccccc"
                            border.width: 1

                            // 左侧色条（组行按类型着色）
                            Rectangle {
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                width: 4
                                visible: modelData.kind === "group"
                                color: modelData.gtype === "sub" ? "#c07840"
                                     : modelData.gtype === "waters" ? "#40a070"
                                                                    : "#4070b0"
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
                                        color: "#b8b8b8"
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
                                    color: modelData.kind === "model" ? "#4070b0"
                                                                      : "#40a070"
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
                                    font.pixelSize: 10
                                    font.bold: true
                                    color: "#404040"
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
                                            ? "#101010" : "black"
                                }

                                // 海况（水域）
                                Text {
                                    text: modelData.widx >= 0
                                          ? MenuPluginKeship.waterAreaSeaStates[modelData.widx] : ""
                                    verticalAlignment: Text.AlignVCenter
                                    height: parent.height
                                    font.pixelSize: 11
                                    color: "#0055aa"
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
                                        radius: 4
                                        color: parent.hovered ? "#b8c8e0" : "#e0e0e8"
                                        border.color: "#909098"
                                        border.width: 1
                                    }
                                    contentItem: Text {
                                        text: parent.text
                                        color: '#303030'
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
                                    StyledMenuItem { text: "浮力体"
                                        visible: !modelData.isStatic && !modelData.mathHull
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditBuoyancy(modelData.midx); } }
                                    StyledMenuItem { text: "推进器"
                                        visible: !modelData.isStatic
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditThrust(modelData.midx); } }
                                    StyledMenuItem { text: "流体动力"
                                        visible: !modelData.isStatic
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditHydro(modelData.midx); } }
                                    StyledMenuItem { text: "传感器"
                                        onTriggered: { modelRowMenu.close(); MenuPluginKeship.OnBtn_EditSensor(modelData.midx); } }
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
                    Button {
                        id: deleteAllBtn
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 8
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        height: 30
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
                visible: root.tabBarCurrent == 1
                anchors.fill: parent

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

                    /// 课程分组头部
                    section.property: "course"
                    section.criteria: ViewSection.FullString
                    section.delegate: Rectangle {
                        width: expListView.width
                        height: 28
                        color: "#b8b8c8"
                        Text {
                            text: "▎" + section
                            color: "#303030"
                            font.pixelSize: 13
                            font.bold: true
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                        }
                    }

                    delegate: Rectangle {
                        width: expListView.width
                        height: 32
                        color: bgColor
                        border.color: "#444444"
                        border.width: 1

                        Rectangle {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 3
                            color: leftBarColor
                        }

                        property color bgColor: "#e8e8ec"
                        property color textColor: enabled ? "#202020" : "#999999"
                        property color leftBarColor: enabled ? "#448844" : "transparent"

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
                                color: model.enabled ? "#338833" : "#666666"
                                font.pixelSize: 10
                                width: 36
                            }
                            Text {
                                text: model.enabled ? "" : "(待开发)"
                                color: "#555555"
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