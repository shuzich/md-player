import QtQuick
import QtQuick.Controls

// 播控条：播放/暂停、时间、章节、音轨/字幕、音量、截图。
Rectangle {
    id: root

    required property var player
    property alias seekBar: seek

    signal requestScreenshot(bool withSubtitles)
    signal requestTitlePanel()

    // 碟类资源才有标题面板可开。
    property bool hasTitles: false

    implicitHeight: 78
    color: "#e6101014"

    function fmt(t) {
        if (!isFinite(t) || t < 0) t = 0
        var s = Math.floor(t % 60), m = Math.floor(t / 60) % 60, h = Math.floor(t / 3600)
        var mm = (m < 10 ? "0" : "") + m, ss = (s < 10 ? "0" : "") + s
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }

    Column {
        anchors { fill: parent; leftMargin: 16; rightMargin: 16; topMargin: 6 }
        spacing: 4

        SeekBar {
            id: seek
            width: parent.width
            position: root.player.position
            duration: root.player.duration
            chapters: root.player.chapters
            onSeekDragged: function(t) { root.player.seekDrag(t) }
            onSeekReleased: function(t) { root.player.seekExact(t) }
        }

        Row {
            width: parent.width
            height: 34
            spacing: 10

            // 标题·章节面板开关（只在碟类资源上出现）
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hasTitles
                text: "☰"
                font.pixelSize: 16
                ToolTip.visible: hovered
                ToolTip.text: qsTr("标题与章节 (T)")
                onClicked: root.requestTitlePanel()
            }

            // 播放 / 暂停
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                text: root.player.paused ? "▶" : "⏸"
                font.pixelSize: 17
                onClicked: root.player.togglePause()
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#e8e8ee"
                font.pixelSize: 13
                font.family: "Menlo"
                text: root.fmt(seek.shownPosition) + " / " + root.fmt(root.player.duration)
            }

            // 章节
            ComboBox {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.player.chapters.length > 0
                width: 190
                model: root.player.chapters
                textRole: "title"
                currentIndex: root.player.chapter
                onActivated: function(i) { root.player.jumpToChapter(i) }
            }

            // 音轨
            ComboBox {
                id: audioBox
                anchors.verticalCenter: parent.verticalCenter
                visible: root.player.audioTracks.length > 1
                width: 170
                model: root.player.audioTracks
                textRole: "label"
                onActivated: function(i) { root.player.setAudioTrack(model[i].id) }
                Component.onCompleted: syncIndex()
                function syncIndex() {
                    for (var i = 0; i < model.length; i++)
                        if (model[i].id === root.player.audioTrackId) { currentIndex = i; return }
                }
                Connections {
                    target: root.player
                    function onAudioTrackIdChanged() { audioBox.syncIndex() }
                    function onTracksChanged() { audioBox.syncIndex() }
                }
            }

            // 字幕（含「关闭」项，id = -1）
            ComboBox {
                id: subBox
                anchors.verticalCenter: parent.verticalCenter
                visible: root.player.subtitleTracks.length > 0
                width: 170
                model: {
                    var list = [{ id: -1, label: "字幕：关" }]
                    for (var i = 0; i < root.player.subtitleTracks.length; i++)
                        list.push(root.player.subtitleTracks[i])
                    return list
                }
                textRole: "label"
                onActivated: function(i) { root.player.setSubtitleTrack(model[i].id) }
                Component.onCompleted: syncIndex()
                function syncIndex() {
                    for (var i = 0; i < model.length; i++)
                        if (model[i].id === root.player.subtitleTrackId) { currentIndex = i; return }
                    currentIndex = 0
                }
                Connections {
                    target: root.player
                    function onSubtitleTrackIdChanged() { subBox.syncIndex() }
                    function onTracksChanged() { subBox.syncIndex() }
                }
            }

            Item { width: 8; height: 1 }   // 间隔

            // 静音 + 音量
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                text: root.player.muted || root.player.volume <= 0 ? "🔇" : "🔊"
                font.pixelSize: 15
                onClicked: root.player.setMuted(!root.player.muted)
            }
            Slider {
                id: volSlider
                anchors.verticalCenter: parent.verticalCenter
                width: 110
                from: 0; to: 130
                value: root.player.volume
                onMoved: root.player.setVolume(value)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: "#8a8a95"
                font.pixelSize: 12
                width: 34
                text: Math.round(root.player.volume) + "%"
            }

            // 截图：两种模式
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                text: "截图"
                ToolTip.visible: hovered
                ToolTip.text: "纯画面（不含字幕）"
                onClicked: root.requestScreenshot(false)
            }
            ToolButton {
                anchors.verticalCenter: parent.verticalCenter
                text: "截图+字幕"
                ToolTip.visible: hovered
                ToolTip.text: "含字幕与 OSD"
                onClicked: root.requestScreenshot(true)
            }
        }
    }
}
