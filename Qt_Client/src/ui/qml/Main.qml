import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import Qt5Compat.GraphicalEffects
import "common"
import "view"
import "playback"
import "thermal"
import "sidebar"
import "dialogs"
import "components" as C

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
    property alias lastMoveX: uiStore.lastMoveX
    property alias lastMoveY: uiStore.lastMoveY
    property alias lastMoveResetMs: uiStore.lastMoveResetMs
    property alias rtspConfigError: uiStore.rtspConfigError
    property alias rtspConfigIsError: uiStore.rtspConfigIsError
    property alias rtspConnecting: uiStore.rtspConnecting
    property alias rtspConnectTimeoutMs: uiStore.rtspConnectTimeoutMs
    property alias rtspConnectSuccessStreak: uiStore.rtspConnectSuccessStreak
    property alias rtspConnectStartedMs: uiStore.rtspConnectStartedMs
    property alias rtspConnectMinCheckMs: uiStore.rtspConnectMinCheckMs
    property alias pendingRtspIp: uiStore.pendingRtspIp
    property alias pendingRtspPort: uiStore.pendingRtspPort
    property alias pendingRtspUseCustomAuth: uiStore.pendingRtspUseCustomAuth
    property alias pendingRtspUser: uiStore.pendingRtspUser
    property alias pendingRtspPass: uiStore.pendingRtspPass
    property alias streamPrewarmEnabled: uiStore.streamPrewarmEnabled
    property alias inlineMainCameraIndex: uiStore.inlineMainCameraIndex
    property alias inlineMainViewVisible: uiStore.inlineMainViewVisible
    property alias lastStackIndex: uiStore.lastStackIndex
    property alias playbackSourceUrl: uiStore.playbackSourceUrl
    property alias playbackTitleText: uiStore.playbackTitleText
    property alias cameraNames: uiStore.cameraNames
    property alias pendingExportChannel: uiStore.pendingExportChannel
    property alias pendingExportDate: uiStore.pendingExportDate
    property alias pendingExportStart: uiStore.pendingExportStart
    property alias pendingExportEnd: uiStore.pendingExportEnd
    property alias exportProgressVisible: uiStore.exportProgressVisible
    property alias exportProgressPercent: uiStore.exportProgressPercent
    property alias exportProgressText: uiStore.exportProgressText
    property var clientSystemSpecsCache: ({})
    property bool clientSystemSpecsLoaded: false
    property bool startupOverlayVisible: true
    property bool startupOverlayDismissed: false
    property bool startupBootReady: false
    property bool startupMinElapsed: false
    property bool startupCameraReady: false
    property bool startupTimeoutElapsed: false
    property int startupCameraTarget: 4
    property int startupMaxWaitMs: 10000
    property string startupStatusText: "초기 리소스 준비 중..."
    property int windowRadius: 18

    QtObject {
        id: uiStore
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
    }

    function cameraLocationName(index) {
        if (index < 0 || index >= cameraNames.length) return "Camera"
        return cameraNames[index]
    }

    function showClientSystemSpecs() {
        if (!backend.isLoggedIn)
            return
        if (!clientSystemSpecsLoaded) {
            clientSystemSpecsCache = backend.getClientSystemInfo()
            clientSystemSpecsLoaded = true
        }
        systemSpecsDialog.showWithData(clientSystemSpecsCache)
    }

    // 시작 오버레이 카메라 준비 상태 계산
    function refreshStartupCameraReady() {
        if (!streamPrewarmEnabled) {
            startupCameraReady = true
            return
        }
        startupCameraReady = backend.activeCameras >= startupCameraTarget
    }

    // 시작 오버레이 상태 문구 갱신
    function updateStartupStatusText() {
        if (startupOverlayDismissed) {
            startupStatusText = "로그인 화면 준비 완료"
            return
        }
        if (!startupBootReady) {
            startupStatusText = "초기 리소스 준비 중..."
            return
        }
        if (!startupMinElapsed) {
            startupStatusText = "초기 UI 준비 중..."
            return
        }
        if (startupCameraReady) {
            startupStatusText = "로그인 화면 준비 완료"
            return
        }
        if (startupTimeoutElapsed) {
            startupStatusText = "일부 카메라 준비 지연, 로그인 화면으로 이동"
            return
        }
        var count = Math.max(0, backend.activeCameras)
        startupStatusText = "카메라 스트림 준비 중... (" + count + "/" + startupCameraTarget + ")"
    }

    // 시작 오버레이 표시/해제 상태 갱신
    function updateStartupOverlayState() {
        if (startupOverlayDismissed) {
            startupOverlayVisible = false
            startupStatusText = "로그인 화면 준비 완료"
            return
        }
        refreshStartupCameraReady()
        startupOverlayVisible = !(startupBootReady && startupMinElapsed && (startupCameraReady || startupTimeoutElapsed))
        updateStartupStatusText()
        if (!startupOverlayVisible) {
            startupOverlayDismissed = true
            startupStatusText = "로그인 화면 준비 완료"
            if (startupTimeoutTimer.running) {
                startupTimeoutTimer.stop()
            }
        }
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
        setSidebarPlaybackState(false, false)
        playbackSourceUrl = ""
    }

    function setSidebarPlaybackState(running, pending) {
        if (!rightSidebar)
            return
        rightSidebar.playbackRunning = running
        rightSidebar.playbackPending = pending
    }

    function applyPlaybackSeek(seconds) {
        if (!rightSidebar)
            return
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

    function canApplyPlaybackChannel(channelIndex) {
        if (!rightSidebar || !rightSidebar.showPlaybackControls)
            return false
        return channelIndex === rightSidebar.playbackChannelIndex
    }

    function applyPlaybackTimelineSegments(channelIndex, segments) {
        if (!canApplyPlaybackChannel(channelIndex))
            return false
        rightSidebar.playbackSegments = segments
        return true
    }

    function clearPlaybackTimelineSegments() {
        if (!rightSidebar || !rightSidebar.showPlaybackControls)
            return
        rightSidebar.playbackSegments = []
    }

    function applyPlaybackRecordedDays(channelIndex, yearMonth, days) {
        if (!canApplyPlaybackChannel(channelIndex))
            return false
        var ym = rightSidebar.playbackViewYear + "-" + (rightSidebar.playbackViewMonth + 1 < 10 ? "0" : "") + (rightSidebar.playbackViewMonth + 1)
        if (yearMonth !== ym)
            return false
        rightSidebar.playbackRecordedDays = days
        return true
    }

    function clearPlaybackRecordedDays() {
        if (!rightSidebar || !rightSidebar.showPlaybackControls)
            return
        rightSidebar.playbackRecordedDays = []
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
            rtspSettingsPopup.closeDialog()
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

    Component.onCompleted: {
        startupBootReady = true
        updateStartupOverlayState()
    }

    onStreamPrewarmEnabledChanged: updateStartupOverlayState()

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

    title: "AEGIS Vision VMS"
    color: "transparent"

    Rectangle {
        id: windowMaskSource
        anchors.fill: parent
        radius: window.windowRadius
        color: "black"
        antialiasing: true
        visible: false
    }

    Item {
        id: windowChrome
        anchors.fill: parent
        property var maskSourceItem: windowMaskSource
        layer.enabled: true
        layer.effect: OpacityMask {
            maskSource: windowChrome.maskSourceItem
        }

        Rectangle {
            anchors.fill: parent
            radius: window.windowRadius
            color: theme.bgPrimary
            antialiasing: true
        }

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

                    RowLayout {
                        spacing: 6

                        Image {
                            Layout.alignment: Qt.AlignVCenter
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 14
                            source: "qrc:/qt/qml/Team3VideoReceiver/icons/Hanwha_logo.ico"
                            fillMode: Image.PreserveAspectFit
                            sourceSize.width: 14
                            sourceSize.height: 14
                            clip: true
                            smooth: true
                            mipmap: true
                        }

                        Text {
                            text: "AEGIS Vision VMS"
                            color: theme.textPrimary
                            font.pixelSize: 12
                            font.bold: true
                        }
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
            twoFactorEnabled: backend.twoFactorEnabled
            userId: backend.userId
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
            onRequestTwoFactorSetup: {
                twoFactorDialog.openForSetup()
            }
            onRequestTwoFactorDisable: {
                twoFactorDialog.openForDisable()
            }
            onRequestAccountDelete: {
                accountDeleteDialog.openDialog()
            }
            onRequestPasswordChange: {
                changePasswordDialog.openDialog()
            }
            onRequestHome: {
                window.showClientSystemSpecs()
            }
            onRequestRtspSettings: {
                rtspSettingsPopup.prepareAndShow()
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
                                        ToolTip.text: "열화상 모니터링/제어 화면"
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

                                ViewGridContent {
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

                                ThermalContent {
                                    id: loginScreen
                                    theme: window.appTheme
                                    onRequestReturnLiveView: {
                                        stackLayout.currentIndex = 0
                                        backend.stopThermalStream()
                                    }
                                }

                                PlaybackContent {
                                    id: playbackScreen
                                    theme: window.appTheme
                                    playbackSource: window.playbackSourceUrl
                                    playbackTitle: window.playbackTitleText
                                    playbackCurrentSeconds: rightSidebar.playbackCurrentSeconds
                                    playbackSegments: rightSidebar.playbackSegments
                                    playbackTimelineInfoText: rightSidebar.playbackTimelineInfoText
                                    onSeekRequested: function(seconds) {
                                        window.applyPlaybackSeek(seconds)
                                    }
                                }

                                InlineMainViewContent {
                                    id: inlineMainView
                                    visible: stackLayout.currentIndex === 3
                                    theme: window.appTheme
                                    isLoggedIn: backend.isLoggedIn
                                    mapModeEnabled: rightSidebar.mapModeEnabled
                                    cameraIndex: window.inlineMainCameraIndex
                                    locationName: window.cameraLocationName(window.inlineMainCameraIndex)
                                    onRequestClose: {
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

            Sidebar {
                id: rightSidebar
                backendObject: backend
                Layout.preferredWidth: backend.isLoggedIn ? 256 : 0
                Layout.fillHeight: true
                theme: window.appTheme
                visible: backend.isLoggedIn
                showCameraControls: window.inlineMainViewVisible && stackLayout.currentIndex === 3
                showPlaybackControls: backend.isLoggedIn && stackLayout.currentIndex === 2
                showThermalControls: backend.isLoggedIn && stackLayout.currentIndex === 1
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
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        z: 5000
        visible: startupOverlayVisible
        color: theme.bgSecondary
        radius: window.windowRadius
        antialiasing: true

        Rectangle {
            anchors.fill: parent
            radius: window.windowRadius
            antialiasing: true
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: theme.bgSecondary }
                GradientStop { position: 1.0; color: theme.bgPrimary }
            }
            opacity: 0.95
        }

        Column {
            anchors.centerIn: parent
            spacing: 16

            Image {
                width: 84
                height: 84
                anchors.horizontalCenter: parent.horizontalCenter
                source: "qrc:/qt/qml/Team3VideoReceiver/icons/AEGIS_logo.png"
                fillMode: Image.PreserveAspectFit
                sourceSize.width: 168
                sourceSize.height: 168
                smooth: true
                mipmap: true
            }

            Text {
                text: "AEGIS Vision VMS"
                color: theme.textPrimary
                font.pixelSize: 30
                font.bold: true
                anchors.horizontalCenter: parent.horizontalCenter
            }

            Text {
                text: startupStatusText
                color: theme.textSecondary
                font.pixelSize: 14
                anchors.horizontalCenter: parent.horizontalCenter
            }

            BusyIndicator {
                running: startupOverlayVisible
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }

        MouseArea {
            anchors.fill: parent
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
        function onActiveCamerasChanged() {
            if (window.startupOverlayDismissed)
                return
            window.updateStartupOverlayState()
        }
        function onIsLoggedInChanged() {
            if (!backend.isLoggedIn) {
                inlineMainViewVisible = false
                inlineMainCameraIndex = -1
                closeRtspSettingsPopup()
                twoFactorDialog.close()
                accountDeleteDialog.close()
                changePasswordDialog.close()
            }
            stackLayout.currentIndex = backend.isLoggedIn
                                     ? (inlineMainViewVisible ? 3 : 0)
                                     : 1
        }
        function onTwoFactorSetupCompleted() {
            backend.refreshTwoFactorStatus()
            accountActionDialog.title = "OTP 생성"
            accountActionDialog.text = "OTP가 성공적으로 활성화되었습니다.\n현재 로그인은 유지되며, 다음 로그인부터 OTP 인증이 필요합니다."
            accountActionDialog.open()
        }
        function onTwoFactorDisableCompleted() {
            backend.refreshTwoFactorStatus()
            accountActionDialog.title = "OTP 삭제"
            accountActionDialog.text = "OTP가 성공적으로 비활성화되었습니다.\n현재 로그인은 유지됩니다."
            accountActionDialog.open()
        }
        function onAccountDeleteCompleted() {
            accountActionDialog.title = "회원탈퇴"
            accountActionDialog.text = "계정이 삭제되었습니다.\n로그인 화면으로 이동합니다."
            accountActionDialog.open()
        }
        function onPasswordChangeCompleted() {
            accountActionDialog.title = "비밀번호 변경"
            accountActionDialog.text = "비밀번호가 성공적으로 변경되었습니다."
            accountActionDialog.open()
        }
        function onSessionExpired() {
            inlineMainViewVisible = false
            inlineMainCameraIndex = -1
            closeRtspSettingsPopup()
            twoFactorDialog.close()
            accountDeleteDialog.close()
            changePasswordDialog.close()
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
            window.setSidebarPlaybackState(true, false)
            console.log("[PLAYBACK] source:", window.playbackSourceUrl)
        }
        function onPlaybackPrepareFailed(error) {
            window.setSidebarPlaybackState(false, false)
            console.warn("[PLAYBACK] prepare failed:", error)
        }
        function onPlaybackTimelineLoaded(channelIndex, dateText, segments) {
            window.applyPlaybackTimelineSegments(channelIndex, segments)
        }
        function onPlaybackTimelineFailed(error) {
            window.clearPlaybackTimelineSegments()
            console.warn("[PLAYBACK][TIMELINE]", error)
        }
        function onPlaybackMonthRecordedDaysLoaded(channelIndex, yearMonth, days) {
            window.applyPlaybackRecordedDays(channelIndex, yearMonth, days)
        }
        function onPlaybackMonthRecordedDaysFailed(error) {
            window.clearPlaybackRecordedDays()
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
    PlaybackExportSaveDialog {
        id: playbackExportSaveDialog
        hostWindow: window
        onSaveConfirmed: function(savePath) {
            backend.requestPlaybackExport(window.pendingExportChannel,
                                          window.pendingExportDate,
                                          window.pendingExportStart,
                                          window.pendingExportEnd,
                                          savePath)
        }
    }

    Timer {
        id: startupMinTimer
        interval: 1500
        repeat: false
        running: true
        onTriggered: {
            startupMinElapsed = true
            updateStartupOverlayState()
        }
    }

    Timer {
        id: startupTimeoutTimer
        interval: startupMaxWaitMs
        repeat: false
        running: true
        onTriggered: {
            startupTimeoutElapsed = true
            updateStartupOverlayState()
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
                    rtspSettingsPopup.closeDialog()
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
    RtspSettingsDialog {
        id: rtspSettingsPopup
        theme: window.appTheme
        hostWindow: window
    }

    TwoFactorDialog {
        id: twoFactorDialog
        theme: window.appTheme
        backendObject: backend
    }

    AccountDeleteDialog {
        id: accountDeleteDialog
        theme: window.appTheme
        backendObject: backend
    }

    ChangePasswordDialog {
        id: changePasswordDialog
        theme: window.appTheme
        backendObject: backend
    }

    StatusDialog {
        id: accountActionDialog
        theme: window.appTheme
    }

    SystemSpecsDialog {
        id: systemSpecsDialog
        theme: window.appTheme
    }

    Window {
        id: cctv3dMapDebugWindow
        visible: backend.isLoggedIn && rightSidebar.mapModeEnabled
        width: 1000
        height: 760
        minimumWidth: 640
        minimumHeight: 520
        title: "AEGIS Vision VMS - 3D Map"
        flags: Qt.Window | Qt.FramelessWindowHint
        color: "transparent"
        property int chromeRadius: window.windowRadius
        property real viewRx: -20.0
        property real viewRy: 35.0
        property bool dragging: false
        property real lastDragX: 0
        property real lastDragY: 0
        property int dragSendIntervalMs: 90
        property double lastDragSendMs: 0

        function clamp(v, lo, hi) {
            return Math.max(lo, Math.min(hi, v))
        }

        function wrapDeg(v) {
            while (v > 180.0)
                v -= 360.0
            while (v < -180.0)
                v += 360.0
            return v
        }

        function sendViewUpdate(force) {
            if (!visible)
                return
            var now = Date.now()
            if (!force && (now - lastDragSendMs) < dragSendIntervalMs)
                return
            lastDragSendMs = now
            backend.updateCctv3dMapView(viewRx, viewRy)
        }

        onClosing: function(close) {
            if (rightSidebar.mapModeEnabled) {
                backend.stopCctv3dMapSequence()
                rightSidebar.mapModeEnabled = false
            }
            close.accepted = true
        }

        onVisibleChanged: {
            if (visible) {
                lastDragSendMs = 0
                sendViewUpdate(true)
            }
        }

        Rectangle {
            id: mapWindowMaskSource
            anchors.fill: parent
            radius: cctv3dMapDebugWindow.chromeRadius
            color: "black"
            antialiasing: true
            visible: false
        }

        Item {
            id: mapWindowChrome
            anchors.fill: parent
            property var maskSourceItem: mapWindowMaskSource
            layer.enabled: true
            layer.effect: OpacityMask {
                maskSource: mapWindowChrome.maskSourceItem
            }

            Rectangle {
                anchors.fill: parent
                radius: cctv3dMapDebugWindow.chromeRadius
                color: theme.bgPrimary
                antialiasing: true
            }

        Rectangle {
            anchors.fill: parent
            color: theme.bgPrimary

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 0
                spacing: 8

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
                            text: "3D Map Viewer"
                            color: theme.textPrimary
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            id: mapTitleMinBtn
                            width: 28
                            height: 22
                            radius: 4
                            color: mapTitleMinMouse.pressed
                                   ? (window.isDarkMode ? "#3f3f46" : "#d4d4d8")
                                   : (mapTitleMinMouse.containsMouse ? (window.isDarkMode ? "#27272a" : "#e4e4e7") : "transparent")
                            scale: mapTitleMinMouse.pressed ? 0.95 : 1.0
                            Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                            Text {
                                anchors.centerIn: parent
                                text: "-"
                                color: theme.textSecondary
                                font.pixelSize: 14
                            }

                            MouseArea {
                                id: mapTitleMinMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: cctv3dMapDebugWindow.showMinimized()
                            }
                        }

                        Rectangle {
                            id: mapTitleCloseBtn
                            width: 28
                            height: 22
                            radius: 4
                            color: mapTitleCloseMouse.pressed ? "#b91c1c" : (mapTitleCloseMouse.containsMouse ? "#dc2626" : "transparent")
                            scale: mapTitleCloseMouse.pressed ? 0.95 : 1.0
                            Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                            Text {
                                anchors.centerIn: parent
                                text: "x"
                                color: mapTitleCloseMouse.containsMouse ? "white" : theme.textSecondary
                                font.pixelSize: 12
                                font.bold: true
                            }

                            MouseArea {
                                id: mapTitleCloseMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: cctv3dMapDebugWindow.close()
                            }
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        z: -1
                        onPressed: function(mouse) {
                            if (mouse.button === Qt.LeftButton)
                                cctv3dMapDebugWindow.startSystemMove()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 52
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    color: theme.bgComponent
                    border.color: theme.border
                    border.width: 1
                    radius: 8

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            Text {
                                text: "3D Map Viewer"
                                color: theme.textPrimary
                                font.pixelSize: 14
                                font.bold: true
                            }
                            Text {
                                text: backend.cctv3dMapFrameDataUrl.length > 0
                                      ? "RGBD 스트림 수신 중"
                                      : "프레임 수신 대기 중"
                                color: theme.textSecondary
                                font.pixelSize: 11
                            }
                        }

                        C.SidebarControlButton {
                            text: "일시정지"
                            compact: true
                            theme: window.appTheme
                            enabled: rightSidebar.mapModeEnabled
                            onClicked: backend.pauseCctv3dMapSequence()
                        }

                        C.SidebarControlButton {
                            text: "재개"
                            compact: true
                            theme: window.appTheme
                            enabled: rightSidebar.mapModeEnabled
                            onClicked: backend.resumeCctv3dMapSequence()
                        }

                        C.SidebarControlButton {
                            text: "Stop"
                            compact: true
                            accentStyle: true
                            theme: window.appTheme
                            enabled: rightSidebar.mapModeEnabled
                            onClicked: {
                                backend.stopCctv3dMapSequence()
                                rightSidebar.mapModeEnabled = false
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.bottomMargin: 10
                    color: theme.bgSecondary
                    border.color: theme.border
                    border.width: 1
                    radius: 8

                    Image {
                        id: cctv3dMapDebugImage
                        anchors.fill: parent
                        anchors.margins: 10
                        source: backend.cctv3dMapFrameDataUrl
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        smooth: true
                        mipmap: false
                    }

                    MouseArea {
                        anchors.fill: cctv3dMapDebugImage
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.OpenHandCursor

                        onPressed: function(mouse) {
                            if (mouse.button !== Qt.LeftButton)
                                return
                            cctv3dMapDebugWindow.dragging = true
                            cctv3dMapDebugWindow.lastDragX = mouse.x
                            cctv3dMapDebugWindow.lastDragY = mouse.y
                        }

                        onReleased: function(mouse) {
                            if (mouse.button !== Qt.LeftButton)
                                return
                            cctv3dMapDebugWindow.dragging = false
                            cctv3dMapDebugWindow.sendViewUpdate(true)
                        }

                        onCanceled: {
                            cctv3dMapDebugWindow.dragging = false
                        }

                        onPositionChanged: function(mouse) {
                            if (!cctv3dMapDebugWindow.dragging)
                                return

                            var dx = mouse.x - cctv3dMapDebugWindow.lastDragX
                            var dy = mouse.y - cctv3dMapDebugWindow.lastDragY
                            cctv3dMapDebugWindow.lastDragX = mouse.x
                            cctv3dMapDebugWindow.lastDragY = mouse.y

                            var sensitivity = 0.22
                            cctv3dMapDebugWindow.viewRx = cctv3dMapDebugWindow.clamp(
                                        cctv3dMapDebugWindow.viewRx + dy * sensitivity, -89.0, 89.0)
                            cctv3dMapDebugWindow.viewRy = cctv3dMapDebugWindow.wrapDeg(
                                        cctv3dMapDebugWindow.viewRy + dx * sensitivity)
                            cctv3dMapDebugWindow.sendViewUpdate(false)
                        }
                    }

                    Rectangle {
                        anchors.left: cctv3dMapDebugImage.left
                        anchors.top: cctv3dMapDebugImage.top
                        anchors.leftMargin: 12
                        anchors.topMargin: 10
                        radius: 6
                        color: "#66000000"
                        border.color: "#22ffffff"
                        border.width: 1
                        implicitWidth: debugGuideText.implicitWidth + 14
                        implicitHeight: debugGuideText.implicitHeight + 8
                        z: 10

                        Text {
                            id: debugGuideText
                            anchors.centerIn: parent
                            text: "Drag LMB: Rotate | rx "
                                  + cctv3dMapDebugWindow.viewRx.toFixed(1)
                                  + " | ry "
                                  + cctv3dMapDebugWindow.viewRy.toFixed(1)
                            color: "#e5e7eb"
                            font.pixelSize: 12
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: cctv3dMapDebugImage.source.length === 0
                        text: "3D Map 프레임 수신 대기 중..."
                        color: theme.textSecondary
                        font.pixelSize: 16
                    }
                }
            }
        }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: window.windowRadius
        antialiasing: true
        border.color: window.isDarkMode ? "transparent" : "#d4d4d8"
        border.width: window.isDarkMode ? 0 : 1
        z: 1000
    }
    }
}

