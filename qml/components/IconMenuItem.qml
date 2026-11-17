import QtQuick
import QtQuick.Controls
import TidalWave

// A context-menu row with a drawn icon. The menus used to lead each label with
// an emoji, which came out in a different size, weight and colour per glyph
// and ignored the disabled and destructive tints the text beside it took.
MenuItem {
    id: root

    property string iconName: ""
    property color  textColor: Theme.textPrimary
    // False for a row that only states something and cannot be chosen.
    property bool   hoverable: true

    readonly property color tint: root.enabled ? root.textColor : Theme.textDim

    contentItem: Row {
        leftPadding: 12
        spacing: 10
        VectorIcon {
            anchors.verticalCenter: parent.verticalCenter
            name: root.iconName
            color: root.tint
            width: 14; height: 14
            strokeWidth: 1.8
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            color: root.tint
            font.pixelSize: 13
        }
    }
    background: Rectangle {
        color: root.highlighted && root.hoverable ? Theme.surfaceHov : "transparent"
    }
}
