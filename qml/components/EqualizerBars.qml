import QtQuick
import TidalWave

// Tiny "now playing" equalizer: three bars dancing at slightly different
// tempos so their phases drift. When `running` turns false the bars freeze
// where they are — paused mid-song, which is exactly what happened.
Item {
    id: root
    property bool  running: true
    property color barColor: Theme.accent

    implicitWidth: 12
    implicitHeight: 12

    Repeater {
        model: 3
        Rectangle {
            required property int index
            x: index * 4.5
            width: 3
            radius: 1.5
            color: root.barColor
            anchors.bottom: parent.bottom
            height: 4 + index * 2

            SequentialAnimation on height {
                running: root.running
                loops: Animation.Infinite
                NumberAnimation {
                    to: 11 - index * 1.5
                    duration: 240 + index * 90
                    easing.type: Easing.InOutQuad
                }
                NumberAnimation {
                    to: 3 + index
                    duration: 280 + index * 70
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }
}
