import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "Mix"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var pageRequests: []
        property var saved: ({})
        property var calls: []
        signal favoriteTracksChanged()
        signal favoriteMixesChanged()
        function isTrackFavorite(id) { return false }
        function isMixFavorite(id) { return saved[id] === true }
        function fetchMixPage(id, callback) { pageRequests.push({id: id, callback: callback}) }
        // The real bridge writes its cache only on a yes, then signals.
        function addMixFavorite(id, callback) {
            calls.push("add:" + id); saved[id] = true; favoriteMixesChanged(); callback(true)
        }
        function removeMixFavorite(id, callback) {
            calls.push("remove:" + id); saved[id] = false; favoriteMixesChanged(); callback(true)
        }
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

    Component { id: pageComponent; MixPage {} }
    property var page: null

    function track(id) {
        return {id: id, title: "T" + id, artists: "A", albumTitle: "Al", durationStr: "1:00",
                coverUrl80: "", coverUrl: ""}
    }
    function init() {
        bridge.pageRequests = []
        bridge.saved = {}
        bridge.calls = []
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        verify(page !== null)
    }

    // "Playing from" opens a mix by id alone. The page has to label itself.
    function test_mixOpenedByIdFillsItsOwnHeader() {
        page.mixId = "m1"
        compare(page.title, "")
        bridge.pageRequests[0].callback({title: "My Mix 3", subtitle: "Radiohead and more",
                                         coverUrl: "", isRadio: false},
                                        [track(1), track(2)], "")
        compare(page.title, "My Mix 3")
        compare(page.subtitle, "Radiohead and more")
        compare(page.tracks.length, 2)
        compare(page.isRadio, false)
        compare(page.loading, false)
    }

    function test_trackRadioIsLabelledAsOne() {
        page.title = "Guessed Radio"
        page.mixId = "r1"
        bridge.pageRequests[0].callback({title: "Song Radio", subtitle: "", coverUrl: "", isRadio: true},
                                        [track(1)], "")
        compare(page.isRadio, true)
        compare(page.title, "Song Radio", "the server's title wins over the caller's guess")
    }

    function test_headerlessReplyKeepsWhatTheCallerPassed() {
        page.title = "From the card"
        page.mixId = "m2"
        bridge.pageRequests[0].callback({title: "", subtitle: "", coverUrl: "", isRadio: false}, [track(1)], "")
        compare(page.title, "From the card")
    }

    function test_staleReplyCannotFillANewerMix() {
        page.mixId = "first"
        page.mixId = "second"
        bridge.pageRequests[0].callback({title: "First"}, [track(1)], "")
        compare(page.tracks.length, 0)
        compare(page.title, "")
    }

    function test_failureIsSaid() {
        page.mixId = "m1"
        bridge.pageRequests[0].callback({}, [], "Network unavailable")
        compare(page.loading, false)
        verify(page.loadError.length > 0)
    }

    function test_saveAndUnsaveFollowTheBridge() {
        page.mixId = "m1"
        bridge.pageRequests[0].callback({title: "Mix"}, [track(1)], "")
        compare(page.isSaved, false)
        var save = findChild(page, "mixSave")
        verify(save !== null)
        waitForRendering(page)
        mouseClick(save)
        compare(bridge.calls, ["add:m1"])
        compare(page.isSaved, true)
        mouseClick(save)
        compare(bridge.calls, ["add:m1", "remove:m1"])
        compare(page.isSaved, false)
    }

    function test_savedStateIsReadOnOpen() {
        bridge.saved = {"m9": true}
        page.mixId = "m9"
        compare(page.isSaved, true)
    }
}
