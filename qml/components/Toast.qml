import QtQuick
import TidalWave

// A short message that fades in over the page and leaves on its own.
//
// One of these lives in the window, because the events worth announcing —
// an import result, a library error, a drop that cannot be honoured — do not
// belong to whichever page happens to be open. A per-page copy meant an
// import started from the local library reported nothing once you navigated
// away, which read as the drop having done nothing at all.
Rectangle {
    id: root

    visible: opacity > 0
    opacity: 0
    radius: 8
    color: Theme.surfaceHigh
    border.color: root.warning ? Theme.red : Theme.border
    width: label.implicitWidth + 32
    height: 40

    property string message: ""
    property bool   warning: false

    function show(m)     { message = m; warning = false; opacity = 1; life.restart() }
    function warn(m)     { message = m; warning = true;  opacity = 1; life.restart() }

    Text {
        id: label
        anchors.centerIn: parent
        text: root.message
        color: Theme.textPrimary
        font.pixelSize: 13
    }

    Timer { id: life; interval: 4000; onTriggered: root.opacity = 0 }
    Behavior on opacity { NumberAnimation { duration: 200 } }
}
