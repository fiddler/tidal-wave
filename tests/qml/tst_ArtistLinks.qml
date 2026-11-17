import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "ArtistLinks"
    width: 600
    height: 200
    visible: true
    when: windowShown

    Component {
        id: linksComponent
        ArtistLinks {
            navigateOnClick: false
            artists: [{id: 11, name: "First Artist"}, {id: 22, name: "Second"}, {id: 0, name: "Session Player"}]
            fallbackText: "First Artist, Second, Session Player"
            pixelSize: 14
            height: 20
        }
    }
    SignalSpy { id: clicked; signalName: "artistClicked" }
    property var links: null

    function make(width) {
        links = createTemporaryObject(linksComponent, testCase, {width: width})
        clicked.clear()
        clicked.target = links
        waitForRendering(links)
    }

    // Hands back the name Text for one artist, found by its text.
    function nameItem(text) {
        var found = null
        function walk(item) {
            if (found) return
            if (item.text === text && item.visible) { found = item; return }
            for (var i = 0; i < item.children.length; i++) walk(item.children[i])
        }
        walk(links)
        return found
    }

    function test_eachNameIsItsOwnLink() {
        make(500)
        compare(links.useLinks, true)
        var second = nameItem("Second")
        verify(second !== null)
        mouseClick(second)
        compare(clicked.count, 1)
        compare(clicked.signalArguments[0][0], 22)
        mouseClick(nameItem("First Artist"))
        compare(clicked.signalArguments[1][0], 11)
    }

    function test_nameWithoutAnArtistPageIsNotALink() {
        make(500)
        mouseClick(nameItem("Session Player"))
        compare(clicked.count, 0)
    }

    function test_tooNarrowFallsBackToOneLinkToTheFirstArtist() {
        make(90)
        compare(links.useLinks, false)
        mouseClick(links, 10, 10)
        compare(clicked.count, 1)
        compare(clicked.signalArguments[0][0], 11)
    }

    function test_disabledLinksAreInert() {
        make(500)
        links.linksEnabled = false
        waitForRendering(links)
        compare(links.useLinks, false)
        mouseClick(links, 10, 10)
        compare(clicked.count, 0)
    }

    function test_noListShowsThePlainString() {
        make(500)
        links.artists = []
        waitForRendering(links)
        compare(links.useLinks, false)
        mouseClick(links, 10, 10)
        compare(clicked.count, 0)
    }

    // Album header, player bar and now-playing links were Tab stops before
    // the names were split.
    function test_keyboardReachesEachLinkWhenAsked() {
        make(500)
        links.keyboardFocus = true
        var second = nameItem("Second")
        compare(second.activeFocusOnTab, true)
        compare(nameItem("Session Player").activeFocusOnTab, false)
        second.forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(clicked.count, 1)
        compare(clicked.signalArguments[0][0], 22)
        keyClick(Qt.Key_Space)
        compare(clicked.count, 2)
    }

    function test_trackRowLinksAreNotTabStops() {
        make(500)
        compare(nameItem("Second").activeFocusOnTab, false)
    }
}
