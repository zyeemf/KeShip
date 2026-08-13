import QtQuick 2.0

Rectangle {
    color: "transparent"
    anchors.fill: parent

    Row {
        id: viewerRow
        spacing: 8
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8

        Repeater {
            model: SensorOverlayPlugin.viewers

            Rectangle {
                width: 340
                height: 300
                color: "#1a1a2e"
                border.color: "#444"

                /// 标题栏
                Rectangle {
                    id: titleBar
                    width: parent.width; height: 26
                    color: "#2a2a3e"
                    Text {
                        text: modelData.typeLabel + ": " + modelData.modelName + " / " + modelData.instName
                        color: "#ccc"; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left; anchors.leftMargin: 8
                    }
                    Rectangle {
                        width: 20; height: 20; color: "transparent"
                        anchors.right: parent.right; anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            text: "×"; color: "#aaa"; font.pixelSize: 16
                            anchors.centerIn: parent
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                var cmd = {
                                    "action": "close",
                                    "model": modelData.modelName,
                                    "name": modelData.instName
                                };
                                SensorOverlayPlugin.onSensorViewCmd(
                                    {"data": JSON.stringify(cmd)});
                            }
                        }
                    }
                }

                /// 内容
                Rectangle {
                    anchors.top: titleBar.bottom
                    anchors.left: parent.left; anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 2
                    color: "black"

                    // 相机画面
                    Image {
                        visible: modelData.typeLabel === "相机"
                        anchors.fill: parent
                        fillMode: Image.PreserveAspectFit
                        source: "image://camera/" + modelData.modelName + " / " + modelData.instName
                              + "?" + SensorOverlayPlugin.refreshCounter
                        cache: false
                    }

                    // 激光雷达占位
                    Rectangle {
                        visible: modelData.typeLabel === "激光雷达"
                        anchors.fill: parent
                        color: "#12121c"
                        Text {
                            anchors.centerIn: parent
                            text: "激光雷达数据"
                            color: "#666"
                        }
                    }
                }
            }
        }
    }
}
