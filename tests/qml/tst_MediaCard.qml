import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "MediaCard"
    width: 400
    height: 400
    visible: true
    when: windowShown

    MediaCard {
        id: card
        x: 20; y: 20
        cardSize: 160
        title: "Album"
        mediaType: "album"
        mediaId: 7
    }

    SignalSpy { id: played; target: card; signalName: "playClicked" }
    SignalSpy { id: opened; target: card; signalName: "clicked" }

    function init() { played.clear(); opened.clear() }

    // The play button sits inside the cover, which opens the card on a click.
    function test_playButtonPlaysWithoutOpening() {
        var at = card.cardSize - 34          // centre of the 44px button, 12px in
        mouseMove(card, at, at)              // the button shows on hover only
        waitForRendering(card)
        mouseClick(card, at, at)
        compare(played.count, 1)
        compare(opened.count, 0)
    }

    function test_coverOpens() {
        mouseClick(card, 40, 40)
        compare(opened.count, 1)
        compare(played.count, 0)
    }
}
