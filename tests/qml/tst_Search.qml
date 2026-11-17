import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "Search"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var requests: []
        property var recent: []
        property var added: []
        signal favoriteTracksChanged()
        signal recentSearchesChanged()
        function isTrackFavorite(id) { return false }
        function search(q, callback, limit, offset, types) {
            requests.push({q: q, callback: callback, limit: limit, offset: offset, types: types})
        }
        function recentSearches() { return recent }
        function addRecentSearch(q) { added.push(q) }
        function removeRecentSearch(q) {}
        function clearRecentSearches() {}
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

    Component { id: pageComponent; SearchPage {} }
    property var page: null

    function tracks(from, n) {
        var out = []
        for (var i = 0; i < n; i++)
            out.push({id: from + i, title: "T" + (from + i), artists: "A", albumTitle: "Al",
                      durationStr: "1:00", coverUrl80: "", coverUrl: ""})
        return out
    }
    function reply(list, total, extra) {
        var r = {tracks: list, tracksTotal: total, albums: [], albumsTotal: 0, artists: [],
                 artistsTotal: 0, playlists: [], playlistsTotal: 0, mixes: [], mixesTotal: 0}
        for (var k in (extra || {})) r[k] = extra[k]
        return r
    }
    function status() { return findChild(page, "searchStatusText").text }

    // Types into the field and waits for the debounced request.
    function type(q) {
        var n = bridge.requests.length
        page.pendingQuery = q
        tryCompare(bridge.requests, "length", n + 1, 2000)
        return bridge.requests[n]
    }

    function init() {
        bridge.requests = []
        bridge.recent = []
        bridge.added = []
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        verify(page !== null)
    }

    function test_saysNothingBeforeTheReply() {
        page.pendingQuery = "ab"
        compare(page.query, "ab")
        compare(status(), "", "empty arrays before a reply are not an answer")
        tryCompare(bridge.requests, "length", 1, 2000)
        compare(status(), "")
        bridge.requests[0].callback(reply([], 0), "")
        compare(status(), "No results for \"ab\"")
    }

    function test_failedSearchClearsStaleResultsAndSaysSo() {
        type("abc").callback(reply(tracks(1, 3), 3), "")
        compare(page.tracks.length, 3)
        type("abcd").callback({}, "Network unavailable")
        compare(page.tracks.length, 0, "the old query's results must not stand under the new text")
        verify(status().indexOf("Search failed") === 0)
        compare(page.loading, false)
    }

    function test_typeTabWithoutHitsNamesTheKind() {
        type("abc").callback(reply(tracks(1, 3), 3), "")
        compare(status(), "")
        page.activeTab = 2
        compare(status(), "No albums for \"abc\"")
        page.activeTab = 1
        compare(status(), "")
    }

    function test_resultsAreRememberedAndMissesAreNot() {
        type("abc").callback(reply(tracks(1, 3), 3), "")
        compare(bridge.added, ["abc"])
        type("zzzz").callback(reply([], 0), "")
        compare(bridge.added, ["abc"])
    }

    function test_pagingAppendsDedupesAndStops() {
        type("abc").callback(reply(tracks(1, 20), 44), "")
        compare(bridge.requests[0].offset, 0)
        compare(bridge.requests[0].types, "")
        page.activeTab = 1
        // 20 rows do not fill past the paging margin, so the next page is asked for.
        tryCompare(bridge.requests, "length", 2, 2000)
        compare(bridge.requests[1].offset, 20)
        compare(bridge.requests[1].types, "TRACKS")
        compare(bridge.requests[1].q, "abc")

        // One row the page already has: it must not appear twice.
        var second = tracks(20, 20)
        bridge.requests[1].callback({tracks: second, tracksTotal: 44}, "")
        compare(page.tracks.length, 39)
        compare(findChild(page, "searchTracksView").count, 39)

        // A full page is 20; five is the end.
        findChild(page, "searchTracksView").positionViewAtEnd()
        tryCompare(bridge.requests, "length", 3, 2000)
        compare(bridge.requests[2].offset, 39)
        bridge.requests[2].callback({tracks: tracks(40, 5), tracksTotal: 44}, "")
        compare(page.tracks.length, 44)
        findChild(page, "searchTracksView").positionViewAtEnd()
        wait(300)
        compare(bridge.requests.length, 3, "an exhausted kind is not asked again")
    }

    // Same row count leaves the row model untouched, and the view kept its
    // offset: the new query opened seven rows down.
    function test_newQueryStartsAtTheTop() {
        type("abc").callback(reply(tracks(1, 40), 40), "")
        page.activeTab = 1
        var view = findChild(page, "searchTracksView")
        waitForRendering(page)
        view.positionViewAtEnd()
        tryVerify(function() { return view.contentY > view.originY }, 1000)
        type("xyz").callback(reply(tracks(100, 40), 40), "")
        compare(view.contentY, view.originY)
    }

    function test_pageOfAnOlderQueryIsDropped() {
        type("abc").callback(reply(tracks(1, 20), 44), "")
        page.activeTab = 1
        tryCompare(bridge.requests, "length", 2, 2000)
        var late = bridge.requests[1]
        type("xyz").callback(reply(tracks(100, 2), 2), "")
        late.callback({tracks: tracks(20, 20), tracksTotal: 44}, "")
        compare(page.tracks.length, 2)
        compare(page.tracks[0].id, 100)
    }

    function test_failedPageKeepsRowsAndStopsAsking() {
        type("abc").callback(reply(tracks(1, 20), 44), "")
        page.activeTab = 1
        tryCompare(bridge.requests, "length", 2, 2000)
        bridge.requests[1].callback({}, "Network unavailable")
        compare(page.tracks.length, 20)
        compare(status(), "")
        wait(300)
        compare(bridge.requests.length, 2)
    }

    function test_mixesArriveInTheirOwnTab() {
        type("abc").callback(reply([], 0, {mixes: [{id: "m1", title: "Mix", subtitle: "", coverUrl: ""}],
                                           mixesTotal: 1}), "")
        compare(page.mixes.length, 1)
        compare(status(), "")
        page.activeTab = 5
        compare(status(), "")
        page.activeTab = 1
        compare(status(), "No tracks for \"abc\"")
    }

    function test_recentSearchFillsTheField() {
        page.destroy()
        bridge.recent = ["radiohead", "bjork"]
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        var row = findChild(page, "recentSearchRow")
        verify(row !== null)
        mouseClick(row, 40, 20)
        compare(page.query, "radiohead")
    }
}
