import QtQuick
import QtQuick.Controls
import MdPlayer

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    minimumWidth: 640
    minimumHeight: 360
    visible: true
    title: Player.mediaTitle.length > 0 ? Player.mediaTitle + " — md-player" : "md-player"
    color: "#101014"

    function fmt(t) {
        if (!isFinite(t) || t < 0) t = 0
        var s = Math.floor(t % 60), m = Math.floor(t / 60) % 60, h = Math.floor(t / 3600)
        var mm = (m < 10 ? "0" : "") + m, ss = (s < 10 ? "0" : "") + s
        return h > 0 ? h + ":" + mm + ":" + ss : mm + ":" + ss
    }

    // 视频渲染面。必须始终 visible —— 一旦不可见，Qt 不会调用 createRenderer，
    // mpv_render_context 就建不起来，vo=libmpv 没有输出端，视频链路整条不工作。
    // 无媒体时它渲染成纯背景色，由下方占位层盖住。
    MpvVideo {
        id: video
        anchors.fill: parent
    }

    // 未载入媒体时的占位提示
    Column {
        anchors.centerIn: parent
        spacing: 12
        visible: !Player.hasMedia

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "md-player"
            color: "#f2f2f5"
            font.pixelSize: 34
            font.letterSpacing: 2
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("拖入视频文件，或拖入 BDMV / VIDEO_TS 文件夹、BD / DVD / SACD ISO")
            color: "#7a7a85"
            font.pixelSize: 14
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "v" + Qt.application.version + " · M1 / T1"
            color: "#4a4a55"
            font.pixelSize: 12
        }
    }

    // T1 自检信息条：时间 / 暂停态 / 解码参数。T2 会被真正的播控条取代。
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 34
        visible: Player.hasMedia
        color: "#cc000000"

        Text {
            anchors { left: parent.left; leftMargin: 14; verticalCenter: parent.verticalCenter }
            color: "#e8e8ee"
            font.pixelSize: 13
            text: (Player.paused ? "⏸" : "▶") + "  " + root.fmt(Player.position) + " / " + root.fmt(Player.duration)
        }
        Text {
            anchors { right: parent.right; rightMargin: 14; verticalCenter: parent.verticalCenter }
            color: "#8a8a95"
            font.pixelSize: 12
            text: Player.videoInfo
        }
    }

    // 拖放打开
    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0)
                Player.loadUrl(drop.urls[0])
        }
    }

    // T1 验收要求的快捷键：空格暂停、←/→ ±5s
    Shortcut { sequence: "Space";       onActivated: Player.togglePause() }
    Shortcut { sequence: "Left";        onActivated: Player.seekRelative(-5) }
    Shortcut { sequence: "Right";       onActivated: Player.seekRelative(5) }
    Shortcut { sequence: "Ctrl+Left";   onActivated: Player.seekRelative(-60) }
    Shortcut { sequence: "Ctrl+Right";  onActivated: Player.seekRelative(60) }
    Shortcut { sequence: StandardKey.FullScreen
               onActivated: root.visibility = (root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen) }
}
