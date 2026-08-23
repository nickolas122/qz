import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3
import Qt.labs.settings 1.0

// Screen 3 of STRIP-SPEC.md section 9.6: one scrollable page, four groups, organised
// around what a rider changes rather than around vendors. No nesting and no accordions.
//
// The groups are the shape; the controls inside them are not final. Phase 6 is what
// decides which of the surviving keys belong on this page, and it cannot run until the
// tile system and the shortcuts have gone with group F. Until then this holds the ones
// a rider actually reaches for mid-setup.
Item {
    id: settingsScreen

    readonly property real unit: window.unit

    Settings {
        id: qzSettings
        property int bike_resistance_offset: 4
        property double bike_resistance_gain_f: 1.0
        property bool gears_zwift_ratio: false
        property double gear_crankset_size: 50.0
        property double gear_cog_size: 33.0
        property bool dircon_yes: true
        property bool virtual_device_bluetooth: true
        property bool rouvy: false
        property bool ui_next: true
    }

    Flickable {
        anchors.fill: parent
        // Off column.height, not its implicitHeight: the column's width comes from the
        // Flickable, so sizing the Flickable off the column's implicit size closes a loop.
        contentHeight: column.height + unit * 2
        clip: true

        ColumnLayout {
            id: column
            width: settingsScreen.width - unit * 2
            x: unit
            y: unit
            spacing: unit / 2

            SettingsGroup { title: qsTr("Bike") }

            SettingsNumber {
                label: qsTr("Resistance offset")
                value: qzSettings.bike_resistance_offset
                onCommitted: qzSettings.bike_resistance_offset = Math.round(newValue)
            }

            SettingsNumber {
                label: qsTr("Resistance gain")
                value: qzSettings.bike_resistance_gain_f
                decimals: 2
                onCommitted: qzSettings.bike_resistance_gain_f = newValue
            }

            SettingsGroup { title: qsTr("Gears") }

            SettingsSwitch {
                label: qsTr("Zwift gear ratios")
                checked: qzSettings.gears_zwift_ratio
                onToggled: qzSettings.gears_zwift_ratio = checked
            }

            SettingsNumber {
                label: qsTr("Crankset teeth")
                value: qzSettings.gear_crankset_size
                decimals: 1
                onCommitted: qzSettings.gear_crankset_size = newValue
            }

            SettingsNumber {
                label: qsTr("Cog teeth")
                value: qzSettings.gear_cog_size
                decimals: 1
                onCommitted: qzSettings.gear_cog_size = newValue
            }

            SettingsGroup { title: qsTr("Training-app connection") }

            SettingsSwitch {
                label: qsTr("Wi-Fi (DIRCON)")
                checked: qzSettings.dircon_yes
                onToggled: qzSettings.dircon_yes = checked
            }

            SettingsSwitch {
                label: qsTr("Bluetooth virtual bike")
                // Section 3.2.1 again: the switch is honest about the platform rather
                // than pretending the peripheral role exists on Windows.
                enabled: OS_VERSION !== "Other"
                checked: qzSettings.virtual_device_bluetooth && OS_VERSION !== "Other"
                onToggled: qzSettings.virtual_device_bluetooth = checked
            }

            SettingsSwitch {
                label: qsTr("Rouvy compatibility")
                checked: qzSettings.rouvy
                onToggled: qzSettings.rouvy = checked
            }

            SettingsGroup { title: qsTr("Display") }

            // The way back. Section 9.9 promises a bad ride costs a toggle rather than a
            // rebuild, and since 7b flipped the default this screen is the only place
            // that toggle is reachable from. It goes in 7c, with the tree it returns to.
            SettingsSwitch {
                label: qsTr("Use the old UI")
                checked: !qzSettings.ui_next
                onToggled: qzSettings.ui_next = !checked
            }

            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: unit
                wrapMode: Text.WordWrap
                font.pixelSize: unit * 1.2
                opacity: 0.7
                color: Material.foreground
                text: qsTr("Changes to the connection settings, and to which UI loads, take effect when QZ restarts.")
            }
        }
    }
}
