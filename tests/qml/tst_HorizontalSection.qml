import QtQuick
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "HorizontalSection"
    width: 600
    height: 400
    visible: true
    when: windowShown

    Flickable {
        id: page
        anchors.fill: parent
        contentHeight: 2000
        HorizontalSection {
            id: section
            y: 40
            width: page.width
            items: {
                var out = []
                for (var i = 0; i < 12; i++) out.push({id: i + 1, title: "Card " + i, subtitle: "", coverUrl: ""})
                return out
            }
        }
    }

    function init() { page.contentY = 0 }

    // The row must not scroll the page itself: it leaves a vertical wheel to
    // the page's Flickable, which then moves under one set of rules wherever
    // the pointer is.
    function test_verticalWheelOverRowScrollsPage() {
        waitForRendering(page)
        mouseWheel(section, section.width / 2, section.height - 80, 0, -120)
        tryVerify(function () { return page.contentY > 0 })
    }

    function test_sidewaysWheelMovesRowNotPage() {
        waitForRendering(page)
        mouseWheel(section, section.width / 2, section.height - 80, -120, 0)
        wait(50)
        compare(page.contentY, 0)
    }
}
