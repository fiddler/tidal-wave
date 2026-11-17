import QtQuick
import QtQuick.Controls
import QtQuick.Window
import TidalWave

// Who made a recording: each role with the people credited for it, then the
// release date and the rights line. One dialog for the whole window, opened
// through Window.window.showCredits(track).
Popup {
    id: root

    property var    track: null
    property var    credits: null
    property string phase: "none"    // none | loading | ready | empty | error
    property int    _gen: 0

    function openFor(t) {
        if (!t || !(Number(t.id) > 0)) return
        track = t
        credits = null
        phase = "loading"
        var gen = ++_gen
        open()
        bridge.fetchTrackCredits(t.id, function(c, err) {
            if (gen !== root._gen) return        // another track was asked for since
            if (err) { root.phase = "error"; return }
            root.credits = c
            var has = (c.groups && c.groups.length > 0) || c.copyright || c.releaseDate || c.isrc
            root.phase = has ? "ready" : "empty"
        })
    }

    function footerLines() {
        var c = credits || {}
        var lines = []
        if (c.releaseDate) lines.push("Released " + c.releaseDate)
        if (c.copyright)   lines.push(c.copyright)
        if (c.isrc)        lines.push("ISRC " + c.isrc)
        if (c.upc)         lines.push("UPC " + c.upc)
        return lines
    }

    anchors.centerIn: Overlay.overlay
    width: 460
    height: Math.min(560, Overlay.overlay ? Overlay.overlay.height - 80 : 560)
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 20
    background: Rectangle { color: Theme.surfaceHigh; border.color: Theme.border; radius: 12 }

    contentItem: Column {
        spacing: 12

        Column {
            width: parent.width
            spacing: 2
            Text {
                width: parent.width
                text: "Credits"
                color: Theme.textDim
                font.pixelSize: 11; font.bold: true; font.letterSpacing: 1
            }
            Text {
                width: parent.width
                text: root.track ? root.track.title : ""
                color: Theme.textPrimary
                font.pixelSize: 16; font.bold: true
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: root.track ? (root.track.artists || "") : ""
                color: Theme.textSec
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }

        Rectangle { width: parent.width; height: 1; color: Theme.border }

        Text {
            width: parent.width
            visible: root.phase !== "ready"
            text: root.phase === "loading" ? "Loading credits…"
                : root.phase === "error"   ? "Couldn't load credits. Check your connection."
                : root.phase === "empty"   ? "No credits listed for this track."
                : ""
            color: Theme.textSec
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }

        ListView {
            id: list
            objectName: "creditsList"
            width: parent.width
            height: parent.height - y
            visible: root.phase === "ready"
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            spacing: 12
            model: root.credits && root.credits.groups ? root.credits.groups : []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Column {
                id: group
                required property var modelData
                width: list.width - 12
                spacing: 3
                Text {
                    text: group.modelData.type
                    color: Theme.textDim
                    font.pixelSize: 12
                }
                Flow {
                    width: parent.width
                    spacing: 0
                    Repeater {
                        model: group.modelData.contributors
                        Row {
                            id: person
                            required property var modelData
                            required property int index
                            readonly property bool isLink: Number(modelData.id) > 0
                            Text {
                                text: person.modelData.name
                                color: personHov.hovered && person.isLink ? Theme.accent : Theme.textPrimary
                                font.pixelSize: 14
                                font.underline: personHov.hovered && person.isLink
                                HoverHandler {
                                    id: personHov
                                    enabled: person.isLink
                                    cursorShape: Qt.PointingHandCursor
                                }
                                TapHandler {
                                    enabled: person.isLink
                                    onTapped: {
                                        var id = Number(person.modelData.id)
                                        root.close()
                                        root.parent.Window.window.navigate("artist", { artistId: id })
                                    }
                                }
                            }
                            Text {
                                visible: person.index < group.modelData.contributors.length - 1
                                text: ", "
                                color: Theme.textPrimary
                                font.pixelSize: 14
                            }
                        }
                    }
                }
            }

            footer: Column {
                width: list.width - 12
                topPadding: 16
                spacing: 3
                Repeater {
                    model: root.footerLines()
                    Text {
                        required property string modelData
                        width: parent.width
                        text: modelData
                        color: Theme.textDim
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
