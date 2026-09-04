import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TidalWave

Rectangle {
    id: root
    color: Theme.surface

    property string currentPage: "home"
    signal navigate(string page, var params)
    signal openPalette()

    function openSettings()    { settingsPopup.open() }
    function openNewPlaylist() { createPlaylistPopup.open() }

    // Sort/filter preferences for the Tidal playlist list, persisted.
    Settings {
        id: plPrefs
        category: "sidebar"
        property string sortMode: "recent"    // recent | updated | created | alpha
        property string filterMode: "all"     // all | mine | followed
    }

    // Heading and row for the sort/filter menu (inline components must live at
    // the document root).
    component MenuHeading: Text {
        leftPadding: 10; topPadding: 6; bottomPadding: 4
        color: Theme.textDim
        font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5
    }
    component MenuRow: Rectangle {
        id: menuRow
        required property string key
        required property string label
        required property string group   // "sort" | "filter"
        readonly property bool active:
            (group === "sort" ? plPrefs.sortMode : plPrefs.filterMode) === key
        signal picked()
        width: parent ? parent.width : 0
        height: 30; radius: 6
        color: menuRowHov.hovered ? Theme.surfaceHov : "transparent"
        Text {
            anchors.left: parent.left; anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: menuRow.label
            color: menuRow.active ? Theme.accent : Theme.textPrimary
            font.pixelSize: 13
        }
        VectorIcon {
            visible: menuRow.active
            anchors.right: parent.right; anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            name: "check"; width: 11; height: 11; strokeWidth: 2.5
            color: Theme.accent
        }
        HoverHandler { id: menuRowHov; cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: menuRow.picked() }
    }

    ColumnLayout {
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        spacing: 0

        Item { height: 20 }

        Row {
            Layout.leftMargin: 20
            spacing: 8
            Image {
                width: 28
                height: 28
                source: "../../assets/icon.png"
                sourceSize: Qt.size(112, 112)   // oversample so it stays sharp on retina
                smooth: true
                mipmap: true
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "TIDAL WAVE"
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                font.letterSpacing: 1
            }
        }

        Item { height: 24 }

        // The only visible sign the palette exists. Nothing else in the window
        // advertises Cmd+K.
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 16
            Layout.rightMargin: 16
            Layout.preferredHeight: 34
            radius: Theme.radius
            color: paletteHov.hovered ? Theme.surfaceHov : Theme.surfaceHigh
            border.color: paletteHov.hovered ? Theme.border : "transparent"
            border.width: 1

            Row {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 8
                spacing: 8

                VectorIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"; width: 14; height: 14; strokeWidth: 1.8
                    color: Theme.textDim
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 14 - 8 - paletteKey.width - 8
                    text: "Search or jump to…"
                    color: Theme.textSec
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                Rectangle {
                    id: paletteKey
                    anchors.verticalCenter: parent.verticalCenter
                    width: paletteKeyLabel.implicitWidth + 12
                    height: 20
                    radius: 5
                    color: Theme.surface
                    Text {
                        id: paletteKeyLabel
                        anchors.centerIn: parent
                        text: Qt.platform.os === "osx" ? "⌘K" : "Ctrl+K"
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                }
            }

            HoverHandler { id: paletteHov; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.openPalette() }
        }

        Item { height: 12 }

        SideNavItem {
            icon: "home"
            label: "Home"
            page: "home"
            currentPage: root.currentPage
            onActivated: root.navigate("home", {})
        }
        SideNavItem {
            icon: "search"
            label: "Search"
            page: "search"
            currentPage: root.currentPage
            onActivated: root.navigate("search", {})
        }
        SideNavItem {
            icon: "heart"
            label: "Collection"
            page: "collection"
            currentPage: root.currentPage
            onActivated: root.navigate("collection", {})
        }
        SideNavItem {
            icon: "music"
            label: "Local Files"
            page: "local"
            currentPage: root.currentPage
            onActivated: root.navigate("local", {})
        }

        Item { height: 16 }
        Rectangle { color: Theme.border; height: 1; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
        Item { height: 16 }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 10
            spacing: 4

            Text {
                text: "PLAYLISTS"
                color: Theme.textDim
                font.pixelSize: 10
                font.bold: true
                font.letterSpacing: 1.5
            }
            Item { Layout.fillWidth: true }

            // New playlist
            Rectangle {
                width: 22; height: 22; radius: 11
                color: plusHov.hovered ? Theme.surfaceHov : "transparent"
                VectorIcon {
                    anchors.centerIn: parent
                    name: "plus"; width: 12; height: 12; strokeWidth: 2
                    color: plusHov.hovered ? Theme.textPrimary : Theme.textDim
                }
                HoverHandler { id: plusHov; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: { newPlaylistField.text = ""; createPlaylistPopup.open() }
                }
                ToolTip {
                    visible: plusHov.hovered; delay: 600; text: "New playlist"
                    background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 4 }
                    contentItem: Text { text: "New playlist"; color: Theme.textPrimary; font.pixelSize: 12 }
                }
            }

            // Sort & filter
            Rectangle {
                id: sortBtn
                width: 22; height: 22; radius: 11
                color: sortHov.hovered || plSortPopup.opened ? Theme.surfaceHov : "transparent"
                VectorIcon {
                    anchors.centerIn: parent
                    name: "sort"; width: 12; height: 12; strokeWidth: 2
                    color: sortHov.hovered || plSortPopup.opened ? Theme.textPrimary : Theme.textDim
                }
                HoverHandler { id: sortHov; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: plSortPopup.open() }

                Popup {
                    id: plSortPopup
                    x: -width + parent.width
                    y: parent.height + 6
                    width: 210
                    padding: 6
                    background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 10 }

                    function pick(group, key) {
                        if (group === "sort") plPrefs.sortMode = key
                        else                  plPrefs.filterMode = key
                        root.applyPlaylistView()
                        plSortPopup.close()
                    }

                    Column {
                        width: parent.width
                        spacing: 1

                        MenuHeading { text: "SORT" }
                        MenuRow { group: "sort"; key: "recent";  label: "Recently played"; onPicked: plSortPopup.pick(group, key) }
                        MenuRow { group: "sort"; key: "updated"; label: "Updated date";    onPicked: plSortPopup.pick(group, key) }
                        MenuRow { group: "sort"; key: "created"; label: "Created date";    onPicked: plSortPopup.pick(group, key) }
                        MenuRow { group: "sort"; key: "alpha";   label: "Alphabetical";    onPicked: plSortPopup.pick(group, key) }

                        Rectangle { width: parent.width; height: 1; color: Theme.border }

                        MenuHeading { text: "FILTER" }
                        MenuRow { group: "filter"; key: "all";      label: "All playlists";      onPicked: plSortPopup.pick(group, key) }
                        MenuRow { group: "filter"; key: "mine";     label: "Your playlists";     onPicked: plSortPopup.pick(group, key) }
                        MenuRow { group: "filter"; key: "followed"; label: "Followed playlists"; onPicked: plSortPopup.pick(group, key) }
                    }
                }
            }
        }

        Item { height: 8 }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            DragScrollEdge {
                view: playlistList
                direction: -1
                anchors { left: parent.left; right: parent.right; top: parent.top }
                onDroppedAt: (index, source) => playlistList.addTracksAt(index, source)
            }
            DragScrollEdge {
                view: playlistList
                direction: 1
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                onDroppedAt: (index, source) => playlistList.addTracksAt(index, source)
            }

        ListView {
            id: playlistList
            anchors.fill: parent
            clip: true
            bottomMargin: 8
            model: ListModel { id: playlistModel }

            // Shared by the row drop targets and the edge sensors above.
            function addTracksAt(index, source) {
                if (index < 0 || index >= playlistModel.count) return
                if (!source || source.kind !== "tidal") return
                var ids = []
                for (var i = 0; i < source.payload.length; i++) ids.push(source.payload[i].id)
                bridge.addTracksToPlaylist(playlistModel.get(index).uuid, ids, function(ok) {})
            }

            delegate: Item {
                id: plDelegate
                width: ListView.view.width
                height: 42
                readonly property int rowIndex: index

                // True while the play queue was started from this playlist —
                // the title tints accent and an equalizer joins the row.
                readonly property bool isSource: player.sourceType === "playlist"
                                                 && player.sourceId === model.uuid

                // Offline pin state of this playlist, rendered as the small
                // icon on the right: green check = downloaded, dim arrow =
                // still syncing.
                property string offState: offline.status(model.uuid).state
                Connections {
                    target: offline
                    function onPlaylistChanged(uuid) {
                        if (uuid === model.uuid)
                            plDelegate.offState = offline.status(model.uuid).state
                    }
                }

                function activate() {
                    root.navigate("playlist", { playlistUuid: model.uuid, playlistTitle: model.title, coverUrl: model.coverUrl || "", playlistType: model.type || "" })
                }

                activeFocusOnTab: true
                Keys.onReturnPressed: activate()
                Keys.onSpacePressed:  activate()

                // Tidal playlists take Tidal tracks only. A local file has no
                // Tidal id, so the API could not accept it.
                DropArea {
                    id: plDrop
                    anchors.fill: parent
                    keys: ["tidalwave/tracks"]
                    property bool willAccept: containsDrag && drag.source
                                              && drag.source.kind === "tidal"
                    onEntered: (d) => { if (!d.source || d.source.kind !== "tidal") d.accepted = false }
                    onDropped: (d) => {
                        if (!d.source || d.source.kind !== "tidal") { d.accepted = false; return }
                        playlistList.addTracksAt(plDelegate.rowIndex, d.source)
                        d.accept()
                    }
                }

                Rectangle {
                    id: plRect
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 6
                    color: plDrop.willAccept ? Qt.rgba(0, 0.698, 0.973, 0.28)
                           : plHov.hovered ? Theme.surfaceHov : "transparent"
                    border.width: plDrop.willAccept ? 1 : (plDelegate.activeFocus ? 2 : 0)
                    border.color: Theme.accent
                    HoverHandler { id: plHov }
                    TapHandler {
                        onTapped: plDelegate.activate()
                    }
                    Column {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 26 - plIndicators.width
                        spacing: 1
                        Text {
                            id: plText
                            width: parent.width
                            text: model.title
                            color: plDelegate.isSource ? Theme.accent : Theme.textSec
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: model.numTracks === 1 ? "1 track" : model.numTracks + " tracks"
                            color: Theme.textDim
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }
                    Row {
                        id: plIndicators
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 6

                        EqualizerBars {
                            visible: plDelegate.isSource
                            running: player.playing
                            anchors.verticalCenter: parent.verticalCenter
                        }

                    // Downloaded = the Spotify-style green circle with a down
                    // arrow; syncing = a dim bare arrow; failed = a red one.
                    Item {
                        id: plOfflineIcon
                        visible: plDelegate.offState !== "none"
                        anchors.verticalCenter: parent.verticalCenter
                        width: 15; height: 15

                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            visible: plDelegate.offState === "offline"
                            color: Theme.green
                            VectorIcon {
                                anchors.centerIn: parent
                                width: 9; height: 9
                                name: "arrow-down-filled"
                                color: Theme.bg
                            }
                        }
                        VectorIcon {
                            anchors.centerIn: parent
                            visible: plDelegate.offState !== "offline"
                            width: 12; height: 12
                            strokeWidth: 2
                            name: "download"
                            color: plDelegate.offState === "error" ? Theme.red : Theme.textDim
                        }
                    }
                    }
                    ToolTip {
                        id: plToolTip
                        delay: 600
                        visible: plHov.hovered && plText.truncated
                        text: model.title
                        background: Rectangle {
                            color: Theme.surfaceHigh
                            border.color: Theme.border
                            radius: 4
                        }
                        contentItem: Text {
                            text: plToolTip.text
                            color: Theme.textPrimary
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
        }

        // ─── Local playlists ───────────────────────────
        // Kept in their own group: these live only in this app and hold files
        // from disk, so mixing them into the Tidal list above would imply they
        // sync to your account, which they never do.
        Item { height: 12; visible: localPlaylistList.count > 0 }
        Rectangle {
            visible: localPlaylistList.count > 0
            color: Theme.border; height: 1
            Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
        }
        Item { height: 12; visible: localPlaylistList.count > 0 }

        Text {
            visible: localPlaylistList.count > 0
            Layout.leftMargin: 20
            text: "LOCAL PLAYLISTS"
            color: Theme.textDim
            font.pixelSize: 10
            font.bold: true
            font.letterSpacing: 1.5
        }

        Item { height: 8; visible: localPlaylistList.count > 0 }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(localPlaylistList.contentHeight, 160)
            Layout.bottomMargin: 8
            visible: localPlaylistList.count > 0

            DragScrollEdge {
                view: localPlaylistList
                direction: -1
                anchors { left: parent.left; right: parent.right; top: parent.top }
                onDroppedAt: (index, source) => localPlaylistList.addTracksAt(index, source)
            }
            DragScrollEdge {
                view: localPlaylistList
                direction: 1
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                onDroppedAt: (index, source) => localPlaylistList.addTracksAt(index, source)
            }

        ListView {
            id: localPlaylistList
            anchors.fill: parent
            clip: true
            model: ListModel { id: localPlaylistModel }

            // Shared by the row drop targets and the edge sensors above.
            function addTracksAt(index, source) {
                if (index < 0 || index >= localPlaylistModel.count) return
                if (!source || source.kind !== "local") return
                var ids = []
                for (var i = 0; i < source.payload.length; i++) ids.push(source.payload[i].localId)
                library.addToPlaylist(localPlaylistModel.get(index).id, ids)
            }

            function reload() {
                localPlaylistModel.clear()
                var pls = library.playlists()
                for (var i = 0; i < pls.length; i++) localPlaylistModel.append(pls[i])
            }
            Component.onCompleted: reload()
            Connections {
                target: library
                function onPlaylistsChanged() { localPlaylistList.reload() }
            }

            delegate: Item {
                id: lplDelegate
                width: ListView.view.width
                height: 42
                readonly property int rowIndex: index
                readonly property bool isSource: player.sourceType === "localplaylist"
                                                 && player.sourceId === String(model.id)

                function activate() {
                    root.navigate("localplaylist", {
                        localPlaylistId: model.id,
                        playlistTitle:   model.title
                    })
                }

                activeFocusOnTab: true
                Keys.onReturnPressed: activate()
                Keys.onSpacePressed:  activate()

                // Local playlists hold local files only: playlist_items has a
                // foreign key into the local tracks table.
                DropArea {
                    id: lplDrop
                    anchors.fill: parent
                    keys: ["tidalwave/tracks"]
                    property bool willAccept: containsDrag && drag.source
                                              && drag.source.kind === "local"
                    onEntered: (d) => { if (!d.source || d.source.kind !== "local") d.accepted = false }
                    onDropped: (d) => {
                        if (!d.source || d.source.kind !== "local") { d.accepted = false; return }
                        localPlaylistList.addTracksAt(lplDelegate.rowIndex, d.source)
                        d.accept()
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 6
                    color: lplDrop.willAccept ? Qt.rgba(0, 0.698, 0.973, 0.28)
                           : lplHov.hovered ? Theme.surfaceHov : "transparent"
                    border.width: lplDrop.willAccept ? 1 : (lplDelegate.activeFocus ? 2 : 0)
                    border.color: Theme.accent
                    HoverHandler { id: lplHov }
                    TapHandler { onTapped: lplDelegate.activate() }

                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 16
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        VectorIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: "music"; color: Theme.textDim
                            width: 12; height: 12; strokeWidth: 1.6
                        }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 20 - (lplEq.visible ? 18 : 0)
                            spacing: 1
                            Text {
                                id: lplText
                                width: parent.width
                                text: model.title
                                color: lplDelegate.isSource ? Theme.accent : Theme.textSec
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                text: model.numTracks === 1 ? "1 track" : model.numTracks + " tracks"
                                color: Theme.textDim
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                        EqualizerBars {
                            id: lplEq
                            visible: lplDelegate.isSource
                            running: player.playing
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                }
            }
        }
        }

    }

    Rectangle {
        id: footer
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: 56
        color: Theme.surfaceHigh

        RowLayout {
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            height: 56
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10
            Rectangle {
                width: 32
                height: 32
                radius: 16
                color: Theme.surfaceHov
                VectorIcon {
                    anchors.centerIn: parent
                    name: "user"
                    color: Theme.textSec
                    width: 16
                    height: 16
                    strokeWidth: 1.8
                }
            }
            Text {
                id: acctNameText
                Layout.fillWidth: true
                text: auth.username.length > 0 ? auth.username : "My Account"
                color: Theme.textPrimary
                font.pixelSize: 13
                elide: Text.ElideRight
                ToolTip.visible: acctNameHov.hovered && acctNameText.truncated
                ToolTip.text: acctNameText.text
                ToolTip.delay: 600
                HoverHandler { id: acctNameHov }
            }
            Item {
                width: 28; height: 28
                activeFocusOnTab: true
                Keys.onReturnPressed: root.openSettings()
                Keys.onSpacePressed:  root.openSettings()

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: -2
                    radius: 6
                    color: "transparent"
                    border.width: parent.activeFocus ? 2 : 0
                    border.color: Theme.accent
                }
                VectorIcon {
                    id: settingsButton
                    anchors.centerIn: parent
                    name: "settings"
                    color: settingsHover.hovered ? Theme.textPrimary : Theme.textSec
                    width: 18
                    height: 18
                    strokeWidth: 1.8
                    ToolTip.visible: settingsHover.hovered
                    ToolTip.text: "Settings (Ctrl+,)"
                    HoverHandler { id: settingsHover }
                }
                TapHandler {
                    cursorShape: Qt.PointingHandCursor
                    onTapped: root.openSettings()
                }
            }
        }
    }

    // Raw fetch result, in the bridge's recently-played order. The visible
    // model is derived from it by applyPlaylistView() below.
    property var allPlaylists: []

    function loadPlaylists() {
        // The bridge pages the full playlist collection into memory at login
        // (50 per request, until exhausted) and re-emits favoritePlaylistsChanged
        // as pages land — so this cache is always the complete list.
        root.allPlaylists = bridge.getUserPlaylists()
        root.applyPlaylistView()
    }

    function applyPlaylistView() {
        var list = root.allPlaylists.slice()
        var uid = auth.userId

        if (plPrefs.filterMode === "mine")
            list = list.filter(function(p) { return p.creatorId === uid })
        else if (plPrefs.filterMode === "followed")
            list = list.filter(function(p) { return p.creatorId !== uid })

        // "recent" keeps the fetch order (recently played first). The date
        // fields are ISO timestamps, so string comparison sorts correctly.
        if (plPrefs.sortMode === "alpha")
            list.sort(function(a, b) { return a.title.localeCompare(b.title) })
        else if (plPrefs.sortMode === "updated")
            list.sort(function(a, b) { return (b.updated || "").localeCompare(a.updated || "") })
        else if (plPrefs.sortMode === "created")
            list.sort(function(a, b) { return (b.created || "").localeCompare(a.created || "") })

        // A track dropped on a playlist re-runs this only to move one row's
        // track count. Clearing the model there would send the sidebar back to
        // the top and rebuild every delegate, so when the rows are the same
        // playlists in the same order, write the fields in place instead.
        var sameRows = playlistModel.count === list.length
        for (var j = 0; sameRows && j < list.length; j++)
            if (playlistModel.get(j).uuid !== list[j].uuid) sameRows = false

        if (sameRows) {
            for (var k = 0; k < list.length; k++) {
                playlistModel.setProperty(k, "title", list[k].title)
                playlistModel.setProperty(k, "coverUrl", list[k].coverUrl || "")
                playlistModel.setProperty(k, "type", list[k].type || "")
                playlistModel.setProperty(k, "numTracks", list[k].numTracks || 0)
            }
            return
        }

        playlistModel.clear()
        for (var i = 0; i < list.length; i++) {
            playlistModel.append({
                title:   list[i].title,
                uuid:    list[i].uuid,
                coverUrl: list[i].coverUrl || "",
                type:    list[i].type || "",
                numTracks: list[i].numTracks || 0
            })
        }
    }

    Component.onCompleted: { if (auth.state === 2) loadPlaylists() }

    Connections {
        target: auth
        function onStateChanged(state) {
            if (state === 2) loadPlaylists()
        }
        // A launch without a network reaches the app with an unchecked session,
        // so this first load comes back empty. Run it again once the session
        // checks out for real.
        function onSessionRecovered() { loadPlaylists() }
    }

    Connections {
        target: bridge
        function onFavoritePlaylistsChanged() {
            loadPlaylists()
        }
    }

    Popup {
        id: createPlaylistPopup
        anchors.centerIn: Overlay.overlay
        width: 360
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 20
        background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }
        onOpened: newPlaylistField.forceActiveFocus()

        function create() {
            var title = newPlaylistField.text.trim()
            if (title.length === 0) return
            bridge.createPlaylist(title, function(p, err) {
                if (err) { SyncState.fail("Could not create the playlist: " + err); return }
                root.loadPlaylists()
                root.navigate("playlist", {
                    playlistUuid: p.uuid, playlistTitle: p.title,
                    coverUrl: p.coverUrl || "", playlistType: "USER"
                })
            })
            createPlaylistPopup.close()
        }

        Column {
            width: parent.width
            spacing: 14

            Text { text: "New Playlist"; color: Theme.textPrimary; font.pixelSize: 16; font.bold: true }

            Rectangle { width: parent.width; height: 1; color: Theme.border }

            Rectangle {
                width: parent.width; height: 36; radius: 6
                color: Theme.surface
                border.color: newPlaylistField.activeFocus ? Theme.accent : Theme.border
                TextInput {
                    id: newPlaylistField
                    anchors.fill: parent; anchors.margins: 8
                    color: Theme.textPrimary; font.pixelSize: 14
                    selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                    Keys.onReturnPressed: createPlaylistPopup.create()
                    Text {
                        anchors.fill: parent
                        visible: newPlaylistField.text.length === 0 && !newPlaylistField.activeFocus
                        text: "Playlist name"
                        color: Theme.textDim; font.pixelSize: 14
                    }
                }
            }

            Row {
                spacing: 10; anchors.right: parent.right
                PillButton {
                    text: "Cancel"; accent: false
                    onClicked: createPlaylistPopup.close()
                }
                PillButton {
                    text: "Create"; accent: true
                    onClicked: createPlaylistPopup.create()
                }
            }
        }
    }

    Popup {
        id: settingsPopup
        anchors.centerIn: Overlay.overlay
        width: 480
        // Tall enough for the whole panel, and no taller than the window can
        // hold. It was a flat 640, which is less than the content has needed
        // since the storage section went in — the keyboard map sat below the
        // fold on a window with room to spare. The ScrollView underneath still
        // does its job when the window really is too short.
        readonly property int maxHeight: Overlay.overlay ? Overlay.overlay.height - 48 : 640
        height: Math.max(320, Math.min(maxHeight, settingsContent.implicitHeight))

        // Cover art cache. Read when the popup opens rather than bound to
        // anything: it changes only when this panel is on screen.
        property int    artCacheBytes: 0
        property string artCacheNote: ""

        function refreshArtCache() { artCacheBytes = app.artCacheBytes() }

        function formatBytes(b) {
            if (b < 1024)             return b + " B"
            if (b < 1024 * 1024)      return Math.round(b / 1024) + " KB"
            return (b / (1024 * 1024)).toFixed(1) + " MB"
        }

        onOpened: {
            artCacheNote = ""
            refreshArtCache()
        }

        Timer { id: artCacheNoteTimer; interval: 4000; onTriggered: settingsPopup.artCacheNote = "" }

        Connections {
            target: app
            function onArtCacheCleared(filesRemoved) {
                settingsPopup.artCacheNote = filesRemoved === 1
                    ? "Cleared 1 file"
                    : "Cleared " + filesRemoved + " files"
                artCacheNoteTimer.restart()
                settingsPopup.refreshArtCache()
            }
        }
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        padding: 0
        background: Rectangle {
            color: Theme.surfaceHigh
            border.color: Theme.border
            radius: 12
        }

        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: settingsContent
                width: parent.width
                spacing: 0

                // Header
                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 16
                    Layout.topMargin: 20
                    Layout.bottomMargin: 12

                    Text {
                        text: "Settings"
                        color: Theme.textPrimary
                        font.pixelSize: 18
                        font.bold: true
                        Layout.fillWidth: true
                    }
                    VectorIcon {
                        name: "x"
                        color: Theme.textSec
                        width: 14; height: 14
                        strokeWidth: 1.8
                        MouseArea { anchors.fill: parent; anchors.margins: -6; cursorShape: Qt.PointingHandCursor; onClicked: settingsPopup.close() }
                    }
                }

                Rectangle { color: Theme.border; height: 1; Layout.fillWidth: true }

                // ── ACCOUNT ──────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 14
                    Layout.bottomMargin: 4
                    spacing: 10

                    Text {
                        text: "ACCOUNT"
                        color: Theme.textDim
                        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Rectangle {
                            width: 36; height: 36; radius: 18; color: Theme.accent
                            Text { anchors.centerIn: parent; text: "♪"; color: "white"; font.pixelSize: 16 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { text: "Tidal Wave"; color: Theme.textPrimary; font.pixelSize: 14; font.bold: true }
                            Text { text: "v0.1-alpha"; color: Theme.textDim; font.pixelSize: 12 }
                        }
                        Rectangle {
                            height: 30; width: logoutLabel.implicitWidth + 20; radius: 6
                            color: logoutHov.hovered ? Theme.red : Theme.surface
                            border.color: logoutHov.hovered ? Theme.red : Theme.border
                            Text {
                                id: logoutLabel; anchors.centerIn: parent
                                text: "Log out"; color: logoutHov.hovered ? "white" : Theme.red
                                font.pixelSize: 12
                            }
                            HoverHandler { id: logoutHov }
                            TapHandler { onTapped: { settingsPopup.close(); auth.logout() } }
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; acceptedButtons: Qt.NoButton }
                        }
                    }
                }

                Rectangle { color: Theme.border; height: 1; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }

                // ── PLAYBACK ─────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 14
                    Layout.bottomMargin: 4
                    spacing: 10

                    Text {
                        text: "PLAYBACK"
                        color: Theme.textDim
                        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Text { text: "Streaming Quality"; color: Theme.textPrimary; font.pixelSize: 14; Layout.fillWidth: true }
                        ComboBox {
                            id: qualityCombo
                            model: ["Normal (96 kbps)", "High (320 kbps)", "Lossless (FLAC)", "Hi-Res (24-bit)"]
                            currentIndex: {
                                switch (bridge.preferredQuality) {
                                    case "LOW":             return 0
                                    case "HIGH":            return 1
                                    case "HI_RES_LOSSLESS": return 3
                                    default:                return 2
                                }
                            }
                            Layout.preferredWidth: 160
                            onActivated: function(idx) {
                                var codes = ["LOW", "HIGH", "LOSSLESS", "HI_RES_LOSSLESS"]
                                bridge.preferredQuality = codes[idx]
                            }

                            // Custom ComboBox styling to match dark theme
                            delegate: ItemDelegate {
                                width: qualityCombo.width
                                contentItem: Text {
                                    text: modelData
                                    color: highlighted ? Theme.textPrimary : Theme.textSec
                                    font.pixelSize: 13
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: Rectangle {
                                    color: highlighted ? Theme.surfaceHov : Theme.surfaceHigh
                                }
                                highlighted: qualityCombo.highlightedIndex === index
                            }

                            indicator: Canvas {
                                id: canvas
                                x: qualityCombo.width - width - 10
                                y: qualityCombo.topPadding + (qualityCombo.availableHeight - height) / 2
                                width: 12
                                height: 8
                                contextType: "2d"

                                Connections {
                                    target: qualityCombo.popup
                                    function onVisibleChanged() { canvas.requestPaint() }
                                }

                                onPaint: {
                                    var context = getContext("2d");
                                    context.reset();
                                    context.moveTo(0, 0);
                                    context.lineTo(width, 0);
                                    context.lineTo(width / 2, height);
                                    context.closePath();
                                    context.fillStyle = Theme.textSec;
                                    context.fill();
                                }
                            }

                            contentItem: Text {
                                leftPadding: 10
                                rightPadding: qualityCombo.indicator.width + 15
                                text: qualityCombo.displayText
                                color: Theme.textPrimary
                                font.pixelSize: 13
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                implicitWidth: 160
                                implicitHeight: 32
                                border.color: qualityCombo.pressed ? Theme.accent : Theme.border
                                border.width: 1
                                color: Theme.surfaceHigh
                                radius: Theme.radius
                            }

                            popup: Popup {
                                y: qualityCombo.height + 2
                                width: qualityCombo.width
                                implicitHeight: contentItem.implicitHeight
                                padding: 1
                                background: Rectangle {
                                    border.color: Theme.border
                                    border.width: 1
                                    color: Theme.surfaceHigh
                                    radius: Theme.radius
                                }
                                contentItem: ListView {
                                    clip: true
                                    implicitHeight: contentHeight
                                    model: qualityCombo.popup.visible ? qualityCombo.delegateModel : null
                                    currentIndex: qualityCombo.highlightedIndex
                                    ScrollIndicator.vertical: ScrollIndicator { }
                                }
                            }
                        }
                    }

                }

                Rectangle { color: Theme.border; height: 1; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }

                // ── STORAGE ──────────────────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 14
                    Layout.bottomMargin: 4
                    spacing: 10

                    Text {
                        text: "STORAGE"
                        color: Theme.textDim
                        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text {
                                text: "Cover art cache"
                                color: Theme.textPrimary
                                font.pixelSize: 14
                            }
                            Text {
                                Layout.fillWidth: true
                                text: settingsPopup.artCacheNote !== ""
                                      ? settingsPopup.artCacheNote
                                      : (settingsPopup.artCacheBytes === 0
                                         ? "Empty"
                                         : settingsPopup.formatBytes(settingsPopup.artCacheBytes)
                                           + " — artwork re-downloads as you browse")
                                color: settingsPopup.artCacheNote !== "" ? Theme.green : Theme.textDim
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }

                        Rectangle {
                            id: clearCacheButton
                            enabled: settingsPopup.artCacheBytes > 0
                            opacity: enabled ? 1 : 0.4
                            height: 30; width: clearCacheLabel.implicitWidth + 20; radius: 6
                            color: clearCacheHov.hovered && enabled ? Theme.surfaceHov : Theme.surface
                            border.color: Theme.border
                            Text {
                                id: clearCacheLabel; anchors.centerIn: parent
                                text: "Clear"; color: Theme.textPrimary
                                font.pixelSize: 12
                            }
                            HoverHandler { id: clearCacheHov; enabled: clearCacheButton.enabled }
                            TapHandler {
                                enabled: clearCacheButton.enabled
                                onTapped: app.clearArtCache()
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: clearCacheButton.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                acceptedButtons: Qt.NoButton
                            }
                        }
                    }
                }

                Rectangle { color: Theme.border; height: 1; Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20 }

                // ── KEYBOARD SHORTCUTS ───────────────────
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20
                    Layout.rightMargin: 20
                    Layout.topMargin: 14
                    Layout.bottomMargin: 20
                    spacing: 6

                    Text {
                        text: "KEYBOARD SHORTCUTS"
                        color: Theme.textDim
                        font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
                    }

                    Repeater {
                        model: {
                            var shortcuts = [
                                { k: "Space",              d: "Play / Pause" },
                                { k: "Ctrl+Right / Left",  d: "Next / Previous track" },
                                { k: "Right / Left",       d: "Seek forward / back 10s" },
                                { k: "Up / Down",          d: "Volume up / down" },
                                { k: "Ctrl+S",             d: "Toggle shuffle" },
                                { k: "Ctrl+R",             d: "Cycle repeat mode" },
                                { k: "Ctrl+1 / 2 / 3",     d: "Home / Search / Collection" },
                                { k: "Ctrl+N",             d: "Now Playing" },
                                { k: "Ctrl+Q",             d: "Toggle queue" },
                                { k: "Alt+Left / Esc",     d: "Go back" },
                                { k: "Ctrl+,",             d: "Settings" }
                            ]
                            if (Qt.platform.os === "osx")
                                shortcuts.splice(4, 0, { k: "Cmd+M", d: "Minimize" })
                            else
                                shortcuts.splice(4, 0, { k: "Ctrl+M", d: "Mute" })
                            return shortcuts
                        }
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            Rectangle {
                                color: Theme.surface; radius: 4; border.color: Theme.border
                                implicitWidth: shortcutLabel.implicitWidth + 14; implicitHeight: 22
                                Text {
                                    id: shortcutLabel; anchors.centerIn: parent
                                    text: modelData.k; color: Theme.textPrimary
                                    font.pixelSize: 11; font.family: "monospace"
                                }
                            }
                            Text {
                                text: modelData.d; color: Theme.textSec
                                font.pixelSize: 12; Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }

    // Inline component for nav items — properties on separate lines to avoid semicolon issues
    component SideNavItem : Item {
        id: navItem
        property string icon: ""
        property string label: ""
        property string page: ""
        property string currentPage: ""
        signal activated()

        Layout.fillWidth: true
        height: 44

        activeFocusOnTab: true
        Keys.onReturnPressed: activated()
        Keys.onSpacePressed:  activated()

        Rectangle {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            anchors.topMargin: 2
            anchors.bottomMargin: 2
            radius: Theme.radius
            color: root.currentPage === page
                   ? Theme.surfaceHov
                   : sideHov.hovered ? Qt.rgba(1,1,1,0.04) : "transparent"
            border.width: navItem.activeFocus ? 2 : 0
            border.color: Theme.accent

            Rectangle {
                visible: root.currentPage === page
                width: 3
                height: parent.height * 0.5
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: -8
                radius: 2
                color: Theme.accent
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                spacing: 12
                NavIcon {
                    name: icon
                    color: root.currentPage === page ? Theme.accent : Theme.textSec
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: label
                    color: root.currentPage === page ? Theme.textPrimary : Theme.textSec
                    font.pixelSize: 14
                    font.bold: root.currentPage === page
                }
            }

            HoverHandler { id: sideHov }
            TapHandler   { onTapped: activated() }
        }
    }
}
