import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// One setting, several named values, exactly one of them on. Label above, a full-width
// segmented bar below it.
//
// Not a ComboBox: a popup list is three interactions to read and two to change, and the
// Material combo brings its own palette in with it - the same reason SettingsSwitch is
// drawn by hand rather than using Switch. Not a row of chips beside the label either,
// because three language names do not fit next to one on a phone.
Item {
    id: row
    property string label: ""
    property string note: ""
    /** The values, in order. Not shown - `names` is what the rider reads. */
    property var values: []
    /** One name per value, same order. */
    property var names: []
    property string value: ""
    signal picked(string newValue)

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    Layout.fillWidth: true
    implicitHeight: col.implicitHeight + unit * 1.4

    ColumnLayout {
        id: col
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.rightMargin: unit / 2
        anchors.verticalCenter: parent.verticalCenter
        spacing: unit * 0.55

        Label {
            Layout.fillWidth: true
            text: row.label
            wrapMode: Text.WordWrap
            font.family: theme.fontUi
            font.pixelSize: unit * 1.12
            color: theme.ink
        }

        Label {
            Layout.fillWidth: true
            visible: row.note.length > 0
            text: row.note
            wrapMode: Text.WordWrap
            font.family: theme.fontUi
            font.pixelSize: unit * 0.92
            color: theme.dim
        }

        // The bar. Equal-width segments so the hit targets stay honest whatever the
        // names are - a two-letter option must not be harder to press than a long one.
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: unit * 0.2
            implicitHeight: unit * 2.6
            radius: unit * 0.35
            color: theme.line
            border.color: theme.lineSoft
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.margins: 2

                Repeater {
                    model: row.names

                    Item {
                        width: parent.width / Math.max(1, row.names.length)
                        height: parent.height

                        readonly property bool isCurrent: row.values[index] === row.value

                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 1
                            radius: unit * 0.28
                            color: parent.isCurrent ? theme.accentLo : "transparent"
                            border.color: parent.isCurrent ? Qt.rgba(0.95, 0.64, 0.24, 0.5) : "transparent"
                            border.width: 1
                        }

                        Label {
                            anchors.centerIn: parent
                            width: parent.width - unit * 0.5
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: modelData
                            font.family: theme.fontUi
                            font.pixelSize: unit * 0.95
                            font.weight: parent.isCurrent ? Font.DemiBold : Font.Normal
                            color: parent.isCurrent ? theme.accent : theme.dim
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                if (row.values[index] !== row.value) {
                                    row.picked(row.values[index])
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: theme.lineSoft
    }
}
