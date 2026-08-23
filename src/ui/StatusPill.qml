import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12

// A dot and a label. Green when the thing is there, grey when it is not - deliberately
// not red, because "no training app yet" is the normal state before a ride starts.
Rectangle {
    id: pill

    property bool on: false
    property string label: ""

    readonly property real unit: window.unit

    implicitHeight: unit * 2.8
    radius: implicitHeight / 2
    color: Qt.rgba(1, 1, 1, 0.08)

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: unit
        anchors.right: parent.right
        anchors.rightMargin: unit / 2
        spacing: unit / 2

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: unit
            height: unit
            radius: width / 2
            color: pill.on ? Material.color(Material.Green) : Qt.rgba(1, 1, 1, 0.3)
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            width: pill.width - unit * 2.5
            text: pill.label
            elide: Text.ElideRight
            font.pixelSize: unit * 1.2
            opacity: pill.on ? 1.0 : 0.7
            color: Material.foreground
        }
    }
}
