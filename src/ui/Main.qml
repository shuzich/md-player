import QtQuick
import QtQuick.Controls
import MdPlayer

ApplicationWindow {
    id: root

    width: 1280
    height: 720
    minimumWidth: 720
    minimumHeight: 405
    visible: true
    title: Player.mediaTitle.length > 0 ? Player.mediaTitle + " — md-player" : "md-player"
    color: "#101014"

    // 视频渲染面。必须始终 visible —— 一旦不可见，Qt 不会调用 createRenderer，
    // mpv_render_context 就建不起来，vo=libmpv 没有输出端，视频链路整条不工作。
    MpvVideo {
        id: video
        anchors.fill: parent
    }

    // 无媒体时的占位层
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
    }

    // 播控条：鼠标移动后显示，静止 2.5 秒自动隐藏（播放中）。
    ControlBar {
        id: controls
        player: Player
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        visible: Player.hasMedia
        opacity: shouldShow ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 160 } }

        property bool shouldShow: true
        hasTitles: root.activeDisc.discOpen
        onRequestScreenshot: function(withSubs) { Player.screenshot(withSubs) }
        onRequestTitlePanel: titlePanel.visible ? titlePanel.close() : titlePanel.open()
    }

    // 当前打开的碟。蓝光与 DVD 两个 Controller 的 QML 接口是对齐的（鸭子类型），
    // 面板拿到哪一个都照常工作。两者都没开时退回 Bluray——它此时报 discOpen=false、
    // 列表为空，绑定不会踩到 null。
    readonly property var activeDisc: Bluray.discOpen ? Bluray : (Dvd.discOpen ? Dvd : Bluray)

    // 标题·章节面板（T3 蓝光 / T4 DVD 共用）。非模态，开着也不影响播控与快捷键。
    TitlePanel {
        id: titlePanel
        disc: root.activeDisc
        fmt: controls.fmt
    }

    Timer {
        id: hideControls
        interval: 2500
        onTriggered: if (!Player.paused && !controls.seekBar.dragging) controls.shouldShow = false
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        hoverEnabled: true
        onPositionChanged: { controls.shouldShow = true; hideControls.restart() }
    }

    Connections {
        target: Player
        function onPausedChanged() {
            if (Player.paused) { controls.shouldShow = true; hideControls.stop() }
            else hideControls.restart()
        }
        // 断点续播：不擅自跳转，弹框问用户（M1-PLAN T2）。
        function onResumeAvailable(pos, dur) {
            resumeDialog.savedPos = pos
            resumeDialog.open()
        }
        function onScreenshotSaved(path, withSubs) {
            toast.show((withSubs ? "已截图（含字幕）：" : "已截图（纯画面）：") + path)
        }
        function onErrorOccurred(msg) { toast.show("错误：" + msg) }
    }


    // 打开碟只播主标题，不自动弹面板（D-020）——面板会遮住画面，而绝大多数
    // 场景用户就是要看主标题。入口保留两个：播控条的 ☰ 按钮、快捷键 T。
    function discOpened(kind, disc) {
        toast.show(qsTr("已载入%1：%2（%3 条标题）").arg(kind).arg(disc.discName).arg(disc.playlists.length))
        if (disc.takeTitleHint())
            titleHint.restart()
    }

    Connections {
        target: Bluray
        // 加密盘 / 损坏盘等走的都是这条，文案统一在 strings.h。
        function onErrorOccurred(msg) { toast.show(msg) }
        function onDiscChanged() {
            if (Bluray.discOpen) {
                Dvd.closeDisc()          // 一次只持有一张碟
                root.discOpened(qsTr("蓝光碟"), Bluray)
            }
        }
    }

    Connections {
        target: Dvd
        function onErrorOccurred(msg) { toast.show(msg) }
        function onDiscChanged() {
            if (Dvd.discOpen) {
                Bluray.closeDisc()
                root.discOpened(qsTr("DVD"), Dvd)
            }
        }
    }

    Dialog {
        id: resumeDialog
        property real savedPos: 0
        anchors.centerIn: parent
        modal: true
        focus: true                       // 不加这句拿不到键盘焦点，回车/Esc 全无反应
        title: qsTr("继续播放？")
        closePolicy: Popup.CloseOnEscape  // Esc 等同「从头播放」

        // 用自定义 footer 取代 standardButtons，顺带把按钮文案本地化。
        // 注意：下面这句 forceActiveFocus 并**没有**让回车生效——按钮拿到了焦点框，
        // 但 Return 仍不激活它（物理键盘复现）。鼠标点击与 Esc 正常。见 issue #1，排期 T7。
        // 排查前先打印 activeFocusItem 确认按键落点，别再盲试方案。
        onOpened: acceptButton.forceActiveFocus()

        footer: DialogButtonBox {
            Button {
                id: acceptButton
                text: qsTr("继续")
                focus: true
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
            }
            Button {
                text: qsTr("从头播放")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            }
        }

        Label {
            text: qsTr("上次播放到 %1，要从这里继续吗？").arg(controls.fmt(resumeDialog.savedPos))
        }
        onAccepted: Player.resumeFromSaved()
        onRejected: Player.discardSaved()
    }

    // 轻量提示条
    Rectangle {
        id: toast
        anchors { horizontalCenter: parent.horizontalCenter; bottom: controls.top; bottomMargin: 16 }
        width: Math.min(label.implicitWidth + 28, root.width - 60)
        height: 38
        radius: 6
        color: "#e6202028"
        opacity: 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 180 } }

        function show(text) {
            label.text = text
            opacity = 1
            toastTimer.restart()
            if (Player.uiLogEnabled) console.log("[TOAST] " + text)
        }

        Text {
            id: label
            anchors.centerIn: parent
            width: parent.width - 28
            color: "#e8e8ee"
            font.pixelSize: 12
            elide: Text.ElideMiddle
        }
        Timer { id: toastTimer; interval: 3200; onTriggered: toast.opacity = 0 }
        // 一次性上手提示。等上一条「已载入蓝光碟」淡出后再出，免得两条提示打架。
        Timer {
            id: titleHint
            interval: 3600
            onTriggered: toast.show(qsTr("按 T 选择标题 / 章节"))
        }
    }

    DropArea {
        anchors.fill: parent
        onDropped: function(drop) {
            if (!drop.hasUrls || drop.urls.length === 0)
                return
            // 蓝光 → DVD → 直通 mpv。前两个都是「不认就返回 false」。
            // T5 会把这里换成统一路由入口。
            if (!Bluray.openUrl(drop.urls[0]) && !Dvd.openUrl(drop.urls[0]))
                Player.loadUrl(drop.urls[0])
        }
    }

    // 播控快捷键在模态询问框开启时必须失效，否则空格会穿透去切暂停。
    Shortcut { sequence: "Space"; enabled: !resumeDialog.opened; onActivated: Player.togglePause() }
    Shortcut { sequence: "Left"; enabled: !resumeDialog.opened; onActivated: Player.seekRelative(-5) }
    Shortcut { sequence: "Right"; enabled: !resumeDialog.opened; onActivated: Player.seekRelative(5) }
    Shortcut { sequence: "Ctrl+Left"; enabled: !resumeDialog.opened; onActivated: Player.seekRelative(-60) }
    Shortcut { sequence: "Ctrl+Right"; enabled: !resumeDialog.opened; onActivated: Player.seekRelative(60) }
    Shortcut { sequence: "Up"; enabled: !resumeDialog.opened; onActivated: Player.setVolume(Player.volume + 5) }
    Shortcut { sequence: "Down"; enabled: !resumeDialog.opened; onActivated: Player.setVolume(Player.volume - 5) }
    Shortcut { sequence: "m"; enabled: !resumeDialog.opened; onActivated: Player.setMuted(!Player.muted) }
    Shortcut { sequence: "s"; enabled: !resumeDialog.opened; onActivated: Player.screenshot(false) }
    Shortcut { sequence: "Shift+S"; enabled: !resumeDialog.opened; onActivated: Player.screenshot(true) }
    Shortcut { sequence: "t"; enabled: !resumeDialog.opened && root.activeDisc.discOpen
               onActivated: titlePanel.opened ? titlePanel.close() : titlePanel.open() }
    // 面板的 Esc 关闭：TitlePanel 刻意不取焦点，Popup.CloseOnEscape 用不了，改走这里。
    Shortcut { sequence: "Escape"; enabled: titlePanel.opened; onActivated: titlePanel.close() }
    Shortcut { sequence: StandardKey.FullScreen
               onActivated: root.visibility = (root.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen) }
}
