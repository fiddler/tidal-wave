import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "PlaylistLoading"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var requests: []
        signal favoriteTracksChanged()
        function isTrackFavorite(id) { return false }
        function fetchPlaylistTracks(uuid, callback) {
            requests.push({uuid: uuid, callback: callback})
        }
        property var edits: []
        property var metas: []
        function fetchPlaylistMeta(uuid, callback) { metas.push({uuid: uuid, callback: callback}) }
        function editPlaylist(uuid, title, description, callback) {
            edits.push({uuid: uuid, title: title, description: description, callback: callback})
        }
    }
    QtObject {
        id: offline
        signal playlistChanged(string uuid)
        function status(uuid) { return {state: uuid === "cached" ? "offline" : "none"} }
        function cachedTracks(uuid) { return uuid === "cached" ? [testCase.track(42)] : [] }
    }
    QtObject {
        id: player
        property var currentTrack: ({id: 0})
        property bool playing: false
        property bool loading: false
    }
    QtObject {
        id: downloader
        signal downloadStarted(double trackId)
        signal downloadFinished(double trackId, string path)
        signal downloadError(double trackId, string message)
        function isDownloading(id) { return false }
    }
    QtObject {
        id: app
        function isDownloaded(id) { return false }
    }

    Component { id: pageComponent; PlaylistPage {} }
    property var page: null

    function track(id) {
        return {id: id, title: "Test", artists: "Artist", albumTitle: "Album",
                durationStr: "1:00", coverUrl80: "", coverUrl: ""}
    }
    function init() {
        bridge.requests = []
        bridge.edits = []
        bridge.metas = []
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        verify(page !== null)
    }
    function test_navigateFromStalledRequestToCachedPlaylist() {
        page.playlistUuid = "stalled"
        compare(page.loading, true)
        // Main.qml recreates the detail page when opening another playlist.
        page.destroy()
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        page.playlistUuid = "cached"
        compare(page.loading, false, "Cached playlist must not wait for the stalled network")
        compare(page.tracks.length, 1)
        compare(page.tracks[0].id, 42)
    }

    function test_networkErrorReleasesSpinner() {
        page.playlistUuid = "cached"
        bridge.requests[0].callback([], "Network unavailable")
        compare(page.loading, false)
        compare(page.tracks[0].id, 42)
    }

    function test_staleResponseCannotFinishNewLoad() {
        page.playlistUuid = "first"
        page.playlistUuid = "second"
        bridge.requests[0].callback([track(1)], "")
        compare(page.loading, true)
        compare(page.tracks.length, 0)
        bridge.requests[1].callback([track(2)], "")
        compare(page.loading, false)
        compare(page.tracks[0].id, 2)
    }

    function test_cachedRefreshCanReturnEmptyPlaylist() {
        page.playlistUuid = "cached"
        bridge.requests[0].callback([], "")
        compare(page.loading, false)
        compare(page.tracks.length, 0)
    }

    function test_stallTimesOutAndRetryIgnoresLateResponse() {
        page.playlistUuid = "stalled"
        var deadline = findChild(page, "playlistLoadDeadline")
        verify(deadline !== null, "Playlist loads need a deadline")
        deadline.interval = 30
        tryCompare(page, "loading", false, 1000)
        verify(page.loadError.length > 0)
        var retry = findChild(page, "playlistLoadRetry")
        verify(retry !== null)
        deadline.interval = 15000
        mouseClick(retry)
        compare(bridge.requests.length, 2)
        compare(page.loading, true)
        bridge.requests[0].callback([track(1)], "")
        compare(page.loading, true)
        compare(page.tracks.length, 0)
        bridge.requests[1].callback([track(2)], "")
        compare(page.loading, false)
        compare(page.loadError, "")
        compare(page.tracks[0].id, 2)
    }

    // The Edit popup used to write the new title locally and never tell the
    // server, so a rename was lost on the next navigation.
    function openEdit(title, description) {
        page.playlistType = "USER"
        page.playlistTitle = "Old"
        page.playlistDescription = "Old words"
        page.playlistUuid = "cached"
        var popup = findChild(page, "editPlaylistPopup")
        verify(popup !== null)
        popup.open()
        findChild(popup.contentItem, "editPlaylistTitle").text = title
        findChild(popup.contentItem, "editPlaylistDescription").text = description
        return popup
    }

    function test_renameReachesTheServerAndOnlyThenTheScreen() {
        var popup = openEdit("  New  ", "New words")
        popup.save()
        compare(bridge.edits.length, 1)
        compare(bridge.edits[0].uuid, "cached")
        compare(bridge.edits[0].title, "New")
        compare(bridge.edits[0].description, "New words", "both fields go, or the server clears one")
        compare(page.playlistTitle, "Old", "nothing changes before the answer")
        compare(popup.saving, true)
        bridge.edits[0].callback(true)
        compare(page.playlistTitle, "New")
        compare(page.playlistDescription, "New words")
        tryCompare(popup, "opened", false, 1000)
    }

    function test_refusedRenameKeepsTheOldTitleAndSaysSo() {
        var popup = openEdit("New", "Old words")
        popup.save()
        bridge.edits[0].callback(false)
        compare(page.playlistTitle, "Old")
        verify(popup.error.length > 0)
        compare(popup.opened, true, "what was typed is kept")
        compare(popup.saving, false)
        popup.close()
    }

    // Opened from the sidebar the page has no description. Saving a rename
    // then posted an empty one and wiped what the server held.
    function test_renameKeepsADescriptionThePageNeverHad() {
        page.playlistType = "USER"
        page.playlistTitle = "Old"
        page.playlistUuid = "cached"
        var popup = findChild(page, "editPlaylistPopup")
        popup.begin()
        findChild(popup.contentItem, "editPlaylistTitle").text = "New"
        popup.save()
        compare(bridge.edits.length, 0, "no save before the description is known")
        bridge.metas[0].callback({uuid: "cached", description: "Server words"}, "")
        popup.save()
        compare(bridge.edits.length, 1)
        compare(bridge.edits[0].description, "Server words")
        bridge.edits[0].callback(true)
        tryCompare(popup, "opened", false, 1000)
    }

    function test_unknownDescriptionBlocksTheSave() {
        page.playlistType = "USER"
        page.playlistTitle = "Old"
        page.playlistUuid = "cached"
        var popup = findChild(page, "editPlaylistPopup")
        popup.begin()
        bridge.metas[0].callback({}, "Network unavailable")
        findChild(popup.contentItem, "editPlaylistTitle").text = "New"
        popup.save()
        compare(bridge.edits.length, 0)
        verify(popup.error.length > 0)
        popup.close()
    }

    function test_emptyTitleIsNotSent() {
        var popup = openEdit("   ", "x")
        popup.save()
        compare(bridge.edits.length, 0)
        verify(popup.error.length > 0)
        popup.close()
    }

    function test_unchangedEditSendsNothing() {
        var popup = openEdit("Old", "Old words")
        popup.save()
        compare(bridge.edits.length, 0)
        tryCompare(popup, "opened", false, 1000)
    }

    function test_staleSilentReloadCannotOverwriteNewPlaylist() {
        page.playlistUuid = "first"
        bridge.requests[0].callback([track(1)], "")
        page.silentReload()
        page.playlistUuid = "cached"
        bridge.requests[1].callback([track(99)], "")
        compare(page.tracks[0].id, 42)
    }
}
