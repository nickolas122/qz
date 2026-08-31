import QtQuick 2.12

// Every colour and size the new UI uses, in one place. Instantiated once in Main.qml and
// reached as `window.theme`, which is the same way `window.unit` was already reached -
// a QML singleton would need a qmldir, and the qrc here is flat with no module.
//
// The palette is UI-INSTRUMENT-CLUSTER.md section 1. The one rule worth restating,
// because breaking it is easy and silent: `accent` is the instrument colour and carries
// no meaning about state. Green, cyan and red each mean exactly one thing, so a red dot
// is never decoration. If something needs colour and is not a state, it takes accent or
// a neutral - it does not borrow `live`.
QtObject {
    id: theme

    // -- ground and surfaces -------------------------------------------------
    readonly property color ground: "#0C0B0A"      // window background
    readonly property color surface: "#141210"     // chips, buttons
    readonly property color raised: "#1C1916"      // pressed/secondary fills
    readonly property color sunk: "#100E0C"        // disabled, empty
    readonly property color line: "#26221C"        // every border, 1px
    readonly property color lineSoft: "#1B1814"    // separators inside a group

    // -- ink -----------------------------------------------------------------
    readonly property color ink: "#F0EBE1"
    readonly property color muted: "#A39C8E"
    readonly property color dim: "#6B6558"
    readonly property color ghost: "#4A443A"

    // -- accent, and the semantics that are deliberately not it --------------
    readonly property color accent: "#F2A33C"
    readonly property color accentLo: "#241C10"
    readonly property color live: "#63C88A"        // connected and receiving now
    readonly property color work: "#57B6D6"        // scanning, connecting, retrying
    readonly property color fault: "#E5563C"       // was live, is not, and should be
    readonly property color faultLo: "#2A1611"
    readonly property color workLo: "#122630"

    // -- type ----------------------------------------------------------------
    //
    // Barlow Condensed / Barlow / IBM Plex Mono are what the design was drawn in, and
    // none of the three ships with Windows or Android. Rather than name a font that
    // silently falls back to whatever the platform feels like, these are the nearest
    // faces that are actually present: condensed for the numerals, a real monospace for
    // the data so the digits line up, and the system UI face for everything else.
    // Bundling the real families as qrc fonts and loading them with FontLoader is the
    // follow-up; it is a drop-in change to these three lines.
    readonly property string fontDisplay: OS_VERSION === "Android" ? "Roboto Condensed" : "Bahnschrift"
    readonly property string fontMono: OS_VERSION === "Android" ? "Roboto Mono" : "Consolas"
    readonly property string fontUi: OS_VERSION === "Android" ? "Roboto" : "Segoe UI"

    // -- metrics, all multiples of window.unit (12 on both target sizes) -----
    readonly property real radius: 4
    readonly property real hairline: 1
    // Section 9.7: dimensioned for a thumb on a tablet, which is the stricter case.
    readonly property real minTouch: 44

    /** @return the semantic colour for one of RideState's state vocabularies. */
    function stateColor(state) {
        switch (state) {
        case "live":        return theme.live
        case "connecting":
        case "discovering":
        case "searching":   return theme.work
        case "stale":       return theme.accent
        case "lost":
        case "gaveup":      return theme.fault
        case "past":
        case "idle":
        default:            return theme.ghost
        }
    }

    /**
     * Battery fill colour. Neutral until it means something - a healthy battery does
     * not borrow the "live" green, because green here means the link is carrying data.
     */
    function batteryColor(pct) {
        if (pct < 0) return theme.ghost
        if (pct <= 10) return theme.fault
        if (pct <= 20) return theme.accent
        return theme.muted
    }
}
