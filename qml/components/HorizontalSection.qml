import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TidalWave

Item {
    id: root
    height: col.height
    width: parent ? parent.width : 0

    property string  title: ""
    property string  subtitle: ""
    property var     items: []         // [{coverUrl, title, subtitle, id, type}]
    property int     cardSize: 160
    property string  mediaType: "album"
    property bool    showViewAll: true

    signal itemClicked(int index, var item)
    signal itemPlayClicked(int index, var item)
    // Emitted when a card's subtitle is clicked — only fires for items that
    // carry an artistId.
    signal itemSubtitleClicked(int index, var item)
    signal viewAllClicked()

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 12

        // Header
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            spacing: 0

            ColumnLayout {
                spacing: 2
                Text {
                    text: root.title
                    color: Theme.textPrimary
                    font.pixelSize: 20
                    font.bold: true
                }
                Text {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: Theme.textSec
                    font.pixelSize: 13
                }
            }
            Item { Layout.fillWidth: true }

            Text {
                id: viewAllText
                visible: root.showViewAll
                text: "View all →"
                color: viewAllText.activeFocus ? Theme.accent : Theme.textSec
                font.pixelSize: 12
                font.underline: viewAllText.activeFocus
                activeFocusOnTab: root.showViewAll
                Keys.onReturnPressed: root.viewAllClicked()
                Keys.onSpacePressed:  root.viewAllClicked()
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.viewAllClicked()
                }
            }
        }

        // Horizontal scroll list
        Item {
            id: rail
            Layout.fillWidth: true
            height: cardSize + 64 + (rail.overflows ? 8 : 0)

            readonly property real maxX: Math.max(0, hlist.contentWidth - hlist.width)
            readonly property bool overflows: maxX > 1

            HoverHandler { id: railHov }

            NumberAnimation {
                id: railAnim
                target: hlist; property: "contentX"
                duration: 260; easing.type: Easing.OutCubic
            }

            // Wheel and drag land where they land; the arrows move a screenful
            // at a time, which is what a plain mouse has to work with.
            function scrollBy(dx) {
                railAnim.stop()
                railAnim.from = hlist.contentX
                railAnim.to   = Math.max(0, Math.min(rail.maxX, hlist.contentX + dx))
                railAnim.start()
            }

            ListView {
                id: hlist
                anchors { left: parent.left; right: parent.right; top: parent.top }
                height: cardSize + 64
                orientation: ListView.Horizontal
                clip: true
                spacing: 16
                // Leading/trailing inset as real content (header/footer) rather than
                // leftMargin/rightMargin: with margins the rest position is contentX
                // = -leftMargin, which the wheel handler (clamped to >= 0) can't reach,
                // so the inset was lost after scrolling right and back. As content the
                // inset lives in [0, contentWidth-width] and is always preserved.
                header: Item { width: 24; height: 1 }
                footer: Item { width: 24; height: 1 }
                model: root.items
                interactive: false  // let parent page handle wheel; users drag the scrollbar

                ScrollBar.horizontal: ScrollBar {
                    id: hbar
                    // Draggable even though the list itself is not interactive,
                    // and only there when there is something to scroll to.
                    policy: rail.overflows ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                    minimumSize: 0.05
                }

                delegate: MediaCard {
                    required property var modelData
                    required property int index
                    coverUrl:  modelData.coverUrl  || ""
                    title:     modelData.title     || ""
                    subtitle:  modelData.subtitle  || ""
                    artistId:  modelData.artistId || 0
                    mediaType: root.mediaType
                    cardSize:  root.cardSize
                    onClicked:         root.itemClicked(index, modelData)
                    onPlayClicked:     root.itemPlayClicked(index, modelData)
                    onSubtitleClicked: root.itemSubtitleClicked(index, modelData)
                }
            }

            // Wheel handling goes through a MouseArea, not a WheelHandler: a
            // WheelHandler reacts only on the axis its `orientation` names and
            // decides that from angleDelta, and a macOS trackpad sends precise
            // pixelDelta scrolls — so a sideways two-finger swipe reached
            // neither orientation and these rows would not move. NoButton keeps
            // presses falling through to the cards, and without hoverEnabled it
            // leaves the cards' own hover handlers (and their cursor) alone.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: (wheel) => {
                    var usesPixels = wheel.pixelDelta.x !== 0 || wheel.pixelDelta.y !== 0
                    var dx = usesPixels ? wheel.pixelDelta.x : wheel.angleDelta.x
                    var dy = usesPixels ? wheel.pixelDelta.y : wheel.angleDelta.y
                    // Pixel deltas are already in the units the view moves in;
                    // angle deltas are eighths of a degree and need scaling.
                    var step = usesPixels ? 1.0 : 0.8
                    var hasShift = (wheel.modifiers & Qt.ShiftModifier) !== 0

                    // Sideways swipe, or a vertical wheel held with shift.
                    var h = Math.abs(dx) > Math.abs(dy) ? dx : (hasShift ? dy : 0)
                    if (h !== 0) {
                        var newX = Math.max(0, Math.min(rail.maxX, hlist.contentX - h * step))
                        if (newX === hlist.contentX) { wheel.accepted = false; return }
                        railAnim.stop()
                        hlist.contentX = newX
                        wheel.accepted = true
                        return
                    }

                    // A plain vertical wheel belongs to the page behind the row.
                    if (dy !== 0) {
                        var p = root.parent
                        var scrollParent = null
                        while (p) {
                            if (p.contentY !== undefined && p.contentHeight !== undefined && p.flickableDirection !== undefined) {
                                scrollParent = p
                                break
                            }
                            p = p.parent
                        }
                        if (scrollParent) {
                            var maxY = Math.max(0, scrollParent.contentHeight - scrollParent.height)
                            scrollParent.contentY = Math.max(0, Math.min(maxY, scrollParent.contentY - dy * step))
                            wheel.accepted = true
                            return
                        }
                    }
                    wheel.accepted = false
                }
            }

            // An inline component cannot see the ids around it, so everything
            // it needs is handed in at the instantiation below.
            component RailArrow: Rectangle {
                id: arrow
                property bool pointsRight: true
                property bool canScroll: false
                property bool rowHovered: false
                property real coverSize: 0
                signal activated()

                width: 34; height: 34; radius: 17
                y: (coverSize - height) / 2
                color: arrowHov.hovered ? Theme.surfaceHov : Theme.surfaceHigh
                border.width: 1
                border.color: Theme.border
                visible: opacity > 0
                opacity: (rowHovered && canScroll) ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 120 } }

                VectorIcon {
                    anchors.centerIn: parent
                    name: arrow.pointsRight ? "chevron-right" : "chevron-left"
                    color: Theme.textPrimary
                    width: 16; height: 16; strokeWidth: 2
                }
                HoverHandler { id: arrowHov; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: arrow.activated() }
            }

            RailArrow {
                x: 8
                pointsRight: false
                rowHovered: railHov.hovered
                coverSize: root.cardSize
                canScroll: hlist.contentX > 1
                onActivated: rail.scrollBy(-hlist.width * 0.8)
            }
            RailArrow {
                x: rail.width - width - 8
                pointsRight: true
                rowHovered: railHov.hovered
                coverSize: root.cardSize
                canScroll: hlist.contentX < rail.maxX - 1
                onActivated: rail.scrollBy(hlist.width * 0.8)
            }
        }
    }

    // NOTE: there used to be a full-section hoverEnabled MouseArea here, meant
    // to drive scrollbar visibility. Nothing ever read it (the scrollbar is
    // AlwaysOff), but because it sat on top of every card it claimed the hover
    // and forced the default arrow cursor — so the cards never showed the
    // pointing hand their own HoverHandlers ask for. Do not reintroduce it: use
    // a HoverHandler, which cooperates with the handlers underneath instead of
    // shadowing them.
}
