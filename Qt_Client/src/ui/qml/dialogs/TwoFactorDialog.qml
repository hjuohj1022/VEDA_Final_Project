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
    readonly property string qrCodeSource: root.otpAuthUrl.length > 0
                                           ? "image://qrcode/" + encodeURIComponent(root.otpAuthUrl)
                                           : ""

    modal: true
    parent: Overlay.overlay
    width: 520
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))
    // 설정 화면 열기 함수
    function openForSetup() {
        mode = "setup"
        manualKey = ""
        otpAuthUrl = ""
        statusText = ""
        otpField.text = ""
        busy = true
        open()
        if (root.backendObject)
            root.backendObject.startTwoFactorSetup()
    }
    // 비활성화 화면 열기 함수
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
    // 열림 처리 함수
    onOpened: if (root.backendObject) root.backendObject.resetSessionTimer()

    Connections {
        target: root.backendObject
        // 이중 인증 설정 준비 처리 함수
        function onTwoFactorSetupReady(manualKey, otpAuthUrl) {
            if (!root.visible || root.mode !== "setup")
                return
            root.busy = false
            root.manualKey = manualKey
            root.otpAuthUrl = otpAuthUrl
            root.statusText = ""
            otpField.forceActiveFocus()
        }
        // 이중 인증 설정 완료 처리 함수
        function onTwoFactorSetupCompleted() {
            if (!root.visible || root.mode !== "setup")
                return
            root.busy = false
            root.close()
        }
        // 이중 인증 설정 실패 처리 함수
        function onTwoFactorSetupFailed(error) {
            if (!root.visible || root.mode !== "setup")
                return
            root.busy = false
            root.statusText = error
        }
        // 이중 인증 비활성화 완료 처리 함수
        function onTwoFactorDisableCompleted() {
            if (!root.visible || root.mode !== "disable")
                return
            root.busy = false
            root.close()
        }
        // 이중 인증 비활성화 실패 처리 함수
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
            text: root.mode === "setup" ? "2단계 인증 활성화" : "2단계 인증 비활성화"
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
                      ? "인증 앱으로 이 QR 코드를 스캔한 뒤, 앱에 표시된 6자리 OTP를 입력하세요."
                      : "2단계 인증을 해제하려면 인증 앱에 표시된 현재 6자리 OTP를 입력하세요."
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
                    spacing: 10

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: 300
                        Layout.preferredHeight: 300
                        radius: 10
                        color: "white"
                        border.color: root.theme ? root.theme.border : "#d4d4d8"
                        border.width: 1
                        clip: true

                        Image {
                            anchors.fill: parent
                            anchors.margins: 0
                            asynchronous: false
                            cache: false
                            fillMode: Image.PreserveAspectFit
                            smooth: false
                            source: root.qrCodeSource
                            sourceSize.width: 300
                            sourceSize.height: 300
                        }

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: root.busy && root.qrCodeSource.length === 0
                            visible: running
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: "QR 스캔이 어려운 경우 아래 키를 직접 등록할 수 있습니다."
                        color: root.theme ? root.theme.textSecondary : "#71717a"
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                    }

                    Text {
                        text: "수동 등록 키"
                        color: root.theme ? root.theme.accent : "#f97316"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    TextField {
                        Layout.fillWidth: true
                        readOnly: true
                        selectByMouse: true
                        text: root.manualKey.length > 0 ? root.manualKey : "생성 중..."
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
                placeholderText: root.mode === "setup" ? "6자리 OTP 입력" : "현재 6자리 OTP 입력"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: otpField.activeFocus ? (root.theme ? root.theme.accent : "#f97316")
                                                      : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                // 텍스트 편집 처리 함수
                onTextEdited: {
                    if (root.backendObject)
                        root.backendObject.resetSessionTimer()
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
                id: closeButton
                text: "닫기"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 34
                // 클릭 이벤트 처리 함수
                onClicked: root.close()
                background: Rectangle {
                    color: closeButton.down ? "#3f3f46" : (root.theme ? root.theme.bgSecondary : "#1f2937")
                    border.color: root.theme ? root.theme.border : "#374151"
                    radius: 6
                }
                contentItem: Text {
                    text: closeButton.text
                    color: root.theme ? root.theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Button {
                id: confirmButton
                text: {
                    if (root.mode === "setup")
                        return root.busy ? "처리 중..." : "2단계 인증 활성화"
                    return root.busy ? "처리 중..." : "2단계 인증 비활성화"
                }
                Layout.preferredWidth: 120
                Layout.preferredHeight: 34
                enabled: !root.busy && (root.mode === "disable" || root.manualKey.length > 0)
                // 클릭 이벤트 처리 함수
                onClicked: {
                    if (root.backendObject)
                        root.backendObject.resetSessionTimer()
                    root.statusText = ""
                    root.busy = true
                    if (!root.backendObject) {
                        root.busy = false
                        root.statusText = "백엔드를 사용할 수 없습니다."
                    } else if (root.mode === "setup") {
                        root.backendObject.confirmTwoFactorSetup(otpField.text)
                    } else {
                        root.backendObject.disableTwoFactor(otpField.text)
                    }
                }
                background: Rectangle {
                    color: confirmButton.enabled
                           ? (confirmButton.down ? "#ea580c" : (root.theme ? root.theme.accent : "#f97316"))
                           : (root.theme ? root.theme.border : "#3f3f46")
                    radius: 6
                }
                contentItem: Text {
                    text: confirmButton.text
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
