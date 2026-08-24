import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import TidalWave

Item {
    id: root
    height: 52
    implicitWidth: 100

    property int    trackNum: 1
    property string title: ""
    property string artists: ""
    property string albumTitle: ""
    property string durationStr: ""
    property string coverUrl: ""
    property bool   isPlaying: false
    // True between the click and the first audio: the row shows a subtle
    // highlight and a spinner instead of the track number, so the click gets
    // immediate feedback while the stream URL is fetched.
    property bool   isLoading: false
    property bool   showAlbum: true
    property bool   showCover: true
    property var    trackData: null   // full track map (has albumId, id, etc.)

    // The track the player is on, whether or not it is playing. isPlaying and
    // isLoading are both false while paused — and after a session restore,
    // where the queue is back but no stream is loaded — so neither can mark
    // the row on its own. Derived here rather than passed in by every page.
    readonly property bool isCurrent: !!(trackData && player.currentTrack
                                         && player.currentTrack.id === trackData.id)

    // Local library tracks have no Tidal id, so every action that talks to
    // Tidal (download, favourite, radio, album/artist links, share URL) is
    // hidden for them. Playback and queueing work exactly the same.
    readonly property bool isLocalTrack: !!(trackData && trackData.localPath)

    // Tidal still lists tracks whose rights expired, but refuses to stream
    // them. They are shown greyed out and cannot be played or queued.
    readonly property bool isUnavailable: !!(trackData && trackData.available === false)
    property int    localPlaylistId: 0   // >0 when shown inside a local playlist

    // Selection + drag. `selection` is the page's TrackSelection object and
    // `rowIndex` this row's position in its list; both must be set for click
    // selection and dragging to work. Pages that set neither keep the old
    // behaviour where a single click plays.
    property var selection: null
    property int rowIndex: -1
    readonly property bool selected: (selection && trackData)
                                     ? selection.isSelected(trackData.id) : false

    property bool   isLiked: (trackData && !trackData.localPath) ? bridge.isTrackFavorite(trackData.id) : false
    // Playlist context: set when TrackRow is inside a PlaylistPage
    property string playlistUuid: ""
    property int    trackItemIndex: -1  // 0-based position in playlist
    property bool   showPopularity: false
    // Download state for this row: "idle" | "busy" | "done" | "error"
    property string dlState: "idle"
    property string dlError: ""

    Connections {
        target: bridge
        function onFavoriteTracksChanged() {
            root.isLiked = (root.trackData && !root.trackData.localPath) ? bridge.isTrackFavorite(root.trackData.id) : false
        }
    }

    // Reflect download progress for this track. Delegates are recycled on scroll,
    // so re-evaluate whenever trackData is (re)assigned.
    Connections {
        target: downloader
        function onDownloadStarted(id) {
            if (root.trackData && id === root.trackData.id) root.dlState = "busy"
        }
        function onDownloadFinished(id, path) {
            if (root.trackData && id === root.trackData.id) { root.dlState = "done"; dlResetTimer.restart() }
        }
        function onDownloadError(id, msg) {
            if (root.trackData && id === root.trackData.id) { root.dlState = "error"; root.dlError = msg; dlResetTimer.restart() }
        }
    }
    Timer { id: dlResetTimer; interval: 3000; onTriggered: root.dlState = "idle" }
    onTrackDataChanged: {
        root.dlState = (root.trackData && downloader.isDownloading(root.trackData.id)) ? "busy" : "idle"
        root.dlError = ""
    }

    // ── drag source ────────────────────────────────────
    // The dragged item cannot be this row: ListView clips it and recycles the
    // delegate mid-drag. Instead the window hosts one shared ghost that this
    // row borrows for the duration of the drag.
    property Item dragProxy: null
    property bool didDrag: false          // true once a press became a drag

    // The layer is looked up once, on press, and remembered. Teardown can run
    // while the row itself is being destroyed — and by then `Window.window` is
    // already null, so every path that reached for it there threw instead of
    // releasing, leaving the ghost and the global drag flag stuck on for good.
    property Item dragLayer: null

    // The proxy is prepared on press because MouseArea needs a drag.target
    // before the threshold is crossed. It stays invisible until the drag
    // actually starts — see onDragActiveChanged below.
    function beginDrag(mouse) {
        var layer = Window.window ? Window.window.dragLayer : null
        if (!layer || !root.selection) return
        var payload = root.selection.selectedTracks()
        if (payload.length === 0) return
        root.dragProxy = layer.acquire(payload, root.isLocalTrack ? "local" : "tidal", root)
        if (!root.dragProxy) return
        root.dragLayer = layer
        // A drag lives on this row's MouseArea, so the row has to outlive it.
        // Auto-scrolling towards a distant drop target sweeps the source row
        // out of view, and a plain delegate is destroyed once it gets there —
        // taking the mouse grab with it, mid-drag. ListView keeps the current
        // item instantiated wherever it has scrolled to, so claiming it is
        // what lets a drag reach the far end of a long playlist. No list here
        // draws a highlight or reads isCurrentItem, so this is invisible.
        if (root.ListView.view) root.ListView.view.currentIndex = root.rowIndex
        // Pin the proxy origin to the cursor: that origin is the point drop
        // targets are hit-tested against.
        var p = mapToItem(layer, mouse.x, mouse.y)
        root.dragProxy.x = p.x
        root.dragProxy.y = p.y
    }

    // A delegate can still be destroyed mid-drag — a model reload, or leaving
    // the page. Nothing else would ever release the drag then.
    Component.onDestruction: {
        if (root.dragProxy && root.dragLayer) root.dragLayer.release(root)
    }

    function endDrag(deliver) {
        var layer = root.dragLayer
        if (layer && root.dragProxy && deliver) layer.drop(root)   // deliver, then tear down
        root.dragProxy = null
        if (layer) layer.release(root)
    }

    readonly property bool dragActive: hov.drag.active
    onDragActiveChanged: {
        var layer = root.dragLayer
        if (!layer) return
        if (root.dragActive) { root.didDrag = true; layer.show() }
        else                  layer.release(root)
    }

    signal playRequested()
    signal menuRequested(real x, real y)
    signal removeFromPlaylistRequested(int itemIndex)

    activeFocusOnTab: true
    Keys.onReturnPressed: { if (!root.isUnavailable) root.playRequested() }
    Keys.onSpacePressed:  { if (!root.isUnavailable) root.playRequested() }
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Menu || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
            root.menuRequested(width / 2, height / 2)
            contextMenu.popup()
            event.accepted = true
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 6
        // isLoading is tested before isCurrent: the current track is already
        // set while its stream loads, so the dim "buffering" tint would never
        // be seen if the brighter one won first.
        color: root.selected ? Qt.rgba(1, 1, 1, 0.13)
               : isLoading ? Qt.rgba(0, 0.698, 0.973, 0.035)
               : (isPlaying || isCurrent) ? Qt.rgba(0, 0.698, 0.973, 0.08)
               : hov.hovered ? Theme.surfaceHov : "transparent"
        border.width: root.activeFocus ? 2 : 0
        border.color: Theme.accent

        Behavior on color { ColorAnimation { duration: 120 } }

        MouseArea {
            id: hov
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: root.isUnavailable ? Qt.ArrowCursor : Qt.PointingHandCursor
            readonly property bool hovered: containsMouse

            drag.target: root.selection ? root.dragProxy : null
            drag.threshold: 8
            // ListView is a Flickable and steals the grab as soon as the
            // pointer moves along its scroll axis, which cancelled every
            // vertical drag — exactly the ones that reorder a playlist.
            preventStealing: root.selection !== null

            // A press that turns into a drag must carry the whole selection,
            // so the row joins the selection on press rather than on release.
            onPressed: (mouse) => {
                root.didDrag = false
                if (mouse.button !== Qt.LeftButton || !root.selection) return
                if (!root.selected) root.selection.handleClick(root.rowIndex, mouse.modifiers)
                root.beginDrag(mouse)
            }

            onReleased: root.endDrag(true)    // a real release drops
            onCanceled: root.endDrag(false)   // a cancelled grab does not

            // Click selects; double click plays — matching the Tidal client
            // and the desktop convention.
            onClicked: (mouse) => {
                if (mouse.button === Qt.RightButton) {
                    if (root.selection && !root.selected)
                        root.selection.selectOnly(root.rowIndex)
                    contextMenu.popup()
                    return
                }
                if (root.didDrag) return                  // the press became a drag
                if (root.selection) root.selection.handleClick(root.rowIndex, mouse.modifiers)
                else if (!root.isUnavailable) root.playRequested()
            }

            onDoubleClicked: (mouse) => {
                if (mouse.button === Qt.LeftButton && !root.isUnavailable)
                    root.playRequested()
            }

            ToolTip.visible: root.isUnavailable && containsMouse
            ToolTip.text: "No longer available on Tidal"
            ToolTip.delay: 500
        }

        RowLayout {
            anchors { fill: parent; leftMargin: 12; rightMargin: 28 }
            spacing: 12

            // Track number / now playing indicator
            Item {
                width: 24
                Layout.alignment: Qt.AlignVCenter
                Text {
                    anchors.centerIn: parent
                    visible: !isCurrent && !isLoading && (!hov.hovered || root.isUnavailable)
                    text: root.trackNum
                    color: Theme.textDim
                    font.pixelSize: 13
                }
                VectorIcon {
                    anchors.centerIn: parent
                    visible: isCurrent && !isLoading && !hov.hovered
                    name: "music"
                    color: Theme.accent
                    width: 14
                    height: 14
                    strokeWidth: 1.5
                }
                // Loading spinner (same idiom as the download spinner). It wins
                // over the hover glyph: the pointer is still on the row right
                // after the click, and the spinner is the useful feedback.
                Item {
                    id: loadSpinner
                    anchors.centerIn: parent
                    width: 14; height: 14
                    visible: root.isLoading
                    Rectangle {
                        width: 3; height: 6; radius: 1.5
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: Theme.accent
                    }
                    RotationAnimator {
                        target: loadSpinner
                        from: 0; to: 360
                        duration: 800
                        loops: Animation.Infinite
                        running: root.isLoading && loadSpinner.visible && AppFocus.active
                    }
                }
                Text {
                    anchors.centerIn: parent
                    visible: hov.hovered && !isLoading && !root.isUnavailable
                    text: isPlaying ? "⏸" : "▶"
                    color: Theme.textPrimary
                    font.pixelSize: 14
                }
            }

            // Cover art
            Rectangle {
                visible: showCover
                width: 36; height: 36; radius: 4
                color: Theme.surfaceHigh
                clip: true
                opacity: root.isUnavailable ? 0.35 : 1.0
                Image {
                    anchors.fill: parent
                    source: coverUrl.length > 0 ? "image://tidal/" + coverUrl : ""
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                    mipmap: true
                }
            }

            // Title + artists
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Text {
                    Layout.fillWidth: true
                    text: root.title
                    color: root.isUnavailable ? Theme.textDim
                           : (isPlaying || isCurrent) ? Theme.accent : Theme.textPrimary
                    font.pixelSize: 14
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: root.artists
                    color: root.isUnavailable ? Theme.textDim : Theme.textSec
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            // Album — a link to the album page for Tidal tracks. Local files
            // have no album to open, so there it stays plain text.
            Text {
                id: albumText
                visible: showAlbum
                Layout.preferredWidth: 160
                text: root.albumTitle
                color: albumText.linkHovered ? Theme.textPrimary
                       : root.isUnavailable ? Theme.textDim : Theme.textSec
                font.pixelSize: 13
                font.underline: albumText.linkHovered
                elide: Text.ElideRight

                readonly property bool isLink: !root.isLocalTrack
                                               && !!(root.trackData && root.trackData.albumId > 0)
                // The cell is a fixed 160 wide but the name rarely fills it.
                // Both the hit area and the hover test stop at the text, so the
                // empty gap beside a short album name neither underlines nor
                // swallows the press that starts a drag or selects the row.
                readonly property real linkWidth: Math.min(implicitWidth, width)
                // Hover is read off the row's own MouseArea rather than a
                // hoverEnabled area here: anything that tracks hover on a child
                // takes it away from the row, and the row highlight and the
                // menu button both follow the row's hover.
                readonly property bool linkHovered: {
                    if (!isLink || text.length === 0 || !hov.hovered) return false
                    var p = mapFromItem(hov, hov.mouseX, hov.mouseY)
                    return p.x >= 0 && p.x <= linkWidth && p.y >= 0 && p.y <= height
                }

                MouseArea {
                    width: albumText.linkWidth
                    height: parent.height
                    enabled: albumText.isLink
                    // Disabled does not stop a MouseArea from setting the
                    // cursor, so this has to follow isLink too.
                    cursorShape: albumText.isLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: Window.window.navigate("album", { albumId: root.trackData.albumId })
                }
            }

            // Duration
            Text {
                text: root.durationStr
                color: Theme.textDim
                font.pixelSize: 13
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
            }

            // Popularity — shown only when showPopularity is true (Search page)
            Text {
                id: popText
                visible: root.showPopularity && root.trackData && root.trackData.popularity > 0
                text: root.trackData ? root.trackData.popularity + "%" : ""
                color: Theme.textDim
                font.pixelSize: 11
                Layout.preferredWidth: 40
                horizontalAlignment: Text.AlignRight
                ToolTip.visible: popHov.hovered && visible
                ToolTip.text: "Popularity"
                ToolTip.delay: 400
                HoverHandler { id: popHov }
            }

            // Download button — revealed on hover; stays visible while busy/done/error
            Item {
                id: dlButton
                visible: !root.isLocalTrack && !root.isUnavailable && (hov.hovered || root.dlState !== "idle")
                Layout.preferredWidth: 24
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignVCenter

                // idle / error glyph (error tints red)
                VectorIcon {
                    anchors.centerIn: parent
                    visible: root.dlState === "idle" || root.dlState === "error"
                    name: "download"
                    color: root.dlState === "error" ? Theme.red : Theme.textSec
                    width: 16; height: 16
                    strokeWidth: 1.8
                }
                // done glyph
                VectorIcon {
                    anchors.centerIn: parent
                    visible: root.dlState === "done"
                    name: "check"
                    color: Theme.green
                    width: 16; height: 16
                    strokeWidth: 2
                }
                // busy spinner (matches LoadingOverlay idiom)
                Item {
                    id: dlSpinner
                    anchors.centerIn: parent
                    width: 16; height: 16
                    visible: root.dlState === "busy"
                    Rectangle {
                        width: 3; height: 7; radius: 1.5
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        color: Theme.accent
                    }
                    RotationAnimator {
                        target: dlSpinner
                        from: 0; to: 360
                        duration: 800
                        loops: Animation.Infinite
                        running: root.dlState === "busy" && dlSpinner.visible && AppFocus.active
                    }
                }

                // Mirror the menu button exactly: a plain click MouseArea with NO
                // hover detection. Anything that tracks hover on the button itself
                // (hoverEnabled MouseArea or a HoverHandler) desyncs from the row's
                // hover and makes the button flicker/shift as it toggles.
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    enabled: root.dlState !== "busy"
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.trackData && root.dlState !== "busy")
                            downloader.downloadTrack(root.trackData)
                    }
                }
            }

            // Context menu button
            Item {
                visible: hov.hovered
                Layout.preferredWidth: 24
                Layout.fillHeight: true
                Layout.alignment: Qt.AlignVCenter
                VectorIcon {
                    anchors.centerIn: parent
                    name: "more"
                    color: Theme.textSec
                    width: 16
                    height: 16
                }
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -4
                    cursorShape: Qt.PointingHandCursor
                    onClicked: (m) => {
                        root.menuRequested(m.x, m.y)
                        contextMenu.popup()
                    }
                }
            }
        }
    }

    Menu {
        id: contextMenu
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 8; implicitWidth: 200 }

        // Shown in place of the playback items when Tidal no longer streams
        // the track, so the reason is visible where the user looks for it.
        MenuItem {
            text: "⃠  Not available on Tidal"
            visible: root.isUnavailable
            height: visible ? implicitHeight : 0
            enabled: false
            contentItem: Text { text: parent.text; color: Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: "transparent" }
        }
        MenuItem {
            text: "▶  Play now"
            visible: !root.isUnavailable
            height: visible ? implicitHeight : 0
            contentItem: Text { text: parent.text; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: root.playRequested()
        }
        MenuItem {
            text: "+  Add to queue"
            visible: !root.isUnavailable
            height: visible ? implicitHeight : 0
            contentItem: Text { text: parent.text; color: Theme.textPrimary; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: { if (root.trackData) player.appendQueue([root.trackData]) }
        }
        MenuItem {
            text: "⬇  Download…"
            visible: !root.isLocalTrack && !root.isUnavailable
            height: visible ? implicitHeight : 0
            enabled: root.trackData !== null && root.dlState !== "busy"
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: { if (root.trackData) downloader.downloadTrack(root.trackData) }
        }
        MenuItem {
            text: "📋  Add to playlist"
            enabled: root.trackData !== null
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (!root.trackData) return
                // Acting on a multi-selection adds everything selected.
                var picked = (root.selection && root.selected && root.selection.count > 1)
                             ? root.selection.selectedTracks() : [root.trackData]
                if (root.isLocalTrack) {
                    var localIds = []
                    for (var i = 0; i < picked.length; i++) localIds.push(picked[i].localId)
                    localPlaylistPicker.openFor(localIds)
                } else {
                    var ids = []
                    for (var j = 0; j < picked.length; j++) ids.push(picked[j].id)
                    playlistPicker.openFor(ids)
                }
            }
        }
        MenuItem {
            text: "🗑  Remove from playlist"
            visible: root.playlistUuid.length > 0 || root.localPlaylistId > 0
            height: visible ? implicitHeight : 0
            enabled: root.trackData !== null && (root.playlistUuid.length > 0 || root.localPlaylistId > 0)
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.red : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (root.trackData && root.trackItemIndex >= 0
                    && (root.playlistUuid.length > 0 || root.localPlaylistId > 0))
                    root.removeFromPlaylistRequested(root.trackItemIndex)
            }
        }
        MenuItem {
            text: "📻  Start radio"
            visible: !root.isLocalTrack
            height: visible ? implicitHeight : 0
            enabled: root.trackData !== null
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (!root.trackData) return
                Window.window.navigate("radio", {
                    trackId:    root.trackData.id,
                    radioTitle: root.trackData.title
                })
            }
        }
        MenuItem {
            text: root.isLiked ? "♥  Unlike" : "♡  Like"
            visible: !root.isLocalTrack
            height: visible ? implicitHeight : 0
            enabled: root.trackData !== null
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (!root.trackData) return
                if (root.isLiked) {
                    bridge.removeTrackFavorite(root.trackData.id, function(success) {})
                } else {
                    bridge.addTrackFavorite(root.trackData.id, function(success) {})
                }
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "💿  Go to album"
            visible: !root.isLocalTrack
            height: visible ? implicitHeight : 0
            enabled: root.trackData && root.trackData.albumId > 0
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (root.trackData && root.trackData.albumId > 0)
                    Window.window.navigate("album", { albumId: root.trackData.albumId })
            }
        }
        MenuItem {
            text: "🎤  Go to artist"
            visible: !root.isLocalTrack
            height: visible ? implicitHeight : 0
            enabled: root.trackData && root.trackData.artistId > 0
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (root.trackData && root.trackData.artistId > 0)
                    Window.window.navigate("artist", { artistId: root.trackData.artistId })
            }
        }
        MenuSeparator {}
        MenuItem {
            text: "🔗  Copy link"
            visible: !root.isLocalTrack
            height: visible ? implicitHeight : 0
            enabled: root.trackData !== null
            contentItem: Text { text: parent.text; color: parent.enabled ? Theme.textPrimary : Theme.textDim; font.pixelSize: 13; leftPadding: 12; horizontalAlignment: Text.AlignLeft; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: parent.highlighted ? Theme.surfaceHov : "transparent" }
            onTriggered: {
                if (root.trackData)
                    bridge.copyToClipboard("https://tidal.com/browse/track/" + root.trackData.id)
            }
        }
    }

    // Playlist picker popup (for "Add to playlist")
    Popup {
        id: playlistPicker
        anchors.centerIn: Overlay.overlay
        width: 340
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0
        property var pendingTrackIds: []

        function openFor(trackIds) {
            pendingTrackIds = trackIds
            plPickerModel.clear()
            open()
            bridge.fetchUserPlaylists(function(pls, err) {
                plPickerModel.clear()
                for (var i = 0; i < pls.length; i++) {
                    plPickerModel.append(pls[i])
                }
            }, 50, 0)
        }

        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        Column {
            width: parent.width

            Item {
                width: parent.width
                height: 52
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Add to playlist"
                    color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
                }
                VectorIcon {
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "x"; color: Theme.textSec; width: 12; height: 12; strokeWidth: 2
                    MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: playlistPicker.close() }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.border }

            ListView {
                id: plPickerList
                width: parent.width
                height: Math.min(contentHeight, 300)
                clip: true
                model: ListModel { id: plPickerModel }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    width: plPickerList.width
                    height: 44
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 6
                        color: plHov2.hovered ? Theme.surfaceHov : "transparent"
                        HoverHandler { id: plHov2 }
                        TapHandler {
                            onTapped: {
                                bridge.addTracksToPlaylist(model.uuid, playlistPicker.pendingTrackIds, function(ok) {})
                                playlistPicker.close()
                            }
                        }
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            Rectangle {
                                width: 28; height: 28; radius: 4; color: Theme.surface; clip: true
                                Image {
                                    anchors.fill: parent
                                    source: model.coverUrl ? "image://tidal/" + model.coverUrl : ""
                                    fillMode: Image.PreserveAspectCrop; smooth: true
                                }
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1
                                Text { text: model.title; color: Theme.textPrimary; font.pixelSize: 13 }
                                Text { text: model.numTracks + " tracks"; color: Theme.textSec; font.pixelSize: 11 }
                            }
                        }
                    }
                }
            }

            Item { width: parent.width; height: 8 }
        }
    }

    // Local-library counterpart of playlistPicker above. Local playlists live
    // only in this app, so this list never touches the Tidal API.
    Popup {
        id: localPlaylistPicker
        anchors.centerIn: Overlay.overlay
        width: 340
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0
        property var pendingLocalIds: []

        function openFor(localIds) {
            pendingLocalIds = localIds
            reload()
            open()
        }
        function reload() {
            localPickerModel.clear()
            var pls = library.playlists()
            for (var i = 0; i < pls.length; i++) localPickerModel.append(pls[i])
        }

        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        Column {
            width: parent.width

            Item {
                width: parent.width
                height: 52
                Text {
                    anchors.left: parent.left; anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Add to local playlist"
                    color: Theme.textPrimary; font.pixelSize: 15; font.bold: true
                }
                VectorIcon {
                    anchors.right: parent.right; anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    name: "x"; color: Theme.textSec; width: 12; height: 12; strokeWidth: 2
                    MouseArea { anchors.fill: parent; anchors.margins: -6; onClicked: localPlaylistPicker.close() }
                }
            }
            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Text {
                visible: localPickerModel.count === 0
                width: parent.width
                padding: 16
                text: "No local playlists yet. Create one on the Local Files page."
                color: Theme.textSec; font.pixelSize: 13; wrapMode: Text.WordWrap
            }

            ListView {
                id: localPickerList
                width: parent.width
                height: Math.min(contentHeight, 300)
                clip: true
                model: ListModel { id: localPickerModel }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                delegate: Item {
                    width: localPickerList.width
                    height: 44
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 4
                        radius: 6
                        color: localPlHov.hovered ? Theme.surfaceHov : "transparent"
                        HoverHandler { id: localPlHov }
                        TapHandler {
                            onTapped: {
                                library.addToPlaylist(model.id, localPlaylistPicker.pendingLocalIds)
                                localPlaylistPicker.close()
                            }
                        }
                        Row {
                            anchors.left: parent.left; anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            Rectangle {
                                width: 28; height: 28; radius: 4
                                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.18)
                                VectorIcon {
                                    anchors.centerIn: parent
                                    name: "music"; color: Theme.accent
                                    width: 14; height: 14; strokeWidth: 1.6
                                }
                            }
                            Column {
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 1
                                Text { text: model.title; color: Theme.textPrimary; font.pixelSize: 13 }
                                Text { text: model.numTracks + " tracks"; color: Theme.textSec; font.pixelSize: 11 }
                            }
                        }
                    }
                }
            }

            Item { width: parent.width; height: 8 }
        }
    }
}
