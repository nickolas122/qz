import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// The screen gamepadcontroller has been waiting for. buttonNames() carries a comment
// saying it returns the names "in a stable order for the settings UI"; that UI was never
// built, so until now the only way to rebind was to edit gamepad_gear_up by hand.
//
// It does not ask the rider to type "rt". Sixteen names in a list assumes they know
// which physical trigger their pad calls that, on a pad that may be mounted upside down
// on the bars. Pressing it removes the question.
Rectangle {
    id: screen

    readonly property real unit: window.unit
    readonly property var theme: window.theme
    readonly property string capturing: gamepad.capturing
    readonly property string remapping: gamepad.remapping
    /**
     * Whether the drawn pad is a control rather than a readout.
     *
     * Two different questions get asked with the same gesture on this screen, and they must not
     * be asked at once: "which button drives shifting" (the + chips, below) and "which thing on
     * your pad is this button" (this mode). A rider who has to think about which one a press
     * will answer has been given a worse screen than the hand-edited setting.
     */
    property bool layoutMode: false

    color: theme.ground

    // Swallows clicks so the pages underneath cannot be operated through the overlay.
    MouseArea { anchors.fill: parent }

    function actionLabel(action) {
        switch (action) {
        case "gearUp":   return qsTr("Shift up")
        case "gearDown": return qsTr("Shift down")
        case "erg":      return qsTr("ERG mode")
        default:         return action
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // -- header ----------------------------------------------------------
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: unit * 4.4

            // Item around the Row for the same reason as the tab strip in Main.qml: a
            // Row manages its children's x, so a MouseArea filling one is refused.
            Item {
                anchors.left: parent.left
                anchors.leftMargin: unit * 1.5
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: backRow.implicitWidth
                implicitHeight: backRow.implicitHeight

                Row {
                    id: backRow
                    spacing: unit

                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "‹"
                        font.family: theme.fontDisplay
                        font.pixelSize: unit * 1.8
                        color: theme.muted
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Settings")
                        font.family: theme.fontUi
                        font.pixelSize: unit * 0.92
                        font.weight: Font.DemiBold
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: unit * 0.15
                        color: theme.dim
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -unit
                    onClicked: {
                        gamepad.cancelCapture()
                        screen.layoutMode = false
                        window.gamepadOpen = false
                    }
                }
            }

            Label {
                anchors.right: parent.right
                anchors.rightMargin: unit * 1.5
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Gamepad")
                font.family: theme.fontDisplay
                font.pixelSize: unit * 1.6
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: unit * 0.08
                color: theme.ink
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: theme.lineSoft
            }
        }

        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: column.height + unit * 2
            clip: true

            ColumnLayout {
                id: column
                width: screen.width - unit * 2.7
                x: unit * 1.33
                y: unit * 1.33
                spacing: unit

                // -- pad status ----------------------------------------------
                StatusChip {
                    Layout.fillWidth: true

                    readonly property bool listening: screen.capturing.length > 0 || screen.remapping.length > 0

                    state: !gamepad.available ? "idle"
                                              : (listening ? "connecting"
                                                           : (gamepad.padConnected ? "live" : "idle"))
                    headline: {
                        if (!gamepad.available)
                            return qsTr("Gamepad not supported here")
                        if (screen.remapping.length > 0)
                            return qsTr("Press the button your pad calls %1").arg(screen.remapping.toUpperCase())
                        if (screen.capturing.length > 0)
                            return qsTr("Press a button for %1").arg(screen.actionLabel(screen.capturing))
                        return gamepad.padConnected ? qsTr("Controller connected")
                                                    : qsTr("No pad detected")
                    }
                    detail: {
                        if (!gamepad.available)
                            return qsTr("No gamepad backend on this platform")
                        if (screen.remapping.length > 0) {
                            // What the pad actually sent, whether or not it has a name yet. On a
                            // press QZ has no name for, this line is the only proof it arrived.
                            return gamepad.pressedInputs.length > 0
                                   ? qsTr("Got %1 · release to name it").arg(gamepad.pressedInputs.join(", "))
                                   : qsTr("Listening · press it on the pad")
                        }
                        if (screen.capturing.length > 0)
                            return qsTr("Listening · release to bind")
                        if (!gamepad.padConnected)
                            return qsTr("Looking for a controller")
                        // XInput knows a slot but not a name; the other two backends know a
                        // name but have no slot. Each says the thing it actually knows.
                        if (gamepad.backend === "XInput")
                            return qsTr("XInput slot %1").arg(gamepad.padSlot + 1)
                        return gamepad.padName.length > 0 ? gamepad.padName : gamepad.backend
                    }
                    progress: listening ? 1 : -1
                    action: listening ? qsTr("Cancel") : ""
                    onActionClicked: gamepad.cancelCapture()
                }

                // -- the pad itself ------------------------------------------
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: padDiagram.height + unit * 2
                    visible: gamepad.available

                    readonly property bool listening: screen.capturing.length > 0 || screen.remapping.length > 0

                    color: listening ? theme.workLo : theme.surface
                    border.color: listening ? theme.work : theme.line
                    border.width: 1
                    radius: theme.radius

                    PadDiagram {
                        id: padDiagram
                        anchors.centerIn: parent
                        width: parent.width - unit * 2
                        boundButtons: gamepad.gearUpButtons
                                      .concat(gamepad.gearDownButtons)
                                      .concat(gamepad.ergButtons)
                        pressedButtons: gamepad.pressedButtons
                        selectable: screen.layoutMode && gamepad.remappable
                        highlight: screen.remapping
                        onButtonClicked: function(name) { gamepad.beginRemap(name) }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: noPadText.height + unit * 3
                    visible: gamepad.available && !gamepad.padConnected && screen.capturing.length === 0
                    color: "transparent"
                    border.color: theme.line
                    border.width: 1
                    radius: theme.radius

                    Label {
                        id: noPadText
                        anchors.centerIn: parent
                        width: parent.width - unit * 3
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Turn a controller on, or plug one in. The bindings below are kept either way.")
                        font.family: theme.fontUi
                        font.pixelSize: unit * 1.08
                        color: theme.muted
                    }
                }

                // -- pad layout ------------------------------------------------
                // Only for HID pads: XInput and Android are told what a pad calls its buttons,
                // so there is nothing there for a rider to correct. See gamepadcontroller's
                // remappable().
                SettingsGroup {
                    title: qsTr("Pad layout")
                    visible: gamepad.remappable
                }

                SettingsSwitch {
                    visible: gamepad.remappable
                    label: qsTr("Rename the buttons")
                    note: qsTr("Tap one on the drawing, then press it on your pad")
                    checked: screen.layoutMode
                    onToggled: {
                        screen.layoutMode = checked
                        // Leaving the mode has to take the half-finished question with it, or
                        // the next press would answer a question the rider can no longer see.
                        if (!checked)
                            gamepad.cancelCapture()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: gamepad.remappable && screen.layoutMode
                    wrapMode: Text.WordWrap
                    font.family: theme.fontUi
                    font.pixelSize: unit
                    color: theme.dim
                    text: qsTr("This pad reports numbered buttons rather than names, so QZ starts "
                               + "from a guess. Where the guess is wrong, tap the button on the "
                               + "drawing and press the one you meant. Naming a button takes it "
                               + "away from wherever it was.")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.minTouch
                    visible: gamepad.remappable && screen.layoutMode
                    color: "transparent"
                    border.color: theme.line
                    border.width: 1
                    radius: theme.radius

                    Label {
                        anchors.centerIn: parent
                        text: qsTr("Back to the guess")
                        font.family: theme.fontUi
                        font.pixelSize: unit
                        color: theme.muted
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            gamepad.cancelCapture()
                            gamepad.resetMap()
                        }
                    }
                }

                // -- actions ---------------------------------------------------
                SettingsGroup { title: qsTr("Actions") }

                Repeater {
                    model: [
                        { action: "gearUp",   note: qsTr("repeats") },
                        { action: "gearDown", note: qsTr("repeats") },
                        { action: "erg",      note: qsTr("single press") }
                    ]

                    RowLayout {
                        id: row
                        Layout.fillWidth: true
                        spacing: unit
                        // While one action is being bound the others step back, so it
                        // is obvious which row the next press lands on.
                        opacity: screen.capturing.length === 0 || screen.capturing === row.actionName
                                 ? 1.0 : 0.4

                        // Lifted out of modelData here because the inner Repeater below
                        // shadows modelData with the button name.
                        readonly property string actionName: modelData.action
                        readonly property var buttons: actionName === "gearUp" ? gamepad.gearUpButtons
                                    : (actionName === "gearDown" ? gamepad.gearDownButtons
                                                                 : gamepad.ergButtons)

                        ColumnLayout {
                            Layout.preferredWidth: unit * 9.7
                            spacing: 0
                            Label {
                                text: screen.actionLabel(row.actionName)
                                font.family: theme.fontUi
                                font.pixelSize: unit * 1.12
                                font.weight: Font.DemiBold
                                color: screen.capturing === row.actionName ? theme.work : theme.ink
                            }
                            Label {
                                text: modelData.note
                                font.family: theme.fontUi
                                font.pixelSize: unit * 0.83
                                font.capitalization: Font.AllUppercase
                                font.letterSpacing: unit * 0.1
                                color: theme.dim
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: unit / 2

                            Repeater {
                                model: row.buttons

                                // A bound button, with the way to remove it attached.
                                // Clearing the last one disables the action, which is
                                // what an empty setting has always meant to
                                // buttonMask() - the screen only makes it reachable.
                                Rectangle {
                                    width: keyRow.width + unit * 1.4
                                    height: theme.minTouch
                                    radius: theme.radius
                                    color: "#241C10"
                                    border.color: Qt.rgba(0.95, 0.64, 0.24, 0.45)
                                    border.width: 1

                                    Row {
                                        id: keyRow
                                        anchors.centerIn: parent
                                        spacing: unit * 0.6

                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.toUpperCase()
                                            font.family: theme.fontMono
                                            font.pixelSize: unit
                                            color: theme.accent
                                        }
                                        Label {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "×"
                                            font.family: theme.fontUi
                                            font.pixelSize: unit * 1.2
                                            color: theme.ghost
                                        }
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        onClicked: gamepad.unbind(row.actionName, modelData)
                                    }
                                }
                            }

                            // Add another. The setting has always been a comma-separated
                            // list because the shifter is meant to be complete on both
                            // shoulders whichever way the pad is mounted.
                            Rectangle {
                                width: theme.minTouch
                                height: theme.minTouch
                                radius: theme.radius
                                color: "transparent"
                                border.color: theme.line
                                border.width: 1

                                Label {
                                    anchors.centerIn: parent
                                    text: "+"
                                    font.family: theme.fontDisplay
                                    font.pixelSize: unit * 1.5
                                    color: theme.dim
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: gamepad.available
                                    onClicked: gamepad.beginCapture(row.actionName)
                                }
                            }
                        }
                    }
                }

                // -- repeat timings --------------------------------------------
                SettingsGroup { title: qsTr("Hold to keep shifting") }

                SettingsNumber {
                    label: qsTr("Wait before repeating (ms)")
                    value: gamepad.repeatDelay
                    onCommitted: gamepad.setRepeatDelay(Math.round(newValue))
                }

                SettingsNumber {
                    label: qsTr("Then one shift every (ms)")
                    value: gamepad.repeatRate
                    onCommitted: gamepad.setRepeatRate(Math.round(newValue))
                }

                Label {
                    Layout.fillWidth: true
                    Layout.bottomMargin: unit
                    wrapMode: Text.WordWrap
                    font.family: theme.fontUi
                    font.pixelSize: unit
                    color: theme.dim
                    text: OS_VERSION === "Android"
                          ? qsTr("Any pad Android recognises works, in whatever mode it pairs in. "
                                 + "Android gives input to the app on screen, though, so shifting "
                                 + "from the pad works while QZ is in front - not while the training "
                                 + "app is.")
                          : qsTr("Xbox pads are read through XInput, wired or Bluetooth, as is any "
                                 + "pad in X-input mode, and their buttons carry their own names. A "
                                 + "pad with no X-input mode - an 8BitDo in D-input, a DualSense, a "
                                 + "Switch Pro - is read as a plain HID pad instead, which reports "
                                 + "numbers rather than names. QZ guesses; Pad layout above is where "
                                 + "you correct it.")
                }
            }
        }
    }
}
