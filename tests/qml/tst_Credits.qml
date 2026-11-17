import QtQuick
import QtQuick.Controls
import QtTest
import TidalWave

TestCase {
    id: testCase
    name: "Credits"
    width: 800
    height: 700
    visible: true
    when: windowShown

    QtObject {
        id: bridge
        property var requests: []
        function fetchTrackCredits(id, callback) { requests.push({id: id, callback: callback}) }
    }

    Component { id: dialogComponent; CreditsDialog {} }
    property var dialog: null

    function init() {
        bridge.requests = []
        dialog = createTemporaryObject(dialogComponent, testCase)
        verify(dialog !== null)
    }
    function cleanup() { if (dialog) dialog.close() }

    function test_creditsArrive() {
        dialog.openFor({id: 5, title: "Song", artists: "A"})
        compare(dialog.phase, "loading")
        compare(bridge.requests[0].id, 5)
        bridge.requests[0].callback({groups: [{type: "Producer", contributors: [{id: 1, name: "N"}]}],
                                     copyright: "(c) Label", releaseDate: "2017-09-22", isrc: "X", upc: ""}, "")
        compare(dialog.phase, "ready")
        compare(dialog.footerLines(), ["Released 2017-09-22", "(c) Label", "ISRC X"])
    }

    function test_nothingListedIsSaidRatherThanShownBlank() {
        dialog.openFor({id: 5, title: "Song", artists: "A"})
        bridge.requests[0].callback({groups: [], copyright: "", releaseDate: "", isrc: "", upc: ""}, "")
        compare(dialog.phase, "empty")
    }

    function test_failureIsSaid() {
        dialog.openFor({id: 5, title: "Song", artists: "A"})
        bridge.requests[0].callback({}, "Network unavailable")
        compare(dialog.phase, "error")
    }

    function test_lateReplyForAnotherTrackIsDropped() {
        dialog.openFor({id: 5, title: "First", artists: "A"})
        dialog.openFor({id: 6, title: "Second", artists: "B"})
        bridge.requests[0].callback({groups: [{type: "Producer", contributors: [{id: 1, name: "N"}]}]}, "")
        compare(dialog.phase, "loading")
        compare(dialog.track.title, "Second")
    }

    function test_localTrackHasNoCredits() {
        dialog.openFor({id: -4, title: "File", artists: "A", localPath: "/x.flac"})
        compare(bridge.requests.length, 0)
        compare(dialog.opened, false)
    }
}
