import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs

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
    property bool pendingRtspUseCustomAuth: false
    property string pendingRtspUser: ""
    property string pendingRtspPass: ""
    property bool streamPrewarmEnabled: true
    property int inlineMainCameraIndex: -1
    property bool inlineMainViewVisible: false
    property int lastStackIndex: -1
    property string playbackSourceUrl: ""
    property string playbackTitleText: "Playback Stream"
    property var cameraNames: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
    property int pendingExportChannel: -1
    property string pendingExportDate: ""
    property string pendingExportStart: ""
    property string pendingExportEnd: ""
    property bool exportProgressVisible: false
    property int exportProgressPercent: 0
    property string exportProgressText: ""
    property bool thermalViewerVisible: false

    function cameraLocationName(index) {
        if (index < 0 || index >= cameraNames.length) return "Camera"
        return cameraNames[index]
    }

    function urlToLocalPath(u) {
        var s = String(u || "")
        if (s.indexOf("file:///") === 0) {
            return decodeURIComponent(s.slice(8))
        }
        if (s.indexOf("file://") === 0) {
            return decodeURIComponent(s.slice(7))
        }
        if (s.indexOf("file:/") === 0) {
            return decodeURIComponent(s.slice(6))
        }
        return s
    }

    function teardownPlaybackSession() {
        if (!rightSidebar) {
            playbackSourceUrl = ""
            return
        }
        if (rightSidebar.playbackRunning || rightSidebar.playbackPending || playbackSourceUrl.length > 0) {
            backend.playbackWsPause()
            backend.streamingWsDisconnect()
        }
        rightSidebar.playbackRunning = false
        rightSidebar.playbackPending = false
        playbackSourceUrl = ""
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
        pendingRtspUseCustomAuth = false
        pendingRtspUser = ""
        pendingRtspPass = ""
        rtspConnectPoll.stop()
        rtspConnectTimeout.stop()
    }

    function closeRtspSettingsPopup() {
        stopRtspConnectCheck()
        rtspConfigError = ""
        rtspConfigIsError = false
        if (rtspSettingsPopup && rtspSettingsPopup.visible) {
            rtspSettingsPopup.visible = false
        }
    }

    function startRtspProbe(ip, portText, useCustomAuth, username, password) {
        stopRtspConnectCheck()
        pendingRtspIp = ip
        pendingRtspPort = portText
        pendingRtspUseCustomAuth = !!useCustomAuth
        pendingRtspUser = username || ""
        pendingRtspPass = password || ""
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
            currentSection: stackLayout.currentIndex
            sessionRemainingSeconds: backend.sessionRemainingSeconds
            exportProgressVisible: window.exportProgressVisible
            exportProgressPercent: window.exportProgressPercent
            exportProgressText: window.exportProgressText
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
                rtspAdvancedToggle.checked = false
                rtspUserField.text = ""
                rtspPassField.text = ""
                rtspConfigError = ""
                rtspConfigIsError = false
                rtspSettingsPopup.visible = true
            }
            onRequestExportCancel: {
                backend.cancelPlaybackExport()
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
                                    Layout.leftMargin: 8
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
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: backend.isLoggedIn
                                            onClicked: {
                                                backend.resetSessionTimer()
                                                window.inlineMainViewVisible = false
                                                window.inlineMainCameraIndex = -1
                                                stackLayout.currentIndex = 0
                                            }
                                        }

                                        ToolTip.visible: gridMouse.containsMouse
                                        ToolTip.text: "라이브 그리드 모니터링 화면"
                                        ToolTip.delay: 250
                                        ToolTip.timeout: 1800
                                    }

                                    Rectangle {
                                        id: playbackBtn
                                        width: 82; height: 28
                                        color: playbackMouseArea.pressed
                                               ? "#ea580c"
                                               : (!backend.isLoggedIn
                                               ? (window.isDarkMode ? "#27272a" : "#e4e4e7")
                                               : (stackLayout.currentIndex === 2 ? theme.accent : theme.bgComponent))
                                        radius: 6
                                        border.color: theme.border
                                        border.width: stackLayout.currentIndex === 2 ? 0 : 1
                                        scale: playbackMouseArea.pressed ? 0.97 : 1.0
                                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                                        Text {
                                            anchors.centerIn: parent
                                            text: "Playback"
                                            color: !backend.isLoggedIn
                                                   ? (window.isDarkMode ? "#71717a" : "#a1a1aa")
                                                   : (stackLayout.currentIndex === 2 ? "white" : theme.textSecondary)
                                            font.bold: true
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            id: playbackMouseArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            enabled: backend.isLoggedIn
                                            onClicked: {
                                                backend.resetSessionTimer()
                                                window.inlineMainViewVisible = false
                                                window.inlineMainCameraIndex = -1
                                                stackLayout.currentIndex = 2
                                            }
                                        }

                                        ToolTip.visible: playbackMouseArea.containsMouse
                                        ToolTip.text: "녹화 재생 화면"
                                        ToolTip.delay: 250
                                        ToolTip.timeout: 1800
                                    }

                                    Rectangle {
                                        id: exportBtn
                                        width: 72; height: 28
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
                                            text: "Thermal"
                                            color: stackLayout.currentIndex === 1 ? "white" : theme.textSecondary
                                            font.bold: true
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            id: exportMouseArea
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                backend.resetSessionTimer()
                                                window.inlineMainViewVisible = false
                                                window.inlineMainCameraIndex = -1
                                                stackLayout.currentIndex = 1
                                                backend.startThermalStream()
                                            }
                                        }

                                        ToolTip.visible: exportMouseArea.containsMouse
                                        ToolTip.text: "열화상 모니터링/제어 패널"
                                        ToolTip.delay: 250
                                        ToolTip.timeout: 1800
                                    }
                                }
                            }

                            StackLayout {
                                id: stackLayout
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: backend.isLoggedIn
                                              ? (window.inlineMainViewVisible ? 3 : 0)
                                              : 1
                                onCurrentIndexChanged: {
                                    if (window.lastStackIndex === 2 && currentIndex !== 2) {
                                        window.teardownPlaybackSession()
                                    }
                                    if (currentIndex !== 1) {
                                        backend.stopThermalStream()
                                    }
                                    window.lastStackIndex = currentIndex
                                }

                                VideoGrid {
                                    id: videoGrid
                                    theme: window.appTheme
                                    cameraNames: window.cameraNames
                                    // 인라인 메인뷰 오픈 상태에서도 서브스트림 유지
                                    isActive: (backend.isLoggedIn || (window.streamPrewarmEnabled && !backend.isLoggedIn))
                                    visible: stackLayout.currentIndex === 0
                                    onOpenMainViewRequested: function(cameraIndex) {
                                        window.inlineMainCameraIndex = cameraIndex
                                        window.inlineMainViewVisible = true
                                        stackLayout.currentIndex = 3
                                    }
                                }

                                LoginScreen {
                                    id: loginScreen
                                    theme: window.appTheme
                                    onRequestReturnLiveView: {
                                        stackLayout.currentIndex = 0
                                        backend.stopThermalStream()
                                    }
                                }

                                PlaybackScreen {
                                    id: playbackScreen
                                    theme: window.appTheme
                                    playbackSource: window.playbackSourceUrl
                                    playbackTitle: window.playbackTitleText
                                    playbackCurrentSeconds: rightSidebar.playbackCurrentSeconds
                                    playbackSegments: rightSidebar.playbackSegments
                                    playbackTimelineInfoText: rightSidebar.playbackTimelineInfoText
                                    onSeekRequested: function(seconds) {
                                        rightSidebar.syncTimeFromSeconds(seconds)
                                        rightSidebar.applyExportRangeFromSecond(seconds)
                                        if (rightSidebar.playbackRunning && !rightSidebar.playbackPending) {
                                            var d = rightSidebar.formatPlaybackDate()
                                            var t = rightSidebar.formatPlaybackTime()
                                            rightSidebar.playbackPending = true
                                            window.playbackTitleText = "CH " + (rightSidebar.playbackChannelIndex + 1) + " - " + d + " " + t
                                            window.playbackSourceUrl = ""
                                            backend.preparePlaybackRtsp(rightSidebar.playbackChannelIndex, d, t)
                                        }
                                    }
                                }

                                Item {
                                    id: inlineMainView
                                    visible: stackLayout.currentIndex === 3

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
                                                    id: inlineMainPlayer
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
                id: rightSidebar
                Layout.preferredWidth: backend.isLoggedIn ? 256 : 0
                Layout.fillHeight: true
                theme: window.appTheme
                visible: backend.isLoggedIn
                showCameraControls: window.inlineMainViewVisible && stackLayout.currentIndex === 3
                showPlaybackControls: backend.isLoggedIn && stackLayout.currentIndex === 2
                selectedCameraIndex: window.inlineMainViewVisible ? window.inlineMainCameraIndex : -1
                cameraNames: window.cameraNames
                onRequestCameraNameChange: function(cameraIndex, name) {
                    if (cameraIndex < 0 || cameraIndex >= window.cameraNames.length)
                        return
                    var next = window.cameraNames.slice(0)
                    next[cameraIndex] = name
                    window.cameraNames = next
                }
                onRequestPlayback: function(channelIndex, dateText, timeText) {
                    if (dateText.length === 0 || timeText.length === 0) {
                        console.warn("[PLAYBACK] date/time is empty")
                        return
                    }
                    rightSidebar.applyPlaybackStart(dateText, timeText)
                    window.playbackTitleText = "CH " + (channelIndex + 1) + " - " + dateText + " " + timeText
                    window.playbackSourceUrl = ""
                    backend.preparePlaybackRtsp(channelIndex, dateText, timeText)
                }
                onRequestPlaybackTimeline: function(channelIndex, dateText) {
                    backend.loadPlaybackTimeline(channelIndex, dateText)
                }
                onRequestPlaybackMonthDays: function(channelIndex, year, month) {
                    backend.loadPlaybackMonthRecordedDays(channelIndex, year, month)
                }
                onRequestPlaybackPause: {
                    playbackScreen.pausePlayback()
                }
                onRequestPlaybackResume: {
                    playbackScreen.resumePlayback()
                }
                onRequestPlaybackExport: function(channelIndex, dateText, startTimeText, endTimeText) {
                    pendingExportChannel = channelIndex
                    pendingExportDate = dateText
                    pendingExportStart = startTimeText
                    pendingExportEnd = endTimeText
                    playbackExportSaveDialog.currentFile = "playback_" + dateText + "_" + startTimeText.replace(/:/g, "-")
                    playbackExportSaveDialog.open()
                    // thermalViewerVisible = true
                    // thermalViewer.open()
                    // backend.startThermalStream()
                }
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
                closeRtspSettingsPopup()
            }
            stackLayout.currentIndex = backend.isLoggedIn
                                     ? (inlineMainViewVisible ? 3 : 0)
                                     : 1
        }
        function onSessionExpired() {
            inlineMainViewVisible = false
            inlineMainCameraIndex = -1
            closeRtspSettingsPopup()
            stackLayout.currentIndex = 1
        }
        function onRtspProbeFinished(success, error) {
            if (!window.rtspConnecting || window.pendingRtspIp.length === 0) {
                return
            }

            var ip = window.pendingRtspIp
            var portText = window.pendingRtspPort
            var useCustomAuth = window.pendingRtspUseCustomAuth
            var authUser = window.pendingRtspUser
            var authPass = window.pendingRtspPass
            window.pendingRtspIp = ""
            window.pendingRtspPort = ""
            window.pendingRtspUseCustomAuth = false
            window.pendingRtspUser = ""
            window.pendingRtspPass = ""

            if (!success) {
                window.rtspConnecting = false
                window.rtspConfigIsError = true
                window.rtspConfigError = error
                return
            }

            var prevIp = backend.rtspIp
            var prevPort = backend.rtspPort
            if (backend.updateRtspConfig(ip, portText)) {
                if (useCustomAuth) {
                    if (!backend.updateRtspCredentials(authUser, authPass)) {
                        window.rtspConnecting = false
                        window.rtspConfigIsError = true
                        window.rtspConfigError = "RTSP 계정 형식을 확인해 주세요."
                        return
                    }
                } else {
                    backend.useEnvRtspCredentials()
                }
                var changed = (prevIp !== backend.rtspIp) || (prevPort !== backend.rtspPort)
                startRtspConnectCheck(changed)
            } else {
                window.rtspConnecting = false
                window.rtspConfigIsError = true
                window.rtspConfigError = "IP/포트 형식을 확인해 주세요."
            }
        }
        function onPlaybackPrepared(url) {
            window.playbackSourceUrl = url
            rightSidebar.playbackRunning = true
            rightSidebar.playbackPending = false
            console.log("[PLAYBACK] source:", window.playbackSourceUrl)
        }
        function onPlaybackPrepareFailed(error) {
            rightSidebar.playbackRunning = false
            rightSidebar.playbackPending = false
            console.warn("[PLAYBACK] prepare failed:", error)
        }
        function onPlaybackTimelineLoaded(channelIndex, dateText, segments) {
            if (!rightSidebar.showPlaybackControls)
                return
            if (channelIndex !== rightSidebar.playbackChannelIndex)
                return
            rightSidebar.playbackSegments = segments
        }
        function onPlaybackTimelineFailed(error) {
            if (rightSidebar.showPlaybackControls) {
                rightSidebar.playbackSegments = []
            }
            console.warn("[PLAYBACK][TIMELINE]", error)
        }
        function onPlaybackMonthRecordedDaysLoaded(channelIndex, yearMonth, days) {
            if (!rightSidebar.showPlaybackControls)
                return
            if (channelIndex !== rightSidebar.playbackChannelIndex)
                return
            var ym = rightSidebar.playbackViewYear + "-" + (rightSidebar.playbackViewMonth + 1 < 10 ? "0" : "") + (rightSidebar.playbackViewMonth + 1)
            if (yearMonth !== ym)
                return
            rightSidebar.playbackRecordedDays = days
        }
        function onPlaybackMonthRecordedDaysFailed(error) {
            if (rightSidebar.showPlaybackControls) {
                rightSidebar.playbackRecordedDays = []
            }
            console.warn("[PLAYBACK][MONTH_DAYS]", error)
        }
        function onStreamingWsStateChanged(state) {
            console.log("[PLAYBACK][WS] state:", state)
        }
        function onStreamingWsFrame(direction, hexPayload) {
            if (direction === "recv-bin") {
                // 로그 노이즈 방지: RTP 바이너리 프레임은 생략하고 RTSP 제어응답만 출력
                if (!hexPayload.startsWith("525453502F312E30"))
                    return
            }
            var preview = hexPayload
            if (preview.length > 96)
                preview = preview.slice(0, 96) + "..."
            console.log("[PLAYBACK][WS]", direction, preview)
        }
        function onStreamingWsError(error) {
            console.warn("[PLAYBACK][WS] error:", error)
        }
        function onPlaybackExportStarted(message) {
            console.log("[PLAYBACK][EXPORT]", message)
            exportProgressHideTimer.stop()
            window.exportProgressVisible = true
            window.exportProgressPercent = 0
            window.exportProgressText = message
        }
        function onPlaybackExportProgress(percent, message) {
            console.log("[PLAYBACK][EXPORT]", percent + "%", message)
            exportProgressHideTimer.stop()
            window.exportProgressVisible = true
            window.exportProgressPercent = Math.max(0, Math.min(100, percent))
            window.exportProgressText = message
        }
        function onPlaybackExportFinished(path) {
            console.log("[PLAYBACK][EXPORT] saved:", path)
            window.exportProgressVisible = true
            window.exportProgressPercent = 100
            window.exportProgressText = "내보내기 완료"
            exportProgressHideTimer.restart()
        }
        function onPlaybackExportFailed(error) {
            console.warn("[PLAYBACK][EXPORT]", error)
            window.exportProgressVisible = true
            window.exportProgressText = "내보내기 실패"
            exportProgressHideTimer.restart()
        }
    }

    FileDialog {
        id: playbackExportSaveDialog
        title: "Playback 내보내기 저장 경로 선택"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Video files (*.avi *.zip)", "All files (*)"]
        onAccepted: {
            var savePath = window.urlToLocalPath(selectedFile)
            if (!savePath || savePath.length === 0)
                return
            backend.requestPlaybackExport(window.pendingExportChannel,
                                          window.pendingExportDate,
                                          window.pendingExportStart,
                                          window.pendingExportEnd,
                                          savePath)
        }
    }

    ThermalViewer {
        id: thermalViewer
        x: (window.width - width) / 2
        y: (window.height - height) / 2
        frameSource: backend.thermalFrameDataUrl
        infoText: backend.thermalInfoText
        onCloseRequested: {
            close()
            window.thermalViewerVisible = false
            backend.stopThermalStream()
        }
        onClosed: {
            if (window.thermalViewerVisible) {
                window.thermalViewerVisible = false
                backend.stopThermalStream()
            }
        }
    }

    Timer {
        id: exportProgressHideTimer
        interval: 3500
        repeat: false
        running: false
        onTriggered: {
            window.exportProgressVisible = false
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
        height: rtspAdvancedToggle.checked ? 330 : 230
        visible: false
        modality: Qt.NonModal
        flags: Qt.Dialog | Qt.FramelessWindowHint
        color: "transparent"

        onVisibleChanged: {
            if (!visible) {
                stopRtspConnectCheck()
                return
            }
            backend.resetSessionTimer()
            x = window.x + (window.width - width) / 2
            y = window.y + (window.height - height) / 2
            rtspIpField.forceActiveFocus()
            rtspIpField.selectAll()
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

        Rectangle {
            anchors.fill: parent
            color: theme.bgComponent
            border.color: theme.border
            border.width: 1
            radius: 10

            Keys.onPressed: backend.resetSessionTimer()

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.AllButtons
                propagateComposedEvents: true
                onPressed: (mouse) => {
                    backend.resetSessionTimer()
                    mouse.accepted = false
                }
                onPositionChanged: (mouse) => {
                    backend.resetSessionTimer()
                    mouse.accepted = false
                }
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
                        backend.resetSessionTimer()
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
                        backend.resetSessionTimer()
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
                    enabled: !window.rtspConnecting
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
                        border.color: rtspAdvancedToggle.checked ? theme.accent : theme.border
                        color: rtspAdvancedToggle.checked ? theme.accent : "transparent"
                    }
                    contentItem: Text {
                        text: rtspAdvancedToggle.text
                        color: theme.textSecondary
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
                        color: theme.textPrimary
                        placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                        background: Rectangle {
                            color: theme.bgSecondary
                            border.color: rtspUserField.activeFocus ? theme.accent : theme.border
                            radius: 6
                        }
                        onTextEdited: backend.resetSessionTimer()
                    }

                    TextField {
                        id: rtspPassField
                        Layout.fillWidth: true
                        placeholderText: "RTSP 비밀번호"
                        echoMode: TextInput.Password
                        color: theme.textPrimary
                        placeholderTextColor: theme ? "#9ca3af" : "#6b7280"
                        background: Rectangle {
                            color: theme.bgSecondary
                            border.color: rtspPassField.activeFocus ? theme.accent : theme.border
                            radius: 6
                        }
                        onTextEdited: backend.resetSessionTimer()
                        onAccepted: {
                            backend.resetSessionTimer()
                            if (!window.rtspConnecting) {
                                saveRtspButton.clicked()
                            }
                        }
                    }
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
                            backend.resetSessionTimer()
                            var changed = backend.resetRtspConfigToEnv()
                            rtspIpField.text = backend.rtspIp
                            rtspPortField.text = backend.rtspPort
                            rtspAdvancedToggle.checked = false
                            rtspUserField.text = ""
                            rtspPassField.text = ""
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
                            backend.resetSessionTimer()
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
                            backend.resetSessionTimer()
                            var ip = rtspIpField.text.trim()
                            var portText = rtspPortField.text.trim()
                            var useCustomAuth = rtspAdvancedToggle.checked
                            var authUser = rtspUserField.text.trim()
                            var authPass = rtspPassField.text

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

                            if (useCustomAuth && authUser.length === 0) {
                                rtspConfigIsError = true
                                rtspConfigError = "고급 설정 사용 시 RTSP 아이디를 입력해 주세요."
                                return
                            }

                            startRtspProbe(ip, portText, useCustomAuth, authUser, authPass)
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






