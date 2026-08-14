import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TidalWave

// 10-band EQ popup, anchored to the player-bar EQ button. The DSP is in
// libmpv; every control here just talks to the `equalizer` context object.
//
// The sliders stay editable while the EQ is off — you can sculpt a curve in
// silence and flip it on when ready. The toggle only controls whether the
// filter is in the audio chain.
Popup {
    id: root
    width: 440
    padding: 16
    // Not plain CloseOnPressOutside: a press on the parent button must reach
    // the button, so its click handler can close us — otherwise the press
    // closes the popup and the click immediately reopens it.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
    background: Rectangle {
        color: Theme.surfaceHigh; border.color: Theme.border; radius: Theme.radiusLg
    }

    // Save-as-profile entry row is only shown on demand.
    property bool naming: false
    onClosed: naming = false

    readonly property bool eqOn: equalizer.enabled
    readonly property bool hasProfiles: equalizer.profiles.length > 0
    readonly property string profileLabel:
        equalizer.activeProfile.length > 0 ? equalizer.activeProfile : "Custom"

    contentItem: ColumnLayout {
        spacing: 12

        // ── Header: title + enable toggle ─────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                text: "Equalizer"
                color: Theme.textPrimary; font.pixelSize: 16; font.bold: true
            }
            Text {
                // The cast path streams the file straight to the device, so
                // the EQ cannot touch it. Say so instead of confusing people.
                visible: !!cast && cast.connected
                text: "not applied while casting"
                color: Theme.textDim; font.pixelSize: 11
            }
            Item { Layout.fillWidth: true }
            Text {
                text: root.eqOn ? "On" : "Off"
                color: root.eqOn ? Theme.accent : Theme.textDim
                font.pixelSize: 11
            }
            Rectangle {
                id: eqToggle
                width: 40; height: 22; radius: 11
                color: root.eqOn ? Theme.accent : Theme.surfaceHov
                Rectangle {
                    width: 16; height: 16; radius: 8
                    color: Theme.textPrimary
                    anchors.verticalCenter: parent.verticalCenter
                    x: root.eqOn ? parent.width - width - 3 : 3
                    Behavior on x { NumberAnimation { duration: 120 } }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler   { onTapped: equalizer.enabled = !root.eqOn }
            }
        }

        // ── Profile row: picker + save + delete ───────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: !root.naming

            Rectangle {
                Layout.fillWidth: true
                height: 32; radius: 6
                color: profHov.hovered ? Theme.surfaceHov : Theme.surface
                border.color: Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10; anchors.rightMargin: 10
                    Text {
                        Layout.fillWidth: true
                        text: root.hasProfiles ? root.profileLabel : "No profiles yet"
                        color: root.hasProfiles ? Theme.textPrimary : Theme.textDim
                        font.pixelSize: 13
                        elide: Text.ElideRight
                    }
                    VectorIcon {
                        name: "arrow-down"; width: 12; height: 12
                        strokeWidth: 2; color: Theme.textSec
                    }
                }
                HoverHandler { id: profHov; cursorShape: Qt.PointingHandCursor }
                TapHandler   { onTapped: profileMenu.open() }

                Popup {
                    id: profileMenu
                    y: parent.height + 4
                    width: parent.width
                    padding: 6
                    background: Rectangle {
                        color: Theme.surfaceHigh; border.color: Theme.border
                        radius: Theme.radius
                    }
                    contentItem: ColumnLayout {
                        spacing: 2
                        Text {
                            visible: !root.hasProfiles
                            Layout.fillWidth: true
                            Layout.margins: 8
                            text: "Nothing saved yet.\nSet the sliders, then press Save profile."
                            color: Theme.textSec; font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: equalizer.profiles
                            delegate: ProfileRow {
                                required property var modelData
                                name: modelData.name
                                active: equalizer.activeProfile === modelData.name
                                onSelected: {
                                    equalizer.applyProfile(modelData.name)
                                    profileMenu.close()
                                }
                            }
                        }
                    }
                }
            }

            // Labeled on purpose: a bare "+" icon read as "add a band", not
            // "save these sliders as a profile".
            Rectangle {
                width: saveLabel.implicitWidth + 22; height: 32; radius: 6
                color: saveHov.hovered ? Theme.surfaceHov : Theme.surface
                border.color: Theme.accentDim
                Text {
                    id: saveLabel
                    anchors.centerIn: parent
                    text: "Save profile"
                    color: Theme.accent; font.pixelSize: 12
                }
                HoverHandler { id: saveHov; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        nameField.text = equalizer.activeProfile
                        root.naming = true
                        nameField.forceActiveFocus()
                        nameField.selectAll()
                    }
                }
            }
            SmallActionButton {
                icon: "trash"
                tip: "Delete profile \"" + equalizer.activeProfile + "\""
                visible: equalizer.activeProfile.length > 0
                onClicked: equalizer.deleteProfile(equalizer.activeProfile)
            }
        }

        // ── Save-profile name entry (replaces the profile row) ────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.naming

            Rectangle {
                Layout.fillWidth: true
                height: 32; radius: 6
                color: Theme.surface; border.color: Theme.accentDim
                TextField {
                    id: nameField
                    anchors.fill: parent
                    color: Theme.textPrimary; font.pixelSize: 13
                    placeholderText: "Name this profile — e.g. Metal, Podcasts"
                    placeholderTextColor: Theme.textDim
                    leftPadding: 10; rightPadding: 10
                    verticalAlignment: TextInput.AlignVCenter
                    background: null
                    onAccepted: root.commitName()
                    Keys.onEscapePressed: root.naming = false
                }
            }
            Rectangle {
                width: commitLabel.implicitWidth + 22; height: 32; radius: 6
                color: nameField.text.trim().length > 0 ? Theme.accent : Theme.surfaceHov
                Text {
                    id: commitLabel
                    anchors.centerIn: parent
                    text: "Save"
                    color: nameField.text.trim().length > 0 ? Theme.bg : Theme.textDim
                    font.pixelSize: 12; font.bold: true
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler   { onTapped: root.commitName() }
            }
            SmallActionButton {
                icon: "x"
                tip: "Cancel"
                onClicked: root.naming = false
            }
        }

        // ── The bands ─────────────────────────────────────────────────
        // A plain Row with computed equal column widths, not a RowLayout:
        // explicit sizes spread the faders across the panel's full width, and
        // each fader's hit area spans its whole column — easier to operate.
        Row {
            id: bandsRow
            Layout.fillWidth: true
            Layout.topMargin: 4
            readonly property real colW: width / equalizer.bandLabels.length
            Repeater {
                // The labels are the band list — one source of truth with C++.
                model: equalizer.bandLabels
                delegate: Column {
                    id: band
                    required property int index
                    required property var modelData
                    readonly property real gain: equalizer.gains[index]
                    width: bandsRow.colW
                    spacing: 6

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: (band.gain > 0 ? "+" : "") + band.gain.toFixed(1)
                        color: band.gain !== 0 ? Theme.accent : Theme.textDim
                        font.pixelSize: 9
                    }
                    BandSlider {
                        width: parent.width
                        height: 150
                        value: band.gain
                        // Dimmed while the EQ is off, but still editable —
                        // the curve just isn't in the audio chain yet.
                        dimmed: !root.eqOn
                        onMoved: (dB) => equalizer.setGain(band.index, dB)
                        onReset: equalizer.setGain(band.index, 0)
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: band.modelData
                        color: Theme.textSec; font.pixelSize: 9
                    }
                }
            }
        }

        // ── Preamp + reset ────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 10

            Text { text: "Preamp"; color: Theme.textSec; font.pixelSize: 11 }

            // Same hand-rolled slider style as VolumeSlider, mapped to dB.
            // Read-only under auto preamp: it then just shows the headroom
            // the EQ is applying.
            Item {
                id: preampSlider
                Layout.fillWidth: true
                height: 20
                readonly property bool interactive: !equalizer.autoPreamp
                // Clamped: the auto estimate can exceed the manual range when
                // several neighbouring bands are boosted hard.
                readonly property real norm:
                    Math.max(0, Math.min(1,
                        (equalizer.preamp + equalizer.gainLimit) / (equalizer.gainLimit * 2)))
                Rectangle {
                    anchors { left: parent.left; right: parent.right
                              verticalCenter: parent.verticalCenter }
                    height: 3; radius: 2
                    color: Theme.border
                    Rectangle {
                        width: preampSlider.norm * parent.width
                        height: parent.height; radius: parent.radius
                        color: preampSlider.interactive ? Theme.textSec : Theme.textDim
                    }
                    Rectangle {
                        x: preampSlider.norm * parent.width - 5
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10; height: 10; radius: 5
                        color: preampSlider.interactive ? "white" : Theme.textSec
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: preampSlider.interactive
                    preventStealing: true
                    cursorShape: Qt.PointingHandCursor
                    function apply(mx) {
                        var n = Math.max(0, Math.min(1, mx / width))
                        var span = equalizer.gainLimit * 2
                        equalizer.setPreamp(
                            Math.round((n * span - equalizer.gainLimit) * 2) / 2)
                    }
                    onPressed: (mouse) => apply(mouse.x)
                    onPositionChanged: (mouse) => { if (pressed) apply(mouse.x) }
                }
            }

            Text {
                text: (equalizer.preamp > 0 ? "+" : "") +
                      equalizer.preamp.toFixed(1) + " dB"
                color: Theme.textSec; font.pixelSize: 11
                horizontalAlignment: Text.AlignRight
                Layout.preferredWidth: 46
            }

            // Auto preamp: counter the largest boost so nothing clips.
            Rectangle {
                width: autoLabel.implicitWidth + 16; height: 22; radius: 11
                color: equalizer.autoPreamp ? Theme.accentDim : Theme.surfaceHov
                Text {
                    id: autoLabel
                    anchors.centerIn: parent
                    text: "Auto"
                    color: equalizer.autoPreamp ? Theme.textPrimary : Theme.textSec
                    font.pixelSize: 11
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler   { onTapped: equalizer.autoPreamp = !equalizer.autoPreamp }
            }

            Text {
                text: "Reset"
                color: resetHov.hovered ? Theme.textPrimary : Theme.textSec
                font.pixelSize: 11
                HoverHandler { id: resetHov; cursorShape: Qt.PointingHandCursor }
                TapHandler   { onTapped: equalizer.reset() }
            }
        }
    }

    function commitName() {
        if (nameField.text.trim().length === 0) return
        equalizer.saveProfile(nameField.text)
        naming = false
    }

    // One vertical dB fader. Hand-rolled like VolumeSlider: groove, a fill
    // from the 0 dB center line to the handle, drag anywhere to set.
    component BandSlider : Item {
        id: bs
        property real value: 0        // dB, −limit…+limit
        property bool dimmed: false
        signal moved(real dB)
        signal reset()

        readonly property real limit: equalizer.gainLimit
        readonly property real handleR: 6
        // Handle travel: top = +limit dB, bottom = −limit dB.
        readonly property real trackH: height - handleR * 2
        readonly property real handleY:
            handleR + (1 - (value + limit) / (limit * 2)) * trackH

        Rectangle {   // groove
            anchors.horizontalCenter: parent.horizontalCenter
            y: bs.handleR; height: bs.trackH
            width: 4; radius: 2
            color: Theme.border
        }
        Rectangle {   // 0 dB center line
            anchors.horizontalCenter: parent.horizontalCenter
            y: bs.handleR + bs.trackH / 2 - 0.5
            width: 12; height: 1
            color: Theme.textDim
        }
        Rectangle {   // fill: center → handle
            anchors.horizontalCenter: parent.horizontalCenter
            width: 4; radius: 2
            color: bs.dimmed ? Theme.accentDim : Theme.accent
            y: Math.min(bs.handleY, bs.handleR + bs.trackH / 2)
            height: Math.abs(bs.handleY - (bs.handleR + bs.trackH / 2))
        }
        Rectangle {   // handle
            anchors.horizontalCenter: parent.horizontalCenter
            y: bs.handleY - bs.handleR
            width: bs.handleR * 2; height: bs.handleR * 2; radius: bs.handleR
            color: bs.dimmed ? Theme.textSec : "white"
        }

        MouseArea {
            anchors.fill: parent
            anchors.margins: -4   // easier to grab a 4px groove
            preventStealing: true
            cursorShape: Qt.PointingHandCursor
            function apply(my) {
                var n = 1 - Math.max(0, Math.min(1, (my - 4 - bs.handleR) / bs.trackH))
                bs.moved(Math.round((n * bs.limit * 2 - bs.limit) * 2) / 2)   // 0.5 dB steps
            }
            onPressed: (mouse) => apply(mouse.y)
            onPositionChanged: (mouse) => { if (pressed) apply(mouse.y) }
            onDoubleClicked: bs.reset()
        }
    }

    // A row in the profile dropdown.
    component ProfileRow : Rectangle {
        id: pr
        property string name
        property bool   active: false
        signal selected()
        Layout.fillWidth: true
        implicitHeight: 32
        radius: 6
        color: prHov.hovered ? Theme.surfaceHov : "transparent"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8; anchors.rightMargin: 8
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: pr.name
                color: pr.active ? Theme.accent : Theme.textPrimary
                font.pixelSize: 13; elide: Text.ElideRight
            }
            VectorIcon {
                visible: pr.active; name: "check"
                width: 13; height: 13; strokeWidth: 2; color: Theme.accent
            }
        }
        HoverHandler { id: prHov; cursorShape: Qt.PointingHandCursor }
        TapHandler   { onTapped: pr.selected() }
    }

    // Small square icon button (delete / cancel).
    component SmallActionButton : Rectangle {
        id: sab
        property string icon
        property string tip
        signal clicked()
        width: 32; height: 32; radius: 6
        color: sabHov.hovered ? Theme.surfaceHov : Theme.surface
        border.color: Theme.border
        VectorIcon {
            anchors.centerIn: parent
            name: sab.icon; width: 14; height: 14; strokeWidth: 1.8
            color: Theme.textSec
        }
        ToolTip.visible: sabHov.hovered && tip.length > 0
        ToolTip.text: tip
        ToolTip.delay: 600
        HoverHandler { id: sabHov; cursorShape: Qt.PointingHandCursor }
        TapHandler   { onTapped: sab.clicked() }
    }
}
