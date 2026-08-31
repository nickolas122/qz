import QtQuick 2.12
import QtQuick.Controls 2.12

// One live number. Replaces a quarter of the concatenated Label the ride screen used to
// carry - `power + " W · " + cadence + " rpm · " ...` - which meant four values shared
// one colour, one size and one fate.
//
// The fate is the point. When the trainer stops sending, a chip can mark itself without
// the other three lying about it, and TODO.md is explicit that holding a number with no
// marker is the only option that is definitely wrong.
Rectangle {
    id: chip

    property string key: ""
    property string unitLabel: ""
    /** The formatted value, or empty for "nothing has arrived". */
    property string value: ""
    /** Seconds since this reading arrived. -1 or below the threshold means current. */
    property int age: -1
    /** No sensor fitted - not a fault, so it must not look like one. */
    property bool absent: false

    readonly property real unit: window.unit
    readonly property var theme: window.theme
    readonly property bool stale: !absent && age >= 5

    implicitHeight: unit * 4.2
    color: (stale || absent) ? theme.sunk : theme.surface
    border.color: stale ? "#3E362B" : (absent ? theme.lineSoft : theme.line)
    border.width: theme.hairline
    radius: theme.radius

    // Qt Quick has no dashed border, and faking one with a Canvas for a 1px rule is not
    // worth the paint. The sunk fill plus the amber key and the age badge already say
    // "not current" three times over; the dashes were the least of the three.

    Column {
        anchors.fill: parent
        anchors.margins: unit * 0.72
        spacing: unit / 4

        Item {
            width: parent.width
            height: keyLabel.height

            Label {
                id: keyLabel
                text: chip.key
                font.family: theme.fontUi
                font.pixelSize: unit * 0.75
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: unit * 0.13
                color: chip.stale ? theme.accent : (chip.absent ? theme.ghost : theme.dim)
            }

            Label {
                anchors.right: parent.right
                anchors.verticalCenter: keyLabel.verticalCenter
                visible: chip.stale
                text: chip.age + "s"
                font.family: theme.fontMono
                font.pixelSize: unit * 0.75
                color: theme.accent
            }
        }

        Row {
            spacing: unit / 4

            Label {
                anchors.baseline: unitLabelText.baseline
                // Dashes rather than zeroes: 0 W is a real reading that a coasting
                // rider produces, so it cannot double as "no reading".
                text: chip.value.length > 0 ? chip.value : "––"
                font.family: theme.fontMono
                font.pixelSize: unit * 1.6
                font.weight: Font.Medium
                color: chip.value.length === 0 || chip.absent
                       ? theme.ghost : (chip.stale ? theme.dim : theme.ink)
            }

            Label {
                id: unitLabelText
                text: chip.unitLabel
                font.family: theme.fontUi
                font.pixelSize: unit * 0.75
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: unit * 0.08
                color: chip.absent ? theme.ghost : theme.dim
            }
        }
    }
}
