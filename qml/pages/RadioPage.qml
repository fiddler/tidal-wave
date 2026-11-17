import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Controls
import TidalWave

Rectangle {
    id: root

    // Exposed so Cmd+A and Escape can reach it from the window.

    property var pageSelection: trackSel

    // Click-selection state for the track list below.
    TrackSelection { id: trackSel; tracks: root.tracks }
    color: Theme.bg

    property var    radioTitle: ""
    property var    trackId:    0
    property var    tracks:     []
    property bool   loading:    false

    // Records this radio station as the "playing from" source, then plays.
    function playFrom(list, i) {
        player.setPlaybackSource("radio", "" + root.trackId, root.radioTitle)
        player.playTracks(list, i)
    }

    onTrackIdChanged: if (trackId > 0) loadRadio()

    // "failed" or "empty" once a reply has landed with nothing to show.
    property string loadState: ""

    function loadRadio() {
        loading = true
        loadState = ""
        tracks  = []
        var id = trackId
        bridge.fetchTrackRadio(trackId, function(t, err) {
            if (id !== root.trackId) return
            loading = false
            if (err) { root.loadState = "failed"; return }
            tracks = t
            if (t.length === 0) root.loadState = "empty"
        })
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            Text {
                text: "←"
                color: Theme.textSec
                font.pixelSize: 18
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: Window.window.goBack() }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: "Radio"
                    color: Theme.textDim
                    font.pixelSize: 11
                    font.bold: true
                    font.letterSpacing: 1
                }
                Text {
                    Layout.fillWidth: true
                    text: root.radioTitle || "Track Radio"
                    color: Theme.textPrimary
                    font.pixelSize: 22
                    font.bold: true
                    elide: Text.ElideRight
                }
            }

            PillButton {
                visible: root.tracks.length > 0
                text: "Play all"
                glyph: "▶"
                accent: true
                onClicked: root.playFrom(root.tracks, 0)
            }
        }

        Item { height: 16 }

        Rectangle { color: Theme.border; height: 1; Layout.fillWidth: true }

        Item { height: 8 }

        ListView {
            id: trackList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.tracks
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: TrackRow {
                selection: trackSel
                rowIndex:  index
                required property var modelData
                required property int index
                width: trackList.width
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
        }
    }

    ColumnLayout {
        objectName: "radioFailure"
        visible: root.loadState.length > 0 && !root.loading
        anchors.centerIn: parent
        spacing: 12
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.loadState === "failed" ? "Couldn't load this radio" : "No radio for this track"
            color: Theme.textPrimary; font.pixelSize: 18; font.bold: true
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.loadState === "failed" ? "Check your connection and try again."
                                              : "Tidal has no station built from it."
            color: Theme.textSec; font.pixelSize: 13
        }
        PillButton {
            objectName: "radioRetry"
            visible: root.loadState === "failed"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 8
            text: "Retry"; accent: true
            onClicked: root.loadRadio()
        }
    }

    LoadingOverlay { loading: root.loading }
}
