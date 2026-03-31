import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property var backendObject
    property string statusText: ""
    property bool busy: false
    readonly property bool requiresOtp: backendObject ? backendObject.twoFactorEnabled : false
    readonly property int dialogRadius: 8

    modal: true
    parent: Overlay.overlay
    width: 430
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))
    // 다이얼로그 열기 함수
    function openDialog() {
        statusText = ""
        busy = false
        passwordField.text = ""
        otpField.text = ""
        open()
        passwordField.forceActiveFocus()
        if (backendObject)
            backendObject.refreshTwoFactorStatus()
    }
    // 열림 처리 함수
    onOpened: if (backendObject) backendObject.resetSessionTimer()

    Connections {
        target: backendObject
        // 계정 삭제 완료 처리 함수
        function onAccountDeleteCompleted() {
            if (!root.visible)
                return
            root.busy = false
            root.close()
        }
        // 계정 삭제 실패 처리 함수
        function onAccountDeleteFailed(error) {
            if (!root.visible)
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
            text: "회원탈퇴"
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
                text: root.requiresOtp
                      ? "계정을 삭제하려면 현재 비밀번호와 OTP 6자리를 다시 입력해 주세요. 삭제 후에는 기존 로그인 세션도 바로 종료됩니다."
                      : "계정을 삭제하려면 현재 비밀번호를 다시 입력해 주세요. 삭제 후에는 기존 로그인 세션도 바로 종료됩니다."
                color: root.theme ? root.theme.textSecondary : "#a1a1aa"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }

            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                placeholderText: "현재 비밀번호"
                echoMode: TextInput.Password
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: passwordField.activeFocus ? "#fb7185"
                                                           : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                // 텍스트 편집 처리 함수
                onTextEdited: {
                    if (backendObject)
                        backendObject.resetSessionTimer()
                    root.statusText = ""
                }
            }

            TextField {
                id: otpField
                Layout.fillWidth: true
                visible: root.requiresOtp
                enabled: visible
                placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                placeholderText: "현재 OTP 6자리"
                inputMethodHints: Qt.ImhDigitsOnly
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: otpField.activeFocus ? "#fb7185"
                                                      : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                // 텍스트 편집 처리 함수
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
                text: "취소"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 34
                // 클릭 이벤트 처리 함수
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
                text: root.busy ? "처리 중..." : "회원탈퇴"
                Layout.preferredWidth: 120
                Layout.preferredHeight: 34
                enabled: !root.busy
                // 클릭 이벤트 처리 함수
                onClicked: {
                    if (!backendObject) {
                        root.statusText = "Backend is not available."
                        return
                    }
                    if (passwordField.text.length === 0) {
                        root.statusText = "현재 비밀번호를 입력해 주세요."
                        passwordField.forceActiveFocus()
                        return
                    }
                    if (root.requiresOtp && !/^\d{6}$/.test(otpField.text)) {
                        root.statusText = "OTP 6자리를 입력해 주세요."
                        otpField.forceActiveFocus()
                        return
                    }

                    backendObject.resetSessionTimer()
                    root.statusText = ""
                    root.busy = true
                    backendObject.deleteAccount(passwordField.text, otpField.text)
                }
                background: Rectangle {
                    color: parent.enabled ? (parent.down ? "#be123c" : "#e11d48")
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
