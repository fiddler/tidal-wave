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

    property string mixId: ""
    property string title: ""
    property string subtitle: ""
    property string coverUrl: ""
    property var    tracks: []
    property bool   loading: false

    // Records this mix as the "playing from" source, then starts playback.
    function playFrom(list, i) {
        player.setPlaybackSource("mix", root.mixId, root.title)
        player.playTracks(list, i)
    }

    // A track's radio station is a mix like any other; only the label differs.
    property bool   isRadio: false
    property bool   isSaved: false
    property string loadError: ""

    function updateSavedState() {
        isSaved = mixId.length > 0 ? bridge.isMixFavorite(mixId) : false
    }

    onMixIdChanged: if (mixId.length > 0) { loadMix(); updateSavedState() }

    Connections {
        target: bridge
        function onFavoriteMixesChanged() { root.updateSavedState() }
    }

    function loadMix() {
        loading = true
        loadError = ""
        var id = mixId
        bridge.fetchMixPage(mixId, function(header, t, err) {
            if (id !== root.mixId) return
            loading = false
            if (err) { root.loadError = "Couldn't load this mix. Check your connection."; return }
            tracks = t
            // The page's own header wins over what the caller passed: a mix
            // opened by id alone ("Playing from") arrives with no title, and
            // one opened from a track arrives with a guessed one.
            root.isRadio = !!header.isRadio
            if (header.title)    root.title    = header.title
            if (header.subtitle) root.subtitle = header.subtitle
            if (header.coverUrl) root.coverUrl = header.coverUrl
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
                        id: mixCover
                        anchors.fill: parent
                        source: root.coverUrl.length > 0 ? "image://tidal/" + root.coverUrl : ""
                        fillMode: Image.PreserveAspectCrop
                        smooth: true
                        mipmap: true
                        opacity: status === Image.Ready ? 1 : 0
                        Behavior on opacity { NumberAnimation { duration: Theme.dur(200) } }
                    }
                    VectorIcon {
                        visible: mixCover.status !== Image.Ready
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
                        text: root.isRadio ? "Radio" : "Mix"
                        color: Theme.textDim
                        font.pixelSize: 12
                        font.bold: true
                        font.letterSpacing: 1
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.title
                        color: Theme.textPrimary
                        font.pixelSize: 28
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: root.subtitle.length > 0
                        Layout.fillWidth: true
                        text: root.subtitle
                        color: Theme.textSec
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
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
                            objectName: "mixSave"
                            text: root.isSaved ? "Saved" : "Save"
                            glyph: root.isSaved ? "♥" : "♡"
                            accent: root.isSaved
                            onClicked: {
                                var w = root.Window.window
                                var failed = function(ok) { if (!ok) w.favoriteFailed("your saved mixes") }
                                if (root.isSaved) bridge.removeMixFavorite(root.mixId, failed)
                                else              bridge.addMixFavorite(root.mixId, failed)
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
            trackNum:    index + 1
            title:       modelData.title
            artists:     modelData.artists
            albumTitle:  modelData.albumTitle
            durationStr: modelData.durationStr
            coverUrl:    modelData.coverUrl80
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

    BackButton { anchors { top: parent.top; left: parent.left; margins: 16 } }

    Text {
        visible: root.loadError.length > 0 && !root.loading && root.tracks.length === 0
        anchors.centerIn: parent
        text: root.loadError
        color: Theme.textSec
        font.pixelSize: 14
    }

    LoadingOverlay { loading: root.loading }
}
