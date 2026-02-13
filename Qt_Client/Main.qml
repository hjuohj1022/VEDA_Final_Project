import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    minimumWidth: 1280
    minimumHeight: 720
    maximumWidth: 1280
    maximumHeight: 720
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    property bool isDarkMode: true
    property alias appTheme: theme
    property real lastMoveX: -1
    property real lastMoveY: -1
    property double lastMoveResetMs: 0
    property string rtspConfigError: ""

    function resetSessionFromMove(x, y) {
        var now = Date.now()
        if (lastMoveX < 0 || lastMoveY < 0) {
            lastMoveX = x
            lastMoveY = y
            backend.resetSessionTimer()
            lastMoveResetMs = now
            return
        }

        var dx = x - lastMoveX
        var dy = y - lastMoveY
        var moved = Math.abs(dx) + Math.abs(dy)
        if (moved >= 2 && (now - lastMoveResetMs) >= 700) {
            backend.resetSessionTimer()
            lastMoveX = x
            lastMoveY = y
            lastMoveResetMs = now
        }
    }

    QtObject {
        id: theme
        property color bgPrimary: window.isDarkMode ? "#000000" : "#f4f4f5"
        property color bgSecondary: window.isDarkMode ? "#09090b" : "#ffffff"
        property color bgComponent: window.isDarkMode ? "#18181b" : "#ffffff"
        property color border: window.isDarkMode ? "#27272a" : "#e4e4e7"
        property color textPrimary: window.isDarkMode ? "#ffffff" : "#09090b"
        property color textSecondary: window.isDarkMode ? "#a1a1aa" : "#71717a"
        property color accent: "#f97316"
    }

    title: "Vision VMS"
    color: theme.bgPrimary
    
    ColumnLayout {
        // 전체 레이아웃(타이틀바 + 헤더 + 본문)
        anchors.fill: parent
        spacing: 0

        // 커스텀 타이틀바
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            color: theme.bgComponent
            border.color: theme.border
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 6
                spacing: 8

                Text {
                    text: "Vision VMS"
                    color: theme.textPrimary
                    font.pixelSize: 12
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    id: minBtn
                    width: 28
                    height: 22
                    radius: 4
                    color: minMouse.pressed
                           ? (window.isDarkMode ? "#3f3f46" : "#d4d4d8")
                           : (minMouse.containsMouse ? (window.isDarkMode ? "#27272a" : "#e4e4e7") : "transparent")
                    scale: minMouse.pressed ? 0.95 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                    Text {
                        anchors.centerIn: parent
                        text: "-"
                        color: theme.textSecondary
                        font.pixelSize: 14
                    }

                    MouseArea {
                        id: minMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.showMinimized()
                    }
                }

                Rectangle {
                    id: closeBtn
                    width: 28
                    height: 22
                    radius: 4
                    color: closeMouse.pressed ? "#b91c1c" : (closeMouse.containsMouse ? "#dc2626" : "transparent")
                    scale: closeMouse.pressed ? 0.95 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                    Text {
                        anchors.centerIn: parent
                        text: "x"
                        color: closeMouse.containsMouse ? "white" : theme.textSecondary
                        font.pixelSize: 12
                        font.bold: true
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: window.close()
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                onPressed: (mouse) => {
                    if (mouse.button === Qt.LeftButton) window.startSystemMove()
                }
                z: -1
            }
        }
        // 상단 헤더
        Header {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            z: 10
            theme: window.appTheme
            isDarkMode: window.isDarkMode
            isLoggedIn: backend.isLoggedIn
            sessionRemainingSeconds: backend.sessionRemainingSeconds
            onToggleTheme: window.isDarkMode = !window.isDarkMode
            onRequestLogin: stackLayout.currentIndex = 1
            onRequestLogout: {
                backend.logout()
                stackLayout.currentIndex = 1
            }
            onRequestHome: stackLayout.currentIndex = backend.isLoggedIn ? 0 : 1
            onRequestRtspSettings: {
                rtspIpField.text = ""
                rtspPortField.text = ""
                rtspConfigError = ""
                rtspSettingsPopup.open()
            }
        }
        // 메인 컨텐츠(중앙 화면 + 우측 사이드바)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    
                    Rectangle {
                        anchors.fill: parent
                        color: theme.bgSecondary
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 8

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                visible: backend.isLoggedIn
                                
                                Text {
                                    text: "Live Monitoring"
                                    color: theme.textPrimary
                                    font.pixelSize: 18
                                    font.bold: true
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                RowLayout {
                                    spacing: 8
                                    
                                    Rectangle {
                                        id: gridBtn
                                        width: 50; height: 28
                                        color: gridMouse.pressed
                                               ? "#ea580c"
                                               : (!backend.isLoggedIn
                                               ? (window.isDarkMode ? "#27272a" : "#e4e4e7")
                                               : (stackLayout.currentIndex === 0 ? theme.accent : theme.bgComponent))
                                        border.color: theme.border
                                        border.width: stackLayout.currentIndex === 0 ? 0 : 1
                                        radius: 6
                                        scale: gridMouse.pressed ? 0.97 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                                        
                                        Text { 
                                            anchors.centerIn: parent
                                            text: "Grid"
                                            color: !backend.isLoggedIn
                                                   ? (window.isDarkMode ? "#71717a" : "#a1a1aa")
                                                   : (stackLayout.currentIndex === 0 ? "white" : theme.textSecondary)
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                        
                                        MouseArea {
                                            id: gridMouse
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: backend.isLoggedIn
                                            onClicked: {
                                                backend.resetSessionTimer()
                                                stackLayout.currentIndex = 0
                                            }
                                        }
                                    }

                                    Rectangle {
                                        id: exportBtn
                                        width: 60; height: 28
                                        color: exportMouseArea.pressed
                                               ? "#ea580c"
                                               : (!backend.isLoggedIn
                                               ? theme.accent
                                               : (stackLayout.currentIndex === 1 ? theme.accent : theme.bgComponent))
                                        radius: 6
                                        border.color: theme.border
                                        border.width: stackLayout.currentIndex === 1 ? 0 : 1
                                        scale: exportMouseArea.pressed ? 0.97 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: "Export"
                                            color: stackLayout.currentIndex === 1 ? "white" : theme.textSecondary
                                            font.bold: true
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            id: exportMouseArea
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                backend.resetSessionTimer()
                                                if (backend.isLoggedIn) {
                                                    stackLayout.currentIndex = 1
                                                } else {
                                                    stackLayout.currentIndex = 1
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            StackLayout {
                                id: stackLayout
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: backend.isLoggedIn ? 0 : 1

                                VideoGrid {
                                    id: videoGrid
                                    theme: window.appTheme
                                    isActive: stackLayout.currentIndex === 0
                                }

                                LoginScreen {
                                    id: loginScreen
                                    theme: window.appTheme
                                }
                            }
                        }
                    }
                }
            }

            Sidebar {
                Layout.preferredWidth: backend.isLoggedIn ? 256 : 0
                Layout.fillHeight: true
                theme: window.appTheme
                visible: backend.isLoggedIn
            }
        }
    }

    TapHandler {
        acceptedButtons: Qt.AllButtons
        onTapped: backend.resetSessionTimer()
    }

    HoverHandler {
        onPointChanged: {
            var p = point.position
            resetSessionFromMove(p.x, p.y)
        }
    }

    WheelHandler {
        onWheel: backend.resetSessionTimer()
    }

    Keys.onPressed: backend.resetSessionTimer()

    Connections {
        target: backend
        function onIsLoggedInChanged() {
            stackLayout.currentIndex = backend.isLoggedIn ? 0 : 1
        }
        function onSessionExpired() {
            stackLayout.currentIndex = 1
        }
    }

    Popup {
        id: rtspSettingsPopup
        parent: Overlay.overlay
        width: 380
        height: 230
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        z: 9999

        onOpened: {
            rtspIpField.forceActiveFocus()
            rtspIpField.selectAll()
        }

        background: Rectangle {
            color: theme.bgComponent
            border.color: theme.border
            border.width: 1
            radius: 10
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Text {
                text: "RTSP IP 설정"
                color: theme.textPrimary
                font.bold: true
                font.pixelSize: 15
            }

            TextField {
                id: rtspIpField
                Layout.fillWidth: true
                placeholderText: "IP 입력"
                color: theme.textPrimary
                background: Rectangle {
                    color: theme.bgSecondary
                    border.color: rtspIpField.activeFocus ? theme.accent : theme.border
                    radius: 6
                }
            }

            TextField {
                id: rtspPortField
                Layout.fillWidth: true
                placeholderText: "포트 입력"
                inputMethodHints: Qt.ImhDigitsOnly
                color: theme.textPrimary
                background: Rectangle {
                    color: theme.bgSecondary
                    border.color: rtspPortField.activeFocus ? theme.accent : theme.border
                    radius: 6
                }
                onAccepted: saveRtspButton.clicked()
            }

            Text {
                Layout.fillWidth: true
                visible: rtspConfigError.length > 0
                text: rtspConfigError
                color: "#ef4444"
                font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }

                Button {
                    text: "취소"
                    Layout.preferredWidth: 96
                    onClicked: rtspSettingsPopup.close()
                    background: Rectangle {
                        color: parent.down ? theme.border : "transparent"
                        border.color: theme.border
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    id: saveRtspButton
                    text: "저장"
                    Layout.preferredWidth: 96
                    onClicked: {
                        if (backend.updateRtspConfig(rtspIpField.text, rtspPortField.text)) {
                            rtspConfigError = ""
                            rtspSettingsPopup.close()
                        } else {
                            rtspConfigError = "IP/포트를 확인해 주세요. (포트 1~65535)"
                        }
                    }
                    background: Rectangle {
                        color: parent.down ? "#ea580c" : theme.accent
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





