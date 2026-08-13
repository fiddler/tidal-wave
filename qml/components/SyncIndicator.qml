import QtQuick
import TidalWave

// Corner status for background playlist writes: a quiet pill while syncing,
// which grows into a dismissible warning if the write fails.
Item {
    id: root
    visible: SyncState.busy || SyncState.failed
    implicitWidth: card.width
    implicitHeight: card.height

    Rectangle {
        id: card
        width: SyncState.failed ? Math.min(360, errorCol.implicitWidth + 56)
                                : busyRow.implicitWidth + 28
        height: SyncState.failed ? errorCol.implicitHeight + 28 : 40
        radius: SyncState.failed ? 10 : 20
        color: SyncState.failed ? Qt.rgba(0.15, 0.05, 0.05, 0.97) : Theme.surfaceHigh
        border.color: SyncState.failed ? Theme.red : Theme.border
        border.width: 1

        Behavior on width  { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        Behavior on height { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }

        // ── syncing ──
        Row {
            id: busyRow
            visible: !SyncState.failed
            anchors.centerIn: parent
            spacing: 10

            Item {
                id: spinner
                width: 14; height: 14
                anchors.verticalCenter: parent.verticalCenter
                Rectangle {
                    width: 3; height: 6; radius: 1.5
                    anchors.top: parent.top
                    anchors.horizontalCenter: parent.horizontalCenter
                    color: Theme.accent
                }
                RotationAnimator {
                    target: spinner
                    from: 0; to: 360
                    duration: 800
                    loops: Animation.Infinite
                    running: SyncState.busy
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: SyncState.message
                color: Theme.textSec
                font.pixelSize: 13
            }
        }

        // ── failed ──
        Column {
            id: errorCol
            visible: SyncState.failed
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 6

            Row {
                spacing: 8
                Text {
                    text: "⚠"
                    color: Theme.red
                    font.pixelSize: 14
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: "Playlist not saved"
                    color: Theme.red
                    font.pixelSize: 13
                    font.bold: true
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Text {
                width: parent.width
                text: SyncState.errorText
                color: Theme.textSec
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
            Text {
                text: "Dismiss"
                color: dismissHov.hovered ? Theme.textPrimary : Theme.textDim
                font.pixelSize: 12
                HoverHandler { id: dismissHov; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: SyncState.dismiss() }
            }
        }
    }

    // A failure that nobody dismisses should not sit there for ever.
    Timer {
        interval: 12000
        running: SyncState.failed
        onTriggered: SyncState.dismiss()
    }
}
