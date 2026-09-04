import QtQuick
import QtQuick.Controls
import TidalWave

// Cmd+K palette: jump anywhere, run a command, or reach anything already in
// memory — liked playlists/artists/albums/tracks and the local library — with
// no network call. Nothing here queries the Tidal API; the last row hands the
// typed text to the Search page, which is where remote search lives.
//
// `host` is the ApplicationWindow. The palette drives navigation and the modal
// prompts that live there rather than growing its own copies of them, so it
// needs: navigate(), goBack(), queueOpen, sleepTimerActive, startSleepTimer(),
// cancelSleepTimer(), openSettings(), openEqualizer(), openNewPlaylist(),
// promptImportFiles(), promptImportFolder(), promptNewLocalPlaylist(),
// searchFor().
Popup {
    id: root

    property var host: null

    // Rendered rows, headers included, in display order. Headers are skipped
    // by the arrow keys but occupy an index, so `current` indexes this list.
    property var rows: []
    property int current: -1
    // Read from the field at the top of rebuild(), not bound to it: a binding
    // and the field's onTextChanged handler have no defined order, so a bound
    // property left the palette one keystroke behind the text it was showing.
    property string query: ""

    // Per-section caps. Sections are always rendered in this order — a fixed
    // position is what makes "Cmd+K, three letters, Enter" a reflex, where a
    // best-match-first list would send the same keystrokes somewhere new every
    // time the query grew a letter.
    readonly property int maxCommands:  6
    readonly property int maxPlaylists: 5
    readonly property int maxArtists:   4
    readonly property int maxAlbums:    4
    readonly property int maxTracks:    6

    parent: Overlay.overlay
    width:  Math.min(680, parent ? parent.width - 80 : 680)
    // Chrome is the 56px field, the 30px hint bar and their two dividers; the
    // list takes what is left, so a two-result palette is a small box rather
    // than a mostly-empty tall one.
    //
    // The list's own contentHeight must not appear here: it is an estimate that
    // moves with the view's height, and binding one to the other is a loop. Row
    // heights are known up front, so rebuild() adds them up.
    readonly property int chromeHeight: 56 + 1 + 1 + 30
    property int rowsHeight: 56
    height: Math.min(parent ? parent.height - y - 24 : 560,
                     chromeHeight + rowsHeight)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(24, parent.height * 0.12) : 24
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.surfaceHigh
        border.color: Theme.border
        radius: Theme.radiusLg
    }

    onOpened: {
        // Always opens empty: a remembered query means the first keystroke
        // lands in the middle of stale text.
        input.text = ""
        rebuild()
        input.forceActiveFocus()
    }

    // ─── matching ──────────────────────────────────────
    // Commands are matched on word starts only. Fuzzy subsequence over a fixed
    // list of ~30 labels is a feature ("opset" → Open Settings); the same rule
    // over thousands of track titles is noise, which is why content matching
    // lives in C++ (MatchScore) and only substring-matches.
    function _words(s) {
        return s.toLowerCase().split(/[^a-z0-9]+/).filter(function (w) { return w.length > 0 })
    }

    // Can `q` be split into consecutive word prefixes, starting at word `wi`?
    // "opset" → "Open Settings", "npl" → "Now Playing".
    function _chunkMatch(words, wi, q) {
        if (q.length === 0) return true
        for (var i = wi; i < words.length; i++) {
            var w = words[i]
            var max = Math.min(w.length, q.length)
            var common = 0
            while (common < max && w.charAt(common) === q.charAt(common)) common++
            for (var take = common; take >= 1; take--) {
                if (_chunkMatch(words, i + 1, q.substring(take))) return true
            }
        }
        return false
    }

    function commandScore(label, keywords, q) {
        if (q.length === 0) return 0
        var lower = label.toLowerCase()
        if (lower.indexOf(q) === 0) return 1000
        var words = _words(label)
        for (var i = 0; i < words.length; i++)
            if (words[i].indexOf(q) === 0) return 800
        if (_chunkMatch(words, 0, q)) return 700
        var kw = _words(keywords || "")
        for (var j = 0; j < kw.length; j++)
            if (kw[j].indexOf(q) === 0) return 600
        return -1
    }

    // ─── commands ──────────────────────────────────────
    function go(page, params) { root.close(); host.navigate(page, params || {}) }

    function pageCommands() {
        return [
            { label: "Home",        icon: "home",       keywords: "start",              run: function () { go("home") } },
            { label: "Search",      icon: "search",     keywords: "find tidal",         run: function () { go("search") } },
            { label: "Collection",  icon: "heart",      keywords: "liked favourites favorites library", run: function () { go("collection") } },
            { label: "Local Files", icon: "music",      keywords: "imported disk files", run: function () { go("local") } },
            { label: "Now Playing", icon: "play",       keywords: "current track",      run: function () { go("nowplaying") } },
            { label: "Queue",       icon: "queue",      keywords: "up next",            run: function () { root.close(); host.queueOpen = !host.queueOpen } },
            { label: "Settings",    icon: "settings",   keywords: "preferences options", run: function () { root.close(); host.openSettings() } }
        ]
    }

    function actionCommands() {
        var out = []
        // Play and Pause are separate rows that each only act in one
        // direction: a row named "Play" that pauses because playback happened
        // to be running is a trap. "Stop" is Pause under the name half the
        // world reaches for first.
        out.push({ label: "Play",  icon: "play",  keywords: "resume start", run: function () { root.close(); if (!player.playing) player.playPause() } })
        out.push({ label: "Pause", icon: "pause", keywords: "hold",         run: function () { root.close(); if (player.playing) player.playPause() } })
        out.push({ label: "Stop",  icon: "pause", keywords: "halt",         run: function () { root.close(); if (player.playing) player.playPause() } })
        out.push({ label: "Next track",     icon: "next",     keywords: "skip forward", run: function () { root.close(); player.next() } })
        out.push({ label: "Previous track", icon: "previous", keywords: "back",         run: function () { root.close(); player.previous() } })
        out.push({ label: player.shuffle ? "Shuffle off" : "Shuffle on", icon: "shuffle", keywords: "random",
                   run: function () { root.close(); player.setShuffle(!player.shuffle) } })
        out.push({ label: "Repeat off", icon: "repeat",     keywords: "loop", run: function () { root.close(); player.setRepeatMode(0) } })
        out.push({ label: "Repeat all", icon: "repeat",     keywords: "loop", run: function () { root.close(); player.setRepeatMode(1) } })
        out.push({ label: "Repeat one", icon: "repeat-one", keywords: "loop single", run: function () { root.close(); player.setRepeatMode(2) } })
        out.push({ label: player.muted ? "Unmute" : "Mute", icon: player.muted ? "volume-high" : "volume-mute", keywords: "silence sound",
                   run: function () { root.close(); player.setMuted(!player.muted) } })

        out.push({ label: "Sleep in 15 minutes", icon: "clock", keywords: "timer", run: function () { root.close(); host.startSleepTimer(15, false) } })
        out.push({ label: "Sleep in 30 minutes", icon: "clock", keywords: "timer", run: function () { root.close(); host.startSleepTimer(30, false) } })
        out.push({ label: "Sleep in 60 minutes", icon: "clock", keywords: "timer hour", run: function () { root.close(); host.startSleepTimer(60, false) } })
        out.push({ label: "Sleep at end of track", icon: "clock", keywords: "timer", run: function () { root.close(); host.startSleepTimer(0, true) } })
        if (host && host.sleepTimerActive)
            out.push({ label: "Cancel sleep timer", icon: "x", keywords: "timer stop", run: function () { root.close(); host.cancelSleepTimer() } })

        out.push({ label: "Equalizer", icon: "eq-sliders", keywords: "eq bands tone", run: function () { root.close(); host.openEqualizer() } })
        out.push({ label: equalizer.enabled ? "Equalizer off" : "Equalizer on", icon: "eq-sliders", keywords: "eq bypass",
                   run: function () { root.close(); equalizer.enabled = !equalizer.enabled } })
        out.push({ label: "Reset equalizer", icon: "eq-sliders", keywords: "eq flat clear", run: function () { root.close(); equalizer.reset() } })
        var profiles = equalizer.profiles
        for (var i = 0; i < profiles.length; i++) {
            var name = profiles[i].name
            out.push({ label: "EQ: " + name, icon: "eq-sliders", keywords: "equalizer profile preset",
                       run: (function (n) { return function () { root.close(); equalizer.enabled = true; equalizer.applyProfile(n) } })(name) })
        }

        out.push({ label: "Import files…",  icon: "plus",        keywords: "add local audio", run: function () { root.close(); host.promptImportFiles() } })
        out.push({ label: "Import folder…", icon: "folder-plus", keywords: "add local audio directory", run: function () { root.close(); host.promptImportFolder() } })
        out.push({ label: "New playlist",       icon: "list-plus", keywords: "create tidal", run: function () { root.close(); host.openNewPlaylist() } })
        out.push({ label: "New local playlist", icon: "list-plus", keywords: "create offline files", run: function () { root.close(); host.promptNewLocalPlaylist() } })
        out.push({ label: "Log out", icon: "user", keywords: "sign quit account", run: function () { root.close(); auth.logout() } })
        return out
    }

    // ─── rows ──────────────────────────────────────────
    function header(title, more) { return { header: true, title: title, more: more || 0 } }

    function commandRow(c, typeLabel) {
        return { header: false, label: c.label, sublabel: "", typeLabel: typeLabel,
                 icon: c.icon, cover: "", run: c.run }
    }

    function playlistRow(p) {
        var n = p.numTracks || 0
        return { header: false, label: p.title, typeLabel: "PLAYLIST",
                 sublabel: n + (n === 1 ? " track" : " tracks"),
                 icon: "music", cover: p.coverUrl || "",
                 run: function (alt) {
                     if (!alt) { go("playlist", { playlistUuid: p.uuid, playlistTitle: p.title,
                                                  coverUrl: p.coverUrl || "", playlistType: p.type || "" }); return }
                     root.close()
                     bridge.markPlaylistPlayed(p.uuid)
                     bridge.fetchPlaylistTracks(p.uuid, function (tracks, err) {
                         if (err || tracks.length === 0) return
                         player.setPlaybackSource("playlist", p.uuid, p.title)
                         player.playTracks(tracks, 0)
                     })
                 } }
    }

    function localPlaylistRow(p) {
        var n = p.numTracks || 0
        return { header: false, label: p.title, typeLabel: "LOCAL PLAYLIST",
                 sublabel: n + (n === 1 ? " track" : " tracks"),
                 icon: "music", cover: "",
                 run: function (alt) {
                     if (!alt) { go("localplaylist", { localPlaylistId: p.id, playlistTitle: p.title }); return }
                     root.close()
                     var tracks = library.playlistTracks(p.id)
                     if (tracks.length === 0) return
                     player.setPlaybackSource("localplaylist", String(p.id), p.title)
                     player.playTracks(tracks, 0)
                 } }
    }

    function artistRow(a) {
        return { header: false, label: a.name, sublabel: "Artist", typeLabel: "ARTIST",
                 icon: "artist", cover: a.coverUrl || "",
                 run: function () { go("artist", { artistId: a.id }) } }
    }

    function albumRow(a) {
        return { header: false, label: a.title, sublabel: a.artists || "", typeLabel: "ALBUM",
                 icon: "music", cover: a.coverUrl || "",
                 run: function (alt) {
                     if (!alt) { go("album", { albumId: a.id }); return }
                     root.close()
                     bridge.fetchAlbumTracks(a.id, function (tracks, err) {
                         if (err || tracks.length === 0) return
                         player.setPlaybackSource("album", "" + a.id, a.title)
                         player.playTracks(tracks, 0)
                     })
                 } }
    }

    // A track has no page of its own, so Enter plays it — as a queue of one,
    // replacing whatever was there — and the alternate appends instead.
    function trackRow(t, local) {
        var sub = t.artists || ""
        if (t.albumTitle) sub += (sub.length > 0 ? " · " : "") + t.albumTitle
        return { header: false, label: t.title, sublabel: sub,
                 typeLabel: local ? "LOCAL TRACK" : "TRACK",
                 icon: "music", cover: t.coverUrl80 || t.coverUrl || "",
                 run: function (alt) {
                     root.close()
                     if (alt) { player.appendQueue([t]); return }
                     if (local) player.setPlaybackSource("local", "library", "Local Files")
                     else       player.setPlaybackSource("collection", "tracks", "Liked Songs")
                     player.playTracks([t], 0)
                 } }
    }

    function searchRow(q) {
        return { header: false, label: "Search Tidal for “" + q + "”",
                 sublabel: "", typeLabel: "SEARCH", icon: "search", cover: "",
                 run: function () { root.close(); host.searchFor(q) } }
    }

    function byScore(a, b) { return (b._score || 0) - (a._score || 0) }

    function rebuild() {
        root.query = input.text.trim()
        var out = []
        var q = root.query.toLowerCase()

        if (q.length === 0) {
            var pages = pageCommands()
            out.push(header("Jump to", 0))
            for (var i = 0; i < pages.length; i++) out.push(commandRow(pages[i], "PAGE"))
            var recent = player.recentlyPlayed
            if (recent.length > 0) {
                out.push(header("Recently played", 0))
                for (var r = 0; r < recent.length && r < maxTracks; r++)
                    out.push(trackRow(recent[r], recent[r].isLocal === true))
            }
            root.rows = out
            measure()
            selectFirst()
            return
        }

        // Commands and pages, one list so "se" can reach Search and Settings
        // together, ranked by the same rule.
        var all = pageCommands().map(function (c) { c.typeLabel = "PAGE"; return c })
                  .concat(actionCommands().map(function (c) { c.typeLabel = "ACTION"; return c }))
        var cmdHits = []
        for (var c = 0; c < all.length; c++) {
            var s = commandScore(all[c].label, all[c].keywords, q)
            if (s >= 0) cmdHits.push({ score: s, cmd: all[c] })
        }
        cmdHits.sort(function (a, b) { return b.score - a.score })
        if (cmdHits.length > 0) {
            out.push(header("Commands", Math.max(0, cmdHits.length - maxCommands)))
            for (var ci = 0; ci < cmdHits.length && ci < maxCommands; ci++)
                out.push(commandRow(cmdHits[ci].cmd, cmdHits[ci].cmd.typeLabel))
        }

        // The C++ side gets the query as typed: MatchScore folds case itself,
        // and folding here first would break "Ä" against "ä".
        var raw   = root.query
        var lib   = bridge.searchLibrary(raw, maxTracks)
        var lTr   = library.searchTracks(raw, maxTracks)
        var lPl   = library.searchPlaylists(raw, maxPlaylists)

        // Tidal and local playlists share one section — they are the same idea
        // to the person typing, and the type label on the right says which.
        var pls = (lib.playlists || []).map(function (p) { p._local = false; return p })
                  .concat((lPl.rows || []).map(function (p) { p._local = true; return p }))
        pls.sort(byScore)
        var plTotal = (lib.playlistsTotal || 0) + (lPl.total || 0)
        if (pls.length > 0) {
            out.push(header("Playlists", Math.max(0, plTotal - maxPlaylists)))
            for (var p = 0; p < pls.length && p < maxPlaylists; p++)
                out.push(pls[p]._local ? localPlaylistRow(pls[p]) : playlistRow(pls[p]))
        }

        var artists = lib.artists || []
        if (artists.length > 0) {
            out.push(header("Artists", Math.max(0, (lib.artistsTotal || 0) - maxArtists)))
            for (var a = 0; a < artists.length && a < maxArtists; a++) out.push(artistRow(artists[a]))
        }

        var albums = lib.albums || []
        if (albums.length > 0) {
            out.push(header("Albums", Math.max(0, (lib.albumsTotal || 0) - maxAlbums)))
            for (var b = 0; b < albums.length && b < maxAlbums; b++) out.push(albumRow(albums[b]))
        }

        var trs = (lib.tracks || []).map(function (t) { t._local = false; return t })
                  .concat((lTr.rows || []).map(function (t) { t._local = true; return t }))
        trs.sort(byScore)
        var trTotal = (lib.tracksTotal || 0) + (lTr.total || 0)
        if (trs.length > 0) {
            out.push(header("Tracks", Math.max(0, trTotal - maxTracks)))
            for (var t = 0; t < trs.length && t < maxTracks; t++)
                out.push(trackRow(trs[t], trs[t]._local))
        }

        // Always last, and on its own when nothing local matched — so anything
        // you don't already own is Cmd+K, type, Enter.
        out.push(searchRow(root.query))
        root.rows = out
        measure()
        selectFirst()
    }

    function measure() {
        var h = 0
        for (var i = 0; i < rows.length; i++) h += rows[i].header ? 32 : 56
        rowsHeight = Math.max(56, h)
    }

    function selectFirst() {
        for (var i = 0; i < rows.length; i++) {
            if (!rows[i].header) { current = i; list.positionViewAtIndex(i, ListView.Contain); return }
        }
        current = -1
    }

    function move(step) {
        if (rows.length === 0) return
        var i = current
        for (var n = 0; n < rows.length; n++) {
            i += step
            if (i < 0) i = rows.length - 1
            if (i >= rows.length) i = 0
            if (!rows[i].header) { current = i; list.positionViewAtIndex(i, ListView.Contain); return }
        }
    }

    function activate(alt) {
        if (current < 0 || current >= rows.length) return
        var row = rows[current]
        if (row.header) return
        row.run(alt === true)
    }

    Column {
        anchors.fill: parent

        // ─── input ─────────────────────────────────────
        Item {
            width: parent.width
            height: 56

            Row {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 14
                spacing: 12

                VectorIcon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "search"; width: 18; height: 18; strokeWidth: 1.8
                    color: Theme.textDim
                }

                Item {
                    width: parent.width - 18 - 12 - escPill.width - 12
                    height: parent.height

                    TextInput {
                        id: input
                        anchors.fill: parent
                        focus: true
                        color: Theme.textPrimary
                        font.pixelSize: 16
                        verticalAlignment: TextInput.AlignVCenter
                        selectionColor: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.4)
                        onTextChanged: root.rebuild()

                        Keys.onUpPressed:   root.move(-1)
                        Keys.onDownPressed: root.move(1)
                        Keys.onPressed: (event) => {
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                // Shift is the documented alternate. Cmd (which
                                // Qt reports as ControlModifier on macOS) works
                                // too: the hand that opened the palette is
                                // often still holding it down.
                                root.activate((event.modifiers & Qt.ShiftModifier) !== 0
                                                 || (event.modifiers & Qt.ControlModifier) !== 0)
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape) {
                                root.close()
                                event.accepted = true
                            } else if (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab) {
                                root.move(event.key === Qt.Key_Tab ? 1 : -1)
                                event.accepted = true
                            }
                        }
                    }

                    Text {
                        anchors.fill: parent
                        visible: input.text.length === 0
                        text: "Search or jump to…"
                        color: Theme.textDim
                        font.pixelSize: 16
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Rectangle {
                    id: escPill
                    anchors.verticalCenter: parent.verticalCenter
                    width: escLabel.implicitWidth + 14
                    height: 22
                    radius: 6
                    color: "transparent"
                    border.color: Theme.border
                    Text {
                        id: escLabel
                        anchors.centerIn: parent
                        text: "esc"; color: Theme.textDim; font.pixelSize: 11
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.close() }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        // ─── results ───────────────────────────────────
        ListView {
            id: list
            width: parent.width
            height: root.height - root.chromeHeight
            clip: true
            model: root.rows
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: rowItem
                required property int index
                required property var modelData
                width: ListView.view.width
                height: modelData.header ? 32 : 56

                // Section header
                Item {
                    anchors.fill: parent
                    visible: rowItem.modelData.header
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 18
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        text: rowItem.modelData.header ? rowItem.modelData.title.toUpperCase() : ""
                        color: Theme.textDim
                        font.pixelSize: 10; font.bold: true; font.letterSpacing: 1.5
                    }
                    Text {
                        anchors.right: parent.right
                        anchors.rightMargin: 18
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 6
                        visible: rowItem.modelData.header && rowItem.modelData.more > 0
                        text: "+" + rowItem.modelData.more + " more"
                        color: Theme.textDim
                        font.pixelSize: 10
                    }
                }

                // Result row
                Rectangle {
                    anchors.fill: parent
                    visible: !rowItem.modelData.header
                    color: root.current === rowItem.index ? Theme.surfaceHov : "transparent"

                    Rectangle {
                        width: 3
                        height: parent.height
                        color: Theme.accent
                        visible: root.current === rowItem.index
                    }

                    Row {
                        anchors.fill: parent
                        anchors.leftMargin: 18
                        anchors.rightMargin: 18
                        spacing: 14

                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 40; height: 40; radius: 6
                            color: Theme.surface
                            clip: true
                            Image {
                                anchors.fill: parent
                                visible: !rowItem.modelData.header && rowItem.modelData.cover.length > 0
                                source: (!rowItem.modelData.header && rowItem.modelData.cover.length > 0)
                                        ? "image://tidal/" + rowItem.modelData.cover : ""
                                fillMode: Image.PreserveAspectCrop
                                sourceSize: Qt.size(80, 80)
                                smooth: true
                            }
                            VectorIcon {
                                anchors.centerIn: parent
                                visible: !rowItem.modelData.header && rowItem.modelData.cover.length === 0
                                name: rowItem.modelData.header ? "music" : rowItem.modelData.icon
                                width: 16; height: 16; strokeWidth: 1.8
                                color: root.current === rowItem.index ? Theme.textPrimary : Theme.textSec
                            }
                        }

                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width - 40 - 14 - typeLabel.width - 14
                            spacing: 2
                            Text {
                                width: parent.width
                                text: rowItem.modelData.header ? "" : rowItem.modelData.label
                                color: Theme.textPrimary
                                font.pixelSize: 14; font.bold: true
                                elide: Text.ElideRight
                            }
                            Text {
                                width: parent.width
                                visible: !rowItem.modelData.header && rowItem.modelData.sublabel.length > 0
                                text: rowItem.modelData.header ? "" : rowItem.modelData.sublabel
                                color: Theme.textSec
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }

                        Text {
                            id: typeLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: rowItem.modelData.header ? "" : rowItem.modelData.typeLabel
                            color: Theme.textDim
                            font.pixelSize: 10; font.bold: true; font.letterSpacing: 1
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onEntered: root.current = rowItem.index
                        onClicked: (mouse) => {
                            root.current = rowItem.index
                            root.activate((mouse.modifiers & Qt.ShiftModifier) !== 0
                                          || (mouse.modifiers & Qt.ControlModifier) !== 0)
                        }
                    }
                }
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        // ─── hints ─────────────────────────────────────
        Item {
            width: parent.width
            height: 30
            Text {
                anchors.left: parent.left
                anchors.leftMargin: 18
                anchors.verticalCenter: parent.verticalCenter
                text: "↑↓ navigate    ↵ open    ⇧↵ play    esc close"
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
    }
}
