import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// Screen 3 of STRIP-SPEC.md section 9.6: one scrollable page, organised around what a
// rider changes rather than around vendors. No nesting and no accordions.
//
// The Settings block itself moved up to Main.qml so the ride screen can read miles_unit;
// nothing was added to it. Six groups. The only row here that leads anywhere
// is Gamepad, and its subtitle is its own current binding - so the summary and the route
// to change it are the same control, and the groups stay flat.
Item {
    id: settingsScreen

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    Flickable {
        anchors.fill: parent
        // Off column.height, not its implicitHeight: the column's width comes from the
        // Flickable, so sizing the Flickable off the column's implicit size closes a loop.
        contentHeight: column.height + unit * 2
        clip: true

        ColumnLayout {
            id: column
            width: settingsScreen.width - unit * 2.7
            x: unit * 1.33
            spacing: 0

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
                step: 0.05
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
                step: 0.5
                onCommitted: qzSettings.weight = newValue
            }

            SettingsNumber {
                label: qsTr("Bike weight (kg)")
                value: qzSettings.bike_weight
                decimals: 1
                step: 0.5
                onCommitted: qzSettings.bike_weight = newValue
            }

            SettingsNumber {
                label: qsTr("Resistance slew up")
                note: qsTr("Per second. 0 is off.")
                value: qzSettings.resistance_slew_up
                onCommitted: qzSettings.resistance_slew_up = Math.round(newValue)
            }

            SettingsNumber {
                label: qsTr("Resistance slew down")
                note: qsTr("Per second. 0 is off.")
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
                step: 0.05
                onCommitted: qzSettings.gears_gain = newValue
            }

            SettingsNumber {
                label: qsTr("Neutral gear")
                note: qsTr("0 is none.")
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
                label: qsTr("Wi‑Fi (DIRCON)")
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
                note: OS_VERSION === "Other" ? qsTr("Not available on Windows") : ""
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
                // The subtitle is the binding, so the row answers "what is it set to"
                // without being opened. The chevron is the only route off this page.
                note: gamepad.available
                      ? gamepad.gearUpButtons.concat(gamepad.gearDownButtons)
                                             .concat(gamepad.ergButtons)
                                             .join(" · ").toUpperCase()
                      : qsTr("No gamepad support on this platform")
                hasDetail: gamepad.available
                checked: qzSettings.gamepad_enabled
                onToggled: qzSettings.gamepad_enabled = checked
                onDetailClicked: window.gamepadOpen = true
            }

            // Android only, because it is Android's problem: a pad read as a pad shifts only
            // while QZ is in front, and during a ride it is not. The volume broadcast reaches
            // QZ whoever has focus, so this is the one route that shifts under the training
            // app - including from a pad in its keyboard mode, sending volume up and down.
            SettingsSwitch {
                visible: OS_VERSION === "Android"
                label: qsTr("Volume keys shift")
                note: qsTr("Works while the training app is in front")
                checked: qzSettings.volume_change_gears
                onToggled: qzSettings.volume_change_gears = checked
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

            SettingsChoice {
                // English is the language the .qml files are written in, so picking it
                // uninstalls the catalogue rather than loading one. Either way the
                // change lands here: QzLanguage retranslates the loaded tree, so the
                // page is already in the new language when the finger comes off it.
                label: qsTr("Language")
                values: language.codes
                names: language.names
                value: language.current
                onPicked: language.current = newValue
            }

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

            // Windows only, and both sinks are the reason. RivaTuner does not exist
            // elsewhere - RtssOsd compiles to no-ops - and a Qt window cannot float over
            // a training app that is a separate Android app, because it lives inside QZ's
            // own activity. Rows describing a feature that cannot run are worse than no
            // rows, so the group goes rather than being greyed out.
            SettingsGroup {
                title: qsTr("OSD overlay")
                visible: OS_VERSION === "Other"
            }

            SettingsSwitch {
                label: qsTr("Show the overlay")
                // Trainer warnings are not one of the lines below: they are how a silent
                // reconnect is announced to somebody riding in exclusive fullscreen, so
                // they always show while the overlay is on at all.
                note: qsTr("Trainer warnings always show while it is on.")
                visible: OS_VERSION === "Other"
                checked: qzSettings.osd_enabled
                onToggled: qzSettings.osd_enabled = checked
            }

            // The two sinks, and the difference between them is the whole reason there are
            // two. RTSS draws inside the training app's own frame; the window is a window.
            SettingsSwitch {
                label: qsTr("Draw it in RivaTuner (RTSS)")
                note: qsTr("The only one that survives exclusive fullscreen. Needs RTSS running.")
                visible: OS_VERSION === "Other"
                enabled: qzSettings.osd_enabled
                checked: qzSettings.osd_rtss
                onToggled: qzSettings.osd_rtss = checked
            }

            SettingsSwitch {
                label: qsTr("Draw it in a floating window")
                note: qsTr("No RivaTuner needed, but a training app in exclusive fullscreen covers it.")
                visible: OS_VERSION === "Other"
                enabled: qzSettings.osd_enabled
                checked: qzSettings.osd_window
                onToggled: qzSettings.osd_window = checked
            }

            SettingsSwitch {
                label: qsTr("Lock the window in place")
                note: qsTr("Unlock it to drag it. Locked, clicks go through it to the training app.")
                visible: OS_VERSION === "Other"
                enabled: qzSettings.osd_enabled && qzSettings.osd_window
                checked: qzSettings.osd_window_locked
                onToggled: qzSettings.osd_window_locked = checked
            }

            SettingsSwitch {
                label: qsTr("Gear line")
                visible: OS_VERSION === "Other"
                enabled: qzSettings.osd_enabled
                checked: qzSettings.osd_line_gear
                onToggled: qzSettings.osd_line_gear = checked
            }

            SettingsSwitch {
                label: qsTr("ERG line")
                visible: OS_VERSION === "Other"
                enabled: qzSettings.osd_enabled
                checked: qzSettings.osd_line_erg
                onToggled: qzSettings.osd_line_erg = checked
            }

            SettingsSwitch {
                label: qsTr("Resistance line")
                visible: OS_VERSION === "Other"
                enabled: qzSettings.osd_enabled
                checked: qzSettings.osd_line_resistance
                onToggled: qzSettings.osd_line_resistance = checked
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: unit * 1.5
                Layout.bottomMargin: unit
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit
                color: theme.dim
                text: qsTr("Changes to the connection settings take effect when QZ restarts.")
            }
        }
    }
}
