import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.3

// Sized for a thumb on a tablet held at arm's length, not for a mouse pointer.
// Section 9.7: the tablet is the stricter of the two cases, so it sets the size, and
// 7 units survives the repaint unchanged.
Button {
    id: gearButton

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    Layout.preferredWidth: unit * 7
    Layout.preferredHeight: unit * 7

    // Hold to keep shifting, so a rider changing several gears does not have to
    // find the button that many times.
    autoRepeat: true
    autoRepeatDelay: 500
    autoRepeatInterval: 200

    background: Rectangle {
        color: gearButton.down ? theme.raised
                               : (gearButton.enabled ? theme.surface : theme.sunk)
        border.color: gearButton.enabled ? theme.line : theme.lineSoft
        border.width: 1
        radius: theme.radius
    }

    contentItem: Label {
        text: gearButton.text
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.family: theme.fontDisplay
        font.pixelSize: unit * 3
        font.weight: Font.Medium
        color: gearButton.enabled ? theme.ink : theme.ghost
    }
}
