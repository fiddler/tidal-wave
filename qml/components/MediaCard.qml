import QtQuick
import QtQuick.Layouts
import TidalWave

Item {
    id: root

    property string coverUrl: ""
    property string title: ""
    property string subtitle: ""
    property string mediaType: "album"
    property int    cardSize: 160
    // >0 turns the subtitle into a link to that artist. Cards whose subtitle
    // is not one artist — a mix's blurb, "16 tracks", a release year — leave
    // it at 0 and the subtitle stays plain text.
    property int    artistId: 0

    width: cardSize
    height: col.height + 8

    signal clicked()
    signal playClicked()
    signal subtitleClicked()

    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed:  root.clicked()

    ColumnLayout {
        id: col
        width: parent.width
        spacing: 8

        Rectangle {
            id: imgRect
            Layout.fillWidth: true
            height: cardSize
            radius: mediaType === "artist" ? cardSize/2 : Theme.radiusLg
            color: Theme.surfaceHigh
            clip: true
            border.width: root.activeFocus ? 4 : 0
            border.color: Theme.accent

            Image {
                id: img
                anchors.fill: parent
                source: coverUrl.length > 0 ? "image://tidal/" + coverUrl : ""
                fillMode: Image.PreserveAspectCrop
                smooth: true
                mipmap: true
                opacity: status === Image.Ready ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 200 } }
            }

            VectorIcon {
                visible: root.coverUrl.length === 0 || img.status === Image.Error
                anchors.centerIn: parent
                name: mediaType === "artist" ? "artist" : "music"
                color: Theme.textDim
                width: 48
                height: 48
                strokeWidth: 1.5
            }

            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: Qt.rgba(0, 0, 0, hov.hovered ? 0.45 : 0)
                Behavior on color { ColorAnimation { duration: 150 } }

                Rectangle {
                    visible: hov.hovered
                    width: 44
                    height: 44
                    radius: 22
                    anchors.bottom: parent.bottom
                    anchors.right: parent.right
                    anchors.margins: 12
                    color: Theme.accent

                    Text {
                        anchors.centerIn: parent
                        text: "▶"
                        color: "white"
                        font.pixelSize: 16
                        leftPadding: 2
                    }

                    scale: playHov.hovered ? 1.05 : 1
                    Behavior on scale { NumberAnimation { duration: 100 } }
                    HoverHandler { id: playHov; cursorShape: Qt.PointingHandCursor }
                    TapHandler   { onTapped: root.playClicked() }
                }
            }

            HoverHandler { id: hov; cursorShape: Qt.PointingHandCursor }
            TapHandler   { onTapped: root.clicked() }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            // The title opens what the cover opens.
            Text {
                id: titleText
                Layout.fillWidth: true
                text: root.title
                color: Theme.textPrimary
                font.pixelSize: 14
                font.bold: true
                font.underline: titleHov.containsMouse
                elide: Text.ElideRight
                wrapMode: Text.NoWrap

                MouseArea {
                    id: titleHov
                    // Only as wide as the text: the label fills the card, and a
                    // full-width hit area would underline the gap beside a
                    // short title.
                    width: Math.min(titleText.implicitWidth, titleText.width)
                    height: parent.height
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.clicked()
                }
            }

            Text {
                id: subtitleText
                readonly property bool isLink: root.artistId > 0 && root.subtitle.length > 0
                Layout.fillWidth: true
                text: root.subtitle
                color: isLink && subtitleHov.containsMouse ? Theme.textPrimary : Theme.textSec
                font.pixelSize: 12
                font.underline: isLink && subtitleHov.containsMouse
                elide: Text.ElideRight
                wrapMode: Text.NoWrap

                MouseArea {
                    id: subtitleHov
                    width: Math.min(subtitleText.implicitWidth, subtitleText.width)
                    height: parent.height
                    enabled: subtitleText.isLink
                    hoverEnabled: true
                    // A disabled MouseArea still applies its cursorShape, so a
                    // subtitle that links nowhere — a mix's blurb, a track
                    // count — would offer the hand and then do nothing.
                    cursorShape: subtitleText.isLink ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: root.subtitleClicked()
                }
            }
        }
    }
}
