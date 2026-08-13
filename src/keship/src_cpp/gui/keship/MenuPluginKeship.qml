/* 菜单插件： keship */
/// Zhangyu 创建 2026.1.16
/// 2026.5.28 重构：Tab 布局（模型管理 / 虚拟实验）

import QtQuick 2.0
import QtQuick.Controls 2.0

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
                title: qsTr("模型管理")
                active: root.tabBarCurrent == 0
                onClicked: {
                    if (root.collapsed) root.toggleCollapsed();
                    root.tabBarCurrent = 0;
                }
                onDoubleClicked: root.toggleCollapsed()
            }
            TitleButton {
                title: qsTr("虚拟实验")
                active: root.tabBarCurrent == 1
                onClicked: {
                    if (root.collapsed) root.toggleCollapsed();
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

                    Rectangle {
                        color: "#c0d8f0"
                        radius: 4
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                text: qsTr("载入世界")
                                onClicked: { MenuPluginKeship.OnBtn_LoadWorld(); }
                            }
                            CustomButton {
                                text: qsTr("保存世界")
                                onClicked: { MenuPluginKeship.OnBtn_SaveWorld(); }
                            }
                            CustomButton {
                                text: qsTr("载入模型")
                                onClicked: { MenuPluginKeship.OnBtn_LoadModel(); }
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

                    Rectangle {
                        color: "#c0d8f0"
                        radius: 4
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 8
                            CustomButton {
                                text: qsTr("数学船型")
                                onClicked: { MenuPluginKeship.OnBtn_NewMathHull(); }
                            }
                            CustomButton {
                                text: qsTr("圆柱体")
                                onClicked: { MenuPluginKeship.OnBtn_CylinderBuoyancyTest(); }
                            }
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

                    Rectangle {
                        color: "#c0d8f0"
                        radius: 4
                        width: parent.width
                        height: 44
                        Row {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            // 添加默认水域：仅当当前无任何水域时可用
                            CustomButton {
                                text: qsTr("添加默认水域")
                                width: 130
                                enabled: MenuPluginKeship.customWaterAreaCount === 0
                                onClicked: MenuPluginKeship.OnBtn_AddDefaultWaterArea()
                            }
                            CustomButton {
                                text: qsTr("添加自定义水域")
                                width: 130
                                onClicked: MenuPluginKeship.OnBtn_AddWaterArea()
                            }
                            CustomButton {
                                text: qsTr("清理网格缓存")
                                width: 130
                                bk_normalColor: "#f2c14e"
                                bk_pressedColor: "#d9a62e"
                                onClicked: MenuPluginKeship.OnBtn_CleanMeshCache()
                            }
                        }
                    }

                    // Rectangle {
                    //     color: "#c0d8f0"
                    //     radius: 4
                    //     width: parent.width
                    //     height: 44
                    //     Row {
                    //         anchors.verticalCenter: parent.verticalCenter
                    //         spacing: 8
                    //         CustomButton {
                    //             text: qsTr("任务设定")
                    //             onClicked: { MenuPluginKeship.OnBtn_TaskSet(); }
                    //         }
                    //         CustomButton {
                    //             text: qsTr("任务执行")
                    //             onClicked: { MenuPluginKeship.OnBtn_TaskMonitor(); }
                    //         }
                    //     }
                    // }
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
                            var label = (r.kind === "group")
                                    ? (tab1ModelBox.treeExpanded[
                                            r.chain + "/" + r.gtype] === false ? "+ " : "- ") + r.text
                                    : r.text
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
                            height: 30
                            color: modelData.kind === "group" ? "#c8d8e8"
                                   : (index % 2 == 0 ? "#f0f0f0" : "#e8e8e8")
                            border.color: modelData.kind === "group" ? "#8aa8c8" : "#cccccc"
                            border.width: 1

                            MouseArea {
                                anchors.fill: parent
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

                                Text {
                                    text: String(modelData.label || modelData.text || "")
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignLeft
                                    font.pixelSize: 12
                                    font.bold: modelData.kind === "group"
                                    height: 30
                                    width: modelData.kind === "model"
                                           ? Math.max(50, modelListView.width - 280 - (modelData.depth || 0) * 14)
                                           : modelData.kind === "water"
                                                 ? Math.max(50, modelListView.width - 178 - (modelData.depth || 0) * 14)
                                                 : Math.max(60, modelListView.width - 40 - (modelData.depth || 0) * 14)
                                    color: modelData.kind === "group"
                                            ? "#101010" : "black"
                                }

                                Button {
                                    text: qsTr("属性"); width: 36; height: 30
                                    visible: modelData.kind === "model"
                                    onClicked: MenuPluginKeship.OnBtn_ShowModelSettingsDialogByIndex(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }
                                Button {
                                    text: qsTr("浮力体"); width: 36; height: 30
                                    visible: modelData.kind === "model"
                                             && !modelData.isStatic
                                             && !modelData.mathHull
                                    onClicked: MenuPluginKeship.OnBtn_EditBuoyancy(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }
                                Button {
                                    text: qsTr("推进器"); width: 36; height: 30
                                    visible: modelData.kind === "model"
                                             && !modelData.isStatic
                                    onClicked: MenuPluginKeship.OnBtn_EditThrust(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }
                                Button {
                                    text: qsTr("流体动力"); width: 40; height: 30
                                    visible: modelData.kind === "model"
                                             && !modelData.isStatic
                                    onClicked: MenuPluginKeship.OnBtn_EditHydro(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }
                                Button {
                                    text: qsTr("传感器"); width: 36; height: 30
                                    visible: modelData.kind === "model"
                                    onClicked: MenuPluginKeship.OnBtn_EditSensor(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }
                                Button {
                                    text: qsTr("保存"); width: 36; height: 30
                                    visible: modelData.kind === "model"
                                    onClicked: MenuPluginKeship.OnBtn_SaveModel(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }
                                Button {
                                    text: qsTr("删除"); width: 36; height: 30
                                    visible: modelData.kind === "model"
                                    onClicked: MenuPluginKeship.OnBtn_DeleteModelByIndex(modelData.midx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 9
                                    }
                                }

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
                                Button {
                                    text: qsTr("环境"); width: 40; height: 30
                                    visible: modelData.kind === "water"
                                             && modelData.widx >= 0
                                    onClicked: MenuPluginKeship.OnBtn_ShowWaterEnv(modelData.widx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 10
                                    }
                                }
                                Button {
                                    text: qsTr("编辑"); width: 40; height: 30
                                    visible: modelData.kind === "water"
                                             && modelData.widx >= 0
                                    onClicked: MenuPluginKeship.OnBtn_EditWaterArea(modelData.widx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 10
                                    }
                                }
                                Button {
                                    text: qsTr("删除"); width: 40; height: 30
                                    visible: modelData.kind === "water"
                                             && modelData.widx >= 0
                                    onClicked: MenuPluginKeship.OnBtn_DeleteWaterArea(modelData.widx)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 10
                                    }
                                }

                                Button {
                                    text: qsTr("保存"); width: 40; height: 30
                                    visible: modelData.kind === "group"
                                             && modelData.gtype === "sub"
                                    onClicked: MenuPluginKeship.OnBtn_SaveSubWorld(modelData.chain)
                                    contentItem: Text {
                                        text: parent.text; color: '#080808'
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        font.pixelSize: 10
                                    }
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