import QtQuick

// Selection state for a list of tracks.
//
// It lives on the page rather than on the rows because ListView recycles its
// delegates: a row scrolled out of view and reused for another track would
// otherwise carry the old row's selection with it.
//
// Selection is keyed by track id, not by row index, so it survives the list
// being reloaded (a playlist refresh, an import finishing) as long as the same
// tracks are still there.
QtObject {
    id: sel

    property var tracks: []      // the page's current list; keep this bound
    property var ids: []         // selected track ids
    property int anchor: -1      // shift-click extends from here

    readonly property int count: ids.length
    readonly property bool hasSelection: ids.length > 0

    function isSelected(id) { return ids.indexOf(id) !== -1 }

    function clear() {
        if (ids.length === 0) return
        ids = []
        anchor = -1
    }

    function selectOnly(index) {
        if (index < 0 || index >= tracks.length) return
        ids = [tracks[index].id]
        anchor = index
    }

    function selectAll() {
        var all = []
        for (var i = 0; i < tracks.length; i++) all.push(tracks[i].id)
        ids = all
        anchor = tracks.length > 0 ? 0 : -1
    }

    function toggle(index) {
        if (index < 0 || index >= tracks.length) return
        var id = tracks[index].id
        var next = ids.slice()
        var at = next.indexOf(id)
        if (at === -1) next.push(id)
        else           next.splice(at, 1)
        ids = next
        anchor = index
    }

    // Shift-click selects the run between the anchor and index. Ctrl/Cmd held
    // as well means add that run to what is already selected, which is how
    // Finder and the Tidal client both behave.
    function extendTo(index, additive) {
        if (index < 0 || index >= tracks.length) return
        if (anchor === -1) { selectOnly(index); return }
        var lo = Math.min(anchor, index)
        var hi = Math.max(anchor, index)
        var next = additive ? ids.slice() : []
        for (var i = lo; i <= hi; i++) {
            var id = tracks[i].id
            if (next.indexOf(id) === -1) next.push(id)
        }
        ids = next
        // anchor deliberately unchanged: dragging the shift-click further
        // grows the same run instead of starting a new one.
    }

    // Single entry point for a click on a row, given the keyboard modifiers.
    function handleClick(index, modifiers) {
        var ctrlOrCmd = (modifiers & Qt.ControlModifier) || (modifiers & Qt.MetaModifier)
        if (modifiers & Qt.ShiftModifier) extendTo(index, ctrlOrCmd)
        else if (ctrlOrCmd)               toggle(index)
        else                              selectOnly(index)
    }

    // Selected tracks in list order — the order they get added to a playlist.
    function selectedTracks() {
        var out = []
        for (var i = 0; i < tracks.length; i++)
            if (isSelected(tracks[i].id)) out.push(tracks[i])
        return out
    }

    // Drop the ids that are no longer in the list, so a reload that removed
    // tracks does not leave phantom entries selected.
    function prune() {
        var next = []
        for (var i = 0; i < tracks.length; i++)
            if (isSelected(tracks[i].id)) next.push(tracks[i].id)
        if (next.length !== ids.length) ids = next
    }

    onTracksChanged: prune()
}
