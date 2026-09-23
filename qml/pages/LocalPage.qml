import QtCore
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import QtQuick.Dialogs
import TidalWave

// Browser for the local library: files imported from disk, plus the playlists
// that hold them. Local playlists are separate from Tidal playlists on purpose
// — Tidal's API only accepts Tidal track ids, so a local file can never belong
// to a real Tidal playlist. Both kinds of track mix freely in the play queue.
Rectangle {
    id: root

    // Exposed so Cmd+A and Escape can reach it from the window.

    property var pageSelection: trackSel

    // Click-selection state for the track list below.
    TrackSelection { id: trackSel; tracks: root.tracks }
    color: Theme.bg

    property var tracks: []
    property string filter: ""

    // The library has no hand-made order to drag rows into — it is every file
    // on disk, not a playlist — so the answer to "this ordering is wrong" is a
    // choice of ordering. Remembered, because it is a preference, not a mode.
    Settings {
        id: localPrefs
        category: "local"
        property string sortMode: "artist"   // artist | album | title | duration | added
    }

    readonly property var sortLabels: ({
        "artist":   "Album artist",
        "album":    "Album",
        "title":    "Title",
        "duration": "Length",
        "added":    "Recently added"
    })

    function reload() { root.tracks = library.tracks(root.filter, localPrefs.sortMode) }
    function setSort(mode) { localPrefs.sortMode = mode; reload() }

    Component.onCompleted: reload()

    Connections {
        target: library
        function onTracksChanged() { root.reload() }
    }

    function playFrom(list, i) {
        player.setPlaybackSource("local", "library", "Local Files")
        player.playTracks(list, i)
    }

    ListView {
        id: tracksList
        anchors.fill: parent
        clip: true
        model: root.tracks
        boundsBehavior: Flickable.StopAtBounds

        header: Rectangle {
            width: tracksList.width
            height: 236
            color: "transparent"

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.rgba(0,0.698,0.973,0.15) }
                    GradientStop { position: 1; color: Theme.bg }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                anchors.leftMargin: 64
                spacing: 10

                Text {
                    text: "Local Files"
                    color: Theme.textDim
                    font.pixelSize: 12; font.bold: true; font.letterSpacing: 1
                }
                Text {
                    text: "Your Library"
                    color: Theme.textPrimary
                    font.pixelSize: 28; font.bold: true
                }
                Text {
                    text: {
                        if (library.importing) return "Importing…"
                        var n = library.trackCount
                        return (n === 1 ? "1 track" : n + " tracks")
                               + " • " + library.playlists().length + " local playlists"
                    }
                    color: Theme.textSec
                    font.pixelSize: 14
                }

                Row {
                    spacing: 12
                    PillButton {
                        text: "Play all"
                        glyph: "▶"
                        accent: true
                        enabled: root.tracks.length > 0
                        onClicked: if (root.tracks.length > 0) root.playFrom(root.tracks, 0)
                    }
                    PillButton {
                        text: "Shuffle"
                        glyph: "⇌"
                        accent: false
                        enabled: root.tracks.length > 0
                        onClicked: {
                            if (root.tracks.length === 0) return
                            player.setShuffle(true)
                            root.playFrom(root.tracks, Math.floor(Math.random() * root.tracks.length))
                        }
                    }
                    PillButton {
                        text: "Add files…"
                        glyph: "plus"
                        accent: false
                        onClicked: importDialog.open()
                    }
                    PillButton {
                        text: "Add folder…"
                        glyph: "folder-plus"
                        accent: false
                        onClicked: folderDialog.open()
                    }
                    PillButton {
                        text: "New playlist"
                        glyph: "list-plus"
                        accent: false
                        onClicked: { newPlaylistField.text = ""; newPlaylistPopup.open() }
                    }
                }

                Row {
                    spacing: 8
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Sort by"
                        color: Theme.textDim
                        font.pixelSize: 12
                    }
                    Rectangle {
                        id: sortPill
                        width: sortLabel.implicitWidth + 30
                        height: 26
                        radius: 13
                        color: sortHov.hovered ? Theme.surfaceHov : Theme.surface
                        border.color: Theme.border
                        HoverHandler { id: sortHov }
                        TapHandler { onTapped: sortMenu.popup(0, sortPill.height) }
                        Text {
                            id: sortLabel
                            anchors.centerIn: parent
                            anchors.horizontalCenterOffset: -5
                            text: root.sortLabels[localPrefs.sortMode] || "Album artist"
                            color: Theme.textSec
                            font.pixelSize: 12
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: "▾"
                            color: Theme.textDim
                            font.pixelSize: 9
                        }

                        Menu {
                            id: sortMenu
                            background: Rectangle {
                                implicitWidth: 180
                                color: "#1E1E1E"
                                border.color: Theme.border
                                radius: Theme.radius
                            }
                            Repeater {
                                model: ["artist", "album", "title", "duration", "added"]
                                MenuItem {
                                    id: sortItem
                                    required property string modelData
                                    text: root.sortLabels[sortItem.modelData]
                                    onTriggered: root.setSort(sortItem.modelData)
                                    contentItem: Text {
                                        text: sortItem.text
                                        // The active order is tinted rather
                                        // than check-marked: no indent shift
                                        // between the five items.
                                        color: localPrefs.sortMode === sortItem.modelData
                                               ? Theme.accent : Theme.textPrimary
                                        leftPadding: 16
                                        font.pixelSize: 14
                                    }
                                    background: Rectangle {
                                        color: sortItem.highlighted ? Theme.surfaceHov : "transparent"
                                    }
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }
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

        ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
    }

    // Empty state — also the drop target hint.
    ColumnLayout {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: 60
        spacing: 10
        visible: root.tracks.length === 0 && !library.importing

        VectorIcon {
            Layout.alignment: Qt.AlignHCenter
            name: "music"; color: Theme.textDim
            width: 48; height: 48; strokeWidth: 1.2
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: root.filter.length > 0 ? "No local tracks match that search."
                                         : "Drop audio files or folders anywhere in the window"
            color: Theme.textSec; font.pixelSize: 15
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            visible: root.filter.length === 0
            text: "FLAC, MP3, M4A, OGG, Opus, WAV, AIFF and more"
            color: Theme.textDim; font.pixelSize: 12
        }
    }

    // Import progress
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        visible: library.importing
        width: 300; height: 56; radius: 10
        color: Theme.surfaceHigh
        border.color: Theme.border

        property int done: 0
        property int total: 0

        Connections {
            target: library
            function onImportProgress(done, total) {
                importCard.done = done
                importCard.total = total
            }
        }
        id: importCard

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 6
            Text {
                text: "Importing " + importCard.done + " / " + importCard.total
                color: Theme.textPrimary; font.pixelSize: 13
            }
            Rectangle {
                Layout.fillWidth: true
                height: 4; radius: 2
                color: Theme.surface
                Rectangle {
                    height: parent.height; radius: 2
                    color: Theme.accent
                    width: importCard.total > 0 ? parent.width * (importCard.done / importCard.total) : 0
                    Behavior on width { NumberAnimation { duration: 120 } }
                }
            }
        }
    }

    // Opened from here and from the command palette, which routes through this
    // page so the import progress card is on screen when the files land.
    function promptImportFiles()  { importDialog.open() }
    function promptImportFolder() { folderDialog.open() }
    // Cleared first, like the page's own button: the popup otherwise reopens
    // holding the name of a creation that was cancelled.
    function promptNewPlaylist()  { newPlaylistField.text = ""; newPlaylistPopup.open() }

    FileDialog {
        id: importDialog
        title: "Add audio files"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio files (*.flac *.mp3 *.m4a *.aac *.ogg *.opus *.wav *.aiff *.aif *.alac *.wma *.mp4)",
                      "All files (*)"]
        onAccepted: library.importUrls(selectedFiles)
    }

    FolderDialog {
        id: folderDialog
        title: "Add a music folder"
        onAccepted: library.importUrls([selectedFolder])
    }

    Popup {
        id: newPlaylistPopup
        anchors.centerIn: Overlay.overlay
        width: 360
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

        function submit() {
            var name = newPlaylistField.text.trim()
            if (name.length === 0) return
            library.createPlaylist(name)
            newPlaylistPopup.close()
        }

        Column {
            width: parent.width
            spacing: 14

            Text { text: "New local playlist"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }
            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Rectangle {
                width: parent.width; height: 36; radius: 6
                color: Theme.surface
                border.color: newPlaylistField.activeFocus ? Theme.accent : Theme.border
                TextInput {
                    id: newPlaylistField
                    anchors.fill: parent; anchors.margins: 8
                    color: Theme.textPrimary; font.pixelSize: 14
                    focus: true
                    selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                    onAccepted: newPlaylistPopup.submit()
                }
            }

            Row {
                spacing: 10; anchors.right: parent.right
                PillButton { text: "Cancel"; accent: false; onClicked: newPlaylistPopup.close() }
                PillButton { text: "Create"; accent: true;  onClicked: newPlaylistPopup.submit() }
            }
        }
        onOpened: newPlaylistField.forceActiveFocus()
    }

}
