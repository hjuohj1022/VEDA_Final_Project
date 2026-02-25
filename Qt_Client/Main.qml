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
    property bool rtspConfigIsError: false
    property bool rtspConnecting: false
    property int rtspConnectTimeoutMs: 8000
    property int rtspConnectSuccessStreak: 0
    property int rtspConnectStartedMs: 0
    property int rtspConnectMinCheckMs: 800
    property string pendingRtspIp: ""
    property string pendingRtspPort: ""
    property bool streamPrewarmEnabled: true
    property int inlineMainCameraIndex: -1
    property bool inlineMainViewVisible: false

    function cameraLocationName(index) {
        var names = ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
        if (index < 0 || index >= names.length) return "Camera"
        return names[index]
    }

    function startRtspConnectCheck(resetStats) {
        if (resetStats === undefined)
            resetStats = true
        rtspConnecting = true
        rtspConnectSuccessStreak = 0
        rtspConnectStartedMs = Date.now()
        if (resetStats) {
            backend.activeCameras = 0
            backend.currentFps = 0
        }
        rtspConfigIsError = false
        rtspConfigError = "연결 확인 중..."
        rtspConnectTimeout.restart()
        rtspConnectPoll.start()
    }

    function stopRtspConnectCheck() {
        rtspConnecting = false
        rtspConnectSuccessStreak = 0
        pendingRtspIp = ""
        pendingRtspPort = ""
        rtspConnectPoll.stop()
        rtspConnectTimeout.stop()
    }

    function startRtspProbe(ip, portText) {
        stopRtspConnectCheck()
        pendingRtspIp = ip
        pendingRtspPort = portText
        rtspConnecting = true
        rtspConfigIsError = false
        rtspConfigError = "RTSP 서버 확인 중..."
        backend.probeRtspEndpoint(ip, portText, 1200)
    }

    function isValidIpv4(ip) {
        var parts = ip.trim().split(".")
        if (parts.length !== 4) return false
        for (var i = 0; i < parts.length; i++) {
            if (!/^\d+$/.test(parts[i])) return false
            var v = parseInt(parts[i], 10)
            if (v < 0 || v > 255) return false
        }
        return true
    }

    function isPrivateIpv4(ip) {
        var parts = ip.trim().split(".")
        if (parts.length !== 4) return false
        var a = parseInt(parts[0], 10)
        var b = parseInt(parts[1], 10)
        if (a === 10) return true
        if (a === 172 && b >= 16 && b <= 31) return true
        if (a === 192 && b === 168) return true
        return false
    }

    function sanitizeIpv4Input(raw) {
        var txt = raw.replace(/[^0-9.]/g, "")
        var parts = txt.split(".")
        if (parts.length > 4) {
            parts = parts.slice(0, 4)
        }

        for (var i = 0; i < parts.length; i++) {
            var p = parts[i]
            if (p.length > 3) {
                p = p.slice(0, 3)
            }
            if (p.length > 0) {
                var n = parseInt(p, 10)
                if (!isNaN(n) && n > 255) {
                    p = "255"
                }
            }
            parts[i] = p
        }
        return parts.join(".")
    }

    function sanitizePortInput(raw) {
        var txt = raw.replace(/[^0-9]/g, "")
        if (txt.length === 0) return ""
        if (txt.length > 5) {
            txt = txt.slice(0, 5)
        }
        var n = parseInt(txt, 10)
        if (!isNaN(n) && n > 65535) {
            txt = "65535"
        }
        return txt
    }

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
        focus: true
        Keys.onPressed: backend.resetSessionTimer()

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
                inlineMainViewVisible = false
                inlineMainCameraIndex = -1
                backend.logout()
                stackLayout.currentIndex = 1
            }
            onRequestHome: stackLayout.currentIndex = backend.isLoggedIn ? 0 : 1
            onRequestRtspSettings: {
                stopRtspConnectCheck()
                rtspIpField.text = ""
                rtspPortField.text = ""
                rtspConfigError = ""
                rtspConfigIsError = false
                rtspSettingsPopup.visible = true
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
                                            text: "View"
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
                                currentIndex: backend.isLoggedIn
                                              ? (window.inlineMainViewVisible ? 2 : 0)
                                              : 1

                                VideoGrid {
                                    id: videoGrid
                                    theme: window.appTheme
                                    isActive: backend.isLoggedIn || (window.streamPrewarmEnabled && !backend.isLoggedIn)
                                    visible: stackLayout.currentIndex === 0
                                    onOpenMainViewRequested: function(cameraIndex) {
                                        window.inlineMainCameraIndex = cameraIndex
                                        window.inlineMainViewVisible = true
                                        stackLayout.currentIndex = 2
                                    }
                                }

                                LoginScreen {
                                    id: loginScreen
                                    theme: window.appTheme
                                }

                                Item {
                                    id: inlineMainView
                                    visible: stackLayout.currentIndex === 2

                                    Rectangle {
                                        anchors.fill: parent
                                        color: window.appTheme.bgSecondary

                                        ColumnLayout {
                                            anchors.fill: parent
                                            anchors.margins: 8
                                            spacing: 0

                                            RowLayout {
                                                Layout.fillWidth: true
                                                Layout.fillHeight: true
                                                spacing: 0

                                                VideoPlayer {
                                                    Layout.fillWidth: true
                                                    Layout.fillHeight: true
                                                    theme: window.appTheme
                                                    tileIndex: window.inlineMainCameraIndex
                                                    titleText: window.inlineMainCameraIndex >= 0
                                                               ? ("Cam " + (window.inlineMainCameraIndex + 1) + " - Main Stream")
                                                               : "Main Stream"
                                                    cameraIndex: window.inlineMainCameraIndex
                                                    dptzEnabled: true
                                                    locationName: window.cameraLocationName(window.inlineMainCameraIndex)
                                                    startDelayMs: 0
                                                    source: (backend.isLoggedIn && window.inlineMainCameraIndex >= 0)
                                                            ? ((backend.rtspIp, backend.rtspPort),
                                                               backend.buildRtspUrl(window.inlineMainCameraIndex, false))
                                                            : ""
                                                    onDoubleClicked: {
                                                        window.inlineMainViewVisible = false
                                                        window.inlineMainCameraIndex = -1
                                                        stackLayout.currentIndex = 0
                                                    }
                                                }
                                            }
                                        }
                                    }
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
                showCameraControls: window.inlineMainViewVisible
                selectedCameraIndex: window.inlineMainViewVisible ? window.inlineMainCameraIndex : -1
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
        onWheel: (event) => {
            backend.resetSessionTimer()
            event.accepted = false
        }
    }

    Connections {
        target: backend
        function onIsLoggedInChanged() {
            if (!backend.isLoggedIn) {
                inlineMainViewVisible = false
                inlineMainCameraIndex = -1
            }
            stackLayout.currentIndex = backend.isLoggedIn
                                     ? (inlineMainViewVisible ? 2 : 0)
                                     : 1
        }
        function onSessionExpired() {
            inlineMainViewVisible = false
            inlineMainCameraIndex = -1
            stackLayout.currentIndex = 1
        }
        function onRtspProbeFinished(success, error) {
            if (!window.rtspConnecting || window.pendingRtspIp.length === 0) {
                return
            }

            var ip = window.pendingRtspIp
            var portText = window.pendingRtspPort
            window.pendingRtspIp = ""
            window.pendingRtspPort = ""

            if (!success) {
                window.rtspConnecting = false
                window.rtspConfigIsError = true
                window.rtspConfigError = error
                return
            }

            var prevIp = backend.rtspIp
            var prevPort = backend.rtspPort
            if (backend.updateRtspConfig(ip, portText)) {
                var changed = (prevIp !== backend.rtspIp) || (prevPort !== backend.rtspPort)
                startRtspConnectCheck(changed)
            } else {
                window.rtspConnecting = false
                window.rtspConfigIsError = true
                window.rtspConfigError = "IP/포트 형식을 확인해 주세요."
            }
        }
    }

    Timer {
        id: rtspConnectPoll
        interval: 250
        repeat: true
        running: false
        onTriggered: {
            if (!window.rtspConnecting) {
                stop()
                return
            }

            var elapsed = Date.now() - window.rtspConnectStartedMs
            var canJudgeSuccess = elapsed >= window.rtspConnectMinCheckMs
            var connected = canJudgeSuccess && backend.activeCameras > 0

            if (connected) {
                window.rtspConnectSuccessStreak += 1
                if (window.rtspConnectSuccessStreak >= 2) {
                    window.stopRtspConnectCheck()
                    window.rtspConfigError = ""
                    rtspSettingsPopup.visible = false
                }
            } else {
                window.rtspConnectSuccessStreak = 0
            }
        }
    }

    Timer {
        id: rtspConnectTimeout
        interval: window.rtspConnectTimeoutMs
        repeat: false
        running: false
        onTriggered: {
            if (!window.rtspConnecting)
                return

            window.stopRtspConnectCheck()
            window.rtspConfigIsError = true
            window.rtspConfigError = "연결 시간 초과. IP/포트를 확인 후 다시 시도해 주세요."
        }
    }

    Window {
        id: rtspSettingsPopup
        transientParent: window
        width: 380
        height: 230
        visible: false
        modality: Qt.ApplicationModal
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: "transparent"

        onVisibleChanged: {
            if (!visible) {
                stopRtspConnectCheck()
                return
            }
            x = window.x + (window.width - width) / 2
            y = window.y + (window.height - height) / 2
            rtspIpField.forceActiveFocus()
            rtspIpField.selectAll()
        }

        Rectangle {
            anchors.fill: parent
            color: theme.bgComponent
            border.color: theme.border
            border.width: 1
            radius: 10

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
                    placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                    onTextEdited: {
                        var sanitized = sanitizeIpv4Input(text)
                        if (sanitized !== text) {
                            text = sanitized
                            cursorPosition = text.length
                        }
                    }
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
                    placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                    onTextEdited: {
                        var sanitized = sanitizePortInput(text)
                        if (sanitized !== text) {
                            text = sanitized
                            cursorPosition = text.length
                        }
                    }
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
                    color: rtspConfigIsError ? "#ef4444" : theme.textSecondary
                    font.pixelSize: 12
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }

                    Button {
                        text: "초기화"
                        Layout.preferredWidth: 96
                        enabled: !window.rtspConnecting
                        onClicked: {
                            var changed = backend.resetRtspConfigToEnv()
                            rtspIpField.text = backend.rtspIp
                            rtspPortField.text = backend.rtspPort
                            if (changed) {
                                startRtspConnectCheck(true)
                            } else {
                                rtspConfigIsError = false
                                rtspConfigError = "이미 기본 RTSP 설정입니다."
                            }
                        }
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
                        text: "취소"
                        Layout.preferredWidth: 96
                        enabled: true
                        onClicked: {
                            if (window.rtspConnecting) {
                                window.stopRtspConnectCheck()
                                rtspConfigIsError = false
                                rtspConfigError = "연결이 취소되었습니다."
                            } else {
                                window.stopRtspConnectCheck()
                                rtspConfigError = ""
                                rtspConfigIsError = false
                                rtspSettingsPopup.visible = false
                            }
                        }
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
                        text: window.rtspConnecting ? "연결 시도 중..." : "연결"
                        Layout.preferredWidth: 96
                        enabled: !window.rtspConnecting
                        onClicked: {
                            var ip = rtspIpField.text.trim()
                            var portText = rtspPortField.text.trim()

                            if (ip.length === 0) {
                                rtspConfigIsError = true
                                rtspConfigError = "IP를 입력해 주세요."
                                return
                            }

                            if (!isValidIpv4(ip)) {
                                rtspConfigIsError = true
                                rtspConfigError = "IP 형식이 올바르지 않습니다. (예: 192.168.55.203)"
                                return
                            }

                            if (!isPrivateIpv4(ip)) {
                                rtspConfigIsError = true
                                rtspConfigError = "사설망 IP만 허용됩니다. (10.x / 172.16~31.x / 192.168.x)"
                                return
                            }

                            if (portText.length > 0) {
                                var portNum = parseInt(portText, 10)
                                if (isNaN(portNum) || portNum < 1 || portNum > 65535) {
                                    rtspConfigIsError = true
                                    rtspConfigError = "포트는 1~65535 범위로 입력해 주세요."
                                    return
                                }
                            }

                            var normalizedPort = portText.length > 0
                                                 ? String(parseInt(portText, 10))
                                                 : backend.rtspPort
                            if (ip === backend.rtspIp && normalizedPort === backend.rtspPort) {
                                rtspConfigIsError = false
                                rtspConfigError = "이미 연결된 RTSP 설정입니다."
                                return
                            }

                            startRtspProbe(ip, portText)
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

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: window.isDarkMode ? "transparent" : "#d4d4d8"
        border.width: window.isDarkMode ? 0 : 1
        z: 1000
    }
}






