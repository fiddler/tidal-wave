import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import TidalWave

// A single local playlist. Mirrors PlaylistPage, but every operation goes to
// the local library instead of the Tidal API.
Rectangle {
    id: root

    // Exposed so Cmd+A and Escape can reach it from the window.

    property var pageSelection: trackSel

    // Click-selection state for the track list below.
    TrackSelection { id: trackSel; tracks: root.tracks }
    color: Theme.bg

    property int  localPlaylistId: 0
    property string playlistTitle: ""
    property var  tracks: []

    onLocalPlaylistIdChanged: reload()
    Component.onCompleted: reload()

    Connections {
        target: library
        function onPlaylistsChanged() { root.reload() }
        function onTracksChanged()    { root.reload() }
    }

    // Any library change refreshes the whole list, and handing the ListView a
    // new model array resets it to the top — which after a reorder throws the
    // user back to track 1. Put the viewport back where it was.
    //
    // The list starts at `originY`, not at 0: the page header lives above the
    // first row, so the top of a playlist is contentY === -headerHeight. On
    // the first load there is nothing to restore yet, and the guard leaves the
    // view where ListView put it.
    function reload() {
        if (localPlaylistId <= 0) return
        var meta = library.playlist(localPlaylistId)
        if (meta && meta.title !== undefined) root.playlistTitle = meta.title
        var y = tracksList.contentY
        root.tracks = library.playlistTracks(localPlaylistId)
        var min = tracksList.originY
        var max = min + tracksList.contentHeight - tracksList.height
        if (max > min) tracksList.contentY = Math.max(min, Math.min(y, max))
    }

    function playFrom(list, i) {
        player.setPlaybackSource("localplaylist", String(root.localPlaylistId), root.playlistTitle)
        player.playTracks(list, i)
    }

    readonly property int totalDuration: {
        var d = 0
        for (var i = 0; i < tracks.length; i++) d += tracks[i].duration
        return d
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
                    width: 180; height: 180
                    radius: Theme.radiusLg
                    color: Qt.rgba(0,0.698,0.973,0.2)
                    clip: true
                    Grid {
                        id: collage
                        anchors.fill: parent
                        columns: 2; rows: 2
                        visible: root.tracks.length >= 4
                        Repeater {
                            model: root.tracks.slice(0, 4)
                            Image {
                                width: 90; height: 90
                                source: (modelData && modelData.coverUrl) ? "image://tidal/" + modelData.coverUrl : ""
                                fillMode: Image.PreserveAspectCrop
                                smooth: true; mipmap: true
                            }
                        }
                    }
                    Image {
                        id: singleCover
                        anchors.fill: parent
                        visible: !collage.visible && root.tracks.length > 0
                                 && root.tracks[0].coverUrl.length > 0
                        source: (root.tracks.length > 0 && root.tracks[0].coverUrl.length > 0)
                                ? "image://tidal/" + root.tracks[0].coverUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: true; mipmap: true
                    }
                    VectorIcon {
                        visible: !collage.visible && !singleCover.visible
                        anchors.centerIn: parent
                        name: "music"; color: Theme.accent
                        width: 64; height: 64; strokeWidth: 1.5
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 8

                    Text {
                        text: "Local playlist"
                        color: Theme.textDim
                        font.pixelSize: 12; font.bold: true; font.letterSpacing: 1
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.playlistTitle
                        color: Theme.textPrimary
                        font.pixelSize: 28; font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        text: {
                            var parts = [root.tracks.length === 1 ? "1 track" : root.tracks.length + " tracks"]
                            var d = root.totalDuration
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
                            text: "Play"; glyph: "▶"; accent: true
                            enabled: root.tracks.length > 0
                            onClicked: if (root.tracks.length > 0) root.playFrom(root.tracks, 0)
                        }
                        PillButton {
                            text: "Shuffle"; glyph: "⇌"; accent: false
                            enabled: root.tracks.length > 0
                            onClicked: {
                                if (root.tracks.length === 0) return
                                player.setShuffle(true)
                                root.playFrom(root.tracks, Math.floor(Math.random() * root.tracks.length))
                            }
                        }
                        PillButton {
                            text: "Rename"; glyph: "✎"; accent: false
                            onClicked: { renameField.text = root.playlistTitle; renamePopup.open() }
                        }
                        PillButton {
                            text: "Delete"; glyph: "trash"; accent: false
                            onClicked: deletePopup.open()
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
            trackNum:        index + 1
            title:           modelData.title
            artists:         modelData.artists
            albumTitle:      modelData.albumTitle
            durationStr:     modelData.durationStr
            coverUrl:        modelData.coverUrl80
            isPlaying:       player.currentTrack.id === modelData.id && player.playing
            isLoading:       player.currentTrack.id === modelData.id && player.loading
            trackData:       modelData
            localPlaylistId: root.localPlaylistId
            trackItemIndex:  index
            onPlayRequested: root.playFrom(root.tracks, index)
            onRemoveFromPlaylistRequested: function(itemIndex) {
                library.removeFromPlaylist(root.localPlaylistId, itemIndex)
            }
        }

        footer: Item { height: 32; width: tracksList.width }

        ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
    }

    // Drag the selection within the list to reorder it.
    ReorderDropArea {
        anchors.fill: tracksList
        view: tracksList
        selection: trackSel
        kind: "local"
        onReorder: (fromIndices, toIndex) => {
            library.movePlaylistItems(root.localPlaylistId, fromIndices, toIndex)
        }
    }

    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 60
        visible: root.tracks.length === 0
        text: "Empty playlist — add tracks from Local Files, or drop audio files here"
        color: Theme.textSec
        font.pixelSize: 15
    }

    Rectangle {
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 52; color: "transparent"
        BackButton { anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 } }
    }

    Popup {
        id: renamePopup
        anchors.centerIn: Overlay.overlay
        width: 360
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        function submit() {
            var name = renameField.text.trim()
            if (name.length === 0) return
            library.renamePlaylist(root.localPlaylistId, name)
            root.playlistTitle = name
            renamePopup.close()
        }

        Column {
            width: parent.width
            spacing: 14
            Text { text: "Rename playlist"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Rectangle {
                width: parent.width; height: 36; radius: 6
                color: Theme.surface
                border.color: renameField.activeFocus ? Theme.accent : Theme.border
                TextInput {
                    id: renameField
                    anchors.fill: parent; anchors.margins: 8
                    color: Theme.textPrimary; font.pixelSize: 14
                    focus: true
                    selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                    onAccepted: renamePopup.submit()
                }
            }
            Row {
                spacing: 10; anchors.right: parent.right
                PillButton { text: "Cancel"; accent: false; onClicked: renamePopup.close() }
                PillButton { text: "Save";   accent: true;  onClicked: renamePopup.submit() }
            }
        }
        onOpened: renameField.forceActiveFocus()
    }

    Popup {
        id: deletePopup
        anchors.centerIn: Overlay.overlay
        width: 360
        modal: true; focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        Column {
            width: parent.width
            spacing: 14
            Text { text: "Delete playlist?"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
            Text {
                width: parent.width
                text: "“" + root.playlistTitle + "” will be removed. The audio files stay in your library."
                color: Theme.textSec; font.pixelSize: 13; wrapMode: Text.WordWrap
            }
            Row {
                spacing: 10; anchors.right: parent.right
                PillButton { text: "Cancel"; accent: false; onClicked: deletePopup.close() }
                PillButton {
                    text: "Delete"; accent: true
                    onClicked: {
                        library.deletePlaylist(root.localPlaylistId)
                        deletePopup.close()
                        Window.window.navigate("local")
                    }
                }
            }
        }
    }
}
