import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// Screen 1 of STRIP-SPEC.md section 9.4. The gear dominates because shifting is the only
// thing the rider does through QZ mid-ride; everything else on this screen is
// confirmation, and is sized accordingly.
Item {
    id: ride

    readonly property real unit: window.unit

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: unit
        spacing: unit

        // Status pills. They name the transport, not just "connected", because BLE and
        // DIRCON fail in different ways and which one is carrying the ride is the first
        // thing worth knowing when it stops.
        RowLayout {
            Layout.fillWidth: true
            spacing: unit

            StatusPill {
                id: trainerPill
                Layout.fillWidth: true
                on: rideState.trainerConnected
                label: rideState.trainerConnected
                       ? (rideState.trainerName.length > 0 ? rideState.trainerName : qsTr("Trainer"))
                       : qsTr("No trainer")
            }

            StatusPill {
                id: appPill
                Layout.fillWidth: true
                on: rideState.appConnected
                label: rideState.appConnected
                       ? ((rideState.appName.length > 0 ? rideState.appName : qsTr("Training app"))
                          + " (" + rideState.transport + ")")
                       : qsTr("No training app")
            }
        }

        Item { Layout.fillHeight: true }

        // The gear, and the two controls that change it.
        RowLayout {
            Layout.fillWidth: true
            spacing: unit

            GearButton {
                text: "−"
                enabled: rideState.trainerConnected
                onClicked: rideState.gearDown()
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: rideState.trainerConnected ? rideState.gear : "–"
                    font.pixelSize: unit * 9
                    font.bold: true
                    color: Material.foreground
                }

                // The absolute resistance the gear resolves to, matching the
                // neutral-gear table semantics in bike.cpp.
                Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("res %1").arg(Math.round(rideState.resistance))
                    font.pixelSize: unit * 1.6
                    opacity: 0.7
                    color: Material.foreground
                }
            }

            GearButton {
                text: "+"
                enabled: rideState.trainerConnected
                onClicked: rideState.gearUp()
            }
        }

        Item { Layout.fillHeight: true }

        // One secondary line. Confirmation, not a dashboard.
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: unit * 1.7
            color: Material.foreground
            text: Math.round(rideState.power) + " W · "
                  + Math.round(rideState.cadence) + " rpm · "
                  + rideState.speed.toFixed(1) + " km/h · "
                  + Math.round(rideState.heartRate) + " ♥"
        }

        // ERG changes how the bike behaves mid-effort, so it must not be reachable by
        // accident: this one takes a press and hold, and says so.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: unit / 3

            Button {
                id: ergButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: unit * 4
                Layout.preferredWidth: unit * 14
                enabled: rideState.trainerConnected
                highlighted: rideState.ergMode
                text: qsTr("ERG  %1").arg(rideState.ergMode ? qsTr("ON") : qsTr("OFF"))
                font.pixelSize: unit * 1.8

                // A plain click deliberately does nothing.
                onClicked: ergHint.visible = true

                MouseArea {
                    anchors.fill: parent
                    pressAndHoldInterval: 600
                    onPressAndHold: {
                        ergHint.visible = false
                        rideState.toggleErg()
                    }
                    onClicked: ergHint.visible = true
                }
            }

            Label {
                id: ergHint
                visible: false
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: qsTr("Hold to change ERG")
                font.pixelSize: unit * 1.2
                opacity: 0.7
                color: Material.foreground
            }
        }
    }
}
