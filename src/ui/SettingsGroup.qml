import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// A group heading. Section 9.6 allows six of these and no nesting below them.
//
// Tracked caps on a rule rather than a large bold line: at 24px bold the headings
// competed with the settings themselves for weight, which is backwards on a page whose
// job is to be scanned for one row.
ColumnLayout {
    property string title: ""

    Layout.fillWidth: true
    Layout.topMargin: window.unit * 1.6
    spacing: window.unit * 0.7

    Label {
        text: title
        font.family: window.theme.fontDisplay
        font.pixelSize: window.unit * 1.08
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: window.unit * 0.18
        color: window.theme.accent
    }

    Rectangle {
        Layout.fillWidth: true
        height: 1
        color: window.theme.line
    }
}
