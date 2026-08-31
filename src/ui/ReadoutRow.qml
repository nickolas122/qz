import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// A label and the fact it names, on one hairline-separated row. What the Setup screen is
// made of now that it reports rather than describes.
Item {
    id: readout

    property string label: ""
    property string value: ""
    property string note: ""
    /** Colour of the value. Defaults to muted; pass a semantic colour to mean something. */
    property color tint: window.theme.muted
    /** Values that answer the question the screen exists for get full ink. */
    property bool emphasis: false

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    Layout.fillWidth: true
    implicitHeight: visible ? Math.max(unit * 3.7, labelCol.implicitHeight + unit * 1.6) : 0

    RowLayout {
        anchors.fill: parent
        anchors.rightMargin: unit / 2
        spacing: unit

        ColumnLayout {
            id: labelCol
            Layout.fillWidth: true
            spacing: 0

            Label {
                Layout.fillWidth: true
                text: readout.label
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit * 1.12
                color: theme.ink
            }
            Label {
                Layout.fillWidth: true
                visible: readout.note.length > 0
                text: readout.note
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit * 0.92
                color: theme.dim
            }
        }

        Label {
            Layout.maximumWidth: readout.width * 0.5
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
            text: readout.value
            font.family: theme.fontMono
            font.pixelSize: unit * 1.08
            color: readout.emphasis ? theme.ink : readout.tint
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
