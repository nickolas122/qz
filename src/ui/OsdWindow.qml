// 2.13 where the rest of the tree is on 2.12, and on *both* lines deliberately.
// transientParent only became assignable at QtQuick.Window 2.13 (Qt 5.13), and assigning
// it below is what stops this window being minimised along with QZ. Qt 5 takes Window
// from QtQuick.Window; Qt 6 also registers it in QtQuick itself and resolves it there, so
// a version bump on only one of the two leaves the property gated out on that build - as
// it did, with "transientParent is not available in QtQuick 2.12" on Qt 6.8.
// Every shipping build is Qt 5.15 or Qt 6, so 2.13 costs nothing.
import QtQuick 2.13
import QtQuick.Window 2.13

// The overlay's second sink: a frameless always-on-top window, for riders without RTSS.
//
// It is honest about what it is. This is an ordinary window, so it is composited with
// everything else - a training app in *exclusive* fullscreen bypasses the compositor and
// covers it. Windowed and borderless apps are fine, which is most of them. RTSS remains
// the only sink that draws inside the app's own frame; see qzosd.h.
//
// Two flags carry most of the behaviour. Qt.Tool keeps it out of the taskbar and out of
// alt-tab, where an overlay has no business being. Qt.WindowDoesNotAcceptFocus stops a
// click on it from pulling focus off the training app, which mid-ride would be worse than
// the overlay being wrong.
Window {
    id: osdWindow

    // Declared inside ApplicationWindow so it can see qzSettings, osd and window.theme -
    // but a nested Window is transient for its parent by default, and a transient child
    // is hidden when its parent is minimised. Minimising QZ is exactly what a rider does
    // before starting the ride, so the parent link has to go.
    transientParent: null

    readonly property real unit: window.unit
    readonly property var theme: window.theme

    /** Click-through and immovable. Unlocking it is how the rider drags it somewhere else. */
    readonly property bool locked: qzSettings.osd_window_locked

    visible: osd.windowVisible && osd.text.length > 0

    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool |
           Qt.WindowDoesNotAcceptFocus |
           (locked ? Qt.WindowTransparentForInput : 0)

    color: "transparent"

    // Hugs the text rather than being a fixed box, so switching lines off shrinks it.
    width: panel.width
    height: panel.height

    // Set once rather than bound: a drag assigns x and y, and a binding would either fight
    // the drag or be silently broken by it. -1 is "never moved", which is the top right -
    // out of the way of a training app's own HUD, which tends to live bottom and left.
    Component.onCompleted: {
        x = qzSettings.osd_window_x >= 0 ? qzSettings.osd_window_x
                                         : Screen.desktopAvailableWidth - width - unit * 3
        y = qzSettings.osd_window_y >= 0 ? qzSettings.osd_window_y : unit * 3
    }

    Rectangle {
        id: panel
        width: label.width + unit * 2
        height: label.height + unit * 1.4
        radius: theme.radius
        // Not theme.surface: this sits on top of somebody else's picture, so it needs to be
        // legible over anything without blacking out a slab of the ride.
        color: Qt.rgba(0, 0, 0, 0.62)
        border.width: theme.hairline
        // The border is the only thing that says "unlocked, drag me" - the accent is the
        // instrument colour and carries no state meaning, which is what makes it right here.
        border.color: osdWindow.locked ? Qt.rgba(1, 1, 1, 0.10) : theme.accent

        Text {
            id: label
            anchors.centerIn: parent
            text: osd.text
            // Monospace so the digits do not shuffle the box's width every time the
            // resistance changes by one.
            font.family: theme.fontMono
            font.pixelSize: unit * 1.25
            lineHeight: 1.15
            // The trainer notice is the one thing here that is not a number, and it is the
            // reason the overlay exists at all - so it is the one thing allowed to shout.
            // Anything starting "QZ:" is a notice about the link, not a reading off it.
            color: osd.text.indexOf("QZ:") === 0 ? theme.fault : theme.ink
        }

        // Drawn behind nothing and above everything: a plain drag handle, alive only while
        // the window is unlocked. Locked, the window is click-through and this never runs.
        MouseArea {
            anchors.fill: parent
            enabled: !osdWindow.locked
            cursorShape: Qt.SizeAllCursor
            property real pressX: 0
            property real pressY: 0
            onPressed: {
                pressX = mouse.x
                pressY = mouse.y
            }
            onPositionChanged: {
                if (!pressed)
                    return
                osdWindow.x += mouse.x - pressX
                osdWindow.y += mouse.y - pressY
            }
            // Written on release rather than on every frame of the drag: this is a QSettings
            // write, and one per pixel of travel would be a file write per pixel of travel.
            onReleased: {
                qzSettings.osd_window_x = osdWindow.x
                qzSettings.osd_window_y = osdWindow.y
            }
        }
    }
}
