import QtQuick

// 进度条。CLAUDE.md 深坑 #2 的一半实现在这里：
//   拖动过程中 -> seekDrag()（absolute+keyframes，贴关键帧，即时出画面）
//   松手       -> seekExact()（absolute+exact，精确落点）
//   拖动事件节流到 ~30Hz，避免高码率源被 seek 请求淹没
// 另一半在 configs/mpv-baseline.conf 的大 demuxer 缓存。
Item {
    id: root

    property real position: 0
    property real duration: 0
    property var chapters: []
    property bool dragging: false

    // 拖动中显示手指位置，不跟随 mpv 回报的 position，否则会来回跳。
    property real dragTarget: 0
    readonly property real shownPosition: dragging ? dragTarget : position

    signal seekDragged(real target)
    signal seekReleased(real target)
    signal chapterActivated(int index)

    implicitHeight: 26

    // 30Hz 节流：拖动过程中最多每 33ms 发一次 keyframes seek。
    // 中途积攒的位置存在 _pendingTarget，定时器到点时发最新值。
    //
    // 光靠 Timer 不够：GUI 线程若被阻塞（高码率源解码时会），QML Timer 不做合并，
    // 解除阻塞后会连续补发，实测出现过 3ms 间隔的连发。故再加一道时间戳硬闸，
    // 保证任何情况下两次 keyframes seek 的间隔都不小于 _minIntervalMs。
    property real _pendingTarget: -1
    property double _lastSentMs: 0
    readonly property int _minIntervalMs: 33

    function _flush() {
        if (root._pendingTarget < 0)
            return
        var now = Date.now()
        if (now - root._lastSentMs < root._minIntervalMs)
            return                      // 太密，留到下一拍
        root._lastSentMs = now
        root.seekDragged(root._pendingTarget)
        root._pendingTarget = -1
    }

    Timer {
        id: throttle
        interval: 16                    // 比节流窗口密一些，保证窗口一开就能发出去
        repeat: true
        running: root.dragging
        onTriggered: root._flush()
    }

    function _timeAt(mouseX) {
        if (duration <= 0) return 0
        var ratio = Math.max(0, Math.min(1, mouseX / groove.width))
        return ratio * duration
    }

    // 底槽
    Rectangle {
        id: groove
        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter }
        height: root.dragging ? 8 : 5
        radius: height / 2
        color: "#3a3a44"
        Behavior on height { NumberAnimation { duration: 90 } }

        // 已播放部分
        Rectangle {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: root.duration > 0 ? parent.width * (root.shownPosition / root.duration) : 0
            radius: parent.radius
            color: "#e8503a"
        }

        // 章节刻度
        Repeater {
            model: root.chapters
            delegate: Rectangle {
                required property var modelData
                visible: root.duration > 0 && modelData.time > 0
                x: root.duration > 0 ? groove.width * (modelData.time / root.duration) - width / 2 : 0
                anchors.verticalCenter: parent.verticalCenter
                width: 2
                height: groove.height + 6
                color: "#d8d8e0"
                opacity: 0.75
            }
        }
    }

    // 拖动手柄
    Rectangle {
        id: handle
        width: root.dragging ? 15 : 12
        height: width
        radius: width / 2
        color: "#ffffff"
        border { width: 2; color: "#e8503a" }
        anchors.verticalCenter: groove.verticalCenter
        x: (root.duration > 0 ? groove.width * (root.shownPosition / root.duration) : 0) - width / 2
        visible: root.duration > 0
        Behavior on width { NumberAnimation { duration: 90 } }
    }

    MouseArea {
        anchors.fill: parent
        anchors.margins: -8            // 放大命中区域，细进度条也好抓
        hoverEnabled: true
        preventStealing: true

        onPressed: function(mouse) {
            if (root.duration <= 0) return
            root.dragTarget = root._timeAt(mouse.x)
            root.dragging = true
            root._lastSentMs = 0        // 重置基准，保证本次拖动首发不被上次的时间戳挡住
            root._pendingTarget = root.dragTarget
        }
        onPositionChanged: function(mouse) {
            if (!root.dragging) return
            root.dragTarget = root._timeAt(mouse.x)
            root._pendingTarget = root.dragTarget    // 交给节流定时器发送
        }
        onReleased: function(mouse) {
            if (!root.dragging) return
            root.dragTarget = root._timeAt(mouse.x)
            root.dragging = false
            root._pendingTarget = -1
            root.seekReleased(root.dragTarget)       // 松手：精确落点
        }
        onCanceled: {
            root.dragging = false
            root._pendingTarget = -1
        }
    }
}
