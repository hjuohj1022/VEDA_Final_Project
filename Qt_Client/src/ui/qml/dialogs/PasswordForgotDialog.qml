import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property var backendObject
    property string statusText: ""
    property bool busy: false
    signal resetRequested(string userId, string email, string debugCode, string message)
    readonly property int dialogRadius: 8

    modal: true
    parent: Overlay.overlay
    width: 430
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))
    // 다이얼로그 열기 함수
    function openDialog(initialId) {
        statusText = ""
        busy = false
        userIdField.text = initialId ? initialId.trim() : ""
        emailField.text = ""
        open()
        if (userIdField.text.length > 0) {
            emailField.forceActiveFocus()
        } else {
            userIdField.forceActiveFocus()
        }
    }
    // 이메일 형식 검증 함수
    function validateEmail(email) {
        const trimmed = email.trim()
        if (trimmed.length === 0)
            return "이메일을 입력해 주세요."
        if (/\s/.test(trimmed))
            return "이메일에는 공백을 사용할 수 없습니다."
        if (!/^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$/.test(trimmed))
            return "이메일 형식이 올바르지 않습니다."
        return ""
    }
    // 비밀번호 재설정 요청 전송 함수
    function submitRequest() {
        if (!backendObject) {
            root.statusText = "Backend is not available."
            return
        }
        if (userIdField.text.trim().length === 0) {
            root.statusText = "ID를 입력해 주세요."
            userIdField.forceActiveFocus()
            return
        }
        const emailError = root.validateEmail(emailField.text)
        if (emailError.length > 0) {
            root.statusText = emailError
            emailField.forceActiveFocus()
            return
        }

        root.statusText = ""
        root.busy = true
        backendObject.requestPasswordReset(userIdField.text, emailField.text)
    }

    Connections {
        target: backendObject
        // 비밀번호 재설정 요청 완료 처리 함수
        function onPasswordResetRequested(message, debugCode) {
            if (!root.visible)
                return
            const requestedId = userIdField.text.trim()
            const requestedEmail = emailField.text.trim()
            root.busy = false
            root.statusText = ""
            root.close()
            root.resetRequested(requestedId, requestedEmail, debugCode, message)
        }
        // 비밀번호 재설정 요청 실패 처리 함수
        function onPasswordResetRequestFailed(error) {
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
            text: "비밀번호를 잊으셨나요?"
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
                text: "가입한 ID와 이메일을 입력해 주세요. 입력한 정보가 일치하면 메일로 코드가 전송됩니다."
                color: root.theme ? root.theme.textPrimary : "#e4e4e7"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }

            TextField {
                id: userIdField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: "ID"
                placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: userIdField.activeFocus
                                  ? (root.theme ? root.theme.accent : "#f97316")
                                  : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                // 텍스트 편집 처리 함수
                onTextEdited: root.statusText = ""
            }

            TextField {
                id: emailField
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                placeholderText: "Email"
                placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                color: root.theme ? root.theme.textPrimary : "white"
                background: Rectangle {
                    color: root.theme ? root.theme.bgComponent : "#18181b"
                    border.color: emailField.activeFocus
                                  ? (root.theme ? root.theme.accent : "#f97316")
                                  : (root.theme ? root.theme.border : "#27272a")
                    radius: 6
                }
                // 텍스트 편집 처리 함수
                onTextEdited: root.statusText = ""
                // 입력 확정 처리 함수
                onAccepted: root.submitRequest()
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
                id: requestButton
                text: root.busy ? "요청 중..." : "코드 요청"
                Layout.preferredWidth: 124
                Layout.preferredHeight: 34
                enabled: !root.busy
                // 클릭 이벤트 처리 함수
                onClicked: root.submitRequest()
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
