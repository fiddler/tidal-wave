import QtQuick
import TidalWave

// A sensor strip pinned to the top or bottom edge of a ListView. While a track
// drag hovers it, the list scrolls, so a playlist below the fold can be reached
// without releasing the drag.
//
// It announces itself for the whole duration of a drag rather than only on
// contact: a zone that appears only once you have already found it is no help
// in finding it.
//
// The strip necessarily covers the rows nearest the edge, so it also forwards a
// drop to whichever row sits underneath. Otherwise releasing here would
// silently do nothing.
Item {
    id: root

    property ListView view: null
    property int direction: -1        // -1 scrolls up, +1 scrolls down
    property int pixelsPerTick: 9

    signal droppedAt(int index, var source)

    height: 32
    z: 5

    readonly property bool canScroll:
        view && view.contentHeight > view.height
        && (direction < 0 ? view.contentY > 0
                          : view.contentY < view.contentHeight - view.height)

    // Only meaningful while something is being dragged, and only on the side
    // the list can still move towards.
    readonly property bool armed: DragState.active && canScroll

    DropArea {
        id: sensor
        anchors.fill: parent
        keys: ["tidalwave/tracks"]
        onDropped: (drop) => {
            if (!root.view) { drop.accepted = false; return }
            var p = root.mapToItem(root.view.contentItem, drop.x, drop.y)
            root.droppedAt(root.view.indexAt(p.x, p.y), drop.source)
            drop.accept()
        }
    }

    Timer {
        interval: 16
        repeat: true
        running: sensor.containsDrag && root.canScroll
        onTriggered: {
            var v = root.view
            v.contentY = Math.max(0, Math.min(v.contentHeight - v.height,
                                              v.contentY + root.direction * root.pixelsPerTick))
        }
    }

    // Visible for the whole drag; brightens once the cursor is actually on it.
    Rectangle {
        anchors.fill: parent
        visible: root.armed
        opacity: sensor.containsDrag ? 1 : 0.55
        Behavior on opacity { NumberAnimation { duration: 120 } }
        gradient: Gradient {
            GradientStop {
                position: 0
                color: root.direction < 0 ? Qt.rgba(0, 0.698, 0.973, 0.45) : "transparent"
            }
            GradientStop {
                position: 1
                color: root.direction < 0 ? "transparent" : Qt.rgba(0, 0.698, 0.973, 0.45)
            }
        }

        // A chevron pointing the way the list will move.
        Canvas {
            id: chevron
            width: 14; height: 8
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: root.direction < 0 ? parent.top : undefined
            anchors.bottom: root.direction < 0 ? undefined : parent.bottom
            anchors.topMargin: 5
            anchors.bottomMargin: 5
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = "white"
                ctx.lineWidth = 2
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                if (root.direction < 0) {          // ^
                    ctx.moveTo(2, 6); ctx.lineTo(7, 2); ctx.lineTo(12, 6)
                } else {                            // v
                    ctx.moveTo(2, 2); ctx.lineTo(7, 6); ctx.lineTo(12, 2)
                }
                ctx.stroke()
            }
        }
    }
}
