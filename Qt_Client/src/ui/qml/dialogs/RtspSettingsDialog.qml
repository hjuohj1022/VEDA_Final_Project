import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    property var theme
    property var hostWindow

    transientParent: hostWindow
    width: 380
    height: rtspAdvancedToggle.checked ? 330 : 230
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    // 다이얼로그 표시 준비 함수
    function prepareAndShow() {
        if (hostWindow) {
            hostWindow.stopRtspConnectCheck()
            hostWindow.rtspConfigError = ""
            hostWindow.rtspConfigIsError = false
        }
        rtspIpField.text = ""
        rtspPortField.text = ""
        rtspAdvancedToggle.checked = false
        rtspUserField.text = ""
        rtspPassField.text = ""
        visible = true
    }
    // 다이얼로그 닫기 함수
    function closeDialog() {
        visible = false
    }
    // 가시성 변경 처리 함수
    onVisibleChanged: {
        if (!visible) {
            if (hostWindow)
                hostWindow.stopRtspConnectCheck()
            return
        }
        backend.resetSessionTimer()
        if (hostWindow) {
            x = hostWindow.x + (hostWindow.width - width) / 2
            y = hostWindow.y + (hostWindow.height - height) / 2
        }
        rtspIpField.forceActiveFocus()
        rtspIpField.selectAll()
    }

    TapHandler {
        acceptedButtons: Qt.AllButtons
        // 탭 입력 처리 함수
        onTapped: backend.resetSessionTimer()
    }

    HoverHandler {
        // 포인트 변경 처리 함수
        onPointChanged: {
            if (!hostWindow)
                return
            var p = point.position
            hostWindow.resetSessionFromMove(p.x, p.y)
        }
    }

    WheelHandler {
        // 휠 입력 처리 함수
        onWheel: (event) => {
            backend.resetSessionTimer()
            event.accepted = false
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgComponent : "#18181b"
        border.color: theme ? theme.border : "#27272a"
        border.width: 1
        radius: 10

        Keys.onPressed: backend.resetSessionTimer()

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.AllButtons
            propagateComposedEvents: true
            // 누름 이벤트 처리 함수
            onPressed: (mouse) => {
                backend.resetSessionTimer()
                mouse.accepted = false
            }
            // 위치 변경 처리 함수
            onPositionChanged: (mouse) => {
                backend.resetSessionTimer()
                mouse.accepted = false
            }
            // 휠 입력 처리 함수
            onWheel: (wheel) => {
                backend.resetSessionTimer()
                wheel.accepted = false
            }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Text {
                text: "RTSP IP 설정"
                color: theme ? theme.textPrimary : "white"
                font.bold: true
                font.pixelSize: 15
            }

            TextField {
                id: rtspIpField
                Layout.fillWidth: true
                placeholderText: "IP 입력"
                color: theme ? theme.textPrimary : "white"
                placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                // 텍스트 편집 처리 함수
                onTextEdited: {
                    backend.resetSessionTimer()
                    if (!hostWindow)
                        return
                    var sanitized = hostWindow.sanitizeIpv4Input(text)
                    if (sanitized !== text) {
                        text = sanitized
                        cursorPosition = text.length
                    }
                }
                background: Rectangle {
                    color: theme ? theme.bgSecondary : "#09090b"
                    border.color: rtspIpField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                    radius: 6
                }
            }

            TextField {
                id: rtspPortField
                Layout.fillWidth: true
                placeholderText: "포트 입력"
                inputMethodHints: Qt.ImhDigitsOnly
                color: theme ? theme.textPrimary : "white"
                placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                // 텍스트 편집 처리 함수
                onTextEdited: {
                    backend.resetSessionTimer()
                    if (!hostWindow)
                        return
                    var sanitized = hostWindow.sanitizePortInput(text)
                    if (sanitized !== text) {
                        text = sanitized
                        cursorPosition = text.length
                    }
                }
                background: Rectangle {
                    color: theme ? theme.bgSecondary : "#09090b"
                    border.color: rtspPortField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                    radius: 6
                }
                // 입력 확정 처리 함수
                onAccepted: {
                    backend.resetSessionTimer()
                    saveRtspButton.clicked()
                }
            }

            CheckBox {
                id: rtspAdvancedToggle
                Layout.fillWidth: true
                text: "고급 설정 (RTSP 계정 직접 입력)"
                checked: false
                enabled: hostWindow ? !hostWindow.rtspConnecting : true
                // 토글 처리 함수
                onToggled: {
                    backend.resetSessionTimer()
                    if (!checked) {
                        rtspUserField.text = ""
                        rtspPassField.text = ""
                    }
                }
                indicator: Rectangle {
                    implicitWidth: 14
                    implicitHeight: 14
                    radius: 3
                    border.color: rtspAdvancedToggle.checked ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                    color: rtspAdvancedToggle.checked ? (theme ? theme.accent : "#f97316") : "transparent"
                }
                contentItem: Text {
                    text: rtspAdvancedToggle.text
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    font.pixelSize: 12
                    leftPadding: 20
                    verticalAlignment: Text.AlignVCenter
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: rtspAdvancedToggle.checked

                TextField {
                    id: rtspUserField
                    Layout.fillWidth: true
                    placeholderText: "RTSP 아이디"
                    color: theme ? theme.textPrimary : "white"
                    placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: rtspUserField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                    // 텍스트 편집 처리 함수
                    onTextEdited: backend.resetSessionTimer()
                }

                TextField {
                    id: rtspPassField
                    Layout.fillWidth: true
                    placeholderText: "RTSP 비밀번호"
                    echoMode: TextInput.Password
                    color: theme ? theme.textPrimary : "white"
                    placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: rtspPassField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                    // 텍스트 편집 처리 함수
                    onTextEdited: backend.resetSessionTimer()
                    // 입력 확정 처리 함수
                    onAccepted: {
                        backend.resetSessionTimer()
                        if (!(hostWindow && hostWindow.rtspConnecting))
                            saveRtspButton.clicked()
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: hostWindow ? hostWindow.rtspConfigError.length > 0 : false
                text: hostWindow ? hostWindow.rtspConfigError : ""
                color: (hostWindow && hostWindow.rtspConfigIsError) ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                Button {
                    text: "초기화"
                    Layout.preferredWidth: 96
                    enabled: hostWindow ? !hostWindow.rtspConnecting : true
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        var changed = backend.resetRtspConfigToEnv()
                        rtspIpField.text = backend.rtspIp
                        rtspPortField.text = backend.rtspPort
                        rtspAdvancedToggle.checked = false
                        rtspUserField.text = ""
                        rtspPassField.text = ""
                        if (changed && hostWindow) {
                            hostWindow.startRtspConnectCheck(true)
                        } else if (hostWindow) {
                            hostWindow.rtspConfigIsError = false
                            hostWindow.rtspConfigError = "이미 기본 RTSP 설정입니다."
                        }
                    }
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                        border.color: theme ? theme.border : "#27272a"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: "취소"
                    Layout.preferredWidth: 96
                    enabled: true
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        if (!hostWindow) {
                            root.closeDialog()
                            return
                        }
                        if (hostWindow.rtspConnecting) {
                            hostWindow.stopRtspConnectCheck()
                            hostWindow.rtspConfigIsError = false
                            hostWindow.rtspConfigError = "연결을 취소했습니다."
                        } else {
                            hostWindow.stopRtspConnectCheck()
                            hostWindow.rtspConfigError = ""
                            hostWindow.rtspConfigIsError = false
                            root.closeDialog()
                        }
                    }
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                        border.color: theme ? theme.border : "#27272a"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    id: saveRtspButton
                    text: (hostWindow && hostWindow.rtspConnecting) ? "연결 시도 중..." : "연결"
                    Layout.preferredWidth: 96
                    enabled: hostWindow ? !hostWindow.rtspConnecting : true
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        if (!hostWindow)
                            return

                        var ip = rtspIpField.text.trim()
                        var portText = rtspPortField.text.trim()
                        var useCustomAuth = rtspAdvancedToggle.checked
                        var authUser = rtspUserField.text.trim()
                        var authPass = rtspPassField.text

                        if (ip.length === 0) {
                            hostWindow.rtspConfigIsError = true
                            hostWindow.rtspConfigError = "IP를 입력해 주세요."
                            return
                        }

                        if (!hostWindow.isValidIpv4(ip)) {
                            hostWindow.rtspConfigIsError = true
                            hostWindow.rtspConfigError = "IP 형식이 올바르지 않습니다. (예: 192.168.55.203)"
                            return
                        }

                        if (!hostWindow.isPrivateIpv4(ip)) {
                            hostWindow.rtspConfigIsError = true
                            hostWindow.rtspConfigError = "사설망 IP만 허용됩니다. (10.x / 172.16~31.x / 192.168.x)"
                            return
                        }

                        if (portText.length > 0) {
                            var portNum = parseInt(portText, 10)
                            if (isNaN(portNum) || portNum < 1 || portNum > 65535) {
                                hostWindow.rtspConfigIsError = true
                                hostWindow.rtspConfigError = "포트는 1~65535 범위로 입력해 주세요."
                                return
                            }
                        }

                        if (useCustomAuth && authUser.length === 0) {
                            hostWindow.rtspConfigIsError = true
                            hostWindow.rtspConfigError = "고급 설정 사용 시 RTSP 아이디를 입력해 주세요."
                            return
                        }

                        hostWindow.startRtspProbe(ip, portText, useCustomAuth, authUser, authPass)
                    }
                    background: Rectangle {
                        color: parent.down ? "#ea580c" : (theme ? theme.accent : "#f97316")
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
    }
}
