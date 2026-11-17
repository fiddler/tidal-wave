import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "NowPlaying"
    width: 1200
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var lyricRequests: []
        signal favoriteTracksChanged()
        function isTrackFavorite(id) { return false }
        function addTrackFavorite(id, cb) {}
        function removeTrackFavorite(id, cb) {}
        function fetchLyrics(id, callback) { lyricRequests.push(callback) }
    }
    QtObject {
        id: player
        property var currentTrack: ({id: 5, title: "Song", artists: "One, Two", artistId: 1,
                                     artistList: [{id: 1, name: "One"}, {id: 2, name: "Two"}],
                                     albumTitle: "Album", albumId: 9, coverUrl: "", duration: 200})
        property bool playing: false
        property bool loading: false
        property bool muted: false
        property bool shuffle: false
        property int repeatMode: 0
        property double volume: 1.0
        property double position: 0
        property double duration: 200000
        property string audioQuality: "LOSSLESS"
        property string sourceType: ""
        property string sourceId: ""
        property string sourceName: ""
        property var queueTracks: []
        property int queueIndex: 0
        function qualityLabel(code) { return code }
        function upcomingTracks(max) { return [] }
        function seek(ms) {}
        function playPause() {}
        function next() {}
        function previous() {}
        function setMuted(m) {}
        function setVolume(v) {}
        function setShuffle(s) {}
        function setRepeatMode(m) {}
    }
    QtObject {
        id: downloader
        signal downloadStarted(double trackId)
        signal downloadFinished(double trackId, string path)
        signal downloadError(double trackId, string message)
        function isDownloading(id) { return false }
        function downloadTrack(t) {}
    }
    QtObject {
        id: app
        function isDownloaded(id) { return false }
    }

    Component { id: pageComponent; NowPlayingPage {} }
    property var page: null

    function init() {
        bridge.lyricRequests = []
        page = createTemporaryObject(pageComponent, testCase, {width: 1200, height: 800})
        verify(page !== null)
        waitForRendering(page)
    }
    function lyrics() {
        bridge.lyricRequests[bridge.lyricRequests.length - 1](
            {text: "[00:01.00] first line\n[00:05.00] second line", timed: true}, "")
    }

    function test_severalArtistsAreSeparateLinks() {
        compare(page.multiArtist, true)
        player.currentTrack = {id: 6, title: "Solo", artists: "One", artistId: 1,
                               artistList: [{id: 1, name: "One"}], albumTitle: "A", albumId: 9,
                               coverUrl: "", duration: 100}
        compare(page.multiArtist, false)
        player.currentTrack = {id: 5, title: "Song", artists: "One, Two", artistId: 1,
                               artistList: [{id: 1, name: "One"}, {id: 2, name: "Two"}],
                               albumTitle: "Album", albumId: 9, coverUrl: "", duration: 200}
    }

    function test_readingViewNeedsLyricsOnScreen() {
        var toggle = findChild(page, "readingViewToggle")
        verify(toggle !== null)
        compare(toggle.visible, false, "no reading view without lyrics showing")
        lyrics()
        compare(page.lyricsState, "ready")
        page.showLyrics = true
        compare(toggle.visible, true)
        page.readingView = true
        compare(findChild(page, "creditsLink").visible, false, "the title block gives way to the lyrics")
        page.showLyrics = false
        compare(page.readingView, false, "closing the lyrics closes the reading view with it")
        compare(findChild(page, "creditsLink").visible, true)
    }

    function test_trackWithoutLyricsUnwindsTheReadingView() {
        lyrics()
        page.showLyrics = true
        page.readingView = true
        // The next track has none.
        player.currentTrack = {id: 7, title: "Instrumental", artists: "One", artistId: 1,
                               artistList: [{id: 1, name: "One"}], albumTitle: "A", albumId: 9,
                               coverUrl: "", duration: 100}
        tryCompare(bridge.lyricRequests, "length", 2, 1000)
        bridge.lyricRequests[1]({text: "", timed: false}, "")
        compare(page.lyricsState, "unavailable")
        compare(page.showLyrics, false)
        compare(page.readingView, false)
    }
}
