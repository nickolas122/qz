import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// A group heading. Section 9.6 allows exactly four of these and no nesting below them.
Label {
    property alias title: heading.text

    id: heading
    Layout.fillWidth: true
    Layout.topMargin: window.unit
    Layout.bottomMargin: window.unit / 3
    font.pixelSize: window.unit * 1.9
    font.bold: true
    color: Material.accent
}
