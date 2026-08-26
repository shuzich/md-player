// 标题·章节面板（M1-PLAN T3）。左侧抽屉，列出碟内 playlist——
// 演唱会碟常把曲目组拆成多个短 playlist，所以不按时长排序也不做内容过滤，
// 只给主标题加徽标并默认展开（ARCHITECTURE §bluray、D-014）。
// 唯一的过滤是「隐藏 10 秒以下条目」开关（D-019），它只影响显示，
// BlurayController.playlists 始终保有碟内完整结构。
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
    // 非模态：面板开着时视频与播控仍可直接操作。
    modal: false
    // focus 必须为 false。Popup 一旦拿到 activeFocus，QQuickPopupItem 会成为窗口的
    // activeFocusItem，此后窗口级 Shortcut 虽然 enabled=true 却再也不触发
    // ——面板一开，空格/方向键/截图快捷键就全哑了（实测）。本面板只用鼠标操作，
    // 不需要键盘焦点。代价是 Popup.CloseOnEscape 也随之失效，Esc 改由下面的
    // 窗口级 Shortcut 承担（正因为焦点没被抢走，它才能触发）。
    focus: false
    closePolicy: Popup.NoAutoClose
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
        height: 82

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
            id: countText
            anchors { left: parent.left; right: parent.right; top: nameText.bottom; topMargin: 4 }
            text: qsTr("%1 条标题").arg(root.disc.playlists.length)
                  + (root.disc.hiddenCount > 0 ? qsTr("（隐藏 %1 条）").arg(root.disc.hiddenCount) : "")
                  + (root.disc.mainTitleIndex >= 0
                     ? qsTr("　·　主标题 %1").arg(root.disc.playlists[root.disc.mainTitleIndex].mpls)
                     : "")
            color: "#7a7a85"
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        // 手搓的复选框，不用 Controls 的 CheckBox：macOS 上默认是原生样式，
        // 不接受 contentItem/indicator 定制（运行期直接警告并忽略），颜色也压不下来。
        // 顺带绕开焦点问题——纯 Item + TapHandler 不会把 activeFocus 吸进 Popup（D-018）。
        Row {
            id: shortFilter
            anchors { left: parent.left; bottom: parent.bottom; bottomMargin: 7 }
            spacing: 7
            readonly property bool checked: root.disc.hideShortTitles

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 13
                height: 13
                radius: 3
                color: shortFilter.checked ? "#5548a0ff" : "transparent"
                border.width: 1
                border.color: shortFilter.checked ? "#7a9fe0" : "#55ffffff"
                Text {
                    anchors.centerIn: parent
                    visible: shortFilter.checked
                    text: "✓"
                    color: "#eaf2ff"
                    font.pixelSize: 10
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("隐藏 10 秒以下条目")
                color: filterHover.hovered ? "#c4c4d0" : "#9a9aa6"
                font.pixelSize: 11
            }

            HoverHandler { id: filterHover }
            TapHandler { onTapped: root.disc.hideShortTitles = !shortFilter.checked }
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
        model: root.disc.visiblePlaylists
        ScrollBar.vertical: ScrollBar {}

        // 模型是过滤后的列表，下标与 disc.currentIndex（碟内原始下标）不是一回事，必须映射。
        function revealCurrent() {
            for (var i = 0; i < model.length; i++)
                if (model[i].index === root.disc.currentIndex) {
                    positionViewAtIndex(i, ListView.Contain)
                    return
                }
        }

        delegate: Column {
            id: entry
            required property var modelData
            required property int index
            width: list.width - 12
            spacing: 0

            // 注意用 modelData.index（碟内原始下标），不是 entry.index（过滤后列表的位置）。
            readonly property int discIndex: entry.modelData.index
            readonly property bool isCurrent: entry.discIndex === root.disc.currentIndex
            property bool expanded: entry.modelData.isMainTitle // 主标题默认展开

            Rectangle {
                width: parent.width
                height: 58
                radius: 5
                color: entry.isCurrent ? "#3348a0ff" : (rowHover.hovered ? "#1affffff" : "transparent")

                HoverHandler { id: rowHover }
                TapHandler { onTapped: root.disc.playIndex(entry.discIndex) }

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
                        TapHandler { onTapped: root.disc.playChapter(entry.discIndex, chapterRow.modelData.number) }

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
