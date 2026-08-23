import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// Label on the left, a number on the right that commits when it loses focus or the
// rider presses return. No OK button per row: section 9.6 wants a page that reads.
RowLayout {
    id: row
    property string label: ""
    property double value: 0
    property int decimals: 0
    signal committed(double newValue)

    Layout.fillWidth: true
    spacing: window.unit

    Label {
        Layout.fillWidth: true
        text: row.label
        wrapMode: Text.WordWrap
        font.pixelSize: window.unit * 1.4
        color: Material.foreground
    }

    TextField {
        id: field
        Layout.preferredWidth: window.unit * 8
        horizontalAlignment: Text.AlignRight
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        font.pixelSize: window.unit * 1.4
        text: row.value.toFixed(row.decimals)

        function commit() {
            var parsed = parseFloat(field.text)
            if (!isNaN(parsed))
                row.committed(parsed)
            else
                field.text = row.value.toFixed(row.decimals)
        }

        onAccepted: commit()
        onActiveFocusChanged: if (!activeFocus) commit()
    }
}
