import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property var backendObject
    property string mode: "setup"
    property string manualKey: ""
    property string otpAuthUrl: ""
    property string statusText: ""
    property bool busy: false
    readonly property int dialogRadius: 10

    modal: true
    parent: Overlay.overlay
    width: 430
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))

    function openForSetup() {
        mode = "setup"
        manualKey = ""
        otpAuthUrl = ""
        statusText = ""
        otpField.text = ""
        busy = true
        open()
        if (backendObject)
            backendObject.startTwoFactorSetup()
    }

    function openForDisable() {
        mode = "disable"
        manualKey = ""
        otpAuthUrl = ""
        statusText = ""
        otpField.text = ""
        busy = false
        open()
        otpField.forceActiveFocus()
    }

    onOpened: if (backendObject) backendObject.resetSessionTimer()

    Connections {
        target: backendObject

        function onTwoFactorSetupReady(manualKey, otpAuthUrl) {
            if (!root.visible || root.mode !== "setup")
                return
            root.busy = false
            root.manualKey = manualKey
            root.otpAuthUrl = otpAuthUrl
            root.statusText = ""
            otpField.forceActiveFocus()
        }

        function onTwoFactorSetupCompleted() {
            if (!root.visible || root.mode !== "setup")
                return
            root.busy = false
            root.close()
        }

        function onTwoFactorSetupFailed(error) {
            if (!root.visible || root.mode !== "setup")
                return
            root.busy = false
            root.statusText = error
        }

        function onTwoFactorDisableCompleted() {
            if (!root.visible || root.mode !== "disable")
                return
            root.busy = false
            root.close()
        }

        function onTwoFactorDisableFailed(error) {
            if (!root.visible || root.mode !== "disable")
                return
            root.busy = false
            root.statusText = error
        }
    }

    header: Rectangle {
        implicitHeight: 46
        color: "transparent"

        Rectangle {
            anchors.fill: parent
            color: root.theme ? root.theme.bgSecondary : "#09090b"
            radius: root.dialogRadius
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.dialogRadius
            color: root.theme ? root.theme.bgSecondary : "#09090b"
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 1
            color: root.theme ? root.theme.border : "#27272a"
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            text: root.mode === "setup" ? "OTP 생성" : "OTP 삭제"
            color: root.theme ? root.theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14
        }
    }

    contentItem: Rectangle {
        color: "transparent"
        implicitHeight: dialogColumn.implicitHeight + 24

        ColumnLayout {
            id: dialogColumn
            anchors.fill: parent
            anchors.margins: 12
            spacing: 10

            Text {
                Layout.fillWidth: true
                text: root.mode === "setup"
                      ? "Google Authenticator 등에 아래 설정 키를 등록한 뒤, 앱에 표시된 현재 OTP 6자리를 입력하세요."
                      : "현재 Authenticator 앱에 표시되는 OTP 6자리를 입력하면 2FA가 비활성화됩니다."
                color: root.theme ? root.theme.textSecondary : "#a1a1aa"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }

            Rectangle {
                Layout.fillWidth: true
                visible: root.mode === "setup"
                radius: 8
                color: root.theme ? root.theme.bgSecondary : "#0f172a"
                border.color: root.theme ? root.theme.border : "#27272a"
                border.width: 1
                implicitHeight: setupColumn.implicitHeight + 16

                ColumnLayout {
                    id: setupColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Text {
                        text: "Manual Key"
                        color: root.theme ? root.theme.accent : "#f97316"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: root.manualKey.length > 0 ? root.manualKey : "Generating..."
                        color: root.theme ? root.theme.textPrimary : "white"
                        placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                        background: Rectangle {
                            color: root.theme ? root.theme.bgComponent : "#18181b"
                            border.color: root.theme ? root.theme.border : "#27272a"
                            radius: 6
                        }
                    }
                }
            }

            TextField {
                id: otpField
                Layout.fillWidth: true
                placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                enabled: root.mode === "disable" || root.manualKey.length > 0
                placeholderText: root.mode === "setup" ? "OTP 6 digits" : "Current OTP 6 digits"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: otpField.activeFocus ? (root.theme ? root.theme.accent : "#f97316")
                                                      : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                onTextEdited: {
                    if (backendObject)
                        backendObject.resetSessionTimer()
                    root.statusText = ""
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.statusText.length > 0
                text: root.statusText
                color: "#fb7185"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 58
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: "Close"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 34
                onClicked: root.close()
                background: Rectangle {
                    color: parent.down ? "#3f3f46" : (root.theme ? root.theme.bgSecondary : "#1f2937")
                    border.color: root.theme ? root.theme.border : "#374151"
                    radius: 6
                }
                contentItem: Text {
                    text: parent.text
                    color: root.theme ? root.theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                text: {
                    if (root.mode === "setup")
                        return root.busy ? "Processing..." : "OTP 생성"
                    return root.busy ? "Processing..." : "OTP 삭제"
                }
                Layout.preferredWidth: 120
                Layout.preferredHeight: 34
                enabled: !root.busy && (root.mode === "disable" || root.manualKey.length > 0)
                onClicked: {
                    if (backendObject)
                        backendObject.resetSessionTimer()
                    root.statusText = ""
                    root.busy = true
                    if (!backendObject) {
                        root.busy = false
                        root.statusText = "Backend is not available."
                    } else if (root.mode === "setup") {
                        backendObject.confirmTwoFactorSetup(otpField.text)
                    } else {
                        backendObject.disableTwoFactor(otpField.text)
                    }
                }
                background: Rectangle {
                    color: parent.enabled
                           ? (parent.down ? "#ea580c" : (root.theme ? root.theme.accent : "#f97316"))
                           : (root.theme ? root.theme.border : "#3f3f46")
                    radius: 6
                }
                contentItem: Text {
                    text: parent.text
                    color: "white"
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    background: Rectangle {
        color: root.theme ? root.theme.bgComponent : "#18181b"
        border.color: root.theme ? root.theme.border : "#27272a"
        radius: root.dialogRadius
    }
}
