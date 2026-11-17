import QtQuick
import QtQuick.Window
import TidalWave

// A track's or album's artists, each name its own link.
//
// The joined "A, B, C" string used to be one link to A, so B and C could not
// be reached from anywhere a track was shown. When the names do not fit the
// width they fall back to that elided string, which keeps a long credit on
// one line instead of clipping it mid-name.
Item {
    id: root

    property var    artists: []        // [{id, name}]
    property string fallbackText: ""   // the joined string
    property color  color: Theme.textSec
    property color  hoverColor: Theme.textPrimary
    property int    pixelSize: 12
    property bool   bold: false
    property bool   linksEnabled: true
    // A MouseArea that already owns hover for this region (TrackRow's). Hover
    // is then read off it instead of tracked here: anything that tracks hover
    // on a child takes it away from the row underneath.
    property Item   hoverSource: null

    // Tab stops and Return/Space on each link. Off in track rows, where a
    // stop per name would bury the list.
    property bool   keyboardFocus: false

    // Emitted instead of navigating when a caller wants to do something else.
    property bool   navigateOnClick: true
    signal artistClicked(var artistId)

    readonly property bool hasList: !!artists && artists.length > 0
    readonly property bool useLinks: linksEnabled && hasList && linkRow.implicitWidth <= width + 0.5

    implicitWidth: plain.implicitWidth
    implicitHeight: plain.implicitHeight

    // Too wide for separate names: the whole credit is one link to the first
    // artist, which is what it was before the names were split.
    readonly property var firstArtistId: {
        if (!hasList) return 0
        for (var i = 0; i < artists.length; i++)
            if (Number(artists[i].id) > 0) return Number(artists[i].id)
        return 0
    }

    Text {
        id: plain
        anchors.fill: parent
        visible: !root.useLinks
        text: root.fallbackText
        color: hovered ? root.hoverColor : root.color
        font.pixelSize: root.pixelSize
        font.bold: root.bold
        font.underline: hovered
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter

        readonly property bool isLink: root.linksEnabled && root.firstArtistId > 0 && !root.useLinks
        readonly property real linkWidth: Math.min(implicitWidth, width)
        readonly property bool hovered: {
            if (!isLink) return false
            var src = root.hoverSource
            if (!src) return plainHover.hovered && plainHover.point.position.x <= linkWidth
            if (!src.containsMouse) return false
            var p = mapFromItem(src, src.mouseX, src.mouseY)
            return p.x >= 0 && p.x <= linkWidth && p.y >= 0 && p.y <= height
        }
        HoverHandler { id: plainHover; enabled: !root.hoverSource && plain.isLink }
        activeFocusOnTab: root.keyboardFocus && isLink
        Keys.onReturnPressed: root.open(root.firstArtistId)
        Keys.onSpacePressed:  root.open(root.firstArtistId)
        Rectangle {
            width: plain.linkWidth + 8; height: parent.height + 8; x: -4; y: -4
            radius: 4; color: "transparent"
            border.width: plain.activeFocus ? 2 : 0
            border.color: Theme.accent
        }
        MouseArea {
            width: plain.linkWidth
            height: parent.height
            visible: plain.isLink
            cursorShape: Qt.PointingHandCursor
            onClicked: root.open(root.firstArtistId)
        }
    }

    function open(id) {
        if (!(id > 0)) return
        root.artistClicked(id)
        if (root.navigateOnClick)
            Window.window.navigate("artist", { artistId: id })
    }

    Row {
        id: linkRow
        visible: root.useLinks
        anchors.verticalCenter: parent.verticalCenter

        Repeater {
            model: root.hasList ? root.artists : []

            Row {
                id: entry
                required property var modelData
                required property int index
                readonly property bool isLink: Number(modelData.id) > 0

                Text {
                    id: nameText
                    text: entry.modelData.name || ""
                    font.pixelSize: root.pixelSize
                    font.bold: root.bold
                    font.underline: hovered
                    color: hovered ? root.hoverColor : root.color

                    readonly property bool hovered: {
                        if (!entry.isLink || !root.useLinks) return false
                        var src = root.hoverSource
                        if (!src) return ownHover.hovered
                        if (!src.containsMouse) return false
                        var p = mapFromItem(src, src.mouseX, src.mouseY)
                        return p.x >= 0 && p.x <= width && p.y >= 0 && p.y <= height
                    }

                    HoverHandler {
                        id: ownHover
                        enabled: !root.hoverSource && entry.isLink
                        cursorShape: Qt.PointingHandCursor
                    }

                    // Invisible rather than disabled for a name with no artist
                    // page: a disabled MouseArea still sets the cursor.
                    MouseArea {
                        anchors.fill: parent
                        visible: entry.isLink
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.open(Number(entry.modelData.id))
                    }

                    activeFocusOnTab: root.keyboardFocus && entry.isLink && root.useLinks
                    Keys.onReturnPressed: root.open(Number(entry.modelData.id))
                    Keys.onSpacePressed:  root.open(Number(entry.modelData.id))
                    Rectangle {
                        anchors.fill: parent; anchors.margins: -4; radius: 4; color: "transparent"
                        border.width: nameText.activeFocus ? 2 : 0
                        border.color: Theme.accent
                    }
                }

                Text {
                    visible: entry.index < root.artists.length - 1
                    text: ", "
                    font.pixelSize: root.pixelSize
                    font.bold: root.bold
                    color: root.color
                }
            }
        }
    }
}
