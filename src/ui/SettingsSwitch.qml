import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// Label on the left, switch on the right, one row high.
RowLayout {
    id: row
    property string label: ""
    property alias checked: control.checked
    signal toggled()

    Layout.fillWidth: true
    spacing: window.unit

    Label {
        Layout.fillWidth: true
        text: row.label
        wrapMode: Text.WordWrap
        font.pixelSize: window.unit * 1.4
        opacity: row.enabled ? 1.0 : 0.5
        color: Material.foreground
    }

    Switch {
        id: control
        enabled: row.enabled
        onClicked: row.toggled()
    }
}
