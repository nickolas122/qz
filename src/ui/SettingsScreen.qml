import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3
import Qt.labs.settings 1.0

// Screen 3 of STRIP-SPEC.md section 9.6: one scrollable page, organised around what a
// rider changes rather than around vendors. No nesting and no accordions.
//
// Section 9.6 named four groups and section 8 added Accessories as a fifth, on the
// grounds that forcing a fan into "Display" would be worse than admitting the group.
// Phase 6 kept both decisions and added nothing further: rider weight sits under Bike
// because it is an input to the same resistance model `bike_weight` feeds, not because
// there was nowhere else to put it.
//
// What is *not* here is deliberate. 283 settings survive phase 6 and most are device
// detection and protocol quirks that a rider never opens a screen to change. This page
// carries the ones that get touched between rides.
Item {
    id: settingsScreen

    readonly property real unit: window.unit

    Settings {
        id: qzSettings
        property int bike_resistance_offset: 4
        property double bike_resistance_gain_f: 1.0
        property int bike_resistance_start: 0
        property double bike_weight: 0.0
        property double weight: 75.0
        property int resistance_slew_up: 0
        property int resistance_slew_down: 0
        property bool gears_zwift_ratio: false
        property double gear_crankset_size: 50.0
        property double gear_cog_size: 33.0
        property double gears_gain: 1.0
        property bool gears_restore_value: false
        property int gears_neutral_gear: 0
        property bool dircon_yes: true
        property int dircon_id: 0
        property bool virtual_device_bluetooth: true
        property bool rouvy_compatibility: false
        property bool mywhoosh_link_enabled: false
        property bool zwift_erg: false
        property bool gamepad_enabled: false
        property bool zwift_play: false
        property bool zwift_click: false
        property bool fitmetria_fanfit_enable: false
        property bool miles_unit: false
        property bool log_debug: false
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

            SettingsNumber {
                label: qsTr("Starting resistance")
                value: qzSettings.bike_resistance_start
                onCommitted: qzSettings.bike_resistance_start = Math.round(newValue)
            }

            SettingsNumber {
                label: qsTr("Rider weight (kg)")
                value: qzSettings.weight
                decimals: 1
                onCommitted: qzSettings.weight = newValue
            }

            SettingsNumber {
                label: qsTr("Bike weight (kg)")
                value: qzSettings.bike_weight
                decimals: 1
                onCommitted: qzSettings.bike_weight = newValue
            }

            SettingsNumber {
                label: qsTr("Resistance slew up (per second, 0 = off)")
                value: qzSettings.resistance_slew_up
                onCommitted: qzSettings.resistance_slew_up = Math.round(newValue)
            }

            SettingsNumber {
                label: qsTr("Resistance slew down (per second, 0 = off)")
                value: qzSettings.resistance_slew_down
                onCommitted: qzSettings.resistance_slew_down = Math.round(newValue)
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

            SettingsNumber {
                label: qsTr("Shift size")
                value: qzSettings.gears_gain
                decimals: 2
                onCommitted: qzSettings.gears_gain = newValue
            }

            SettingsNumber {
                label: qsTr("Neutral gear (0 = none)")
                value: qzSettings.gears_neutral_gear
                onCommitted: qzSettings.gears_neutral_gear = Math.round(newValue)
            }

            SettingsSwitch {
                // Off, this starts every ride at gear 0 - below the minimum of 1. Worth
                // a switch rather than a constant: RideState::restoreGear reads it.
                label: qsTr("Restore gear on startup")
                checked: qzSettings.gears_restore_value
                onToggled: qzSettings.gears_restore_value = checked
            }

            SettingsGroup { title: qsTr("Training-app connection") }

            SettingsSwitch {
                label: qsTr("Wi-Fi (DIRCON)")
                checked: qzSettings.dircon_yes
                onToggled: qzSettings.dircon_yes = checked
            }

            SettingsNumber {
                // Two QZ instances on one network need different ids, or their mDNS
                // records collide.
                label: qsTr("DIRCON id")
                value: qzSettings.dircon_id
                onCommitted: qzSettings.dircon_id = Math.round(newValue)
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
                checked: qzSettings.rouvy_compatibility
                onToggled: qzSettings.rouvy_compatibility = checked
            }

            SettingsSwitch {
                label: qsTr("MyWhoosh shifting link")
                checked: qzSettings.mywhoosh_link_enabled
                onToggled: qzSettings.mywhoosh_link_enabled = checked
            }

            SettingsSwitch {
                label: qsTr("ERG mode")
                checked: qzSettings.zwift_erg
                onToggled: qzSettings.zwift_erg = checked
            }

            SettingsGroup { title: qsTr("Accessories") }

            SettingsSwitch {
                label: qsTr("Gamepad shifting")
                checked: qzSettings.gamepad_enabled
                onToggled: qzSettings.gamepad_enabled = checked
            }

            SettingsSwitch {
                label: qsTr("Zwift Play")
                checked: qzSettings.zwift_play
                onToggled: qzSettings.zwift_play = checked
            }

            SettingsSwitch {
                label: qsTr("Zwift Click")
                checked: qzSettings.zwift_click
                onToggled: qzSettings.zwift_click = checked
            }

            SettingsSwitch {
                label: qsTr("Fitmetria fan")
                checked: qzSettings.fitmetria_fanfit_enable
                onToggled: qzSettings.fitmetria_fanfit_enable = checked
            }

            SettingsGroup { title: qsTr("Display") }

            SettingsSwitch {
                label: qsTr("Miles instead of kilometres")
                checked: qzSettings.miles_unit
                onToggled: qzSettings.miles_unit = checked
            }

            SettingsSwitch {
                label: qsTr("Verbose log")
                checked: qzSettings.log_debug
                onToggled: qzSettings.log_debug = checked
            }

            Label {
                Layout.fillWidth: true
                Layout.bottomMargin: unit
                wrapMode: Text.WordWrap
                font.pixelSize: unit * 1.2
                opacity: 0.7
                color: Material.foreground
                text: qsTr("Changes to the connection settings take effect when QZ restarts.")
            }
        }
    }
}
