import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Window
import TidalWave

Rectangle {
    id: root

    // Exposed so Cmd+A and Escape can reach it from the window.

    property var pageSelection: trackSel

    // Click-selection state for the track list below.
    TrackSelection { id: trackSel; tracks: root.tracks }
    color: Theme.bg

    property var albumId: 0
    property var albumData: ({})
    property var tracks: []
    property bool loading: false
    property bool isSaved: false

    // Records this album as the "playing from" source, then starts playback.
    function playFrom(list, i) {
        player.setPlaybackSource("album", "" + root.albumId, root.albumData.title || "")
        player.playTracks(list, i)
    }

    function updateSavedState() {
        isSaved = albumId > 0 ? bridge.isAlbumFavorite(albumId) : false
    }

    readonly property int effectiveArtistId: albumData.artistId > 0 ? albumData.artistId
        : (tracks.length > 0 && tracks[0].artistId > 0 ? tracks[0].artistId : 0)

    onAlbumIdChanged: if (albumId > 0) { loadAlbum(); updateSavedState() }

    Connections {
        target: bridge
        function onFavoriteAlbumsChanged() { root.updateSavedState() }
    }

    // What the server said when it would not hand the album over:
    // "unavailable" for a 404, "error" for anything else. A saved favourite
    // whose edition has been delisted keeps appearing in the collection while
    // albums/<id> answers 404, and a page that drew nothing for that looked
    // exactly like a broken app.
    property string loadState: ""
    readonly property bool showFailure: loadState.length > 0 && !loading
                                        && !(albumData.id > 0) && tracks.length === 0

    function loadAlbum() {
        loading = true
        loadState = ""
        var id = albumId
        bridge.fetchAlbum(albumId, function(album, err, status) {
            if (id !== root.albumId) return
            if (!err) albumData = album
            else root.loadState = status === 404 ? "unavailable" : "error"
        })
        bridge.fetchAlbumTracks(albumId, function(t, err) {
            if (id !== root.albumId) return
            loading = false
            if (!err) tracks = t
        })
    }

    ListView {
        id: tracksList
        anchors.fill: parent
        clip: true
        // Hidden rather than left behind the panel, so a blank hero's Play and
        // Save pills are not hit targets over an album that does not exist.
        visible: !root.showFailure
        model: root.tracks
        boundsBehavior: Flickable.StopAtBounds

        header: Column {
            width: tracksList.width

            // Hero header
            Rectangle {
                width: parent.width
                height: 280
                color: "transparent"
                clip: true

                Image {
                    anchors.fill: parent
                    source: albumData.coverUrl640 ? "image://tidal/" + albumData.coverUrl640 : ""
                    fillMode: Image.PreserveAspectCrop
                    opacity: 0.18
                    smooth: true
                    mipmap: true
                }

                Rectangle {
                    anchors.fill: parent
                    gradient: Gradient {
                        orientation: Gradient.Vertical
                        GradientStop { position: 0; color: Qt.rgba(0.04,0.04,0.04,0.6) }
                        GradientStop { position: 1; color: Theme.bg }
                    }
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    anchors.leftMargin: 64
                    spacing: 24

                    Rectangle {
                        width: 200
                        height: 200
                        radius: Theme.radiusLg
                        color: Theme.surfaceHigh
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: albumData.coverUrl ? "image://tidal/" + albumData.coverUrl : ""
                            fillMode: Image.PreserveAspectCrop
                            smooth: true
                            mipmap: true
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 8

                        Text {
                            text: "Album"
                            color: Theme.textDim
                            font.pixelSize: 12
                            font.bold: true
                            font.letterSpacing: 1
                        }

                        Text {
                            Layout.fillWidth: true
                            text: albumData.title || ""
                            color: Theme.textPrimary
                            font.pixelSize: 28
                            font.bold: true
                            wrapMode: Text.WordWrap
                        }

                        ArtistLinks {
                            Layout.fillWidth: true
                            artists: albumData.artistList || []
                            fallbackText: albumData.artists || ""
                            keyboardFocus: true
                            color: Theme.accent
                            hoverColor: Theme.accent
                            pixelSize: 14
                        }

                        Text {
                            text: {
                                var parts = []
                                if (albumData.year) parts.push(albumData.year)
                                if (albumData.numTracks) parts.push(albumData.numTracks + (albumData.numTracks === 1 ? " track" : " tracks"))
                                if (albumData.duration > 0) {
                                    var d = albumData.duration
                                    var hrs = Math.floor(d / 3600)
                                    var mins = Math.floor((d % 3600) / 60)
                                    parts.push(hrs > 0 ? hrs + " hr " + mins + " min" : mins + " min")
                                }
                                if (albumData.quality) parts.push(albumData.quality)
                                return parts.join(" • ")
                            }
                            color: Theme.textSec
                            font.pixelSize: 13
                        }

                        Row {
                            spacing: 12

                            PillButton {
                                text: "Play"
                                glyph: "▶"
                                accent: true
                                onClicked: if (root.tracks.length > 0) root.playFrom(root.tracks, 0)
                            }

                            PillButton {
                                text: "Shuffle"
                                glyph: "⇌"
                                accent: false
                                onClicked: {
                                    if (root.tracks.length > 0) {
                                        player.setShuffle(true)
                                        root.playFrom(root.tracks, Math.floor(Math.random() * root.tracks.length))
                                    }
                                }
                            }

                            PillButton {
                                text: root.isSaved ? "Saved" : "Save"
                                glyph: root.isSaved ? "♥" : "♡"
                                accent: root.isSaved
                                onClicked: {
                                    var w = Window.window
                                    var failed = function(ok) { if (!ok) w.favoriteFailed("your saved albums") }
                                    if (root.isSaved) bridge.removeAlbumFavorite(root.albumId, failed)
                                    else              bridge.addAlbumFavorite(root.albumId, failed)
                                }
                            }
                        }
                    }
                }
            }

            Item { height: 8; width: parent.width }
        }

        delegate: TrackRow {
            selection: trackSel
            rowIndex:  index
            width: tracksList.width - 32
            x: 16
            trackNum:    index + 1
            title:       modelData.title
            artists:     modelData.artists
            albumTitle:  root.albumData.title || ""
            durationStr: modelData.durationStr
            showAlbum:   false
            showCover:   false
            isPlaying:   player.currentTrack.id === modelData.id && player.playing
            isLoading:   player.currentTrack.id === modelData.id && player.loading
            trackData:   modelData
            onPlayRequested: root.playFrom(root.tracks, index)
        }

        footer: Item { height: 32; width: tracksList.width }

        ScrollBar.vertical: ScrollBar {
            active: true
            policy: ScrollBar.AsNeeded
        }
    }

    // Sticky bar — back button always visible; title fades in once hero scrolls out
    Rectangle {
        id: stickyHeader
        anchors { top: parent.top; left: parent.left; right: parent.right }
        height: 52
        color: Qt.rgba(Theme.bg.r, Theme.bg.g, Theme.bg.b,
                       Math.min(0.95, Math.max(0, (tracksList.contentY - 200) / 80)))

        BackButton { anchors { left: parent.left; verticalCenter: parent.verticalCenter; leftMargin: 8 } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 64
            anchors.rightMargin: 16
            spacing: 12
            opacity: Math.min(1, Math.max(0, (tracksList.contentY - 200) / 80))
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: albumData.title || ""
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: albumData.artists || ""
                    color: Theme.textSec
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border
            opacity: Math.min(1, Math.max(0, (tracksList.contentY - 200) / 80))
        }
    }

    ColumnLayout {
        objectName: "albumFailure"
        visible: root.showFailure
        anchors.centerIn: parent
        width: Math.min(parent.width - 96, 460)
        spacing: 12

        VectorIcon {
            Layout.alignment: Qt.AlignHCenter
            name: "music"; width: 40; height: 40; color: Theme.textDim; strokeWidth: 1.5
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.loadState === "unavailable" ? "This album is no longer on Tidal"
                                                   : "Couldn't load this album"
            color: Theme.textPrimary; font.pixelSize: 18; font.bold: true
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: root.loadState === "unavailable"
                  ? "This release was withdrawn. Searching for the title often finds another edition."
                  : "Check your connection and try again."
            color: Theme.textSec; font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
        Row {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            spacing: 12
            PillButton {
                objectName: "albumRetry"
                visible: root.loadState === "error"
                text: "Retry"; accent: true
                onClicked: root.loadAlbum()
            }
            PillButton {
                visible: root.loadState === "unavailable" && root.isSaved
                width: 190
                text: "Remove from library"; accent: false
                onClicked: {
                    var w = Window.window
                    bridge.removeAlbumFavorite(root.albumId, function(ok) {
                        if (!ok) w.favoriteFailed("your saved albums")
                        else     w.goBack()
                    })
                }
            }
        }
    }

    function navigateTo(page, params) {
        Window.window.navigate(page, params || {})
    }

    LoadingOverlay { loading: root.loading }
}
