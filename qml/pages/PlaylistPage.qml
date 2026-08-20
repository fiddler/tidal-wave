import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TidalWave

Rectangle {
    id: root

    // Exposed so Cmd+A and Escape can reach it from the window.

    property var pageSelection: trackSel

    // Click-selection state for the track list below.
    TrackSelection { id: trackSel; tracks: root.tracks }
    color: Theme.bg

    property string playlistUuid: ""
    property string playlistTitle: ""
    property string coverUrl: ""
    property string playlistDescription: ""
    property int    playlistDuration: 0
    property string playlistType: ""   // "USER" = editable, "" / "EDITORIAL" = read-only
    property var    tracks: []
    property bool   loading: false

    readonly property bool isUserPlaylist: playlistType === "USER"

    // Records this playlist as the "playing from" source, then starts playback.
    function playFrom(list, i) {
        player.setPlaybackSource("playlist", root.playlistUuid, root.playlistTitle)
        player.playTracks(list, i)
    }

    // Offline pin state for this playlist: {state: "none|syncing|offline|error",
    // done, total}. The pill button below renders it; refreshed on every
    // OfflineManager signal for this uuid.
    property var offlineStatus: ({ state: "none", done: 0, total: 0 })
    property var offlineEstimate: ({ count: 0, sizeStr: "", timeStr: "" })

    function refreshOffline() {
        if (playlistUuid.length > 0) offlineStatus = offline.status(playlistUuid)
    }

    Connections {
        target: offline
        function onPlaylistChanged(uuid) {
            if (uuid === root.playlistUuid) root.refreshOffline()
        }
    }

    onPlaylistUuidChanged: if (playlistUuid.length > 0) { loadPlaylist(); refreshOffline() }

    // The list as it will look once the move lands. Mirrors the ordering the
    // server is asked for, so the optimistic view matches the result.
    function reorderedTracks(list, fromIndices, toIndex) {
        var from = fromIndices.slice().sort(function(a, b) { return a - b })
        var above = 0
        for (var i = 0; i < from.length; i++) if (from[i] < toIndex) above++
        var target = Math.max(0, Math.min(toIndex - above, list.length - from.length))
        var moved = []
        for (var j = 0; j < from.length; j++) moved.push(list[from[j]])
        var rest = list.slice()
        for (var k = from.length - 1; k >= 0; k--) rest.splice(from[k], 1)
        for (var m = 0; m < moved.length; m++) rest.splice(target + m, 0, moved[m])
        return rest
    }

    // Handing the ListView a new model array resets it to the top, which after
    // a reorder or a removal throws the user back to track 1 of a few hundred.
    // The edits that rewrite `tracks` in place — reorder, remove, resync —
    // therefore put the viewport back; a fresh load still starts at the top,
    // which is where it belongs.
    //
    // The list starts at `originY`, not at 0: the page header lives above the
    // first row, so the top of a playlist is contentY === -headerHeight.
    function restoreScroll(y) {
        var min = tracksList.originY
        var max = min + tracksList.contentHeight - tracksList.height
        if (max <= min) return                     // nothing to scroll yet
        tracksList.contentY = Math.max(min, Math.min(y, max))
    }

    // Refetch without raising the loading overlay — used to resync after a
    // failed write, where the server is the only trustworthy source.
    function silentReload() {
        bridge.fetchPlaylistTracks(playlistUuid, function(t, err) {
            if (err) return
            // Read the position here, not at call time: the user is free to
            // scroll while the request is in flight.
            var y = tracksList.contentY
            root.tracks = t
            root.restoreScroll(y)
        })
    }

    // Tidal moves one row per request, each preceded by an ETag fetch, so a
    // drag would otherwise freeze the list for seconds. Show the result at
    // once and reconcile in the background.
    function reorderTracks(fromIndices, toIndex) {
        if (!root.isUserPlaylist || fromIndices.length === 0) return
        var y = tracksList.contentY
        root.tracks = root.reorderedTracks(root.tracks, fromIndices, toIndex)
        root.restoreScroll(y)
        SyncState.begin("Syncing playlist changes…")

        // Walk bottom-up so the indices of the rows still to move stay valid.
        var ordered = fromIndices.slice().sort(function(a, b) { return a - b })
        var step = function(k, insertAt) {
            if (k < 0) { SyncState.end(); return }
            var from = ordered[k]
            var to = from < insertAt ? insertAt - 1 : insertAt
            if (from === to) { step(k - 1, insertAt); return }
            bridge.moveTrackInPlaylist(root.playlistUuid, from, to, function(ok) {
                if (!ok) {
                    // Part of the move may have landed, so take the server's
                    // word for the order rather than assuming the old one.
                    SyncState.fail("Tidal rejected the new order for “" + root.playlistTitle
                                   + "”. The list has been put back to what Tidal has.")
                    root.silentReload()
                    return
                }
                step(k - 1, to)
            })
        }
        step(ordered.length - 1, toIndex)
    }

    function loadPlaylist() {
        loading = true
        bridge.fetchPlaylistTracks(playlistUuid, function(t, err) {
            loading = false
            if (!err) { tracks = t; return }
            // No network — a pinned playlist still renders from its stored copy.
            var cached = offline.cachedTracks(playlistUuid)
            if (cached.length > 0) tracks = cached
        })
    }

    ListView {
        id: tracksList
        anchors.fill: parent
        clip: true
        model: root.tracks
        boundsBehavior: Flickable.StopAtBounds

        header: Rectangle {
            width: tracksList.width
            height: 240
            color: "transparent"

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(0,0.698,0.973,0.15) }
                    GradientStop { position: 1; color: Theme.bg }
                }
            }

            RowLayout {
                anchors.fill: parent
                anchors.margins: 24
                anchors.leftMargin: 64
                spacing: 24

                Rectangle {
                    width: 180
                    height: 180
                    radius: Theme.radiusLg
                    color: Qt.rgba(0,0.698,0.973,0.2)
                    clip: true
                    Image {
                        id: playlistCover
                        anchors.fill: parent
                        visible: root.coverUrl.length > 0
                        source: root.coverUrl.length > 0 ? "image://tidal/" + root.coverUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        mipmap: true
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                    }
                    Grid {
                        id: collageGrid
                        anchors.fill: parent
                        columns: 2
                        rows: 2
                        visible: root.coverUrl.length === 0 && root.tracks.length >= 4
                        Repeater {
                            model: root.tracks.slice(0, 4)
                            Image {
                                width: 90
                                height: 90
                                source: (modelData && modelData.coverUrl) ? "image://tidal/" + modelData.coverUrl : ""
                                fillMode: Image.PreserveAspectCrop
                                smooth: true
                                mipmap: true
                            }
                        }
                    }
                    VectorIcon {
                        visible: !playlistCover.visible && !collageGrid.visible
                        anchors.centerIn: parent
                        name: "music"
                        color: Theme.accent
                        width: 64
                        height: 64
                        strokeWidth: 1.5
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 8

                    Text {
                        text: "Playlist"
                        color: Theme.textDim
                        font.pixelSize: 12
                        font.bold: true
                        font.letterSpacing: 1
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.playlistTitle
                        color: Theme.textPrimary
                        font.pixelSize: 28
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: root.playlistDescription.length > 0
                        Layout.fillWidth: true
                        text: root.playlistDescription
                        color: Theme.textSec
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    Text {
                        text: {
                            var parts = [root.tracks.length === 1 ? "1 track" : root.tracks.length + " tracks"]
                            var d = root.playlistDuration
                            if (d > 0) {
                                var hrs = Math.floor(d / 3600)
                                var mins = Math.floor((d % 3600) / 60)
                                parts.push(hrs > 0 ? hrs + " hr " + mins + " min" : mins + " min")
                            }
                            return parts.join(" • ")
                        }
                        color: Theme.textSec
                        font.pixelSize: 14
                    }

                    Row {
                        spacing: 12

                        PillButton {
                            text: "Play"
                            glyph: "▶"
                            accent: true
                            onClicked: {
                                if (root.tracks.length > 0) {
                                    bridge.markPlaylistPlayed(root.playlistUuid)
                                    root.playFrom(root.tracks, 0)
                                }
                            }
                        }

                        PillButton {
                            text: "Shuffle"
                            glyph: "⇌"
                            accent: false
                            onClicked: {
                                if (root.tracks.length > 0) {
                                    bridge.markPlaylistPlayed(root.playlistUuid)
                                    player.setShuffle(true)
                                    root.playFrom(root.tracks, Math.floor(Math.random() * root.tracks.length))
                                }
                            }
                        }

                        PillButton {
                            visible: root.isUserPlaylist
                            text: "Edit"
                            glyph: "✎"
                            accent: false
                            onClicked: {
                                editTitleField.text   = root.playlistTitle
                                editDescField.text    = root.playlistDescription
                                editPlaylistPopup.open()
                            }
                        }

                        // Offline pin — the button doubles as the state
                        // indicator: idle / "done / total" while syncing / a
                        // green "Downloaded" check when the copy is complete.
                        PillButton {
                            width: 150
                            text: {
                                if (root.offlineStatus.state === "syncing")
                                    return root.offlineStatus.done + " / " + root.offlineStatus.total
                                if (root.offlineStatus.state === "offline") return "Downloaded"
                                if (root.offlineStatus.state === "error")   return "Retry offline"
                                return "Make offline"
                            }
                            glyph: root.offlineStatus.state === "offline" ? "arrow-down" : "download"
                            glyphBadge: root.offlineStatus.state === "offline"
                            glyphColor: root.offlineStatus.state === "offline" ? Theme.green
                                      : root.offlineStatus.state === "error"   ? Theme.red
                                      : Theme.textPrimary
                            accent: false
                            onClicked: {
                                if (root.tracks.length === 0) return
                                if (root.offlineStatus.state === "none") {
                                    root.offlineEstimate = offline.estimate(root.tracks)
                                    offlinePinPopup.open()
                                } else if (root.offlineStatus.state === "error") {
                                    offline.pin(root.playlistUuid, root.playlistTitle,
                                                root.coverUrl, root.tracks)
                                } else {
                                    offlineRemovePopup.open()
                                }
                            }
                        }
                    }
                }
            }
        }

        delegate: TrackRow {
            selection: trackSel
            rowIndex:  index
            width: tracksList.width - 32
            x: 16
            trackNum:       index + 1
            title:          modelData.title
            artists:        modelData.artists
            albumTitle:     modelData.albumTitle
            durationStr:    modelData.durationStr
            coverUrl:       modelData.coverUrl80
            isPlaying:      player.currentTrack.id === modelData.id && player.playing
            isLoading:      player.currentTrack.id === modelData.id && player.loading
            trackData:      modelData
            playlistUuid:   root.isUserPlaylist ? root.playlistUuid : ""
            trackItemIndex: index
            onPlayRequested: {
                bridge.markPlaylistPlayed(root.playlistUuid)
                root.playFrom(root.tracks, index)
            }
            onRemoveFromPlaylistRequested: function(itemIndex) {
                bridge.removeTrackFromPlaylist(root.playlistUuid, itemIndex, function(ok) {
                    if (ok) {
                        var arr = root.tracks.slice()
                        arr.splice(itemIndex, 1)
                        var y = tracksList.contentY
                        root.tracks = arr
                        root.restoreScroll(y)
                    }
                })
            }
        }

        footer: Item { height: 32; width: tracksList.width }

        ScrollBar.vertical: ScrollBar {
            active: true
            policy: ScrollBar.AsNeeded
        }
    }

    // Drag the selection within the list to reorder it. Editable playlists
    // only: Tidal rejects reordering an editorial one.
    ReorderDropArea {
        anchors.fill: tracksList
        view: tracksList
        selection: trackSel
        kind: root.isUserPlaylist ? "tidal" : ""
        onReorder: (fromIndices, toIndex) => root.reorderTracks(fromIndices, toIndex)
    }

    // Back button sits in a fixed bar that doesn't overlap the track list
    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 52; color: "transparent"
        BackButton { anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 } }
    }

    LoadingOverlay { loading: root.loading }

    // Edit playlist popup
    Popup {
        id: editPlaylistPopup
        anchors.centerIn: Overlay.overlay
        width: 400
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        Column {
            width: parent.width
            spacing: 14

            Text { text: "Edit Playlist"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }

            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Text { text: "Title"; color: Theme.textSec; font.pixelSize: 12 }
            Rectangle {
                width: parent.width; height: 36; radius: 6
                color: Theme.surface; border.color: titleFocus.activeFocus ? Theme.accent : Theme.border
                TextInput {
                    id: editTitleField
                    anchors.fill: parent; anchors.margins: 8
                    color: Theme.textPrimary; font.pixelSize: 14
                    selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                    FocusScope { id: titleFocus; anchors.fill: parent }
                }
            }

            Text { text: "Description"; color: Theme.textSec; font.pixelSize: 12 }
            Rectangle {
                width: parent.width; height: 72; radius: 6
                color: Theme.surface; border.color: descFocus.activeFocus ? Theme.accent : Theme.border
                TextEdit {
                    id: editDescField
                    anchors.fill: parent; anchors.margins: 8
                    color: Theme.textPrimary; font.pixelSize: 14
                    wrapMode: TextEdit.WordWrap
                    selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                    FocusScope { id: descFocus; anchors.fill: parent }
                }
            }

            Row {
                spacing: 10; anchors.right: parent.right
                PillButton {
                    text: "Cancel"; accent: false
                    onClicked: editPlaylistPopup.close()
                }
                PillButton {
                    text: "Save"; accent: true
                    onClicked: {
                        var newTitle = editTitleField.text.trim()
                        if (newTitle.length > 0) root.playlistTitle = newTitle
                        root.playlistDescription = editDescField.text.trim()
                        editPlaylistPopup.close()
                    }
                }
            }
        }
    }

    // Confirm before pinning: the size and time estimates are the warning —
    // a long playlist announces itself as hours of background syncing.
    Popup {
        id: offlinePinPopup
        anchors.centerIn: Overlay.overlay
        width: 420
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        Column {
            width: parent.width
            spacing: 14

            Text { text: "Make this playlist offline?"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }

            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.textSec
                font.pixelSize: 13
                lineHeight: 1.3
                text: root.offlineEstimate.count + " tracks, about "
                      + root.offlineEstimate.sizeStr + " of storage.\n\n"
                      + "Tracks download at listening speed to go easy on Tidal, so the "
                      + "full offline copy is ready in about " + root.offlineEstimate.timeStr
                      + ". The sync runs in the background while the app is open and "
                      + "resumes on the next start."
            }

            Row {
                spacing: 10; anchors.right: parent.right
                PillButton {
                    text: "Cancel"; accent: false
                    onClicked: offlinePinPopup.close()
                }
                PillButton {
                    text: "Make offline"; accent: true; width: 150
                    onClicked: {
                        offline.pin(root.playlistUuid, root.playlistTitle,
                                    root.coverUrl, root.tracks)
                        offlinePinPopup.close()
                    }
                }
            }
        }
    }

    Popup {
        id: offlineRemovePopup
        anchors.centerIn: Overlay.overlay
        width: 400
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        Column {
            width: parent.width
            spacing: 14

            Text { text: "Remove offline copy?"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }

            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.textSec
                font.pixelSize: 13
                text: root.offlineStatus.state === "syncing"
                      ? "The sync stops, and the tracks downloaded so far are removed from this computer."
                      : "The downloaded tracks are removed from this computer. The playlist itself stays on Tidal."
            }

            Row {
                spacing: 10; anchors.right: parent.right
                PillButton {
                    text: "Cancel"; accent: false
                    onClicked: offlineRemovePopup.close()
                }
                PillButton {
                    text: "Remove"; accent: true
                    onClicked: {
                        offline.unpin(root.playlistUuid)
                        offlineRemovePopup.close()
                    }
                }
            }
        }
    }
}
