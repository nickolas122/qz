import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// Screen 2 of STRIP-SPEC.md section 9.5: what the bridge is connected to, and how.
Item {
    id: setup

    readonly property real unit: window.unit

    Flickable {
        anchors.fill: parent
        // Off column.height, not its implicitHeight: the column's width comes from the
        // Flickable, so sizing the Flickable off the column's implicit size closes a loop.
        contentHeight: column.height + unit * 2
        clip: true

        ColumnLayout {
            id: column
            width: setup.width - unit * 2
            x: unit
            y: unit
            spacing: unit

            Label {
                text: qsTr("Trainer")
                font.pixelSize: unit * 1.8
                font.bold: true
                color: Material.foreground
            }

            Label {
                Layout.fillWidth: true
                Layout.rightMargin: unit * 2
                wrapMode: Text.WordWrap
                font.pixelSize: unit * 1.3
                color: Material.foreground
                text: rideState.trainerConnected
                      ? qsTr("Connected: %1").arg(rideState.trainerName.length > 0
                                                  ? rideState.trainerName : qsTr("unnamed"))
                      : qsTr("Searching…")
            }

            Label {
                Layout.topMargin: unit
                text: qsTr("Training app")
                font.pixelSize: unit * 1.8
                font.bold: true
                color: Material.foreground
            }

            Label {
                Layout.fillWidth: true
                Layout.rightMargin: unit * 2
                wrapMode: Text.WordWrap
                font.pixelSize: unit * 1.3
                color: Material.foreground
                text: rideState.appConnected
                      ? qsTr("Connected over %1").arg(rideState.transport)
                      : qsTr("Nothing connected yet")
            }

            Label {
                Layout.topMargin: unit
                text: qsTr("Virtual device")
                font.pixelSize: unit * 1.8
                font.bold: true
                color: Material.foreground
            }

            // Section 3.2.1: on Windows the BLE peripheral role is not available at all.
            // The code is compiled in, but QLowEnergyController::createPeripheral() does
            // nothing here, so an enabled-looking toggle would be a lie. Say why instead.
            Label {
                Layout.fillWidth: true
                Layout.rightMargin: unit * 2
                wrapMode: Text.WordWrap
                font.pixelSize: unit * 1.3
                color: Material.foreground
                text: OS_VERSION === "Other"
                      ? qsTr("Bluetooth is unavailable on Windows: Qt cannot advertise a BLE "
                             + "peripheral on this platform. Training apps reach QZ over "
                             + "Wi-Fi (DIRCON) instead.")
                      : qsTr("Training apps can reach QZ over Bluetooth or Wi-Fi (DIRCON).")
            }
        }
    }
}
