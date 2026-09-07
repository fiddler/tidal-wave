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

    function test_staleSilentReloadCannotOverwriteNewPlaylist() {
        page.playlistUuid = "first"
        bridge.requests[0].callback([track(1)], "")
        page.silentReload()
        page.playlistUuid = "cached"
        bridge.requests[1].callback([track(99)], "")
        compare(page.tracks[0].id, 42)
    }
}
