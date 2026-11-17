import QtQuick
import QtTest
import TidalWave

// Removing the selection from a playlist, and coming back to where the list
// was scrolled. Backspace reaches the page through the window's shortcut,
// which calls removeSelection(); the row menu calls requestRemove().
TestCase {
    id: testCase
    name: "PlaylistRemove"
    width: 1000
    height: 800
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var requests: []
        property var removals: []
        signal favoriteTracksChanged()
        function isTrackFavorite(id) { return false }
        function fetchPlaylistTracks(uuid, callback) { requests.push({uuid: uuid, callback: callback}) }
        function removeTrackFromPlaylist(uuid, index, callback) {
            removals.push({uuid: uuid, index: index, callback: callback})
        }
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

    function tracks(n) {
        var out = []
        for (var i = 1; i <= n; i++)
            out.push({id: i, title: "Track " + i, artists: "Artist", albumTitle: "Album",
                      durationStr: "1:00", coverUrl80: "", coverUrl: ""})
        return out
    }
    function ids() { return page.tracks.map(function(t) { return t.id }) }

    function init() {
        bridge.requests = []
        bridge.removals = []
        SyncState.end()
        SyncState.dismiss()
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        verify(page !== null)
    }
    function load(n, type) {
        page.playlistType = type === undefined ? "USER" : type
        page.playlistUuid = "p"
        bridge.requests[0].callback(tracks(n), "")
    }

    function test_oneSelectedTrackGoesWithoutAsking() {
        load(5)
        page.pageSelection.selectOnly(2)
        verify(page.canRemoveSelection)
        page.removeSelection()
        compare(findChild(page, "removeTracksDialog").opened, false)
        compare(ids(), [1, 2, 4, 5])
        compare(bridge.removals.length, 1)
        compare(bridge.removals[0].index, 2)
        compare(page.pageSelection.count, 0)
    }

    function test_severalAskFirstAndCancelKeepsThem() {
        load(5)
        page.pageSelection.selectOnly(1)
        page.pageSelection.toggle(3)
        page.removeSelection()
        var dialog = findChild(page, "removeTracksDialog")
        compare(dialog.opened, true)
        compare(findChild(dialog.contentItem, "removeTracksQuestion").text,
                "Remove 2 songs from playlist?")
        compare(bridge.removals.length, 0)
        dialog.close()
        compare(ids(), [1, 2, 3, 4, 5])
        compare(page.pageSelection.count, 2)
    }

    function test_confirmedRemovalRunsHighestIndexFirst() {
        load(5)
        page.pageSelection.selectOnly(1)
        page.pageSelection.toggle(3)
        page.removeSelection()
        findChild(page, "removeTracksDialog").confirm()
        compare(ids(), [1, 3, 5])
        // One request at a time, each waiting for the one before.
        compare(bridge.removals.length, 1)
        compare(bridge.removals[0].index, 3)
        compare(SyncState.busy, true)
        bridge.removals[0].callback(true)
        compare(bridge.removals.length, 2)
        compare(bridge.removals[1].index, 1)
        bridge.removals[1].callback(true)
        compare(SyncState.busy, false)
        compare(SyncState.failed, false)
    }

    function test_menuOnASelectedRowTakesTheWholeSelection() {
        load(5)
        page.pageSelection.selectOnly(0)
        page.pageSelection.toggle(4)
        page.requestRemove(page.selectedIndices())
        compare(findChild(page, "removeTracksDialog").indices, [0, 4])
    }

    function test_refusalSaysSoAndTakesTheServersList() {
        load(5)
        page.pageSelection.selectOnly(2)
        page.removeSelection()
        bridge.removals[0].callback(false)
        compare(SyncState.failed, true)
        compare(bridge.requests.length, 2)
        bridge.requests[1].callback(tracks(5), "")
        compare(page.tracks.length, 5)
    }

    function test_readOnlyPlaylistRemovesNothing() {
        load(5, "EDITORIAL")
        page.pageSelection.selectOnly(2)
        compare(page.canRemoveSelection, false)
        page.removeSelection()
        compare(page.tracks.length, 5)
        compare(bridge.removals.length, 0)
    }

    // The window rebuilds the page on the way back and hands it the offset
    // the old page reported.
    function test_scrollOffsetComesBackAfterTheLoad() {
        load(100)
        var list = findChild(page, "playlistTracks")
        list.contentY = list.originY + 1500
        var offset = page.scrollOffset()
        compare(offset, 1500)

        bridge.requests = []
        page.destroy()
        page = createTemporaryObject(pageComponent, testCase, {width: 1000, height: 800})
        page.pendingScroll = offset
        load(100)
        compare(page.scrollOffset(), 1500)
        compare(page.pendingScroll, null)
    }
}
