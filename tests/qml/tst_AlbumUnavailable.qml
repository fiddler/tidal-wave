import QtQuick
import QtTest
import TidalWave

// A saved album whose edition was delisted answers 404. The page used to drop
// the reason and draw nothing, which looked exactly like a broken app.
TestCase {
    id: testCase
    name: "AlbumUnavailable"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var albumRequests: []
        property var trackRequests: []
        signal favoriteTracksChanged()
        signal favoriteAlbumsChanged()
        function isTrackFavorite(id) { return false }
        function isAlbumFavorite(id) { return false }
        function fetchAlbum(id, callback) { albumRequests.push(callback) }
        function fetchAlbumTracks(id, callback) { trackRequests.push(callback) }
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

    Component { id: pageComponent; AlbumPage {} }
    property var page: null

    function init() {
        bridge.albumRequests = []
        bridge.trackRequests = []
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        verify(page !== null)
        page.albumId = 77
    }

    function test_delistedAlbumSaysSo() {
        var panel = findChild(page, "albumFailure")
        compare(panel.visible, false, "an album still in flight is not accused of being dead")
        bridge.albumRequests[0]({}, "Not Found", 404)
        bridge.trackRequests[0]([], "Not Found")
        compare(page.loadState, "unavailable")
        compare(panel.visible, true)
        compare(findChild(page, "albumRetry").visible, false)
    }

    function test_networkFailureOffersRetry() {
        bridge.albumRequests[0]({}, "Network unavailable", 0)
        bridge.trackRequests[0]([], "Network unavailable")
        compare(page.loadState, "error")
        var retry = findChild(page, "albumRetry")
        tryCompare(retry, "visible", true, 1000)
        waitForRendering(page)      // the panel is laid out on the next polish
        mouseClick(retry)
        compare(bridge.albumRequests.length, 2)
        compare(findChild(page, "albumFailure").visible, false)
        bridge.albumRequests[1]({id: 77, title: "Back", artists: "A", artistList: []}, "", 200)
        bridge.trackRequests[1]([{id: 1, title: "T", artists: "A", durationStr: "1:00"}], "")
        compare(findChild(page, "albumFailure").visible, false)
        compare(page.tracks.length, 1)
    }

    function test_albumThatLoadsShowsNoPanel() {
        bridge.albumRequests[0]({id: 77, title: "Fine", artists: "A", artistList: []}, "", 200)
        bridge.trackRequests[0]([], "")
        compare(findChild(page, "albumFailure").visible, false)
    }
}
