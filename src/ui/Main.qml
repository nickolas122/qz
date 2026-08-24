import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3
import Qt.labs.settings 1.0

// The new UI's entry point, reached only when ui_next is on. Three destinations, Ride
// first and default, no drawer: what survived section 7 does not justify one.
// See STRIP-SPEC.md section 9.3, and UI-INSTRUMENT-CLUSTER.md for the repaint.
ApplicationWindow {
    id: window
    visible: true
    width: 480
    height: 800
    title: "QZ"
    color: theme.ground

    // Everything is sized off this so the same tree works on a phone, a tablet and a
    // resizable desktop window. The tablet is the stricter case and sets the floor.
    readonly property real unit: Math.max(12, Math.min(width, height) / 40)

    // Every colour and size, in one object. Reached from the screens as window.theme,
    // the same way window.unit already was.
    readonly property Theme theme: Theme {}

    // Moved up from SettingsScreen so the ride screen can read miles_unit too. QML ids
    // declared here are visible to the screens because they are created in this
    // context - the same reason `window` itself resolves inside RideScreen.qml.
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

    /** Settings pushes the gamepad mapping screen over everything. */
    property bool gamepadOpen: false

    header: Item {
        implicitHeight: unit * 4.4

        Row {
            id: tabRow
            anchors.left: parent.left
            anchors.leftMargin: unit * 1.5
            anchors.bottom: parent.bottom
            spacing: unit * 2

            Repeater {
                model: [qsTr("Ride"), qsTr("Setup"), qsTr("Settings")]

                // An Item wrapper, not a bare Column: a Column manages its children's y,
                // so a MouseArea anchored to fill one is refused and takes the whole
                // Column's layout down with it.
                Item {
                    implicitWidth: tabCol.implicitWidth
                    implicitHeight: tabCol.implicitHeight

                    Column {
                        id: tabCol
                        spacing: unit * 0.75

                        Label {
                            id: tabLabel
                            text: modelData
                            font.family: theme.fontUi
                            font.pixelSize: unit * 0.92
                            font.weight: Font.DemiBold
                            font.capitalization: Font.AllUppercase
                            font.letterSpacing: unit * 0.15
                            color: pages.currentIndex === index ? theme.accent : theme.dim
                        }

                        // Off the label's width, not the Column's: the Column's width is
                        // derived from its children, so reading it here would close a loop.
                        Rectangle {
                            width: tabLabel.width
                            height: 2
                            radius: 1
                            color: pages.currentIndex === index ? theme.accent : "transparent"
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        // The tab strip is short text; give it a real hit target.
                        anchors.topMargin: -unit
                        anchors.bottomMargin: -unit * 0.6
                        anchors.leftMargin: -unit * 0.6
                        anchors.rightMargin: -unit * 0.6
                        onClicked: pages.currentIndex = index
                    }
                }
            }
        }

        Label {
            anchors.right: parent.right
            anchors.rightMargin: unit * 1.5
            anchors.bottom: parent.bottom
            anchors.bottomMargin: unit * 0.7
            text: "QZ"
            font.family: theme.fontDisplay
            font.pixelSize: unit * 1.17
            font.weight: Font.Bold
            font.letterSpacing: unit * 0.2
            color: theme.ghost
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: theme.lineSoft
        }
    }

    SwipeView {
        id: pages
        anchors.fill: parent

        RideScreen {}
        SetupScreen {}
        SettingsScreen {}
    }

    // Above the pages, below the toasts. A Loader rather than a StackView because there
    // is exactly one thing that opens over the tabs and a stack would be machinery for
    // a single push.
    Loader {
        anchors.fill: parent
        z: 50
        active: window.gamepadOpen
        visible: active
        source: "GamepadScreen.qml"
    }

    // Outside the SwipeView on purpose: a message about the bike is worth reading from
    // whichever tab is up, and it must not swipe away with the page under it.
    ToastArea {}
}
