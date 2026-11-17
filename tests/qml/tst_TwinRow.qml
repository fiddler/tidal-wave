import QtQuick
import QtTest
import TidalWave

// A row Tidal no longer streams is played from another release of the same
// recording, under another id. The row has to say so. After upstream 3f9f4db.
TestCase {
    id: testCase
    name: "TwinRow"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var requests: []
        signal favoriteTracksChanged()
        function isTrackFavorite(id) { return false }
        function fetchPlaylistTracks(uuid, callback) { requests.push({uuid: uuid, callback: callback}) }
    }
    QtObject {
        id: offline
        signal playlistChanged(string uuid)
        function status(uuid) { return {state: "none"} }
        function cachedTracks(uuid) { return [] }
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
    property var list: null

    function track(id, extra) {
        var t = {id: id, title: "Track " + id, artists: "Artist", albumTitle: "Album",
                 durationStr: "1:00", coverUrl80: "", coverUrl: ""}
        for (var k in extra) t[k] = extra[k]
        return t
    }
    function init() {
        bridge.requests = []
        player.currentTrack = {id: 0}
        player.playing = false
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        page.playlistUuid = "p"
        bridge.requests[0].callback([
            track(1),
            track(2, {available: false, isrc: "USABC1234567"}),
            track(3, {available: false})], "")
        list = findChild(page, "playlistTracks")
        waitForRendering(page)
    }

    function test_deadRowWithAnIsrcCanBePlayed() {
        compare(list.itemAtIndex(1).isDead, true)
        compare(list.itemAtIndex(1).isUnavailable, false)
        compare(list.itemAtIndex(2).isUnavailable, true)
    }

    function test_rowWhoseTwinPlaysIsShownAsPlaying() {
        player.currentTrack = {id: 99, standsForId: 2}
        player.playing = true
        compare(list.itemAtIndex(1).isCurrent, true)
        compare(list.itemAtIndex(1).playingNow, true)
    }

    function test_noOtherRowIsMarked() {
        player.currentTrack = {id: 99, standsForId: 2}
        player.playing = true
        compare(list.itemAtIndex(0).isCurrent, false)
        compare(list.itemAtIndex(0).playingNow, false)
        compare(list.itemAtIndex(2).playingNow, false)
    }
}
