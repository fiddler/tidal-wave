import QtQuick
import QtQuick.Controls
import TidalWave

// Asked before more than one track leaves a playlist. A single track goes
// without a question: it is one click to put back.
Popup {
    id: dialog
    anchors.centerIn: Overlay.overlay
    width: 360
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 20
    background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

    // Row indices as they were when the question was asked.
    property var indices: []
    signal confirmed(var indices)

    function ask(rows) {
        indices = rows
        open()
    }
    function confirm() {
        var rows = indices
        close()
        confirmed(rows)
    }

    Column {
        width: parent.width
        spacing: 14

        // Takes the keys while the dialog is up, so Return confirms.
        Item {
            focus: true
            Keys.onReturnPressed: dialog.confirm()
            Keys.onEnterPressed:  dialog.confirm()
        }

        Text {
            objectName: "removeTracksQuestion"
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Remove " + dialog.indices.length + " songs from playlist?"
            color: Theme.textPrimary
            font.pixelSize: 16
            font.bold: true
        }

        Row {
            spacing: 10; anchors.right: parent.right
            PillButton {
                text: "Cancel"; accent: false
                onClicked: dialog.close()
            }
            PillButton {
                objectName: "removeTracksConfirm"
                text: "Remove"; accent: true
                onClicked: dialog.confirm()
            }
        }
    }
}
