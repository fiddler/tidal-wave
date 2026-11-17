import QtQuick
import QtTest
import TidalWave

// The panel reads rows from a model with `track` and `queueIndex` roles. While
// shuffled the row number and the queue index differ, and every action has to
// use the latter.
TestCase {
    id: testCase
    name: "Queue"
    width: 400
    height: 600
    visible: true
    when: windowShown

    ListModel { id: rows }
    QtObject {
        id: player
        property var queueModel: rows
        property int queueIndex: 2
        property int queueCount: rows.count
        property bool shuffle: true
        property var calls: []
        function jumpToQueue(i)     { calls.push("jump:" + i) }
        function removeFromQueue(i) { calls.push("remove:" + i) }
        function moveQueueItem(a, b) { calls.push("move:" + a + ">" + b) }
        function clearQueue()       { calls.push("clear") }
    }

    Component { id: panelComponent; QueuePanel {} }
    property var panel: null

    function init() {
        player.calls = []
        rows.clear()
        // Shuffled: play order 2, 0, 1.
        rows.append({track: {id: 30, title: "Third", artists: "C", coverUrl80: ""}, queueIndex: 2})
        rows.append({track: {id: 10, title: "First", artists: "A", coverUrl80: ""}, queueIndex: 0})
        rows.append({track: {id: 20, title: "Second", artists: "B", coverUrl80: ""}, queueIndex: 1})
        panel = createTemporaryObject(panelComponent, testCase, {width: 400, height: 600})
        waitForRendering(panel)
    }

    function listView() {
        function walk(item) {
            if (item.model === rows) return item
            for (var i = 0; i < item.children.length; i++) {
                var f = walk(item.children[i]); if (f) return f
            }
            return null
        }
        return walk(panel)
    }

    function test_rowsCarryTheirOwnQueueIndex() {
        var view = listView()
        verify(view !== null)
        compare(view.count, 3)
        compare(view.itemAtIndex(0).trackIndex, 2)
        compare(view.itemAtIndex(0).isCurrent, true)
        compare(view.itemAtIndex(1).trackIndex, 0)
        compare(view.itemAtIndex(1).track.title, "First")
        compare(view.itemAtIndex(1).isCurrent, false)
    }

    function test_activatingARowJumpsToTheRealIndex() {
        var view = listView()
        view.itemAtIndex(1).forceActiveFocus()
        keyClick(Qt.Key_Return)
        compare(player.calls, ["jump:0"])
    }

    function test_aMovedRowKeepsItsDelegate() {
        var view = listView()
        var second = view.itemAtIndex(2)
        second.objectName = "theMovedRow"
        rows.move(2, 0, 1)
        wait(350)      // itemAtIndex follows the move only once the transition settles
        compare(view.itemAtIndex(0).objectName, "theMovedRow", "a move must not rebuild the row")
        compare(view.count, 3)
    }
}
