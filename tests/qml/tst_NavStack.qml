import QtQuick
import QtTest
import "../../qml/NavStack.js" as NavStack

// The back history. Before it was a stack, back was an ordinary forward step
// into a one-slot "previous page", so two detail pages bounced for ever.
TestCase {
    name: "NavStack"

    readonly property var detail: ["album", "artist", "playlist", "mix", "nowplaying", "radio", "localplaylist"]

    function at(page, params) { return { page: page, params: params || {} } }
    function go(history, from, to) { return NavStack.forward(history, from, to, detail, 32) }

    function test_detailPagePushesWhereItCameFrom() {
        var h = go([], at("home"), at("album", { albumId: 1 }))
        compare(h.length, 1)
        compare(h[0].page, "home")
    }

    function test_backThenBackDoesNotBounce() {
        // home -> album 1 -> artist 7. Two backs must end on home, which means
        // the history handed to goBack() is exactly [home, album 1].
        var h = go([], at("home"), at("album", { albumId: 1 }))
        h = go(h, at("album", { albumId: 1 }), at("artist", { artistId: 7 }))
        compare(h.length, 2)
        compare(h[1].page, "album")
        compare(h[1].params.albumId, 1)
        compare(h[0].page, "home")
    }

    function test_samePageDifferentParamsIsAStep() {
        var h = go([at("home")], at("album", { albumId: 1 }), at("album", { albumId: 2 }))
        compare(h.length, 2)
        compare(h[1].params.albumId, 1)
    }

    function test_samePageSameParamsIsAReload() {
        var h = go([at("home")], at("album", { albumId: 1 }), at("album", { albumId: 1 }))
        compare(h.length, 1)
    }

    function test_forwardOntoTheTopEntryUnwinds() {
        // album 1 -> nowplaying -> the title link back to album 1.
        var h = [at("home"), at("album", { albumId: 1 })]
        h = go(h, at("nowplaying"), at("album", { albumId: 1 }))
        compare(h.length, 1)
        compare(h[0].page, "home")
    }

    function test_matchDeeperInTheStackStillPushes() {
        var h = [at("album", { albumId: 1 }), at("artist", { artistId: 7 })]
        h = go(h, at("album", { albumId: 2 }), at("album", { albumId: 1 }))
        compare(h.length, 3)
    }

    function test_topLevelPageEmptiesTheStack() {
        var h = [at("home"), at("album", { albumId: 1 }), at("artist", { artistId: 7 })]
        compare(go(h, at("album", { albumId: 2 }), at("collection")).length, 0)
        compare(go(h, at("album", { albumId: 2 }), at("local")).length, 0)
    }

    function test_stackIsCapped() {
        var h = []
        for (var i = 0; i < 40; i++)
            h = NavStack.forward(h, at("album", { albumId: i }), at("album", { albumId: i + 1 }), detail, 32)
        compare(h.length, 32)
        compare(h[31].params.albumId, 39)
    }
}
