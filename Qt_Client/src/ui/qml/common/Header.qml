import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

Rectangle {
    id: root
    property var theme
    property bool isDarkMode: true
    property bool isLoggedIn: false
    property bool twoFactorEnabled: false
    property string userId: ""
    property int currentSection: 0
    property int sessionRemainingSeconds: 0
    property bool exportProgressVisible: false
    property int exportProgressPercent: 0
    property string exportProgressText: ""
    property bool eventAlertActive: false
    property bool eventAlertUnread: false
    signal toggleTheme()
    signal requestLogin()
    signal requestLogout()
    signal requestTwoFactorSetup()
    signal requestTwoFactorDisable()
    signal requestAccountDelete()
    signal requestPasswordChange()
    signal requestHome()
    signal requestRtspSettings()
    signal requestEventAlert()
    signal requestExportCancel()
    // 세션 시간 포맷 함수
    function formatSession(seconds) {
        var s = Math.max(0, seconds)
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" + m : "" + m) + ":" + (r < 10 ? "0" + r : "" + r)
    }
    // 사용자 이름 표시 함수
    function displayUserName() {
        var raw = (root.userId || "").trim()
        if (raw.length === 0)
            return "Guest"
        return raw
    }
    // 사용자 이니셜 표시 함수
    function displayUserInitial() {
        var name = displayUserName()
        return name.length > 0 ? name.charAt(0).toUpperCase() : "G"
    }
    // 계정 메뉴 X 위치 제한 함수
    function clampAccountMenuX(value, menuWidth) {
        return Math.max(12, Math.min(root.width - menuWidth - 12, value))
    }
    // 계정 메뉴 Y 위치 보정 함수
    function clampAccountMenuY(value, menuHeight) {
        return Math.max(8, value)
    }
    // 화면 안내 문구 생성 함수
    function screenGuideText() {
        if (currentSection === 0) {
            return "View 화면 안내\n" +
                   "• 라이브 화면 모니터링 화면\n" +
                   "• 더블 클릭 시 단일 카메라 확대 전환"
        }
        if (currentSection === 3) {
            return "확대 화면 안내\n" +
                   "• 3D map 모드 전환 가능\n" +
                   "• 줌/포커스 제어 사용 가능\n" +
                   "• 표시(대비/밝기/윤곽 조정/컬러 레벨) 제어 가능"
        }
        if (currentSection === 2) {
            return "Playback 화면 안내\n" +
                   "• 날짜/시간/채널 지정 후 녹화 재생\n" +
                   "• 타임라인에서 녹화 구간 선택 및 이동\n" +
                   "• 새로고침으로 녹화 구간 정보 갱신\n" +
                   "• 내보내기로 선택 구간 영상 저장 가능"
        }
        if (currentSection === 1) {
            return "Thermal 화면 안내\n" +
                   "• 열화상 스트림 모니터링 화면\n" +
                   "• 컬러 팔레트 및 온도 범위(자동/수동) 조정 가능"
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        root.requestHome()
                    }
                }

                ToolTip.visible: homeMouse.containsMouse
                ToolTip.text: "클라이언트 사양"
                ToolTip.delay: 350
                ToolTip.timeout: 2000
            }

            Column {
                Layout.alignment: Qt.AlignVCenter
                spacing: 1

                Text {
                    text: "AEGIS Vision VMS"
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
            Layout.preferredWidth: exportProgressVisible ? 290 : 0
            Layout.preferredHeight: 34
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: exportProgressVisible ? 6 : 0
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: exportProgressVisible ? 1 : 0
            radius: 8
            visible: exportProgressVisible

            Column {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 58
                anchors.topMargin: 5
                anchors.bottomMargin: 5
                spacing: 4

                Row {
                    width: parent.width
                    spacing: 6

                    Text {
                        text: "Export"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Text {
                        text: Math.max(0, Math.min(100, exportProgressPercent)) + "%"
                        color: theme ? theme.textPrimary : "#ffffff"
                        font.pixelSize: 10
                        font.bold: true
                    }

                    Text {
                        width: parent.width - 74
                        text: exportProgressText
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }
                }

                Rectangle {
                    width: parent.width
                    height: 5
                    radius: 3
                    color: theme && root.isDarkMode ? "#27272a" : "#e5e7eb"

                    Rectangle {
                        width: Math.max(0, Math.min(100, exportProgressPercent)) / 100 * parent.width
                        height: parent.height
                        radius: parent.radius
                        color: theme ? theme.accent : "#f97316"
                    }
                }
            }

            Rectangle {
                width: 44
                height: 22
                radius: 6
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                color: cancelExportMouse.pressed
                       ? "#dc2626"
                       : (cancelExportMouse.containsMouse
                          ? "#b91c1c"
                          : (theme && root.isDarkMode ? "#3f3f46" : "#e4e4e7"))
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                scale: cancelExportMouse.pressed ? 0.96 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                Text {
                    anchors.centerIn: parent
                    text: "취소"
                    color: (cancelExportMouse.containsMouse || cancelExportMouse.pressed)
                           ? "white"
                           : (theme ? theme.textPrimary : "#ffffff")
                    font.pixelSize: 10
                    font.bold: true
                }

                MouseArea {
                    id: cancelExportMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // 클릭 이벤트 처리 함수
                    onClicked: root.requestExportCancel()
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
                visible: root.isLoggedIn
            }

            Item {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                visible: root.isLoggedIn

                Rectangle {
                    id: eventAlertPulse
                    anchors.centerIn: parent
                    width: 32
                    height: 32
                    radius: 9
                    color: "transparent"
                    border.width: 1
                    border.color: theme ? theme.accent : "#f97316"
                    opacity: 0.0
                    scale: 1.0
                    visible: root.eventAlertUnread
                }

                SequentialAnimation {
                    id: eventAlertPulseAnim
                    running: root.isLoggedIn && root.eventAlertUnread
                    loops: Animation.Infinite

                    ParallelAnimation {
                        NumberAnimation {
                            target: eventAlertPulse
                            property: "opacity"
                            from: 0.15
                            to: 0.78
                            duration: 420
                            easing.type: Easing.OutCubic
                        }
                        NumberAnimation {
                            target: eventAlertPulse
                            property: "scale"
                            from: 1.0
                            to: 1.18
                            duration: 420
                            easing.type: Easing.OutCubic
                        }
                    }

                    ParallelAnimation {
                        NumberAnimation {
                            target: eventAlertPulse
                            property: "opacity"
                            from: 0.78
                            to: 0.0
                            duration: 520
                            easing.type: Easing.OutCubic
                        }
                        NumberAnimation {
                            target: eventAlertPulse
                            property: "scale"
                            from: 1.18
                            to: 1.28
                            duration: 520
                            easing.type: Easing.OutCubic
                        }
                    }
                    // 실행 상태 변경 처리 함수
                    onRunningChanged: {
                        if (!running) {
                            eventAlertPulse.opacity = 0.0
                            eventAlertPulse.scale = 1.0
                        }
                    }
                }

                IconButton {
                    id: eventAlertButton
                    anchors.centerIn: parent
                    theme: root.theme
                    label: "\uE7F4"
                    fg: root.eventAlertUnread
                        ? (theme ? theme.accent : "#f97316")
                        : (theme ? theme.textSecondary : "#a1a1aa")
                    enabledButton: root.isLoggedIn
                    tooltipText: root.eventAlertUnread
                                 ? "새 이벤트 알림"
                                 : (root.eventAlertActive ? "이벤트 알림 보기" : "이벤트 알림")
                    // 클릭 이벤트 처리 함수
                    onClicked: root.requestEventAlert()

                    Rectangle {
                        width: 10
                        height: 10
                        radius: 5
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.rightMargin: 2
                        anchors.topMargin: 2
                        color: "#ef4444"
                        border.color: theme ? theme.bgSecondary : "#09090b"
                        border.width: 2
                        visible: root.eventAlertUnread
                    }
                }
            }

            IconButton {
                theme: root.theme
                label: "\uE713"
                fg: theme ? theme.textSecondary : "#a1a1aa"
                enabledButton: root.isLoggedIn
                tooltipText: "RTSP 설정"
                // 클릭 이벤트 처리 함수
                onClicked: root.requestRtspSettings()
            }

            IconButton {
                theme: root.theme
                label: root.isDarkMode ? "\uE708" : "\uE706"
                fg: theme ? theme.accent : "#f97316"
                tooltipText: root.isDarkMode ? "라이트 모드" : "다크 모드"
                // 클릭 이벤트 처리 함수
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
                Layout.preferredWidth: root.isLoggedIn ? 214 : 104
                radius: 12
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
                        width: 26
                        height: 26
                        radius: 13
                        color: theme ? theme.accent : "#f97316"

                        Text {
                            anchors.centerIn: parent
                            text: root.isLoggedIn ? displayUserInitial() : "\uE77B"
                            color: "white"
                            font.family: root.isLoggedIn ? Qt.application.font.family : "Segoe MDL2 Assets"
                            font.bold: root.isLoggedIn
                            font.pixelSize: root.isLoggedIn ? 11 : 10
                        }
                    }

                    Column {
                        visible: root.isLoggedIn
                        Layout.fillWidth: true
                        spacing: 1

                        Text {
                            text: displayUserName()
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                            font.pixelSize: 12
                            elide: Text.ElideRight
                            width: parent.width
                        }

                        Text {
                            text: "Secure account"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 9
                            elide: Text.ElideRight
                            width: parent.width
                        }
                    }

                    Text {
                        visible: !root.isLoggedIn
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: "로그인"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    Rectangle {
                        visible: root.isLoggedIn
                        width: 18
                        height: 18
                        radius: 9
                        color: authMouse.containsMouse
                               ? (theme ? theme.bgSecondary : "#0f172a")
                               : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: "\uE70D"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 9
                        }
                    }
                }

                MouseArea {
                    id: authMouse
                    property bool menuWasOpenOnPress: false
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // 누름 시 메뉴 열림 상태 기록 함수
                    onPressed: {
                        menuWasOpenOnPress = accountMenu.opened
                    }
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        if (root.isLoggedIn) {
                            if (menuWasOpenOnPress) {
                                accountMenu.close()
                            } else {
                                backend.refreshTwoFactorStatus()
                                accountMenu.open()
                            }
                            menuWasOpenOnPress = false
                        } else {
                            root.requestLogin()
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: accountMenu
        parent: root
        modal: false
        focus: true
        width: 286
        padding: 0
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnReleaseOutside
        x: {
            var point = authBtn.mapToItem(root, authBtn.width - width, authBtn.height + 10)
            return clampAccountMenuX(point.x, width)
        }
        y: {
            var point = authBtn.mapToItem(root, 0, authBtn.height + 10)
            return clampAccountMenuY(point.y, implicitHeight)
        }

        background: Rectangle {
            radius: 18
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
        }

        contentItem: Column {
            width: accountMenu.width
            spacing: 12

            Item {
                width: parent.width
                height: 10
            }

            Rectangle {
                width: parent.width - 22
                height: 94
                x: (parent.width - width) / 2
                radius: 18
                color: theme ? theme.bgSecondary : "#151518"
                border.color: theme ? theme.border : "#2a2a30"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.topMargin: 14
                    anchors.bottomMargin: 10
                    spacing: 14

                    Rectangle {
                        width: 46
                        height: 46
                        radius: 23
                        color: theme ? theme.accent : "#f97316"
                        border.color: "#ffffff22"
                        border.width: 1

                        Text {
                            anchors.centerIn: parent
                            text: displayUserInitial()
                            color: "white"
                            font.bold: true
                            font.pixelSize: 18
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 5

                        RowLayout {
                            width: parent.width
                            spacing: 6

                            Text {
                                Layout.fillWidth: true
                                text: displayUserName()
                                color: theme ? theme.textPrimary : "white"
                                font.bold: true
                                font.pixelSize: 15
                                elide: Text.ElideRight
                            }

                            Rectangle {
                                Layout.preferredWidth: 72
                                Layout.preferredHeight: 20
                                radius: 10
                                color: passwordChangeMouse.containsMouse
                                       ? (theme ? theme.bgSecondary : "#0f172a")
                                       : "transparent"
                                border.color: theme ? theme.border : "#374151"
                                border.width: 1

                                Text {
                                    anchors.centerIn: parent
                                    text: "비밀번호 변경"
                                    color: theme ? theme.textSecondary : "#cbd5e1"
                                    font.pixelSize: 9
                                    font.bold: true
                                }

                                MouseArea {
                                    id: passwordChangeMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    // 클릭 이벤트 처리 함수
                                    onClicked: {
                                        accountMenu.close()
                                        root.requestPasswordChange()
                                    }
                                }
                            }
                        }

                        Text {
                            text: "Signed in account"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 11
                        }

                        Rectangle {
                            width: 88
                            height: 22
                            radius: 11
                            color: theme ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.18) : "#22f97316"
                            border.color: theme ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.35) : "#55f97316"
                            border.width: 1

                            RowLayout {
                                anchors.centerIn: parent
                                spacing: 6

                                Rectangle {
                                    Layout.alignment: Qt.AlignVCenter
                                    width: 6
                                    height: 6
                                    radius: 3
                                    color: theme ? theme.accent : "#f97316"
                                }

                                Text {
                                    Layout.alignment: Qt.AlignVCenter
                                    text: "Authenticated"
                                    color: theme ? theme.textPrimary : "white"
                                    font.bold: true
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }
                }

            }

            Rectangle {
                width: parent.width - 30
                x: (parent.width - width) / 2
                height: 1
                color: theme ? theme.border : "#27272a"
                opacity: 0.75
            }

            Rectangle {
                width: parent.width - 28
                height: visible ? 64 : 0
                x: (parent.width - width) / 2
                visible: !root.twoFactorEnabled
                radius: 14
                color: otpCreateMouse.containsMouse ? (theme ? theme.bgSecondary : "#0f172a")
                                                    : Qt.rgba(1, 1, 1, 0.02)
                border.color: otpCreateMouse.containsMouse
                              ? (theme ? theme.accent : "#f97316")
                              : (theme ? theme.border : "#27272a")
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 10
                        color: theme ? Qt.rgba(theme.accent.r, theme.accent.g, theme.accent.b, 0.16) : "#22f97316"

                        Text {
                            anchors.centerIn: parent
                            text: "+"
                            color: theme ? theme.accent : "#f97316"
                            font.bold: true
                            font.pixelSize: 18
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            text: "OTP 생성"
                            color: theme ? theme.textPrimary : "white"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            text: "Authenticator 앱 연동 시작"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: "\uE72A"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.family: "Segoe MDL2 Assets"
                        font.pixelSize: 10
                    }
                }

                MouseArea {
                    id: otpCreateMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        accountMenu.close()
                        root.requestTwoFactorSetup()
                    }
                }
            }

            Rectangle {
                width: parent.width - 28
                height: visible ? 64 : 0
                x: (parent.width - width) / 2
                visible: root.twoFactorEnabled
                radius: 14
                color: otpDeleteMouse.containsMouse ? (theme ? theme.bgSecondary : "#0f172a")
                                                    : Qt.rgba(1, 1, 1, 0.02)
                border.color: otpDeleteMouse.containsMouse
                              ? "#f59e0b"
                              : (theme ? theme.border : "#27272a")
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 12

                    Rectangle {
                        width: 34
                        height: 34
                        radius: 10
                        color: "#22f59e0b"

                        Text {
                            anchors.centerIn: parent
                            text: "\uE74D"
                            color: "#f59e0b"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 13
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 3

                        Text {
                            text: "OTP 삭제"
                            color: theme ? theme.textPrimary : "white"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Text {
                            text: "현재 2단계 인증 비활성화"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: "\uE72A"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.family: "Segoe MDL2 Assets"
                        font.pixelSize: 10
                    }
                }

                MouseArea {
                    id: otpDeleteMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        accountMenu.close()
                        root.requestTwoFactorDisable()
                    }
                }
            }

            Rectangle {
                width: parent.width - 30
                x: (parent.width - width) / 2
                height: 1
                color: theme ? theme.border : "#27272a"
                opacity: 0.75
            }

            Item {
                width: parent.width
                height: 2
            }

            RowLayout {
                width: parent.width - 28
                x: (parent.width - width) / 2
                height: 54
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 14
                    color: logoutMouse.containsMouse ? (theme ? theme.bgSecondary : "#0f172a")
                                                     : Qt.rgba(1, 1, 1, 0.02)
                    border.color: theme ? theme.border : "#27272a"
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            text: "\uE8AC"
                            color: theme ? theme.textPrimary : "white"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 11
                        }

                        Text {
                            text: "로그아웃"
                            color: theme ? theme.textPrimary : "white"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }

                    MouseArea {
                        id: logoutMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // 클릭 이벤트 처리 함수
                        onClicked: {
                            accountMenu.close()
                            root.requestLogout()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 14
                    color: deleteAccountMouse.containsMouse ? "#3f1d1d" : Qt.rgba(251/255, 113/255, 133/255, 0.05)
                    border.color: deleteAccountMouse.containsMouse ? "#fb7185" : "#5b2330"
                    border.width: 1

                    Row {
                        anchors.centerIn: parent
                        spacing: 8

                        Text {
                            text: "\uE74D"
                            color: deleteAccountMouse.containsMouse ? "#fda4af" : "#fb7185"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 11
                        }

                        Text {
                            text: "회원탈퇴"
                            color: deleteAccountMouse.containsMouse ? "#fda4af" : "#fb7185"
                            font.pixelSize: 12
                            font.bold: true
                        }
                    }

                    MouseArea {
                        id: deleteAccountMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // 클릭 이벤트 처리 함수
                        onClicked: {
                            accountMenu.close()
                            root.requestAccountDelete()
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: 14
            }
        }
    }
}
