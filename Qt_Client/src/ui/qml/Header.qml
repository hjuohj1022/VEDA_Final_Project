import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Team3VideoReceiver 1.0

Rectangle {
    id: root
    property var theme
    property bool isDarkMode: true
    property bool isLoggedIn: false
    property int currentSection: 0
    property int sessionRemainingSeconds: 0
    signal toggleTheme()
    signal requestLogin()
    signal requestLogout()
    signal requestHome()
    signal requestRtspSettings()

    function formatSession(seconds) {
        var s = Math.max(0, seconds)
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" + m : "" + m) + ":" + (r < 10 ? "0" + r : "" + r)
    }

    function screenGuideText() {
        if (currentSection === 0) {
            return "View 화면 안내\n" +
                   "• 라이브 화면 모니터링 화면\n" +
                   "• 더블 클릭 시 단일 카메라 확대 전환\n" +
                   "• 확대 상태에서 3D map 모드 전환 가능\n" +
                   "• 확대 상태에서 줌/포커스 제어 사용 가능"
        }
        if (currentSection === 2) {
            return "Playback 화면 안내\n" +
                   "• 날짜/시간/채널 지정 후 녹화 재생\n" +
                   "• 타임라인에서 녹화 구간 선택 및 이동\n" +
                   "• 새로고침으로 녹화 구간 정보 갱신\n" +
                   "• 내보내기로 선택 구간 영상 저장 가능"
        }
        if (currentSection === 1) {
            return "Export 화면 안내\n" +
                   "• 서버에 시간별 저장된 녹화 영상 목록 조회\n" +
                   "• 원하는 파일 다운로드 및 로컬 저장 가능"
        }
        return "화면 안내"
    }

    color: theme ? theme.bgSecondary : "#09090b"

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme ? theme.border : "#27272a"
        opacity: 0.9
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 14

        RowLayout {
            spacing: 10

            Rectangle {
                id: homeBtn
                width: 34
                height: 34
                radius: 10
                color: homeMouse.pressed ? "#ea580c" : (theme ? theme.accent : "#f97316")
                scale: homeMouse.pressed ? 0.96 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    radius: 5
                    color: "white"
                }

                MouseArea {
                    id: homeMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        backend.resetSessionTimer()
                        root.requestHome()
                    }
                }

                ToolTip.visible: homeMouse.containsMouse
                ToolTip.text: "대시보드 홈"
                ToolTip.delay: 350
                ToolTip.timeout: 2000
            }

            Column {
                Layout.alignment: Qt.AlignVCenter
                spacing: 1

                Text {
                    text: "Vision VMS"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 32/2
                }

                Text {
                    text: "AI Surveillance"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 250
            Layout.preferredHeight: 34
            Layout.leftMargin: 8
            radius: 9
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: "O"
                    color: theme ? theme.accent : "#f97316"
                    font.bold: true
                    font.pixelSize: 12
                }

                Text {
                    text: "Search..."
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
        }

        Item { Layout.fillWidth: true }

        RowLayout {
            spacing: 6

            IconButton {
                theme: root.theme
                iconSource: root.isDarkMode
                            ? "qrc:/qt/qml/Team3VideoReceiver/icons/tooltip_dark.svg"
                            : "qrc:/qt/qml/Team3VideoReceiver/icons/tooltip_light.svg"
                iconSize: 16
                iconBackdropColor: "transparent"
                iconBackdropBorderColor: "transparent"
                tooltipText: root.screenGuideText()
            }

            IconButton {
                theme: root.theme
                label: "\uE7F4"
                fg: theme ? theme.textSecondary : "#a1a1aa"
                tooltipText: "이벤트 알림 (미구현)"
            }

            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: theme ? theme.accent : "#f97316"
                Layout.alignment: Qt.AlignVCenter
                Layout.leftMargin: -10
                Layout.rightMargin: 6
            }

            IconButton {
                theme: root.theme
                label: "\uE713"
                fg: theme ? theme.textSecondary : "#a1a1aa"
                enabledButton: root.isLoggedIn
                tooltipText: "RTSP 설정"
                onClicked: root.requestRtspSettings()
            }

            IconButton {
                theme: root.theme
                label: root.isDarkMode ? "\uE708" : "\uE706"
                fg: theme ? theme.accent : "#f97316"
                tooltipText: root.isDarkMode ? "라이트 모드" : "다크 모드"
                onClicked: root.toggleTheme()
            }

            Rectangle {
                Layout.preferredHeight: 34
                Layout.preferredWidth: 96
                visible: root.isLoggedIn
                radius: 10
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: formatSession(root.sessionRemainingSeconds)
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 12
                }
            }

            Rectangle {
                id: authBtn
                Layout.preferredHeight: 34
                Layout.preferredWidth: 92
                radius: 10
                color: authMouse.pressed
                       ? (theme ? theme.border : "#27272a")
                       : (theme ? theme.bgComponent : "#18181b")
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                scale: authMouse.pressed ? 0.97 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 8

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        color: theme ? theme.accent : "#f97316"

                        Text {
                            anchors.centerIn: parent
                            text: "\uE77B"
                            color: "white"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: root.isLoggedIn ? "Logout" : "Login"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: authMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        backend.resetSessionTimer()
                        if (root.isLoggedIn) {
                            root.requestLogout()
                        } else {
                            root.requestLogin()
                        }
                    }
                }
            }
        }
    }
}
