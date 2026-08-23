import QtQuick 2.12
import QtQuick.Controls 2.12
import QtQuick.Controls.Material 2.12
import QtQuick.Layouts 1.3

// Sized for a thumb on a tablet held at arm's length, not for a mouse pointer.
// Section 9.7: the tablet is the stricter of the two cases, so it sets the size.
Button {
    id: gearButton

    readonly property real unit: window.unit

    Layout.preferredWidth: unit * 7
    Layout.preferredHeight: unit * 7

    font.pixelSize: unit * 3.5
    font.bold: true

    // Hold to keep shifting, so a rider changing several gears does not have to
    // find the button that many times.
    autoRepeat: true
    autoRepeatDelay: 500
    autoRepeatInterval: 200
}
