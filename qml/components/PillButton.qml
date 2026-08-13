import QtQuick
import TidalWave

// Shared pill-shaped action button (Play / Shuffle / etc.) used across
// Album, Artist, Playlist and Mix pages — keyboard accessible (Tab to
// focus, Enter/Space to activate) with a visible focus ring.
Item {
    id: root

    property string text: ""
    property string glyph: ""
    property bool   accent: true
    // Override for the icon color (e.g. a green check for a completed state).
    property color  glyphColor: accent ? "white" : Theme.textPrimary
    // Renders the glyph inside a filled circle of glyphColor (the Spotify-style
    // "downloaded" badge). The glyph itself flips to the background color.
    property bool   glyphBadge: false

    signal clicked()

    width: 120
    height: 40
    activeFocusOnTab: true

    Rectangle {
        anchors.fill: parent
        radius: 20
        color: root.accent ? Theme.accent : Theme.surfaceHigh
        border.width: root.activeFocus ? 2 : (root.accent ? 0 : 1)
        border.color: root.activeFocus ? Theme.accent : Theme.border

        Row {
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                visible: root.glyphBadge && root.glyph !== ""
                width: 16; height: 16; radius: 8
                color: root.glyphColor
                anchors.verticalCenter: parent.verticalCenter
                VectorIcon {
                    anchors.centerIn: parent
                    name: "arrow-down-filled"
                    color: Theme.surfaceHigh
                    width: 10; height: 10
                }
            }
            VectorIcon {
                id: glyphIcon
                name: root.glyph === "▶" ? "play"
                    : root.glyph === "⇌" ? "shuffle"
                    : root.glyph === "♥" ? "heart-filled"
                    : root.glyph === "♡" ? "heart"
                    : root.glyph === "✎" ? "edit"
                    : root.glyph
                color: root.glyphColor
                width: 14
                height: 14
                strokeWidth: 1.8
                anchors.verticalCenter: parent.verticalCenter
                visible: root.glyph !== "" && !root.glyphBadge
            }
            Text { text: root.text;  color: root.accent ? "white" : Theme.textPrimary; font.pixelSize: 14; font.bold: root.accent; anchors.verticalCenter: parent.verticalCenter }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler   { onTapped: root.clicked() }
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed:  root.clicked()
}
