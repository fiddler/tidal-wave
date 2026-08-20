import QtQuick
import TidalWave

// Turns a drag of selected rows into a reorder of the list underneath.
//
// Place it over a playlist's ListView. It only arms for drags of the matching
// kind, so dragging local tracks over a Tidal playlist (or the reverse) shows
// no insertion line and drops nothing — the same rule the sidebar targets use.
Item {
    id: root

    property ListView view: null
    property var selection: null
    property string kind: ""          // "local" | "tidal"

    // fromIndices are positions in the list as it looks right now; toIndex is
    // the gap the rows were dropped into, counted the same way.
    signal reorder(var fromIndices, int toIndex)

    readonly property bool armed: DragState.active && DragState.kind === root.kind
    // Disarming has to clear the scroll direction too. A drag that ends
    // anywhere but on this area leaves no exit or drop event behind, and a
    // direction left over from the last one starts the next drag scrolling
    // the instant it arms.
    onArmedChanged: if (!armed) { insertIndex = -1; edgeScroll.direction = 0 }
    property int insertIndex: -1
    property int edgeMargin: 36

    // Latched when a drag of the right kind enters. The drop event is delivered
    // after the drag has been torn down, so nothing about the drag can be read
    // back by then — this flag and the drop's own coordinates are all the drop
    // handler needs.
    property bool accepting: false

    // The row boundary nearest the pointer. Rows outside the viewport are not
    // instanced, so this walks the realised ones only — which is all the
    // pointer can be over.
    function gapAt(y) {
        if (!view) return -1
        var cy = view.contentY + y
        for (var i = 0; i < view.count; i++) {
            var it = view.itemAtIndex(i)
            if (!it) continue
            if (cy < it.y + it.height / 2) return i
        }
        return view.count
    }

    function gapY(idx) {
        if (!view || idx < 0) return 0
        var it = view.itemAtIndex(idx)
        if (it) return it.y - view.contentY
        var prev = view.itemAtIndex(idx - 1)
        if (prev) return prev.y + prev.height - view.contentY
        return 0
    }

    DropArea {
        id: sensor
        anchors.fill: parent
        keys: ["tidalwave/tracks"]

        // Gate on the source, not on DragState: the source carries its kind
        // from the moment the drag is prepared, so this cannot race the enter
        // event the way a global flag can.
        onEntered: (d) => {
            if (!d.source || d.source.kind !== root.kind) { d.accepted = false; return }
            root.accepting = true
            // The index is computed here as well as in onPositionChanged: a
            // drag that jumps rather than glides arrives as exit+enter, and
            // relying on positionChanged alone left the line never showing.
            root.insertIndex = root.gapAt(d.y)
        }
        onExited: edgeScroll.direction = 0
        onPositionChanged: (d) => {
            if (!d.source || d.source.kind !== root.kind) return
            root.insertIndex = root.gapAt(d.y)
            // A row drag no longer flicks the list, so carry the view along
            // when the pointer reaches either edge.
            edgeScroll.direction = d.y < root.edgeMargin ? -1
                                 : d.y > root.height - root.edgeMargin ? 1 : 0
        }
        onDropped: (d) => {
            edgeScroll.direction = 0
            // Recomputed from the event rather than read back from insertIndex:
            // by the time the drop arrives the drag has already been torn down,
            // so any retained state has been reset.
            var to = root.gapAt(d.y)
            var ok = root.accepting
            root.accepting = false
            root.insertIndex = -1
            if (!ok || to < 0 || !root.selection) { d.accepted = false; return }
            var from = []
            for (var i = 0; i < root.selection.tracks.length; i++)
                if (root.selection.isSelected(root.selection.tracks[i].id)) from.push(i)
            if (from.length === 0) { d.accepted = false; return }
            root.reorder(from, to)
            d.accept()
        }
    }

    Timer {
        id: edgeScroll
        property int direction: 0
        interval: 16
        repeat: true
        running: direction !== 0 && root.armed && view
        onTriggered: {
            var v = root.view
            if (v.contentHeight <= v.height) return
            v.contentY = Math.max(0, Math.min(v.contentHeight - v.height, v.contentY + direction * 9))
            root.insertIndex = root.gapAt(direction < 0 ? root.edgeMargin : root.height - root.edgeMargin)
        }
    }

    // Insertion line
    Rectangle {
        visible: root.armed && root.insertIndex >= 0
        x: 16
        y: root.gapY(root.insertIndex) - height / 2
        width: parent.width - 32
        height: 2
        color: Theme.accent

        Rectangle {
            anchors.verticalCenter: parent.top
            anchors.left: parent.left
            width: 8; height: 8; radius: 4
            color: Theme.accent
        }
    }
}
