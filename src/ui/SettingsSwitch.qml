import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// Label on the left, switch on the right, one row high.
Item {
    id: row
    property string label: ""
    property string note: ""
    property bool checked: false
    /** Set to show a chevron and route taps somewhere instead of just toggling. */
    property bool hasDetail: false
    signal toggled()
    signal detailClicked()

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    Layout.fillWidth: true
    implicitHeight: Math.max(unit * 3.7, labelCol.implicitHeight + unit * 1.4)

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
                text: row.label
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit * 1.12
                color: row.enabled ? theme.ink : theme.dim
            }
            Label {
                Layout.fillWidth: true
                visible: row.note.length > 0
                text: row.note
                wrapMode: Text.WordWrap
                font.family: theme.fontUi
                font.pixelSize: unit * 0.92
                color: row.enabled ? theme.dim : theme.ghost
            }
        }

        // Hand-drawn rather than the Material Switch so it takes the theme's amber
        // instead of Material.accent, which the window no longer sets.
        Rectangle {
            id: track
            Layout.alignment: Qt.AlignVCenter
            width: unit * 3.5
            height: unit * 2
            radius: height / 2
            opacity: row.enabled ? 1.0 : 0.3
            color: row.checked ? theme.accentLo : theme.line
            border.color: row.checked ? Qt.rgba(0.95, 0.64, 0.24, 0.5) : "transparent"
            border.width: 1

            Rectangle {
                width: parent.height - unit * 0.5
                height: width
                radius: width / 2
                y: unit * 0.25
                x: row.checked ? parent.width - width - unit * 0.25 : unit * 0.25
                color: row.checked ? theme.accent : theme.ghost
                Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
            }

            MouseArea {
                anchors.fill: parent
                // The switch is 24px tall; the hit area is not.
                anchors.margins: -unit
                enabled: row.enabled
                onClicked: {
                    row.checked = !row.checked
                    row.toggled()
                }
            }
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            visible: row.hasDetail
            text: "›"
            font.family: theme.fontDisplay
            font.pixelSize: unit * 1.6
            color: theme.accent

            MouseArea {
                anchors.fill: parent
                anchors.margins: -unit
                onClicked: row.detailClicked()
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
