import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

// 播控条：播放/暂停、时间、章节、音轨/字幕、音量、截图。
Rectangle {
    id: root

    required property var player
    property alias seekBar: seek

    signal requestScreenshot(bool withSubtitles)
    signal requestTitlePanel()
    signal requestPrevTitle()
    signal requestNextTitle()

    // 碟类资源才有标题面板可开。
    property bool hasTitles: false

    implicitHeight: 78
    color: "#e6101014"

    // 下拉框收起时的文案。轨数 ≤1 时框是置灰的，它显示的这一行就是用户能拿到的
    // 全部信息，所以「没有」也要明说，不能留空（D-058）。
    readonly property string noTrackText: qsTr("无")
    function trackText(list, idx) {
        if (list.length === 0) return root.noTrackText
        if (idx >= 0 && idx < list.length) return list[idx].label
        return list.length === 1 ? list[0].label : qsTr("默认")
    }

    // 播控条排得下排不下，在没有屏幕录制权限的机器上于日志里完全隐形（同 T3 的面板
    // 开合）。音轨/字幕两个框改成常驻后多占 420px，MD_LOG_UI 下打一行「内容总宽 /
    // 可用宽」，溢出与否一眼可判（D-058）。
    // 给窗口级 Esc 用：三个下拉框的弹层不再自己收 Esc（closePolicy 只留
    // CloseOnPressOutside），关闭动作统一由 Main.qml 的 Shortcut 派发（D-062）。
    readonly property bool anyPopupOpen: audioBox.popup.opened || subBox.popup.opened || chapterBox.popup.opened
    function closePopups() {
        audioBox.popup.close()
        subBox.popup.close()
        chapterBox.popup.close()
    }

    // 播控条这一行压到底时需要多宽。三个下拉框各有 Layout.minimumWidth（120），
    // 其余控件按自身 implicitWidth 钉死，所以这个和是**内容的硬下限**，窗口的
    // minimumWidth 由它 + 左右边距推出来（D-065）。写成算式而不是魔数：以后加一个
    // 按钮，这里自动跟着变，不会又出现「改了控件忘了改窗口下限」的溢出。
    readonly property real minContentWidth: {
        var sum = 0, n = 0
        for (var i = 0; i < controlsRow.children.length; i++) {
            var c = controlsRow.children[i]
            if (c.visible === false) continue
            var m = c.Layout.minimumWidth
            // 逐项向上取整：控件的 implicitWidth 常带小数（文字测量），直接累加会
            // 比实际布局少 1 px，窗口就正好停在「内容比行宽多 1」的位置上（实测）。
            sum += Math.ceil(m > 0 ? m : c.implicitWidth)
            n++
        }
        return sum + Math.max(0, n - 1) * controlsRow.spacing
    }
    readonly property real minWindowWidth: minContentWidth + 32   // 左右边距各 16

    function logExtent(why) {
        if (root.player.uiLogEnabled)
            console.log("[BAR] " + why + " 最小内容宽=" + Math.round(root.minContentWidth)
                        + " 控件总宽=" + Math.round(controlsRow.childrenRect.width)
                        + " 可用宽=" + Math.round(controlsRow.width)
                        + (controlsRow.childrenRect.width > controlsRow.width ? "  溢出!" : "  放得下"))
    }

    Connections {
        target: root.player
        function onTracksChanged() { root.logExtent("轨道表更新") }
        function onChaptersChanged() { root.logExtent("章节表更新") }
    }

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
            // 深坑 #2 的配方（拖动中 ~30Hz 发 keyframes seek）**只对有画面的资源成立**：
            // 它的全部理由是「即时出画面」。纯音频没有画面，理由不成立而代价全在——
            // 实测一次 3.5 秒拖动发出 88 次 seek，在真实 CoreAudio 输出下换来 89 次
            // `starting audio playback`，即每秒约 30 次 AO 重启、每次只放约 4 毫秒
            // 碎片，听感就是拖动全程一片杂音（D-055）。纯音频改为：拖动中只动进度条
            // 与时间标签，松手时发一次 absolute+exact。
            onSeekDragged: function(t) { if (root.player.hasVideo) root.player.seekDrag(t) }
            onSeekReleased: function(t) { root.player.seekExact(t) }
        }

        // RowLayout 而不是 Row：音轨/字幕两个框改成常驻后（D-058），碟类资源上
        // 「章节 + 音轨 + 字幕」三个框同时在场，实测内容总宽 1322 > 可用宽 1248，
        // 右端的截图按钮被挤出窗口。改成布局后空间不够时先压缩三个下拉框
        // （下限 120px），按钮与音量条一律按自身宽度钉死，不参与压缩。
        RowLayout {
            id: controlsRow
            width: parent.width
            height: 34
            spacing: 10
            Component.onCompleted: root.logExtent("载入")

            // 标题·章节面板开关（只在碟类资源上出现）
            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                visible: root.hasTitles
                text: "☰"
                font.pixelSize: 16
                ToolTip.visible: hovered
                ToolTip.text: qsTr("标题与章节 (T)")
                onClicked: root.requestTitlePanel()
            }

            // 上一个 / 下一个条目（SACD=曲目，蓝光/DVD=标题）
            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                visible: root.hasTitles
                text: "⏮"
                font.pixelSize: 15
                ToolTip.visible: hovered
                ToolTip.text: qsTr("上一个 (PgUp)")
                onClicked: root.requestPrevTitle()
            }

            // 播放 / 暂停
            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                text: root.player.paused ? "▶" : "⏸"
                font.pixelSize: 17
                onClicked: root.player.togglePause()
            }

            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                visible: root.hasTitles
                text: "⏭"
                font.pixelSize: 15
                ToolTip.visible: hovered
                ToolTip.text: qsTr("下一个 (PgDn)")
                onClicked: root.requestNextTitle()
            }

            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                color: "#e8e8ee"
                font.pixelSize: 13
                font.family: "Menlo"
                text: root.fmt(seek.shownPosition) + " / " + root.fmt(root.player.duration)
            }

            // 章节。没有章节的资源（纯音频、无章节的普通文件）仍然隐藏——B1 的置灰
            // 口径只覆盖音轨与字幕这两个「用户以为功能缺失」的入口（D-058）。
            ComboBox {
                id: chapterBox
                Layout.alignment: Qt.AlignVCenter
                focusPolicy: Qt.NoFocus
                popup.closePolicy: Popup.CloseOnPressOutside   // 同音轨框，见下方注释（D-062）
                visible: root.player.chapters.length > 0
                // fillWidth 是「允许被压缩」的开关：实测（Qt 6.11）没有它时 RowLayout
                // 不会把子项压到 preferredWidth 以下，宁可整排溢出，maximumWidth 才是
                // 它平时的宽度上限（D-058）。
                Layout.fillWidth: true
                Layout.preferredWidth: 190
                Layout.maximumWidth: 190
                Layout.minimumWidth: 120
                model: root.player.chapters
                textRole: "title"
                currentIndex: root.player.chapter
                onActivated: function(i) { root.player.jumpToChapter(i) }
            }

            // 音轨。轨数 ≤1 时**不隐藏，改为置灰**（D-058）：隐藏会让人以为播放器
            // 没有这个功能——人工验收里就是这么误判的。置灰同时还说明了「这张碟
            // 另有多声道区，只是不可播」。字幕框同理。
            ComboBox {
                id: audioBox
                Layout.alignment: Qt.AlignVCenter
                // 深坑 #7：控件把焦点吸走 = 窗口级快捷键静默失效。播控条不是 Popup，
                // 但没有任何理由让这几个下拉框拿键盘焦点。
                focusPolicy: Qt.NoFocus
                // 去掉默认 closePolicy 里的 CloseOnEscape。吞掉窗口级快捷键的是
                // closePolicy 而不是焦点：实测把焦点留在根项（不给弹层 focus）时
                // 快捷键照样全哑，而把 closePolicy 换成 NoAutoClose 则全部恢复。
                // 只留 CloseOnPressOutside：点外面照样关得掉，弹层开着时 T / 空格 /
                // 方向键全部照常工作，Esc 由窗口级 Shortcut 承担（D-062）。
                popup.closePolicy: Popup.CloseOnPressOutside
                enabled: root.player.audioTracks.length > 1
                Layout.fillWidth: true
                Layout.preferredWidth: 210
                Layout.maximumWidth: 210
                Layout.minimumWidth: 120
                model: root.player.audioTracks
                textRole: "label"
                // 没有前缀时两个下拉框长得一模一样，用户根本不知道哪个是音轨、哪个是
                // 字幕——这就是 issue #3 的主因。displayText 只改收起时的显示，
                // 展开后的列表项保持原样。
                displayText: qsTr("音轨：") + root.trackText(model, currentIndex)
                onActivated: function(i) { root.player.setAudioTrack(model[i].id) }
                // 「点完下拉框快捷键就哑」查过两轮，两次都因为缺这个量而查错方向：
                // 真正的原因是弹层还开着（D-062），不是焦点、也不是激活。弹层开合
                // 本身在日志里完全隐形，故并进 MD_LOG_UI 常驻，零行为改动。
                popup.onOpened: if (root.player.uiLogEnabled)
                    console.log("[WIN] 音轨弹层打开 type=" + audioBox.popup.popupType
                                + " 窗口active=" + Window.window.active)
                popup.onClosed: if (root.player.uiLogEnabled)
                    console.log("[WIN] 音轨弹层关闭 窗口active=" + Window.window.active
                                + " 焦点=" + Window.window.activeFocusItem)
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

            // 字幕（含「关闭」项，id = -1）。碟上没有字幕时同样置灰显示（D-058）。
            ComboBox {
                id: subBox
                Layout.alignment: Qt.AlignVCenter
                focusPolicy: Qt.NoFocus
                popup.closePolicy: Popup.CloseOnPressOutside   // 同音轨框（D-062）
                enabled: root.player.subtitleTracks.length > 0
                Layout.fillWidth: true
                Layout.preferredWidth: 210
                Layout.maximumWidth: 210
                Layout.minimumWidth: 120
                displayText: qsTr("字幕：") + (enabled ? root.trackText(model, currentIndex) : root.noTrackText)
                model: {
                    var list = [{ id: -1, label: "关" }]
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

            Item { Layout.preferredWidth: 8; Layout.minimumWidth: 8 }   // 间隔

            // 静音 + 音量
            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                text: root.player.muted || root.player.volume <= 0 ? "🔇" : "🔊"
                font.pixelSize: 15
                onClicked: root.player.setMuted(!root.player.muted)
            }
            Slider {
                id: volSlider
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 110
                Layout.minimumWidth: 110
                from: 0; to: 130
                value: root.player.volume
                onMoved: root.player.setVolume(value)
            }
            Text {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 34
                Layout.minimumWidth: 34
                color: "#8a8a95"
                font.pixelSize: 12
                text: Math.round(root.player.volume) + "%"
            }

            // 截图：两种模式
            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                text: "截图"
                ToolTip.visible: hovered
                ToolTip.text: "纯画面（不含字幕）"
                onClicked: root.requestScreenshot(false)
            }
            ToolButton {
                Layout.alignment: Qt.AlignVCenter
                Layout.minimumWidth: implicitWidth
                text: "截图+字幕"
                ToolTip.visible: hovered
                ToolTip.text: "含字幕与 OSD"
                onClicked: root.requestScreenshot(true)
            }
        }
    }
}
