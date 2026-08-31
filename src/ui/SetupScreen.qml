import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// Screen 2 of STRIP-SPEC.md section 9.5: what the bridge is connected to, and how.
//
// Repainted from three paragraphs of prose into a readout. This is the screen a rider
// opens when the ride screen has said something is wrong and they want to know what,
// so it answers in facts and in the order they would be asked for.
Item {
    id: setup

    readonly property real unit: window.unit
    readonly property var theme: window.theme
    readonly property string link: rideState.trainerState

    function linkWord() {
        switch (link) {
        case "live":        return qsTr("live")
        case "stale":       return qsTr("no data")
        case "lost":        return qsTr("lost")
        case "gaveup":      return qsTr("gave up")
        case "connecting":  return qsTr("connecting")
        case "discovering": return qsTr("reading services")
        default:            return qsTr("searching")
        }
    }

    Flickable {
        anchors.fill: parent
        // Off column.height, not its implicitHeight: the column's width comes from the
        // Flickable, so sizing the Flickable off the column's implicit size closes a loop.
        contentHeight: column.height + unit * 2
        clip: true

        ColumnLayout {
            id: column
            width: setup.width - unit * 2.7
            x: unit * 1.33
            spacing: 0

            SettingsGroup { title: qsTr("Trainer") }

            ReadoutRow {
                label: qsTr("Name")
                value: rideState.trainerName.length > 0 ? rideState.trainerName : qsTr("none yet")
                emphasis: rideState.trainerName.length > 0
            }
            ReadoutRow {
                label: qsTr("Link")
                value: rideState.transport.length > 0
                       ? qsTr("BLE · %1").arg(setup.linkWord()) : setup.linkWord()
                tint: theme.stateColor(setup.link)
            }
            ReadoutRow {
                label: qsTr("Last data")
                value: rideState.dataAgeSeconds < 0 ? qsTr("never")
                                                    : qsTr("%1 s ago").arg(rideState.dataAgeSeconds)
            }
            ReadoutRow {
                label: qsTr("Resistance range")
                value: rideState.resistanceLevels > 0
                       ? qsTr("1 – %1").arg(rideState.resistanceLevels) : qsTr("unknown")
            }
            ReadoutRow {
                label: qsTr("Battery")
                value: rideState.batteryLevel >= 0 ? rideState.batteryLevel + "%"
                                                   : qsTr("not reported")
                tint: rideState.batteryLevel >= 0 ? theme.batteryColor(rideState.batteryLevel)
                                                  : theme.muted
            }
            ReadoutRow {
                visible: setup.link === "lost" || setup.link === "gaveup"
                label: qsTr("Reconnect")
                value: setup.link === "gaveup"
                       ? qsTr("stopped after 5 min")
                       : qsTr("in %1 s").arg(Math.max(0, rideState.retrySeconds))
                tint: theme.fault
            }

            SettingsGroup { title: qsTr("Training app") }

            ReadoutRow {
                label: qsTr("Transport")
                value: rideState.transport.length > 0 ? rideState.transport : qsTr("none")
                emphasis: rideState.transport.length > 0
            }
            ReadoutRow {
                label: qsTr("State")
                value: {
                    switch (rideState.appState) {
                    case "live":  return qsTr("live")
                    case "stale": return qsTr("no frames just now")
                    case "past":  return qsTr("client disconnected")
                    default:      return qsTr("nothing connected yet")
                    }
                }
                tint: theme.stateColor(rideState.appState)
            }
            ReadoutRow {
                label: qsTr("DIRCON id")
                value: String(qzSettings.dircon_id)
            }

            SettingsGroup { title: qsTr("Virtual device") }

            ReadoutRow {
                label: qsTr("Wi‑Fi (DIRCON)")
                value: qzSettings.dircon_yes ? qsTr("on") : qsTr("off")
                tint: qzSettings.dircon_yes ? theme.live : theme.dim
            }

            // Section 3.2.1: on Windows the BLE peripheral role is not available at all.
            // The code is compiled in, but QLowEnergyController::createPeripheral() does
            // nothing here, so an enabled-looking toggle would be a lie. Say why instead.
            ReadoutRow {
                label: qsTr("Bluetooth")
                value: OS_VERSION === "Other" ? qsTr("unavailable") : qsTr("available")
                tint: OS_VERSION === "Other" ? theme.accent : theme.live
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: unit
                Layout.bottomMargin: unit
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit
                color: theme.dim
                text: OS_VERSION === "Other"
                      ? qsTr("Qt cannot advertise a BLE peripheral on Windows, so training apps "
                             + "reach QZ over Wi‑Fi instead.")
                      : qsTr("Training apps can reach QZ over Bluetooth or Wi‑Fi.")
            }
        }
    }
}
