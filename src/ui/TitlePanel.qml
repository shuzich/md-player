// 标题·章节面板（M1-PLAN T3）。左侧抽屉，列出碟内全部 playlist——
// 演唱会碟常把曲目组拆成多个短 playlist，所以这里不做时长过滤，全部展示，
// 只给主标题加徽标并默认展开（ARCHITECTURE §bluray）。
import QtQuick
import QtQuick.Controls

Drawer {
    id: root

    required property var disc // BlurayController
    required property var fmt  // 时长格式化函数，复用 ControlBar 的实现

    edge: Qt.LeftEdge
    width: Math.min(420, parent ? parent.width * 0.82 : 420)
    height: parent ? parent.height : 0
    dim: false
    // 非模态：面板开着时视频与播控仍可直接操作，空格/方向键也不被抢走。
    modal: false
    closePolicy: Popup.CloseOnEscape
    dragMargin: 0 // 边缘拖拽手势会和进度条打架，只走按钮/快捷键开关

    background: Rectangle {
        color: "#f21a1a20"
        Rectangle {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: 1
            color: "#33ffffff"
        }
    }

    Item {
        id: header
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 16 }
        height: 52

        Text {
            id: nameText
            anchors { left: parent.left; right: parent.right; top: parent.top }
            text: root.disc.discName.length > 0 ? root.disc.discName : qsTr("蓝光碟")
            color: "#f2f2f5"
            font.pixelSize: 17
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            anchors { left: parent.left; right: parent.right; top: nameText.bottom; topMargin: 4 }
            text: qsTr("%1 条标题").arg(root.disc.playlists.length)
                  + (root.disc.mainTitleIndex >= 0
                     ? qsTr("　·　主标题 %1").arg(root.disc.playlists[root.disc.mainTitleIndex].mpls)
                     : "")
            color: "#7a7a85"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: 1
            color: "#26ffffff"
        }
    }

    ListView {
        id: list
        anchors {
            left: parent.left; right: parent.right
            top: header.bottom; bottom: parent.bottom
            leftMargin: 16; rightMargin: 10; topMargin: 8; bottomMargin: 12
        }
        clip: true
        spacing: 4
        model: root.disc.playlists
        ScrollBar.vertical: ScrollBar {}

        function revealCurrent() {
            if (root.disc.currentIndex >= 0)
                positionViewAtIndex(root.disc.currentIndex, ListView.Contain)
        }

        delegate: Column {
            id: entry
            required property var modelData
            required property int index
            width: list.width - 12
            spacing: 0

            readonly property bool isCurrent: entry.index === root.disc.currentIndex
            property bool expanded: entry.modelData.isMainTitle // 主标题默认展开

            Rectangle {
                width: parent.width
                height: 58
                radius: 5
                color: entry.isCurrent ? "#3348a0ff" : (rowHover.hovered ? "#1affffff" : "transparent")

                HoverHandler { id: rowHover }
                TapHandler { onTapped: root.disc.playIndex(entry.index) }

                Row {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                    spacing: 8

                    Column {
                        width: parent.width - expandBtn.width - 8
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3

                        Row {
                            spacing: 6
                            Text {
                                text: entry.modelData.mpls
                                color: entry.isCurrent ? "#ffffff" : "#dcdce4"
                                font.pixelSize: 13
                                font.family: "Menlo"
                            }
                            Text {
                                text: entry.modelData.durationText
                                color: "#9a9aa6"
                                font.pixelSize: 13
                                font.family: "Menlo"
                            }
                            Rectangle {
                                visible: entry.modelData.isMainTitle
                                anchors.verticalCenter: parent.verticalCenter
                                width: badge.implicitWidth + 10
                                height: 16
                                radius: 8
                                color: "#5548a0ff"
                                Text {
                                    id: badge
                                    anchors.centerIn: parent
                                    text: qsTr("主标题")
                                    color: "#eaf2ff"
                                    font.pixelSize: 10
                                }
                            }
                        }
                        Text {
                            width: parent.width
                            text: entry.modelData.chapterCount > 0
                                  ? qsTr("%1 章　·　%2").arg(entry.modelData.chapterCount).arg(entry.modelData.summary)
                                  : entry.modelData.summary
                            color: "#7a7a85"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }

                    // 章节只有 1 条时展开没有意义，隐藏箭头。
                    Item {
                        id: expandBtn
                        width: 26
                        height: parent.height
                        visible: entry.modelData.chapterCount > 1
                        Text {
                            anchors.centerIn: parent
                            text: entry.expanded ? "▾" : "▸"
                            color: "#9a9aa6"
                            font.pixelSize: 13
                        }
                        TapHandler { onTapped: entry.expanded = !entry.expanded }
                    }
                }
            }

            // 章节列表。章节名拿不到时 label 已在 C++ 侧降级为「第 N 章」。
            Column {
                width: parent.width
                visible: entry.expanded && entry.modelData.chapterCount > 1
                Repeater {
                    model: entry.expanded ? entry.modelData.chapters : []
                    delegate: Rectangle {
                        id: chapterRow
                        required property var modelData
                        width: entry.width - 12
                        height: 26
                        color: chHover.hovered ? "#14ffffff" : "transparent"

                        HoverHandler { id: chHover }
                        TapHandler { onTapped: root.disc.playChapter(entry.index, chapterRow.modelData.number) }

                        Text {
                            id: chNumber
                            anchors { left: parent.left; leftMargin: 24; verticalCenter: parent.verticalCenter }
                            width: 24
                            text: chapterRow.modelData.number
                            color: "#6e6e7a"
                            font.pixelSize: 11
                            font.family: "Menlo"
                            horizontalAlignment: Text.AlignRight
                        }
                        Text {
                            id: chStart
                            anchors { right: parent.right; rightMargin: 10; verticalCenter: parent.verticalCenter }
                            text: root.fmt(chapterRow.modelData.start)
                            color: "#6e6e7a"
                            font.pixelSize: 11
                            font.family: "Menlo"
                        }
                        Text {
                            anchors {
                                left: chNumber.right; leftMargin: 10
                                right: chStart.left; rightMargin: 10
                                verticalCenter: parent.verticalCenter
                            }
                            text: chapterRow.modelData.label
                            color: "#c4c4d0"
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    onOpened: list.revealCurrent()
}
