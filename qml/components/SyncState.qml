pragma Singleton
import QtQuick

// Status of a background write to a remote playlist.
//
// Reordering a Tidal playlist costs one ETag fetch plus one POST per moved
// row, which is far too slow to hold the UI for. Pages apply the change to
// their model at once and report progress here; the window renders it in a
// corner instead of blocking.
QtObject {
    property bool   busy: false
    property string message: ""
    property bool   failed: false
    property string errorText: ""

    function begin(text) {
        message = text
        busy = true
        failed = false
        errorText = ""
    }
    function end() {
        busy = false
        message = ""
    }
    function fail(text) {
        busy = false
        message = ""
        failed = true
        errorText = text
    }
    function dismiss() {
        failed = false
        errorText = ""
    }
}
