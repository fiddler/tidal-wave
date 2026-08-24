import QtQuick
import TidalWave

Item {
    property bool loading: false
    visible: loading

    anchors.fill: parent
    z: 100

    Rectangle { anchors.fill: parent; color: Qt.rgba(0,0,0,0.4) }

    Rectangle {
        id: disc
        anchors.centerIn: parent
        width: 48; height: 48; radius: 24
        color: Theme.surfaceHigh

        RotationAnimator on rotation {
            from: 0; to: 360
            duration: 900
            loops: Animation.Infinite
            // Named rather than `parent`: inside an animation used as a
            // property value source `parent` is null, so the binding this
            // used to carry threw a TypeError, read as false, and the disc
            // never turned. `visible` on an Item is effective visibility, so
            // this covers both `loading` and an ancestor page being hidden.
            running: disc.visible && AppFocus.active
        }

        Rectangle {
            width: 4; height: 16; radius: 2
            anchors.top: parent.top
            anchors.topMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.accent
        }
    }
}
