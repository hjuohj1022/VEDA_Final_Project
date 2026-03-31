import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property var backendObject
    property string statusText: ""
    property string helperText: ""
    property string userId: ""
    property string email: ""
    property bool busy: false
    property bool newPasswordVisible: false
    signal resetCompleted(string message)
    readonly property int dialogRadius: 8

    modal: true
    parent: Overlay.overlay
    width: 430
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))
    // 새 비밀번호 복잡도 검증 함수
    function validateNewPasswordComplexity(password) {
        // 새 비밀번호 복잡도 규칙 검증
        if (password.length < 8)
            return "비밀번호는 8자 이상이어야 합니다."
        if (password.length > 16)
            return "비밀번호는 16자 이하여야 합니다."
        if (/\s/.test(password))
            return "비밀번호에는 공백을 사용할 수 없습니다."
        if (!/[0-9]/.test(password))
            return "비밀번호에는 숫자가 1개 이상 포함되어야 합니다."
        if (!/[^A-Za-z0-9]/.test(password))
            return "비밀번호에는 특수문자가 1개 이상 포함되어야 합니다."
        return ""
    }
    // 다이얼로그 열기 함수
    function openDialog(userIdValue, emailValue, initialCode, message) {
        statusText = ""
        helperText = "입력한 정보가 일치하면 메일로 코드가 전송됩니다. 메일을 받은 경우에만 아래 코드를 입력해 주세요."
        userId = userIdValue || ""
        email = emailValue || ""
        busy = false
        newPasswordVisible = false
        codeField.text = initialCode || ""
        newPasswordField.text = ""
        open()
        if (codeField.text.length > 0) {
            newPasswordField.forceActiveFocus()
        } else {
            codeField.forceActiveFocus()
        }
    }
    // 비밀번호 재설정 전송 함수
    function submitReset() {
        if (!backendObject) {
            root.statusText = "Backend is not available."
            return
        }
        if (codeField.text.trim().length === 0) {
            root.statusText = "재설정 코드를 입력해 주세요."
            codeField.forceActiveFocus()
            return
        }
        if (newPasswordField.text.length === 0) {
            root.statusText = "새 비밀번호를 입력해 주세요."
            newPasswordField.forceActiveFocus()
            return
        }

        const complexityError = root.validateNewPasswordComplexity(newPasswordField.text)
        if (complexityError.length > 0) {
            root.statusText = complexityError
            newPasswordField.forceActiveFocus()
            return
        }

        root.statusText = ""
        root.busy = true
        backendObject.resetPasswordWithCode(codeField.text, newPasswordField.text)
    }

    Connections {
        target: backendObject
        // 비밀번호 재설정 완료 처리 함수
        function onPasswordResetCompleted(message) {
            if (!root.visible)
                return
            root.busy = false
            root.statusText = ""
            root.close()
            root.resetCompleted(message)
        }
        // 비밀번호 재설정 실패 처리 함수
        function onPasswordResetFailed(error) {
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
            text: "비밀번호 재설정"
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
                text: root.helperText
                color: root.theme ? root.theme.textPrimary : "#e4e4e7"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }

            Text {
                Layout.fillWidth: true
                visible: root.userId.length > 0 || root.email.length > 0
                text: "ID: " + root.userId + (root.email.length > 0 ? "\nEmail: " + root.email : "")
                color: root.theme ? root.theme.textPrimary : "#e4e4e7"
                wrapMode: Text.WordWrap
                font.pixelSize: 11
            }

            TextField {
                id: codeField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: "재설정 코드"
                placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: codeField.activeFocus
                                  ? (root.theme ? root.theme.accent : "#f97316")
                                  : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                // 텍스트 편집 처리 함수
                onTextEdited: root.statusText = ""
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 40

                TextField {
                    id: newPasswordField
                    anchors.fill: parent
                    rightPadding: 40
                    placeholderText: "새 비밀번호"
                    placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                    echoMode: root.newPasswordVisible ? TextInput.Normal : TextInput.Password
                    color: root.theme ? root.theme.textPrimary : "white"
                    background: Rectangle {
                        color: root.theme ? root.theme.bgComponent : "#18181b"
                        border.color: newPasswordField.activeFocus
                                      ? (root.theme ? root.theme.accent : "#f97316")
                                      : (root.theme ? root.theme.border : "#27272a")
                        radius: 6
                    }
                    // 텍스트 편집 처리 함수
                    onTextEdited: root.statusText = ""
                    // 입력 확정 처리 함수
                    onAccepted: root.submitReset()
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    // 클릭 이벤트 처리 함수
                    onClicked: root.newPasswordVisible = !root.newPasswordVisible
                    contentItem: Item {
                        Text {
                            anchors.centerIn: parent
                            text: "\uE890"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 16
                            color: root.theme ? root.theme.textSecondary : "#d4d4d8"
                        }
                        Rectangle {
                            visible: !root.newPasswordVisible
                            anchors.centerIn: parent
                            width: 2
                            height: 18
                            radius: 1
                            rotation: -45
                            color: root.theme ? root.theme.textSecondary : "#d4d4d8"
                        }
                    }
                    background: Rectangle {
                        color: "transparent"
                        border.color: "transparent"
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                color: root.theme ? root.theme.textPrimary : "#e4e4e7"
                font.pixelSize: 11
                text: "비밀번호 규칙: 8~16자 · 숫자 1개 이상 · 특수문자 1개 이상 · 공백 불가"
                wrapMode: Text.WordWrap
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
                enabled: !root.busy
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
                id: resetButton
                text: root.busy ? "변경 중..." : "비밀번호 재설정"
                Layout.preferredWidth: 148
                Layout.preferredHeight: 34
                enabled: !root.busy
                // 클릭 이벤트 처리 함수
                onClicked: root.submitReset()
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
