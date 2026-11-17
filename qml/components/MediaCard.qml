import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import TidalWave

Item {
    id: root

    property string coverUrl: ""
    property string title: ""
    property string subtitle: ""
    property string mediaType: "album"
    property int    cardSize: 160
    // >0 turns the subtitle into a link to that artist. Cards whose subtitle
    // is not one artist — a mix's blurb, "16 tracks", a release year — leave
    // it at 0 and the subtitle stays plain text.
    property int    artistId: 0
    // The id of what the card shows. Only albums use it, to be dragged onto a
    // playlist; a card left at 0 simply cannot be dragged.
    property var    mediaId: 0

    width: cardSize
    height: col.height + 8

    signal clicked()
    signal playClicked()
    signal subtitleClicked()

    // ── drag source ────────────────────────────────────
    // An album can be dropped on a playlist in the sidebar. The drag carries
    // the album id only; its tracks are fetched by the drop target. The
    // dragged item is the window's shared ghost rather than this card, which
    // its view would clip and recycle mid-drag.
    readonly property bool draggable: mediaType === "album" && Number(mediaId) > 0
    property Item dragProxy: null
    // Looked up on press and remembered: teardown can run while the card is
    // being destroyed, and `Window.window` is already null by then.
    property Item dragLayer: null
    property bool didDrag: false          // true once a press became a drag

    function beginDrag(mouse) {
        if (!root.draggable) return
        var layer = Window.window ? Window.window.dragLayer : null
        if (!layer) return
        root.dragProxy = layer.acquireAlbum(root.mediaId, root.title, root)
        if (!root.dragProxy) return
        root.dragLayer = layer
        // Drop targets are hit-tested against the proxy's origin, so it starts
        // pinned to the cursor.
        var p = coverArea.mapToItem(layer, mouse.x, mouse.y)
        root.dragProxy.x = p.x
        root.dragProxy.y = p.y
    }

    function endDrag(deliver) {
        var layer = root.dragLayer
        if (layer && root.dragProxy && deliver) layer.drop(root)   // deliver, then tear down
        root.dragProxy = null
        if (layer) layer.release(root)
    }

    readonly property bool dragActive: coverArea.drag.active
    onDragActiveChanged: {
        var layer = root.dragLayer
        if (!layer) return
        if (root.dragActive) { root.didDrag = true; layer.show() }
        else                  layer.release(root)
    }

    // A card is a view delegate: navigating away destroys it mid-drag, and
    // nothing else would release the ghost then.
    Component.onDestruction: {
        if (root.dragProxy && root.dragLayer) root.dragLayer.release(root)
    }

    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed:  root.clicked()

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 8

        Rectangle {
            id: imgRect
            Layout.fillWidth: true
            height: cardSize
            radius: mediaType === "artist" ? cardSize/2 : Theme.radiusLg
            color: Theme.surfaceHigh
            clip: true
            border.width: root.activeFocus ? 4 : 0
            border.color: Theme.accent

            Image {
                id: img
                anchors.fill: parent
                source: coverUrl.length > 0 ? "image://tidal/" + coverUrl : ""
                fillMode: Image.PreserveAspectCrop
                smooth: true
                mipmap: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.dur(200) } }
            }

            VectorIcon {
                visible: root.coverUrl.length === 0 || img.status === Image.Error
                anchors.centerIn: parent
                name: mediaType === "artist" ? "artist" : "music"
                color: Theme.textDim
                width: 48
                height: 48
                strokeWidth: 1.5
            }

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Qt.rgba(0, 0, 0, hov.hovered ? 0.45 : 0)
                Behavior on color { ColorAnimation { duration: Theme.dur(150) } }

                Rectangle {
                    visible: hov.hovered
                    width: 44
                    height: 44
                    radius: 22
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.margins: 12
                    color: Theme.accent

                    Text {
                        anchors.centerIn: parent
                        text: "▶"
                        color: "white"
                        font.pixelSize: 16
                        leftPadding: 2
                    }

                    scale: playHov.hovered ? 1.05 : 1
                    Behavior on scale { NumberAnimation { duration: Theme.dur(100) } }
                    HoverHandler { id: playHov; cursorShape: Qt.PointingHandCursor }
                    // A MouseArea, not a TapHandler: it has to take the press
                    // away from the cover's drag area underneath, which a
                    // handler sharing the press would not do. hoverEnabled
                    // stays off so the hover handlers around it keep working.
                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.playClicked()
                    }
                }
            }

            HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }

            // Click opens the card; on an album, a press that crosses the drag
            // threshold starts a drag instead. hoverEnabled is left off so the
            // hover handlers keep the overlay and the cursor.
            MouseArea {
                id: coverArea
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                drag.target: root.draggable ? root.dragProxy : null
                drag.threshold: 8
                // The rows and grids these cards sit in are Flickables, and
                // one would steal the grab the moment the pointer moves along
                // its scroll axis.
                preventStealing: root.draggable

                onPressed: (mouse) => {
                    root.didDrag = false
                    root.beginDrag(mouse)
                }
                onReleased: root.endDrag(true)    // a real release drops
                onCanceled: root.endDrag(false)   // a cancelled grab does not
                onClicked: { if (!root.didDrag) root.clicked() }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            // The title opens what the cover opens.
            Text {
                id: titleText
                Layout.fillWidth: true
                text: root.title
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                font.underline: titleHov.containsMouse
                elide: Text.ElideRight
                wrapMode: Text.NoWrap

                MouseArea {
                    id: titleHov
                    // Only as wide as the text: the label fills the card, and a
                    // full-width hit area would underline the gap beside a
                    // short title.
                    width: Math.min(titleText.implicitWidth, titleText.width)
                    height: parent.height
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.clicked()
                }
            }

            Text {
                id: subtitleText
                readonly property bool isLink: root.artistId > 0 && root.subtitle.length > 0
                Layout.fillWidth: true
                text: root.subtitle
                color: isLink && subtitleHov.containsMouse ? Theme.textPrimary : Theme.textSec
                font.pixelSize: 12
                font.underline: isLink && subtitleHov.containsMouse
                elide: Text.ElideRight
                wrapMode: Text.NoWrap

                MouseArea {
                    id: subtitleHov
                    width: Math.min(subtitleText.implicitWidth, subtitleText.width)
                    height: parent.height
                    enabled: subtitleText.isLink
                    hoverEnabled: true
                    // A disabled MouseArea still applies its cursorShape, so a
                    // subtitle that links nowhere — a mix's blurb, a track
                    // count — would offer the hand and then do nothing.
                    cursorShape: subtitleText.isLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.subtitleClicked()
                }
            }
        }
    }
}
