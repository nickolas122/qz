import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// Label on the left, a stepper on the right. The value still commits on return or focus
// loss, so a rider who wants to type a number can; the − and + are there because most
// of these are nudged by one, and a keyboard on a phone next to a bike is a poor way to
// change a resistance offset from 4 to 5.
Item {
    id: row
    property string label: ""
    property string note: ""
    property double value: 0
    property int decimals: 0
    property double step: 1
    signal committed(double newValue)

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    Layout.fillWidth: true
    implicitHeight: Math.max(unit * 3.9, labelCol.implicitHeight + unit * 1.4)

    // Held, the arms keep stepping - same reason the gear buttons do, and the same two
    // numbers. MouseArea has no autoRepeat of its own, so this is it: one shot at press,
    // then a delay, then a rate.
    property int repeatDir: 0

    Timer {
        id: repeatDelay
        interval: 500
        onTriggered: repeatRate.start()
    }

    Timer {
        id: repeatRate
        interval: 200
        repeat: true
        onTriggered: row.committed(row.value + row.repeatDir * row.step)
    }

    function stepBy(dir) {
        row.repeatDir = dir
        row.committed(row.value + dir * row.step)
        repeatDelay.restart()
    }

    function stopStepping() {
        repeatDelay.stop()
        repeatRate.stop()
        row.repeatDir = 0
    }

    RowLayout {
        anchors.fill: parent
        anchors.rightMargin: unit / 2
        spacing: unit

        ColumnLayout {
            id: labelCol
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: row.label
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit * 1.12
                color: theme.ink
            }
            Label {
                Layout.fillWidth: true
                visible: row.note.length > 0
                text: row.note
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit * 0.92
                color: theme.dim
            }
        }

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            implicitWidth: minus.width + field.width + plus.width
            implicitHeight: theme.minTouch
            color: "transparent"
            border.color: theme.line
            border.width: 1
            radius: 3

            Row {
                anchors.fill: parent

                // Both arms are full-height so the whole control is one 44px target
                // split in three, rather than three small ones with gaps between.
                Rectangle {
                    id: minus
                    width: unit * 2.7
                    height: parent.height
                    color: minusArea.pressed ? theme.line : theme.raised
                    Label {
                        anchors.centerIn: parent
                        text: "−"
                        font.family: theme.fontDisplay
                        font.pixelSize: unit * 1.6
                        color: theme.muted
                    }
                    MouseArea {
                        id: minusArea
                        anchors.fill: parent
                        onPressed: row.stepBy(-1)
                        onReleased: row.stopStepping()
                        onCanceled: row.stopStepping()
                    }
                }

                TextField {
                    id: field
                    width: unit * 5.4
                    height: parent.height
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    font.family: theme.fontMono
                    font.pixelSize: unit * 1.08
                    color: theme.ink
                    text: row.value.toFixed(row.decimals)
                    background: Rectangle { color: "transparent" }

                    function commit() {
                        var parsed = parseFloat(field.text)
                        if (!isNaN(parsed))
                            row.committed(parsed)
                        else
                            field.text = row.value.toFixed(row.decimals)
                    }

                    onAccepted: commit()
                    onActiveFocusChanged: if (!activeFocus) commit()
                }

                Rectangle {
                    id: plus
                    width: unit * 2.7
                    height: parent.height
                    color: plusArea.pressed ? theme.line : theme.raised
                    Label {
                        anchors.centerIn: parent
                        text: "+"
                        font.family: theme.fontDisplay
                        font.pixelSize: unit * 1.6
                        color: theme.muted
                    }
                    MouseArea {
                        id: plusArea
                        anchors.fill: parent
                        onPressed: row.stepBy(1)
                        onReleased: row.stopStepping()
                        onCanceled: row.stopStepping()
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.lineSoft
    }
}
