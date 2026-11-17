import QtQuick
import QtTest
import TidalWave

// Artist and radio pages drew nothing when their load failed, which a dead
// network and a broken app share.
TestCase {
    id: testCase
    name: "EmptyStates"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var detail: []
        property var top: []
        property var albums: []
        property var radio: []
        signal favoriteTracksChanged()
        signal favoriteArtistsChanged()
        function isTrackFavorite(id) { return false }
        function isArtistFavorite(id) { return false }
        function fetchArtistDetail(id, cb)    { detail.push(cb) }
        function fetchArtistTopTracks(id, cb) { top.push(cb) }
        function fetchArtistAlbums(id, cb)    { albums.push(cb) }
        function fetchTrackRadio(id, cb)      { radio.push(cb) }
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

    Component { id: artistComponent; ArtistPage {} }
    Component { id: radioComponent; RadioPage {} }

    function init() {
        bridge.detail = []; bridge.top = []; bridge.albums = []; bridge.radio = []
    }

    function test_artistThatWillNotLoadSaysSoAndRetries() {
        var page = createTemporaryObject(artistComponent, testCase, {width: 1000, height: 800})
        page.artistId = 7
        var panel = findChild(page, "artistFailure")
        compare(panel.visible, false, "still loading is not a failure")
        bridge.detail[0]({}, "Network unavailable")
        bridge.top[0]([], "Network unavailable")
        bridge.albums[0]([], "Network unavailable")
        compare(panel.visible, true)
        tryCompare(findChild(page, "artistRetry"), "visible", true, 1000)
        waitForRendering(page)
        mouseClick(findChild(page, "artistRetry"))
        compare(bridge.detail.length, 2)
        compare(panel.visible, false)
        bridge.detail[1]({id: 7, name: "Back"}, "")
        bridge.top[1]([], "")
        bridge.albums[1]([], "")
        compare(panel.visible, false)
    }

    function test_artistWithPartialDataShowsWhatArrived() {
        var page = createTemporaryObject(artistComponent, testCase, {width: 1000, height: 800})
        page.artistId = 7
        bridge.detail[0]({}, "Timeout")
        bridge.top[0]([], "")
        bridge.albums[0]([{id: 1, title: "A", type: "ALBUM", coverUrl: "", year: "2020"}], "")
        compare(findChild(page, "artistFailure").visible, false)
    }

    function test_radioFailureIsNotAnEmptyStation() {
        var page = createTemporaryObject(radioComponent, testCase, {width: 1000, height: 800})
        page.trackId = 5
        var panel = findChild(page, "radioFailure")
        compare(panel.visible, false)
        bridge.radio[0]([], "Network unavailable")
        compare(page.loadState, "failed")
        compare(panel.visible, true)
        tryCompare(findChild(page, "radioRetry"), "visible", true, 1000)
        waitForRendering(page)
        mouseClick(findChild(page, "radioRetry"))
        compare(bridge.radio.length, 2)
        bridge.radio[1]([], "")
        compare(page.loadState, "empty")
        compare(findChild(page, "radioRetry").visible, false, "nothing to retry: Tidal answered")
    }
}
