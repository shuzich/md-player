import QtQuick
import QtQuick.Controls

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    minimumWidth: 640
    minimumHeight: 360
    visible: true
    title: qsTr("md-player")
    color: "#101014"

    // T0 只要求一个可运行的空窗口；T1 起此处替换为 MpvObject 渲染面。
    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "md-player"
            color: "#f2f2f5"
            font.pixelSize: 34
            font.letterSpacing: 2
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("拖入 BDMV / VIDEO_TS 文件夹，或 BD / DVD / SACD ISO")
            color: "#7a7a85"
            font.pixelSize: 14
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "v" + Qt.application.version + " · M1 / T0"
            color: "#4a4a55"
            font.pixelSize: 12
        }
    }
}
