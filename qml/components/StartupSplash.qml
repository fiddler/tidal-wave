import QtQuick
import QtQuick.Layouts
import TidalWave

// Covers the startup session check (Auth::State::Restoring). The login page
// used to show through that window, which read as "logged out again" on every
// launch even though the saved tokens were fine.
Rectangle {
    id: root
    color: Theme.bg

    // Only say something if the check is slow enough to notice. A label that
    // flashes for 200 ms is the same annoyance as the login page was.
    property bool slow: false
    Timer {
        interval: 800
        running: root.visible
        onTriggered: root.slow = true
    }

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            width: 64
            height: 64
            radius: 16
            color: Theme.accent
            Text {
                anchors.centerIn: parent
                text: "≋"
                color: "white"
                font.pixelSize: 32
                font.bold: true
            }
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "TIDAL WAVE"
            color: Theme.textPrimary
            font.pixelSize: 28
            font.bold: true
            font.letterSpacing: 3
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Restoring your session"
            color: Theme.textSec
            font.pixelSize: 14
            opacity: root.slow ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }
    }
}
