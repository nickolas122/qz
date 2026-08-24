import QtQuick 2.12

// The trainer's battery, as a shape rather than a number alone, so it reads at a glance
// on a chip that is mostly text. Drawn from Rectangles rather than an SVG because the
// whole figure is three rounded boxes and a rule - an image asset would cost a file and
// a colour that cannot follow the theme.
//
// ftmsbike has read characteristic 0x2A19 all along; until 2026-08-24 the only thing it
// did with the value was fire a three-second toast on change. See
// UI-INSTRUMENT-CLUSTER.md section 4.
Item {
    id: glyph

    /** 0-100, or -1 when the bike does not report a battery at all. */
    property int level: -1

    readonly property real unit: window.unit
    readonly property var theme: window.theme
    readonly property color tint: theme.batteryColor(level)

    implicitWidth: unit * 1.75
    implicitHeight: unit * 0.92

    // Body
    Rectangle {
        id: body
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width - cap.width - 1
        height: parent.height
        radius: 2
        color: "transparent"
        border.color: glyph.level < 0 ? theme.ghost : glyph.tint
        border.width: 1

        // Fill. Inset by the border so a full battery does not overpaint it.
        Rectangle {
            visible: glyph.level >= 0
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 2
            width: Math.max(1, (parent.width - 4) * Math.min(100, Math.max(0, glyph.level)) / 100)
            height: parent.height - 4
            radius: 1
            color: glyph.tint
        }

        // Struck through when there is no reading. The glyph stays rather than the chip
        // silently changing shape, so "this bike does not report one" is visible.
        Rectangle {
            visible: glyph.level < 0
            anchors.centerIn: parent
            width: parent.width * 0.55
            height: 1
            color: theme.ghost
        }
    }

    // Terminal
    Rectangle {
        id: cap
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: unit * 0.2
        height: parent.height * 0.45
        radius: 1
        color: glyph.level < 0 ? theme.ghost : glyph.tint
    }
}
