pragma Singleton
import QtQuick

// Global "a track drag is in flight" flag.
//
// Drop targets need this to show themselves for the whole duration of a drag
// rather than only once the cursor lands on them. Reaching the window from a
// nested component (Window.window.dragLayer) proved unreliable — it is null
// while an item is still being attached to the scene — so the state lives
// here instead, where every component can read it directly.
QtObject {
    property bool   active: false
    property int    count:  0
    property string kind:   ""      // "local" | "tidal"
    property real   px: 0           // pointer position, in scene coordinates
    property real   py: 0

    function begin(payload, k) {
        count  = payload ? payload.length : 0
        kind   = k
        active = true
    }
    function end() {
        active = false
        count  = 0
        kind   = ""
    }
}
