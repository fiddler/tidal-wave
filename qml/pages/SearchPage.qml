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
    focus: true

    property string query: ""
    property var    tracks:    []
    property var    albums:    []
    property var    artists:   []
    property var    playlists: []
    property var    mixes:     []
    property bool   loading:   false
    property int    activeTab: 0
    property int    _searchGen: 0

    // The five kinds, in tab order. Tab 0 is "All"; tab n is kinds[n - 1].
    readonly property var kinds: ["tracks", "albums", "artists", "playlists", "mixes"]
    readonly property var kindLabels: ({ tracks: "tracks", albums: "albums", artists: "artists",
                                         playlists: "playlists", mixes: "mixes" })
    readonly property string activeKind: activeTab > 0 ? kinds[activeTab - 1] : ""

    // ── what the arrays above describe ─────────────────
    // Four empty arrays are the state the page starts in, not an answer. Until
    // a reply for the query on screen has landed, nothing may say "No results".
    property string repliedFor: ""
    property string searchError: ""
    readonly property bool settled: query.length >= 2 && repliedFor === query
    readonly property int  totalHits: tracks.length + albums.length + artists.length
                                      + playlists.length + mixes.length

    // ── paging ─────────────────────────────────────────
    readonly property int pageSize: 20
    readonly property int maxRows: 500        // per kind; a list nobody scrolls past
    property var    totals: ({})              // kind -> the server's total
    property var    exhausted: ({})           // kind -> true once there is no more to ask for
    property string pagingKind: ""            // the one page request in flight
    property int    _pageToken: 0

    property var recent: bridge.recentSearches()
    Connections {
        target: bridge
        function onRecentSearchesChanged() { root.recent = bridge.recentSearches() }
    }

    function releaseFocus() {
        searchBar.releaseFocus()
    }
    function focusSearch() {
        searchBar.takeFocus()
    }

    // Set by the command palette through navigate(). Filling the field is
    // enough — the field's own edit signal runs the search. Cleared straight
    // after, or searching for the same text twice would assign an unchanged
    // property, emit nothing, and leave whatever the field had been edited to.
    property string pendingQuery: ""
    onPendingQueryChanged: {
        if (pendingQuery.length === 0) return
        var q = pendingQuery
        pendingQuery = ""
        searchBar.text = q
    }

    function viewFor(kind) {
        return kind === "tracks"    ? tracksView
             : kind === "albums"    ? albumsGrid
             : kind === "artists"   ? artistsGrid
             : kind === "playlists" ? playlistsGrid
             : kind === "mixes"     ? mixesGrid : null
    }
    function rowsFor(kind) {
        return kind === "tracks"    ? tracksRows
             : kind === "albums"    ? albumsRows
             : kind === "artists"   ? artistsRows
             : kind === "playlists" ? playlistsRows
             : kind === "mixes"     ? mixesRows : null
    }

    // The paged views count rows off a ListModel and read each row's data out
    // of the arrays by index. Assigning a longer array to a view's model would
    // reset the view to the top on every page; appending rows does not.
    function syncRows(kind) {
        var rows = rowsFor(kind)
        var n = root[kind].length
        if (rows.count > n) rows.clear()
        for (var i = rows.count; i < n; i++) rows.append({ n: i })
    }

    function keyOf(kind, item) { return kind === "playlists" ? item.uuid : item.id }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item { height: 16 }

        SearchBar {
            id: searchBar
            focus: true
            Layout.fillWidth: true
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            height: 46
            placeholder: "Search tracks, albums, artists, playlists…"
            onTextEdited: (text) => {
                root.query = text
                if (text.length < 2) { clearResults(); return }
                searchDebounce.restart()
            }
            // Enter searches now and remembers the query whatever it finds.
            onSubmitted: (text) => {
                if (text.length < 2) return
                searchDebounce.stop()
                bridge.addRecentSearch(text)
                doSearch(text)
            }
        }

        Item { height: 12 }

        // Tab bar
        Row {
            visible: root.query.length >= 2
            Layout.leftMargin: 24
            spacing: 4
            Repeater {
                model: ["All", "Tracks", "Albums", "Artists", "Playlists", "Mixes"]
                Rectangle {
                    id: searchTab
                    required property string modelData
                    required property int    index
                    // Tidal's search answered no mixes for this client when
                    // probed on 2026-10-09, whatever `types` asked for. The
                    // tab shows itself only if a reply ever carries some.
                    visible: index < 5 || root.mixes.length > 0
                    height: 30; width: tabLabel.implicitWidth + 24; radius: 15
                    color: root.activeTab === index ? Theme.accent
                           : tabMA.containsMouse ? Theme.surfaceHov : "transparent"
                    border.width: searchTab.activeFocus ? 2 : 0
                    border.color: Theme.accent
                    activeFocusOnTab: true
                    Keys.onReturnPressed: root.activeTab = index
                    Keys.onSpacePressed:  root.activeTab = index
                    Text {
                        id: tabLabel; anchors.centerIn: parent
                        text: modelData
                        color: root.activeTab === index ? "white" : Theme.textSec
                        font.pixelSize: 13
                    }
                    MouseArea {
                        id: tabMA
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.activeTab = index
                    }
                }
            }
        }

        Item { height: 8 }

        // Before a search: recent queries, or the bare prompt when there are none.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length < 2

            ColumnLayout {
                visible: root.recent.length === 0
                anchors.centerIn: parent
                spacing: 16
                VectorIcon {
                    Layout.alignment: Qt.AlignHCenter
                    name: "search"
                    color: Theme.textDim
                    width: 48
                    height: 48
                    strokeWidth: 2.0
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: "Search Tidal"; color: Theme.textPrimary; font.pixelSize: 22; font.bold: true }
                Text { Layout.alignment: Qt.AlignHCenter; text: "Find tracks, albums, artists and more"; color: Theme.textSec; font.pixelSize: 14 }
            }

            ColumnLayout {
                visible: root.recent.length > 0
                anchors { top: parent.top; left: parent.left; right: parent.right
                          topMargin: 8; leftMargin: 24; rightMargin: 24 }
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: "Recent searches"
                        color: Theme.textPrimary; font.pixelSize: 18; font.bold: true
                    }
                    Text {
                        text: "Clear"
                        color: clearHov.hovered ? Theme.textPrimary : Theme.textSec
                        font.pixelSize: 13
                        HoverHandler { id: clearHov; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: bridge.clearRecentSearches() }
                    }
                }
                Item { height: 4 }

                Repeater {
                    model: root.recent
                    Rectangle {
                        id: recentRow
                        objectName: "recentSearchRow"
                        required property string modelData
                        Layout.fillWidth: true
                        height: 40; radius: 6
                        color: recentMA.containsMouse ? Theme.surfaceHov : "transparent"
                        border.width: recentRow.activeFocus ? 2 : 0
                        border.color: Theme.accent
                        activeFocusOnTab: true
                        Keys.onReturnPressed: searchBar.text = modelData
                        Keys.onSpacePressed:  searchBar.text = modelData

                        MouseArea {
                            id: recentMA
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: searchBar.text = recentRow.modelData
                        }
                        RowLayout {
                            anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                            spacing: 12
                            VectorIcon { name: "search"; color: Theme.textDim; width: 14; height: 14; strokeWidth: 1.8 }
                            Text {
                                Layout.fillWidth: true
                                text: recentRow.modelData
                                color: Theme.textPrimary; font.pixelSize: 14
                                elide: Text.ElideRight
                            }
                            Item {
                                width: 20; height: 20
                                visible: recentMA.containsMouse || removeMA.containsMouse
                                VectorIcon { anchors.centerIn: parent; name: "x"; color: Theme.textDim; width: 12; height: 12; strokeWidth: 1.8 }
                                MouseArea {
                                    id: removeMA
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: bridge.removeRecentSearch(recentRow.modelData)
                                }
                            }
                        }
                    }
                }
            }
        }

        // One line for whatever the reply was not: a failure, or nothing found
        // for the kind this tab shows.
        Item {
            objectName: "searchStatus"
            Layout.fillWidth: true
            height: 80
            visible: root.query.length >= 2 && statusText.text.length > 0
            Text {
                id: statusText
                objectName: "searchStatusText"
                anchors.centerIn: parent
                font.pixelSize: 15
                color: root.searchError.length > 0 ? Theme.red : Theme.textSec
                text: {
                    if (!root.settled) return ""
                    if (root.searchError.length > 0) return root.searchError
                    if (root.totalHits === 0) return "No results for \"" + root.query + "\""
                    if (root.activeKind.length > 0 && root[root.activeKind].length === 0)
                        return "No " + root.kindLabels[root.activeKind] + " for \"" + root.query + "\""
                    return ""
                }
            }
        }

        // ── All: a five-kind summary ───────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            rightPadding: 14
            contentWidth: availableWidth
            visible: root.query.length >= 2 && root.activeTab === 0

            ColumnLayout {
                width: parent.width
                spacing: 0

                ColumnLayout {
                    visible: root.tracks.length > 0
                    Layout.fillWidth: true
                    spacing: 0

                    Item { height: 16 }
                    Text { Layout.leftMargin: 24; text: "Tracks"; color: Theme.textPrimary; font.pixelSize: 18; font.bold: true }
                    Item { height: 8 }

                    Repeater {
                        model: root.activeTab === 0 ? Math.min(root.tracks.length, 5) : 0
                        TrackRow {
                            selection: trackSel
                            rowIndex:  index
                            required property int index
                            Layout.fillWidth: true
                            Layout.leftMargin: 16
                            Layout.rightMargin: 16
                            trackNum:       index + 1
                            title:          root.tracks[index].title
                            artists:        root.tracks[index].artists
                            albumTitle:     root.tracks[index].albumTitle
                            durationStr:    root.tracks[index].durationStr
                            coverUrl:       root.tracks[index].coverUrl80
                            isPlaying:      player.currentTrack.id === root.tracks[index].id && player.playing
                            isLoading:      player.currentTrack.id === root.tracks[index].id && player.loading
                            trackData:      root.tracks[index]
                            showPopularity: true
                            onPlayRequested: player.playTracks(root.tracks, index)
                        }
                    }
                    Item { height: 16 }
                }

                HorizontalSection {
                    Layout.fillWidth: true
                    visible: root.albums.length > 0
                    title: "Albums"
                    showViewAll: false
                    items: root.albums.map(function(a) {
                        return { id: a.id, title: a.title, subtitle: a.artists,
                                 coverUrl: a.coverUrl, artistId: a.artistId || 0 }
                    })
                    mediaType: "album"
                    onItemClicked: (i, item) => navigateTo("album", { albumId: item.id })
                    onItemSubtitleClicked: (i, item) => navigateTo("artist", { artistId: item.artistId })
                    onItemPlayClicked: (i, item) => root.playAlbum(item.id)
                }

                HorizontalSection {
                    Layout.fillWidth: true
                    visible: root.artists.length > 0
                    title: "Artists"
                    showViewAll: false
                    items: root.artists.map(function(a) {
                        return { id: a.id, title: a.name, subtitle: "Artist", coverUrl: a.coverUrl || "" }
                    })
                    mediaType: "artist"
                    onItemClicked: (i, item) => navigateTo("artist", { artistId: item.id })
                }

                HorizontalSection {
                    Layout.fillWidth: true
                    visible: root.playlists.length > 0
                    title: "Playlists"
                    showViewAll: false
                    items: root.playlists.map(function(p) {
                        return { id: p.uuid, title: p.title, subtitle: p.numTracks + " tracks", coverUrl: p.coverUrl || "", playlistType: p.type || "" }
                    })
                    mediaType: "playlist"
                    onItemClicked: (i, item) => navigateTo("playlist", { playlistUuid: item.id, playlistTitle: item.title, coverUrl: item.coverUrl, playlistType: item.playlistType || "" })
                    onItemPlayClicked: (i, item) => root.playPlaylist(item.id)
                }

                HorizontalSection {
                    Layout.fillWidth: true
                    visible: root.mixes.length > 0
                    title: "Mixes"
                    showViewAll: false
                    items: root.mixes.map(function(m) {
                        return { id: m.id, title: m.title, subtitle: m.subtitle || "", coverUrl: m.coverUrl || "" }
                    })
                    mediaType: "mix"
                    onItemClicked: (i, item) => root.openMix(item)
                    onItemPlayClicked: (i, item) => root.playMix(item.id)
                }

                Item { height: 32 }
            }
        }

        // ── Tracks ─────────────────────────────────────
        ListModel { id: tracksRows }
        ListView {
            id: tracksView
            objectName: "searchTracksView"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length >= 2 && root.activeTab === 1
            clip: true
            model: tracksRows
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.checkMore()

            delegate: TrackRow {
                required property int index
                readonly property var item: root.tracks[index] || ({})
                selection: trackSel
                rowIndex:  index
                width: tracksView.width - 32
                x: 16
                trackNum:       index + 1
                title:          item.title || ""
                artists:        item.artists || ""
                albumTitle:     item.albumTitle || ""
                durationStr:    item.durationStr || ""
                coverUrl:       item.coverUrl80 || ""
                isPlaying:      player.currentTrack.id === item.id && player.playing
                isLoading:      player.currentTrack.id === item.id && player.loading
                trackData:      item
                showPopularity: true
                onPlayRequested: player.playTracks(root.tracks, index)
            }

            footer: Item { height: 32; width: tracksView.width }
            ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
        }

        // ── Albums ─────────────────────────────────────
        ListModel { id: albumsRows }
        GridView {
            id: albumsGrid
            objectName: "searchAlbumsGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length >= 2 && root.activeTab === 2
            clip: true
            model: albumsRows
            cellWidth: 184; cellHeight: 232
            leftMargin: 24; rightMargin: 24; topMargin: 16
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.checkMore()

            delegate: Item {
                required property int index
                readonly property var item: root.albums[index] || ({})
                width: 184; height: 232
                MediaCard {
                    anchors.centerIn: parent
                    title: item.title || ""
                    subtitle: item.artists || ""
                    coverUrl: item.coverUrl || ""
                    mediaType: "album"
                    artistId: item.artistId || 0
                    mediaId: item.id || 0
                    onClicked: navigateTo("album", { albumId: item.id })
                    onSubtitleClicked: navigateTo("artist", { artistId: item.artistId })
                    onPlayClicked: root.playAlbum(item.id)
                }
            }
            ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
        }

        // ── Artists ────────────────────────────────────
        ListModel { id: artistsRows }
        GridView {
            id: artistsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length >= 2 && root.activeTab === 3
            clip: true
            model: artistsRows
            cellWidth: 184; cellHeight: 232
            leftMargin: 24; rightMargin: 24; topMargin: 16
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.checkMore()

            delegate: Item {
                required property int index
                readonly property var item: root.artists[index] || ({})
                width: 184; height: 232
                MediaCard {
                    anchors.centerIn: parent
                    title: item.name || ""
                    subtitle: "Artist"
                    coverUrl: item.coverUrl || ""
                    mediaType: "artist"
                    onClicked: navigateTo("artist", { artistId: item.id })
                }
            }
            ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
        }

        // ── Playlists ──────────────────────────────────
        ListModel { id: playlistsRows }
        GridView {
            id: playlistsGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length >= 2 && root.activeTab === 4
            clip: true
            model: playlistsRows
            cellWidth: 184; cellHeight: 232
            leftMargin: 24; rightMargin: 24; topMargin: 16
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.checkMore()

            delegate: Item {
                required property int index
                readonly property var item: root.playlists[index] || ({})
                width: 184; height: 232
                MediaCard {
                    anchors.centerIn: parent
                    title: item.title || ""
                    subtitle: item.numTracks === 1 ? "1 track" : (item.numTracks || 0) + " tracks"
                    coverUrl: item.coverUrl || ""
                    mediaType: "playlist"
                    onClicked: navigateTo("playlist", { playlistUuid: item.uuid, playlistTitle: item.title,
                                                        coverUrl: item.coverUrl || "", playlistType: item.type || "" })
                    onPlayClicked: root.playPlaylist(item.uuid)
                }
            }
            ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
        }

        // ── Mixes ──────────────────────────────────────
        ListModel { id: mixesRows }
        GridView {
            id: mixesGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.query.length >= 2 && root.activeTab === 5
            clip: true
            model: mixesRows
            cellWidth: 184; cellHeight: 232
            leftMargin: 24; rightMargin: 24; topMargin: 16
            boundsBehavior: Flickable.StopAtBounds
            onContentYChanged: root.checkMore()

            delegate: Item {
                required property int index
                readonly property var item: root.mixes[index] || ({})
                width: 184; height: 232
                MediaCard {
                    anchors.centerIn: parent
                    title: item.title || ""
                    subtitle: item.subtitle || ""
                    coverUrl: item.coverUrl || ""
                    mediaType: "mix"
                    onClicked: root.openMix(item)
                    onPlayClicked: root.playMix(item.id)
                }
            }
            ScrollBar.vertical: ScrollBar { active: true; policy: ScrollBar.AsNeeded }
        }
    }

    Timer {
        id: searchDebounce
        interval: 400
        onTriggered: {
            if (root.query.length >= 2) doSearch(root.query)
        }
    }

    // A check that follows a change in content height rather than a scroll.
    // Appended delegates arrive over several layout passes, and for the first
    // few the bottom is still within the margin: asked at once, one scroll
    // would page straight to the end of the result set.
    Timer {
        id: moreSettle
        objectName: "searchMoreSettle"
        interval: 80
        onTriggered: root.checkMore()
    }
    onActiveTabChanged: moreSettle.restart()

    function doSearch(q) {
        loading = true
        var gen = ++root._searchGen
        root._pageToken++          // a page of the previous query is no longer wanted
        root.pagingKind = ""
        bridge.search(q, function(results, err) {
            // Network replies can arrive out of order — a slow response for
            // an earlier keystroke could otherwise overwrite the results of
            // a more recent search (often with an empty result set, making
            // it look like "search finds nothing"). Only the most recently
            // dispatched request may update the UI.
            if (gen !== root._searchGen) return
            loading = false
            var failed = !!err && err.length > 0
            // A failed search must not leave the previous query's results
            // standing under the new query text.
            var r = failed ? {} : results
            var totals = {}, done = {}
            for (var i = 0; i < root.kinds.length; i++) {
                var k = root.kinds[i]
                var list = r[k] || []
                root[k] = list
                totals[k] = r[k + "Total"] || 0
                done[k] = list.length === 0 || list.length >= totals[k]
                root.syncRows(k)
                // Same row count as the last query leaves the model untouched,
                // and with it the old scroll offset.
                var v = root.viewFor(k)
                if (v) v.positionViewAtBeginning()
            }
            root.totals = totals
            root.exhausted = done
            root.searchError = failed ? "Search failed. Check your connection and try again." : ""
            if (root.activeTab === 5 && root.mixes.length === 0) root.activeTab = 0
            root.repliedFor = q
            if (!failed && root.totalHits > 0) bridge.addRecentSearch(q)
            moreSettle.restart()
        }, root.pageSize, 0, "")
    }

    // Near the bottom of the tab that is showing: ask for its next page.
    function checkMore() {
        var kind = root.activeKind
        if (kind.length === 0 || !root.settled) return
        var view = viewFor(kind)
        if (!view || !view.visible) return
        if (view.contentHeight - view.contentY - view.height < 600) loadMore(kind)
    }

    function loadMore(kind) {
        if (!root.settled || root.pagingKind.length > 0 || root.exhausted[kind]) return
        var have = root[kind].length
        if (have >= root.maxRows) return
        root.pagingKind = kind
        var token = ++root._pageToken
        bridge.search(root.query, function(results, err) {
            if (token !== root._pageToken) return     // superseded by a new search
            root.pagingKind = ""
            var done = Object.assign({}, root.exhausted)
            if (err && err.length > 0) {
                // Keep the rows already on screen and stop asking for this kind.
                done[kind] = true
                root.exhausted = done
                return
            }
            var page = results[kind] || []
            var seen = {}
            var current = root[kind]
            for (var i = 0; i < current.length; i++) seen[root.keyOf(kind, current[i])] = true
            var added = []
            for (var j = 0; j < page.length; j++) {
                var key = root.keyOf(kind, page[j])
                if (seen[key]) continue
                seen[key] = true
                added.push(page[j])
            }
            var total = results[kind + "Total"] || root.totals[kind] || 0
            if (added.length > 0) {
                root[kind] = current.concat(added)
                root.syncRows(kind)
            }
            // A short page, the total reached, or a page of nothing new: done.
            done[kind] = page.length < root.pageSize || added.length === 0
                         || root[kind].length >= total
            root.exhausted = done
            moreSettle.restart()
        // The server ignores the kind filter today and answers all of them;
        // only `kind` is read from the reply either way.
        }, root.pageSize, have, kind.toUpperCase())
    }

    function clearResults() {
        for (var i = 0; i < kinds.length; i++) { root[kinds[i]] = []; syncRows(kinds[i]) }
        totals = {}; exhausted = {}
        repliedFor = ""; searchError = ""
        pagingKind = ""; _pageToken++
        _searchGen++; loading = false
    }

    function playAlbum(id) {
        bridge.fetchAlbumTracks(id, function(tracks, err) {
            if (!err && tracks.length > 0) player.playTracks(tracks, 0)
        })
    }
    function playPlaylist(uuid) {
        bridge.markPlaylistPlayed(uuid)
        bridge.fetchPlaylistTracks(uuid, function(tracks, err) {
            if (!err && tracks.length > 0) player.playTracks(tracks, 0)
        })
    }
    function playMix(id) {
        bridge.fetchMixTracks(id, function(tracks, err) {
            if (!err && tracks.length > 0) player.playTracks(tracks, 0)
        })
    }
    function openMix(m) {
        navigateTo("mix", { mixId: m.id, title: m.title || "", subtitle: m.subtitle || "",
                            coverUrl: m.coverUrl || "" })
    }

    function navigateTo(page, params) {
        Window.window.navigate(page, params)
    }

    LoadingOverlay { loading: root.loading }
}
