pragma Singleton
import QtQuick

QtObject {
    readonly property color bg:          "#0A0A0A"
    readonly property color surface:     "#141414"
    readonly property color surfaceHigh: "#1E1E1E"
    readonly property color surfaceHov:  "#262626"
    readonly property color border:      "#2A2A2A"
    readonly property color accent:      "#00B2F8"
    readonly property color accentDim:   "#0082B8"
    readonly property color textPrimary: "#FFFFFF"
    readonly property color textSec:     "#A0A0A0"
    readonly property color textDim:     "#555555"
    readonly property color red:         "#FF4D4D"
    // Warning, not failure: the offline notice, where nothing is broken and
    // nothing is lost.
    readonly property color amber:       "#E8A13A"
    readonly property color green:       "#1DB954"
    readonly property int   radius:      8
    readonly property int   radiusLg:    12

    // The system "reduce motion" setting, copied in by Main.qml at startup: a
    // singleton cannot see the `app` context property itself. Every finite
    // animation takes its duration through dur(), so one switch turns them into
    // jumps. Spinners keep turning: they report progress, they do not decorate.
    property bool reducedMotion: false
    function dur(ms) { return reducedMotion ? 0 : ms }
}
