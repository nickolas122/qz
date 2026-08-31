import QtQuick 2.12
import QtQuick.Controls 2.12

// Short messages from the bridge - battery levels, "restart to apply", "another device
// has the bike". They arrive on QzNotify, which is where the device drivers post now
// that they no longer reach into homeform for it.
//
// Written rather than reusing the old tree's Toast.qml/ToastManager.qml pair: 40 lines
// against their 156, and no dependency on AndroidStatusBar, whose height is 0 for the
// life of the process on Android (see TODO.md). That pair went with group F in 7c-2b.
Item {
    id: toastArea

    readonly property real unit: window.unit
    readonly property var theme: window.theme
    readonly property int visibleLimit: 3

    anchors.fill: parent
    // The ride screen underneath has to stay shiftable while a toast is up.
    z: 100

    ListModel { id: messages }

    Connections {
        target: qzNotify
        function onToastRequested(message) {
            messages.append({ text: message })
            // Only the count is capped, not the lifetime: an old message that is still
            // inside its three seconds is dropped when a fourth arrives, which is the
            // right way round when the newest one is the one being read.
            while (messages.count > toastArea.visibleLimit)
                messages.remove(0)
            expiry.restart()
        }
    }

    // One timer for the queue rather than one per message: they are appended in order,
    // so the oldest is always the next to go, and a single tick drains it.
    Timer {
        id: expiry
        interval: 3000
        repeat: true
        running: messages.count > 0
        onTriggered: {
            if (messages.count > 0)
                messages.remove(0)
            if (messages.count === 0)
                stop()
        }
    }

    Column {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: unit * 2
        spacing: unit / 2

        Repeater {
            model: messages

            Rectangle {
                width: Math.min(toastArea.width - unit * 2, label.implicitWidth + unit * 2)
                height: label.implicitHeight + unit
                radius: unit / 2
                color: theme.raised
                border.color: theme.line
                border.width: 1
                opacity: 0.95

                Label {
                    id: label
                    anchors.centerIn: parent
                    width: toastArea.width - unit * 4
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    font.family: theme.fontUi
                    font.pixelSize: unit * 1.08
                    color: theme.ink
                    text: model.text
                }
            }
        }
    }
}
