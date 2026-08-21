import QtQuick
import TidalWave

// Says out loud that Tidal cannot be reached, and offers the retry.
//
// A session that could not be checked keeps running on whatever is saved
// (Auth::holdOffline), which without this reads as an app full of empty pages.
// The card stays up until the check goes through — this is a state, not an
// event, so there is nothing to dismiss.
Item {
    id: root
    visible: auth.offline
    implicitWidth: card.width
    implicitHeight: card.height

    // The automatic retry backs off to minutes; a person who just reconnected
    // should not have to wait for it. Their tap re-checks at once.
    property bool trying: false
    onVisibleChanged: if (!visible) trying = false

    Rectangle {
        id: card
        width: Math.min(360, col.implicitWidth + 28)
        height: col.implicitHeight + 28
        radius: 10
        color: Qt.rgba(0.15, 0.11, 0.04, 0.97)
        border.color: Theme.amber
        border.width: 1

        Column {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 6

            Row {
                spacing: 8
                Text {
                    text: "⚠"
                    color: Theme.amber
                    font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Can't reach Tidal"
                    color: Theme.textPrimary
                    font.pixelSize: 13
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            Text {
                width: parent.width
                // Signed in, so the pages render from what was cached and
                // pinned; signed out, the sign-in itself is what could not be
                // checked.
                text: auth.state === 2
                      ? "You're still signed in — showing what's saved. Retrying on its own."
                      : "Your sign-in couldn't be checked. Nothing has been lost."
                color: Theme.textSec
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }

            Text {
                text: root.trying ? "Trying…" : "Try again"
                color: root.trying ? Theme.textDim
                                   : (retryHov.hovered ? Theme.textPrimary : Theme.accent)
                font.pixelSize: 12
                HoverHandler { id: retryHov; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    enabled: !root.trying
                    onTapped: { root.trying = true; auth.retryNow() }
                }
            }
        }
    }

    // The check either clears `offline` or fails again; either way the label
    // goes back to being tappable rather than sitting on "Trying…".
    Timer {
        interval: 4000
        running: root.trying
        onTriggered: root.trying = false
    }
}
