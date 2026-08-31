import QtQuick 2.12
import QtQuick.Controls 2.12

// A controller's buttons in their real spatial arrangement - no silhouette, because
// XInput is not only Xbox pads and drawing one would claim more than QZ knows.
//
// What it is for: pressing a trigger to bind it only works if the rider can see which
// one the app thinks they pressed. Bound buttons are amber, the button under a thumb
// right now is cyan, everything else is a dim outline.
//
// Laid out against a 340x190 design grid and scaled to fit, so the whole figure keeps
// its proportions on a phone and on a resizable desktop window alike.
Item {
    id: pad

    /** Names, as gamepadcontroller::buttonNames() spells them, currently bound. */
    property var boundButtons: []
    /** Names held down this instant. */
    property var pressedButtons: []

    readonly property var theme: window.theme
    readonly property real gx: width / 340
    readonly property real gy: height / 190
    readonly property real s: Math.min(gx, gy)

    implicitHeight: width * (190 / 340)

    function isBound(name) { return boundButtons.indexOf(name) >= 0 }
    function isPressed(name) { return pressedButtons.indexOf(name) >= 0 }
    function strokeFor(name) {
        if (isPressed(name)) return theme.work
        return isBound(name) ? theme.accent : theme.line
    }
    function fillFor(name) {
        if (isPressed(name)) return theme.workLo
        return isBound(name) ? theme.accentLo : theme.surface
    }
    function textFor(name) {
        if (isPressed(name)) return theme.work
        return isBound(name) ? theme.accent : theme.dim
    }

    Repeater {
        // name, x, y, w, h, radius, label
        model: [
            { n: "lt", x: 18,  y: 10,  w: 54, h: 24, r: 5 },
            { n: "lb", x: 78,  y: 10,  w: 54, h: 24, r: 5 },
            { n: "rb", x: 208, y: 10,  w: 54, h: 24, r: 5 },
            { n: "rt", x: 268, y: 10,  w: 54, h: 24, r: 5 },
            { n: "dpad_up",    x: 48, y: 76,  w: 24, h: 22, r: 3 },
            { n: "dpad_down",  x: 48, y: 122, w: 24, h: 22, r: 3 },
            { n: "dpad_left",  x: 24, y: 99,  w: 22, h: 24, r: 3 },
            { n: "dpad_right", x: 74, y: 99,  w: 22, h: 24, r: 3 },
            { n: "back",  x: 124, y: 99, w: 44, h: 22, r: 11 },
            { n: "start", x: 176, y: 99, w: 44, h: 22, r: 11 },
            { n: "y", x: 265, y: 61,  w: 30, h: 30, r: 15 },
            { n: "x", x: 231, y: 95,  w: 30, h: 30, r: 15 },
            { n: "b", x: 299, y: 95,  w: 30, h: 30, r: 15 },
            { n: "a", x: 265, y: 129, w: 30, h: 30, r: 15 },
            { n: "l3", x: 98,  y: 152, w: 28, h: 28, r: 14 },
            { n: "r3", x: 218, y: 152, w: 28, h: 28, r: 14 }
        ]

        Rectangle {
            x: modelData.x * pad.gx
            y: modelData.y * pad.gy
            width: modelData.w * pad.gx
            height: modelData.h * pad.gy
            radius: modelData.r * pad.s
            color: pad.fillFor(modelData.n)
            border.color: pad.strokeFor(modelData.n)
            border.width: pad.isPressed(modelData.n) ? 2 : 1.3

            Behavior on border.color { ColorAnimation { duration: 90 } }

            Label {
                anchors.centerIn: parent
                // The d-pad arms are drawn as shapes below rather than labelled -
                // "DPAD_LEFT" does not fit in a 22px box and does not need to.
                visible: modelData.n.indexOf("dpad") !== 0
                text: modelData.n.toUpperCase()
                font.family: pad.theme.fontUi
                font.pixelSize: (modelData.n.length > 2 ? 9 : 11) * pad.s
                font.weight: Font.DemiBold
                font.letterSpacing: pad.s
                color: pad.textFor(modelData.n)
            }
        }
    }

    // D-pad arrowheads, so the cross reads as a direction pad rather than four boxes.
    Repeater {
        model: [
            { n: "dpad_up",    cx: 60, cy: 87,  rot: 0 },
            { n: "dpad_down",  cx: 60, cy: 133, rot: 180 },
            { n: "dpad_left",  cx: 35, cy: 111, rot: 270 },
            { n: "dpad_right", cx: 85, cy: 111, rot: 90 }
        ]

        Canvas {
            x: (modelData.cx - 6) * pad.gx
            y: (modelData.cy - 5) * pad.gy
            width: 12 * pad.gx
            height: 10 * pad.gy
            rotation: modelData.rot
            renderStrategy: Canvas.Cooperative

            property color tint: pad.textFor(modelData.n)
            onTintChanged: requestPaint()

            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.fillStyle = tint
                ctx.beginPath()
                ctx.moveTo(width / 2, 0)
                ctx.lineTo(width, height)
                ctx.lineTo(0, height)
                ctx.closePath()
                ctx.fill()
            }
        }
    }
}
