import QtQuick 2.12
import QtQuick.Controls 2.12

// The trainer and training-app status chips. Replaces StatusPill.qml, which was a dot
// and a label driven by a boolean - and the boolean was a latch that never went back
// down (TODO.md, "both status indicators are latches, not state").
//
// Two lines rather than one because the state and the reason are different facts: the
// headline names the thing, the sub-line says what it is doing and since when. Three
// visual registers answer the question the TODO asks the chip to answer - hollow ring
// for never-connected, filled colour for now, and a countdown bar for was-and-is-not.
Rectangle {
    id: chip

    // One of RideState's state vocabularies. Drives every colour here.
    property string state: "idle"
    property string headline: ""
    property string detail: ""
    /** 0..1, or -1 for no bar. The countdown between reconnect attempts. */
    property real progress: -1
    /** Text of the trailing button, or empty for none. */
    property string action: ""
    /** -1 hides the battery glyph entirely. */
    property int battery: -1

    signal actionClicked()

    readonly property real unit: window.unit
    readonly property var theme: window.theme
    readonly property color tint: theme.stateColor(state)
    // Only a fault pulses. A pulsing chip is a demand for attention, so the states that
    // are merely working get a steady dot and the ones that are fine get nothing.
    readonly property bool working: state === "searching" || state === "connecting"
                                    || state === "discovering" || state === "lost"

    implicitHeight: Math.max(unit * 4.5, theme.minTouch + unit / 2)
    color: theme.surface
    border.color: state === "lost" || state === "gaveup"
                  ? Qt.rgba(0.90, 0.34, 0.24, 0.4) : theme.line
    border.width: theme.hairline
    radius: theme.radius
    clip: true

    Row {
        anchors.fill: parent
        anchors.leftMargin: unit
        anchors.rightMargin: unit / 2
        spacing: unit * 0.9

        // The dot. Hollow for a state that has never been up, filled otherwise - which
        // is the whole never/now/was distinction in one 9px circle.
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: unit * 0.75
            height: width
            radius: width / 2
            color: chip.state === "idle" ? "transparent" : chip.tint
            border.color: chip.state === "idle" ? theme.ghost : "transparent"
            border.width: 1.5

            SequentialAnimation on opacity {
                running: chip.working
                loops: Animation.Infinite
                NumberAnimation { to: 0.35; duration: 800; easing.type: Easing.InOutQuad }
                NumberAnimation { to: 1.0; duration: 800; easing.type: Easing.InOutQuad }
            }
            // Without this an animation stopped mid-fade leaves the dot half-lit.
            onOpacityChanged: if (!chip.working && opacity !== 1.0) opacity = 1.0
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - unit * 2.6
                   - (batteryGlyph.visible ? batteryGlyph.width + pct.width + unit * 1.4 : 0)
                   - (actionButton.visible ? actionButton.width + unit : 0)
            spacing: unit / 5

            Label {
                width: parent.width
                text: chip.headline
                elide: Text.ElideRight
                font.family: theme.fontUi
                font.pixelSize: unit * 1.17
                font.weight: chip.state === "idle" || chip.state === "past"
                             ? Font.Medium : Font.DemiBold
                color: chip.state === "idle" || chip.state === "past" ? theme.muted : theme.ink
            }

            Label {
                width: parent.width
                text: chip.detail
                visible: text.length > 0
                elide: Text.ElideRight
                font.family: theme.fontUi
                font.pixelSize: unit * 0.83
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: unit * 0.12
                color: theme.dim
            }
        }

        BatteryGlyph {
            id: batteryGlyph
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.battery >= 0
            level: chip.battery
        }

        Label {
            id: pct
            anchors.verticalCenter: parent.verticalCenter
            visible: batteryGlyph.visible
            text: chip.battery + "%"
            font.family: theme.fontMono
            font.pixelSize: unit
            color: theme.batteryColor(chip.battery)
        }

        Button {
            id: actionButton
            anchors.verticalCenter: parent.verticalCenter
            visible: chip.action.length > 0
            text: chip.action
            implicitHeight: theme.minTouch
            font.family: theme.fontUi
            font.pixelSize: unit * 0.83
            font.weight: Font.Bold
            font.capitalization: Font.AllUppercase
            font.letterSpacing: unit * 0.11
            onClicked: chip.actionClicked()

            background: Rectangle {
                color: actionButton.down ? theme.line : theme.raised
                border.color: chip.state === "lost" || chip.state === "gaveup"
                              ? Qt.rgba(0.90, 0.34, 0.24, 0.5) : theme.line
                border.width: 1
                radius: 3
            }
            contentItem: Label {
                text: actionButton.text
                font: actionButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: chip.state === "lost" || chip.state === "gaveup" ? "#FFB09E" : theme.ink
                leftPadding: unit * 0.7
                rightPadding: unit * 0.7
            }
        }
    }

    // The countdown. A window onto reconnectTimer, which was already running with
    // correct backoff while the rider saw a frozen screen and restarted the app.
    Rectangle {
        visible: chip.progress >= 0
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        height: 2
        width: parent.width * Math.max(0, Math.min(1, chip.progress))
        color: chip.tint
        Behavior on width { NumberAnimation { duration: 400 } }
    }
}
