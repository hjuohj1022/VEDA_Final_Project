import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property var backendObject
    property string statusText: ""
    property bool busy: false
    property bool currentPasswordVisible: false
    property bool newPasswordVisible: false
    readonly property int dialogRadius: 8

    modal: true
    parent: Overlay.overlay
    width: 430
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))

    function validateNewPasswordComplexity(password) {
        // 비밀번호 복잡도 규칙 검증
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

    function openDialog() {
        // 다이얼로그 초기화 후 표시
        statusText = ""
        busy = false
        currentPasswordVisible = false
        newPasswordVisible = false
        currentPasswordField.text = ""
        newPasswordField.text = ""
        open()
        currentPasswordField.forceActiveFocus()
    }

    onOpened: if (backendObject) backendObject.resetSessionTimer()

    Connections {
        target: backendObject

        function onPasswordChangeCompleted() {
            if (!root.visible)
                return
            root.busy = false
            root.statusText = ""
            root.close()
        }

        function onPasswordChangeFailed(error) {
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
            text: "비밀번호 변경"
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
                text: "현재 비밀번호를 확인한 뒤 새 비밀번호로 변경합니다."
                color: root.theme ? root.theme.textSecondary : "#a1a1aa"
                wrapMode: Text.WordWrap
                font.pixelSize: 12
            }

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: 40

                TextField {
                    id: currentPasswordField
                    anchors.fill: parent
                    rightPadding: 40
                    placeholderText: "현재 비밀번호"
                    placeholderTextColor: root.theme ? "#d1d5db" : "#6b7280"
                    echoMode: root.currentPasswordVisible ? TextInput.Normal : TextInput.Password
                    color: root.theme ? root.theme.textPrimary : "white"
                    background: Rectangle {
                        color: root.theme ? root.theme.bgComponent : "#18181b"
                        border.color: currentPasswordField.activeFocus
                                      ? (root.theme ? root.theme.accent : "#f97316")
                                      : (root.theme ? root.theme.border : "#27272a")
                        radius: 6
                    }
                    onTextEdited: {
                        if (backendObject)
                            backendObject.resetSessionTimer()
                        root.statusText = ""
                    }
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    onClicked: root.currentPasswordVisible = !root.currentPasswordVisible
                    contentItem: Item {
                        Text {
                            anchors.centerIn: parent
                            text: "\uE890"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 16
                            color: root.theme ? root.theme.textSecondary : "#d4d4d8"
                        }
                        Rectangle {
                            visible: !root.currentPasswordVisible
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
                    onTextEdited: {
                        if (backendObject)
                            backendObject.resetSessionTimer()
                        root.statusText = ""
                    }
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
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
                color: root.theme ? root.theme.textSecondary : "#a1a1aa"
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
                text: root.busy ? "변경 중..." : "비밀번호 변경"
                Layout.preferredWidth: 132
                Layout.preferredHeight: 34
                enabled: !root.busy
                onClicked: {
                    if (!backendObject) {
                        root.statusText = "Backend is not available."
                        return
                    }
                    if (currentPasswordField.text.length === 0) {
                        root.statusText = "현재 비밀번호를 입력해 주세요."
                        currentPasswordField.forceActiveFocus()
                        return
                    }
                    if (newPasswordField.text.length === 0) {
                        root.statusText = "새 비밀번호를 입력해 주세요."
                        newPasswordField.forceActiveFocus()
                        return
                    }
                    if (currentPasswordField.text === newPasswordField.text) {
                        root.statusText = "새 비밀번호는 현재 비밀번호와 달라야 합니다."
                        newPasswordField.forceActiveFocus()
                        return
                    }

                    const complexityError = root.validateNewPasswordComplexity(newPasswordField.text)
                    if (complexityError.length > 0) {
                        root.statusText = complexityError
                        newPasswordField.forceActiveFocus()
                        return
                    }

                    backendObject.resetSessionTimer()
                    root.statusText = ""
                    root.busy = true
                    backendObject.changePassword(currentPasswordField.text, newPasswordField.text)
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
