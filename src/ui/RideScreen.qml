import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// Screen 1 of STRIP-SPEC.md section 9.4, repainted to UI-INSTRUMENT-CLUSTER.md.
//
// The gear still dominates, because shifting is still the only thing the rider does
// through QZ mid-ride, and the two dimensions the spec fixed - an 84px thumb target and
// a 9-unit numeral - are unchanged. What is new is that the screen can now say the
// trainer is gone: the status pills carry state instead of a latched boolean, the
// metrics sentence became four chips that can each mark themselves stale, and a lost
// link gets a countdown and a way to override it.
Item {
    id: ride

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    readonly property string link: rideState.trainerState
    readonly property bool usable: link === "live" || link === "stale"
    readonly property bool metricsStale: link === "stale" || link === "lost" || link === "gaveup"
    readonly property int dataAge: rideState.dataAgeSeconds

    // How full the countdown bar is, between the last attempt and the next. The delay
    // doubles 1-2-4-8-16-30, so the bar is drawn against whichever step is running
    // rather than a fixed span - otherwise a 30-second wait would look like a 1-second
    // one that had stalled.
    readonly property real retryProgress: {
        if (link !== "lost" || rideState.retrySeconds < 0)
            return -1
        var step = 1
        while (step < rideState.retrySeconds && step < 30) step *= 2
        return 1 - (rideState.retrySeconds / Math.max(1, step))
    }

    function trainerHeadline() {
        if (link === "searching")
            return qsTr("Searching for trainer")
        return rideState.trainerName.length > 0 ? rideState.trainerName : qsTr("Trainer")
    }

    function trainerDetail() {
        switch (link) {
        case "searching":   return qsTr("Scanning")
        case "connecting":  return qsTr("Connecting")
        case "discovering": return qsTr("Reading services")
        case "live":        return qsTr("Live")
        case "stale":       return qsTr("No data for %1 s").arg(dataAge)
        case "lost":
            // Past the first minute the wait between attempts is longer than most
            // people will sit still for, so the chip leads with how long the bike has
            // been gone - the number that says whether the 5-minute ceiling is close.
            if (dataAge >= 60)
                return qsTr("Lost %1 min ago · retrying in %2 s").arg(Math.floor(dataAge / 60))
                                                                 .arg(Math.max(0, rideState.retrySeconds))
            return rideState.retrySeconds >= 0
                   ? qsTr("Lost · retrying in %1 s").arg(rideState.retrySeconds)
                   : qsTr("Lost · reconnecting")
        case "gaveup":      return qsTr("Gave up after 5 min")
        default:            return ""
        }
    }

    function appHeadline() {
        return rideState.appState === "live" || rideState.appState === "stale"
               ? qsTr("Training app") : qsTr("No training app")
    }

    function appDetail() {
        switch (rideState.appState) {
        case "live":  return qsTr("Live · %1").arg(rideState.transport)
        case "stale": return qsTr("No frames for a moment")
        // Quitting the training app is how rides end, so this is past tense and grey,
        // not an alarm. Only the trainer goes red.
        case "past":  return qsTr("%1 client left").arg(rideState.transport)
        default:      return qsTr("Waiting for a connection")
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: unit * 1.33
        spacing: unit

        StatusChip {
            Layout.fillWidth: true
            state: ride.link
            headline: ride.trainerHeadline()
            detail: ride.trainerDetail()
            progress: ride.retryProgress
            battery: rideState.batteryLevel
            action: ride.link === "lost" ? qsTr("Retry now")
                                         : (ride.link === "gaveup" ? qsTr("Search") : "")
            onActionClicked: rideState.retryNow()
        }

        StatusChip {
            Layout.fillWidth: true
            state: rideState.appState
            headline: ride.appHeadline()
            detail: ride.appDetail()
        }

        Item { Layout.fillHeight: true }

        // The gear, and the two controls that change it.
        RowLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignHCenter
            spacing: unit * 1.7

            GearButton {
                text: "−"
                enabled: ride.usable
                onClicked: rideState.gearDown()
            }

            Label {
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                Layout.preferredWidth: unit * 9.3
                text: ride.usable ? rideState.gear : "–"
                font.family: theme.fontDisplay
                // A dash set at the numeral's own size is a 100px bar, not a
                // placeholder. Only a digit earns the full size.
                font.pixelSize: ride.usable ? unit * 8.7 : unit * 4
                font.weight: Font.DemiBold
                color: ride.usable ? theme.accent : theme.ghost
            }

            GearButton {
                text: "+"
                enabled: ride.usable
                onClicked: rideState.gearUp()
            }
        }

        // The absolute resistance the gear resolves to, matching the neutral-gear table
        // semantics in bike.cpp, with a ladder for where it sits in the bike's range.
        // The range is maxResistance(), which was already virtual and already correct.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: unit / 2
            spacing: unit * 0.8

            // fillWidth plus horizontalAlignment, not Layout.alignment: the latter left
            // this sitting against the margin while everything around it centred.
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: {
                    if (ride.usable) {
                        return rideState.resistanceLevels > 0
                               ? qsTr("res %1 / %2").arg(Math.round(rideState.resistance))
                                                    .arg(rideState.resistanceLevels)
                               : qsTr("res %1").arg(Math.round(rideState.resistance))
                    }
                    if (ride.metricsStale)
                        return qsTr("res %1 · held").arg(Math.round(rideState.resistance))
                    // No bike, so no range to divide by. "res –– / 0" reads as a
                    // reading against a real ceiling of zero, which it is not.
                    return qsTr("res ––")
                }
                font.family: theme.fontMono
                font.pixelSize: unit * 0.92
                font.letterSpacing: unit * 0.17
                font.capitalization: Font.AllUppercase
                color: ride.usable ? theme.muted : theme.ghost
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: ladder.height
                visible: rideState.resistanceLevels > 0

                Row {
                    id: ladder
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: 2

                    Repeater {
                        model: Math.min(40, rideState.resistanceLevels)
                        Rectangle {
                            width: 5
                            height: unit * 0.83
                            radius: 1
                            color: index < Math.round(rideState.resistance)
                                   ? (ride.usable ? theme.accent : theme.ghost)
                                   : theme.line
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }

        // One row of chips. Confirmation, not a dashboard - section 7 deleted the
        // charts and none of them come back through a redesign.
        RowLayout {
            Layout.fillWidth: true
            spacing: unit * 0.58

            MetricChip {
                Layout.fillWidth: true
                key: qsTr("Power"); unitLabel: qsTr("w")
                age: ride.metricsStale ? ride.dataAge : -1
                value: ride.link === "searching" ? "" : String(Math.round(rideState.power))
            }
            MetricChip {
                Layout.fillWidth: true
                key: qsTr("Cad"); unitLabel: qsTr("rpm")
                age: ride.metricsStale ? ride.dataAge : -1
                value: ride.link === "searching" ? "" : String(Math.round(rideState.cadence))
            }
            MetricChip {
                Layout.fillWidth: true
                key: qsTr("Speed")
                unitLabel: qzSettings.miles_unit ? qsTr("mph") : qsTr("km/h")
                age: ride.metricsStale ? ride.dataAge : -1
                value: ride.link === "searching"
                       ? "" : (qzSettings.miles_unit ? (rideState.speed * 0.621371).toFixed(1)
                                                     : rideState.speed.toFixed(1))
            }
            MetricChip {
                Layout.fillWidth: true
                key: qsTr("HR"); unitLabel: qsTr("bpm")
                // Not fitted is not a fault. A bike with no strap paired gets a sunk
                // chip, not the dashed border that means "this stopped being true".
                absent: rideState.heartRate <= 0
                age: ride.metricsStale ? ride.dataAge : -1
                value: rideState.heartRate > 0 ? String(Math.round(rideState.heartRate)) : ""
            }
        }

        // ERG changes how the bike behaves mid-effort, so it must not be reachable by
        // accident: this one takes a press and hold, and says so.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: unit / 2
            spacing: unit / 3

            Button {
                id: ergButton
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredHeight: unit * 4.7
                Layout.preferredWidth: unit * 16.3
                enabled: ride.usable
                opacity: enabled ? 1.0 : 0.32

                background: Rectangle {
                    color: rideState.ergMode ? theme.accentLo : theme.surface
                    border.color: rideState.ergMode ? theme.accent : theme.line
                    border.width: 1
                    radius: theme.radius
                }

                contentItem: Row {
                    spacing: unit * 1.2
                    Label {
                        text: qsTr("ERG")
                        font.family: theme.fontDisplay
                        font.pixelSize: unit * 1.6
                        font.weight: Font.DemiBold
                        font.letterSpacing: unit * 0.2
                        color: rideState.ergMode ? theme.ink : theme.muted
                    }
                    Label {
                        text: rideState.ergMode ? qsTr("ON") : qsTr("OFF")
                        font.family: theme.fontDisplay
                        font.pixelSize: unit * 1.6
                        font.weight: Font.DemiBold
                        font.letterSpacing: unit * 0.1
                        color: rideState.ergMode ? theme.accent : theme.dim
                    }
                }

                // A plain click deliberately does nothing.
                onClicked: ergHint.showHint = true

                MouseArea {
                    anchors.fill: parent
                    pressAndHoldInterval: 600
                    onPressAndHold: {
                        ergHint.showHint = false
                        rideState.toggleErg()
                    }
                    onClicked: ergHint.showHint = true
                }
            }

            Label {
                id: ergHint
                property bool showHint: false
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: ride.usable
                      ? (showHint ? qsTr("Hold to change ERG") : qsTr("Hold to change"))
                      : qsTr("Needs the trainer")
                font.family: theme.fontUi
                font.pixelSize: unit * 0.83
                font.capitalization: Font.AllUppercase
                font.letterSpacing: unit * 0.12
                color: showHint && ride.usable ? theme.accent : theme.ghost
            }
        }
    }
}
