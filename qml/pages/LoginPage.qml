import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import TidalWave

Rectangle {
    id: root
    color: Theme.bg

    property string errorMessage: ""

    // The authorize URL only exists after startPkceFlow() builds it, and that
    // call is synchronous, so the browser can be opened on the next line.
    function beginPkce() {
        errorMessage = ""
        auth.startPkceFlow()
        Qt.openUrlExternally(auth.verificationUrl)
    }

    function submitRedirect() {
        if (redirectField.text.trim().length === 0) return
        errorMessage = ""
        auth.submitPkceRedirect(redirectField.text)
    }

    Connections {
        target: auth
        function onLoginFailed(reason) { errorMessage = reason }
        function onStateChanged(s) { if (s === 1) errorMessage = "" }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 48, 420)
        spacing: 0

        // Logo
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
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
                text: "Native Linux Tidal Client"
                color: Theme.textSec
                font.pixelSize: 14
            }
        }

        Item { height: 48 }

        // Auth card
        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusLg
            color: Theme.surface
            border.color: Theme.border
            border.width: 1
            height: auth.state === 0 ? (errorMessage.length > 0 ? 320 : 280)
                    : auth.state === 1 ? 400
                    : auth.state === 3 ? (errorMessage.length > 0 ? 420 : 370)
                    : 100
            Behavior on height { NumberAnimation { duration: 200 } }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 32
                spacing: 20

                // Initial state
                ColumnLayout {
                    visible: auth.state === 0
                    spacing: 16

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Sign in with your Tidal account"
                        color: Theme.textSec
                        font.pixelSize: 14
                    }

                    Rectangle {
                        id: loginBtn
                        Layout.fillWidth: true
                        height: 48
                        radius: Theme.radius
                        color: Theme.accent
                        border.width: activeFocus ? 2 : 0
                        border.color: Theme.textPrimary
                        scale: loginHov.hovered ? 0.98 : 1
                        Behavior on scale { NumberAnimation { duration: 100 } }

                        activeFocusOnTab: true
                        Keys.onReturnPressed: root.beginPkce()
                        Keys.onSpacePressed:  root.beginPkce()

                        Text {
                            anchors.centerIn: parent
                            text: "Log in with Tidal"
                            color: "white"
                            font.pixelSize: 15
                            font.bold: true
                        }
                        HoverHandler { id: loginHov; cursorShape: Qt.PointingHandCursor }
                        TapHandler   { onTapped: root.beginPkce() }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Requires an active Tidal subscription"
                        color: Theme.textDim
                        font.pixelSize: 12
                    }

                    // Device code is the old flow. Tidal caps it at 320 kbps AAC,
                    // so it stays only as a way in if PKCE login breaks.
                    Text {
                        id: deviceFallback
                        Layout.alignment: Qt.AlignHCenter
                        text: "Use device code instead (no lossless)"
                        color: deviceFallback.activeFocus ? Theme.accent : Theme.textDim
                        font.pixelSize: 12
                        font.underline: true
                        activeFocusOnTab: true
                        Keys.onReturnPressed: auth.startDeviceFlow()
                        Keys.onSpacePressed:  auth.startDeviceFlow()
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: auth.startDeviceFlow()
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        visible: errorMessage.length > 0
                        text: errorMessage
                        color: Theme.red
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }

                // PKCE pending — the redirect lands on a real tidal.com page, so
                // the app cannot catch it. The user pastes the address back.
                ColumnLayout {
                    visible: auth.state === 3
                    spacing: 14

                    Text {
                        Layout.fillWidth: true
                        text: "Log in in the browser window that just opened. Tidal then "
                              + "sends you to a page that says \"Oops\" — that is expected. "
                              + "Copy that page's full address and paste it below."
                        color: Theme.textSec
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        id: reopenLink
                        Layout.alignment: Qt.AlignHCenter
                        text: "Browser did not open? Click here"
                        color: reopenLink.activeFocus ? Theme.textPrimary : Theme.accent
                        font.pixelSize: 12
                        activeFocusOnTab: true
                        Keys.onReturnPressed: Qt.openUrlExternally(auth.verificationUrl)
                        Keys.onSpacePressed:  Qt.openUrlExternally(auth.verificationUrl)
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(auth.verificationUrl)
                        }
                    }

                    TextField {
                        id: redirectField
                        Layout.fillWidth: true
                        placeholderText: "https://tidal.com/android/login/auth?code=…"
                        color: Theme.textPrimary
                        placeholderTextColor: Theme.textDim
                        font.pixelSize: 12
                        selectByMouse: true
                        background: Rectangle {
                            radius: Theme.radius
                            color: Theme.surfaceHigh
                            border.color: redirectField.activeFocus ? Theme.accent : Theme.border
                            border.width: 1
                        }
                        onAccepted: root.submitRedirect()
                    }

                    Rectangle {
                        id: continueBtn
                        Layout.fillWidth: true
                        height: 44
                        radius: Theme.radius
                        color: redirectField.text.length > 0 ? Theme.accent : Theme.surfaceHigh
                        border.width: continueBtn.activeFocus ? 2 : 0
                        border.color: Theme.textPrimary
                        activeFocusOnTab: true
                        Keys.onReturnPressed: root.submitRedirect()
                        Keys.onSpacePressed:  root.submitRedirect()
                        Text {
                            anchors.centerIn: parent
                            text: "Complete login"
                            color: redirectField.text.length > 0 ? "white" : Theme.textDim
                            font.pixelSize: 14
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.submitRedirect()
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.fillWidth: true
                        visible: errorMessage.length > 0
                        text: errorMessage
                        color: Theme.red
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }

                    Text {
                        id: cancelPkce
                        Layout.alignment: Qt.AlignHCenter
                        text: "Cancel"
                        color: cancelPkce.activeFocus ? Theme.accent : Theme.textSec
                        font.pixelSize: 13
                        font.underline: cancelPkce.activeFocus
                        activeFocusOnTab: true
                        Keys.onReturnPressed: auth.cancelDeviceFlow()
                        Keys.onSpacePressed:  auth.cancelDeviceFlow()
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: auth.cancelDeviceFlow()
                        }
                    }
                }

                // Device flow pending
                ColumnLayout {
                    visible: auth.state === 1
                    spacing: 16

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Open your browser and go to:"
                        color: Theme.textSec
                        font.pixelSize: 14
                    }

                    Rectangle {
                        id: verificationLink
                        Layout.fillWidth: true
                        height: 44
                        radius: Theme.radius
                        color: Theme.surfaceHigh
                        border.width: activeFocus ? 2 : 0
                        border.color: Theme.accent
                        activeFocusOnTab: true
                        Keys.onReturnPressed: Qt.openUrlExternally(auth.verificationUrl)
                        Keys.onSpacePressed:  Qt.openUrlExternally(auth.verificationUrl)
                        Text {
                            anchors.centerIn: parent
                            text: auth.verificationUrl || "tidal.com/link"
                            color: Theme.accent
                            font.pixelSize: 14
                            font.bold: true
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Qt.openUrlExternally(auth.verificationUrl)
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Enter this code:"
                        color: Theme.textSec
                        font.pixelSize: 14
                    }

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 200
                        height: 64
                        radius: Theme.radius
                        color: Theme.surfaceHigh
                        border.color: Theme.accent
                        border.width: 1
                        Text {
                            anchors.centerIn: parent
                            text: auth.userCode || "…"
                            color: Theme.textPrimary
                            font.pixelSize: 28
                            font.bold: true
                            font.letterSpacing: 6
                        }
                    }

                    Row {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 6
                        Repeater {
                            model: 3
                            Rectangle {
                                required property int index
                                width: 6
                                height: 6
                                radius: 3
                                color: Theme.accent
                                opacity: 0.3
                                SequentialAnimation on opacity {
                                    loops: Animation.Infinite
                                    running: auth.state === 1 && AppFocus.active
                                    PauseAnimation { duration: index * 200 }
                                    NumberAnimation { to: 1; duration: 400 }
                                    NumberAnimation { to: 0.3; duration: 400 }
                                }
                            }
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: "Waiting for you to log in…"
                        color: Theme.textDim
                        font.pixelSize: 13
                    }

                    Text {
                        id: cancelText
                        Layout.alignment: Qt.AlignHCenter
                        text: "Cancel"
                        color: cancelText.activeFocus ? Theme.accent : Theme.textSec
                        font.pixelSize: 13
                        font.underline: cancelText.activeFocus
                        activeFocusOnTab: true
                        Keys.onReturnPressed: auth.cancelDeviceFlow()
                        Keys.onSpacePressed:  auth.cancelDeviceFlow()
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6
                            cursorShape: Qt.PointingHandCursor
                            onClicked: auth.cancelDeviceFlow()
                        }
                    }
                }
            }
        }

        Item { height: 32 }

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Tidal Wave is not affiliated with TIDAL Music AS"
            color: Theme.textDim
            font.pixelSize: 11
        }
    }
}
