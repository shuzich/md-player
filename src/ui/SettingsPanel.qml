import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// 设置页（M1-PLAN T7 第一项 / D-070）。五项：硬解 / 独占输出 / 直通 /
// 截图目录 / SACD 增益。全部运行时下发，configs/mpv-baseline.conf 不动（D-013）。
//
// 三条 UI 硬约束，都是踩过的坑：
//   · Popup 一律 `focus: false`（深坑 #7 / D-018），Esc 由窗口级 Shortcut 承担；
//   · 页内没有 ComboBox——真要加，`popup.closePolicy` 只能留 CloseOnPressOutside（D-062）；
//   · 截图目录用系统文件夹选择器，**不放可输入文本框**：文本框要键盘焦点，
//     直接撞 D-032，为一个路径不值得。
Popup {
    id: root

    required property var player

    modal: false
    dim: true
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: 0

    signal toast(string text)

    width: Math.min(520, parent ? parent.width - 80 : 520)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(24, (parent.height - height) / 2 - 40) : 24

    background: Rectangle {
        color: "#f2151519"
        radius: 10
        border { width: 1; color: "#3a3a44" }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Text {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 6
            text: qsTr("设置")
            color: "#e8e8ee"
            font { pixelSize: 17; bold: true }
        }

        // ── 五项 ───────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
            spacing: 14

            component Row2: RowLayout {
                property alias label: t.text
                property alias hint: h.text
                default property alias extra: holder.data
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { id: t; color: "#e8e8ee"; font.pixelSize: 14 }
                    Text {
                        id: h
                        color: "#9a9aa6"; font.pixelSize: 11
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                    }
                }
                Item { id: holder; Layout.preferredWidth: childrenRect.width; Layout.preferredHeight: childrenRect.height }
            }

            Row2 {
                label: qsTr("硬件解码")
                hint: qsTr("关掉后强制软解，排查花屏/黑屏时用")
                Switch {
                    focusPolicy: Qt.NoFocus
                    checked: root.player.hwdecEnabled
                    onToggled: root.player.setHwdecEnabled(checked)
                }
            }
            Row2 {
                label: qsTr("独占音频输出")
                hint: qsTr("独占声卡，其它应用暂时发不出声")
                Switch {
                    focusPolicy: Qt.NoFocus
                    checked: root.player.audioExclusive
                    onToggled: root.player.setAudioExclusive(checked)
                }
            }
            Row2 {
                label: qsTr("音频直通（比特流）")
                hint: qsTr("把 AC3 / DTS / TrueHD 原样送给功放解码，需要 HDMI 或光纤")
                Switch {
                    focusPolicy: Qt.NoFocus
                    checked: root.player.audioPassthrough
                    onToggled: root.player.setAudioPassthrough(checked)
                }
            }
            Row2 {
                label: qsTr("SACD 增益 +6dB")
                hint: qsTr("补足 DSD 解码后比 foobar2000 低的约 6dB；快捷键 G 同样可切")
                // G 快捷键保留（D-046 定的正式入口是设置页，但 D-059 第 1 项的人工
                // 验收还要用它当仪器）。两者读写同一个属性，天然双向同步（D-070）。
                Switch {
                    focusPolicy: Qt.NoFocus
                    checked: root.player.sacdGain
                    onToggled: root.player.setSacdGain(checked)
                }
            }
            Row2 {
                label: qsTr("截图目录")
                hint: root.player.screenshotDirectory
                Button {
                    focusPolicy: Qt.NoFocus
                    text: qsTr("选择…")
                    onClicked: dirPicker.open()
                }
            }
        }

        Item { Layout.preferredHeight: 20 }
    }

    FolderDialog {
        id: dirPicker
        title: qsTr("选择截图目录")
        currentFolder: "file://" + root.player.screenshotDirectory
        onAccepted: root.player.setScreenshotDirectory(selectedFolder)
    }
}
