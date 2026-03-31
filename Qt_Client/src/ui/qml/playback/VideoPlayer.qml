import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Team3VideoReceiver 1.0

Item {
    id: root
    property string source: ""
    property int tileIndex: -1
    property string titleText: ""
    property string locationName: "Camera"
    property var theme
    property int cameraIndex: -1
    property bool dptzEnabled: false
    property real dptzScale: 1.0
    property real dptzMinScale: 1.0
    property real dptzMaxScale: 4.0
    property real dptzPanX: 0
    property real dptzPanY: 0
    property real dptzPointerX: 0
    property real dptzPointerY: 0
    property bool dptzFocusLocked: false
    property real dptzLockedFocusX: 0
    property real dptzLockedFocusY: 0
    property bool dptzDebugLog: false
    property int startDelayMs: 0

    signal cameraStateChanged(int cameraIndex, bool isLive)
    signal cameraFpsChanged(int cameraIndex, int fps)
    signal doubleClicked()
    // 스트림 일시 정지 함수
    function pauseStream() {
        if (root.source === "")
            return
        startPlayTimer.stop()
        reconnectTimer.stop()
        vlc.userPaused = true
        mediaPlayer.pause()
    }
    // 스트림 재개 함수
    function resumeStream() {
        if (root.source === "")
            return
        reconnectTimer.stop()
        vlc.userPaused = false
        if (mediaPlayer.source.toString() !== root.source) {
            mediaPlayer.source = root.source
        }
        mediaPlayer.play()
    }
    // 스트림 재시작 함수
    function restartStream() {
        if (root.source === "")
            return
        startPlayTimer.stop()
        vlc.stop()
        // 카메라 측 스트림 재구성 안정화용 짧은 지연
        startPlayTimer.interval = 350
        startPlayTimer.start()
    }
    // DPTZ 상태 초기화 함수
    function dptzReset() {
        dptzScale = 1.0
        dptzPanX = 0
        dptzPanY = 0
    }
    // DPTZ 팬 범위 제한 함수
    function dptzClampPan() {
        if (!dptzEnabled || dptzScale <= 1.0) {
            dptzPanX = 0
            dptzPanY = 0
            return
        }

        var maxX = (videoViewport.width * dptzScale - videoViewport.width) / 2
        var maxY = (videoViewport.height * dptzScale - videoViewport.height) / 2
        dptzPanX = Math.max(-maxX, Math.min(maxX, dptzPanX))
        dptzPanY = Math.max(-maxY, Math.min(maxY, dptzPanY))
    }
    // DPTZ 줌 적용 함수
    function dptzZoom(step) {
        dptzZoomAt(step, videoViewport.width / 2, videoViewport.height / 2)
    }
    // 기준 좌표 DPTZ 줌 적용 함수
    function dptzZoomAt(step, focusX, focusY) {
        if (!dptzEnabled) return

        var oldScale = dptzScale
        var newScale = Math.max(dptzMinScale, Math.min(dptzMaxScale, dptzScale + step))
        if (Math.abs(newScale - oldScale) < 0.0001) return

        // 포인터 위치를 고정점으로 유지하도록 pan 보정
        var cx = videoViewport.width / 2
        var cy = videoViewport.height / 2
        var fx = isFinite(focusX) ? focusX : cx
        var fy = isFinite(focusY) ? focusY : cy

        // 현재 화면 좌표(fx,fy)가 가리키는 원본 좌표를 역변환으로 계산
        var contentX = ((fx - cx - dptzPanX) / oldScale) + cx
        var contentY = ((fy - cy - dptzPanY) / oldScale) + cy

        dptzScale = newScale
        dptzPanX = fx - ((contentX - cx) * newScale + cx)
        dptzPanY = fy - ((contentY - cy) * newScale + cy)

        if (dptzDebugLog) {
            console.log("[DPTZ][QML]",
                        "step=", step,
                        "scale=", newScale,
                        "focus=", fx, fy)
        }
        root.dptzClampPan()
    }
    // DPTZ 사용 여부 변경 처리 함수
    onDptzEnabledChanged: {
        if (!dptzEnabled) dptzReset()
    }

    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgSecondary : "#09090b"
        radius: 8
        border.color: mouseArea.containsMouse ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
        border.width: 2
        clip: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 2
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36
                color: "transparent"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    Column {
                        spacing: 0
                        Text {
                            text: root.titleText.length > 0 ? root.titleText : ("Cam " + (root.tileIndex + 1))
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                            font.pixelSize: 12
                        }
                        Text { text: root.locationName; color: theme ? theme.textSecondary : "#a1a1aa"; font.pixelSize: 10 }
                    }

                    Item { Layout.fillWidth: true }

                    Rectangle {
                        Layout.preferredHeight: 18
                        Layout.preferredWidth: 46
                        color: statusLabel.text === "LIVE" ? (theme ? theme.accent + "33" : "#33f97316") : "#3371717a"
                        radius: 4
                        border.width: 1
                        border.color: statusLabel.text === "LIVE" ? (theme ? theme.accent : "#f97316") : "#71717a"

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4
                            Rectangle {
                                width: 6
                                height: 6
                                radius: 3
                                color: statusLabel.text === "LIVE" ? (theme ? theme.accent : "#f97316") : "#71717a"
                                visible: statusLabel.text === "LIVE"
                            }
                            Text {
                                id: statusLabel
                                text: (vlc.isPlaying && vlc.hasRecentFrames) ? "LIVE" : "OFFLINE"
                                color: text === "LIVE" ? (theme ? theme.accent : "#f97316") : "#71717a"
                                font.bold: true
                                font.pixelSize: 9
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }
            }

            Item {
                id: videoViewport
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true

                Item {
                    id: vlc
                    anchors.fill: parent
                    property string url: ""
                    property bool isPlaying: false
                    property bool hasRecentFrames: false
                    property real fps: 0
                    property int videoWidth: (videoOutput.videoSink && videoOutput.videoSink.videoSize.width > 0) ? videoOutput.videoSink.videoSize.width : 0
                    property int videoHeight: (videoOutput.videoSink && videoOutput.videoSink.videoSize.height > 0) ? videoOutput.videoSink.videoSize.height : 0
                    property int frameCounter: 0
                    property int noFrameSeconds: 0
                    property int reconnectAttempt: 0
                    property int maxReconnectAttempt: 6
                    property bool userPaused: false
                    // 재생 시작 함수
                    function play() {
                        if (url === "")
                            return
                        reconnectTimer.stop()
                        reconnectAttempt = 0
                        userPaused = false
                        if (mediaPlayer.source.toString() !== url) {
                            mediaPlayer.source = url
                        }
                        mediaPlayer.play()
                    }
                    // 재생 중지 함수
                    function stop() {
                        reconnectTimer.stop()
                        mediaPlayer.stop()
                        mediaPlayer.source = ""
                        reconnectAttempt = 0
                        userPaused = false
                        frameCounter = 0
                        fps = 0
                    }
                    // 재연결 예약 함수
                    function scheduleReconnect() {
                        if (url === "")
                            return
                        if (reconnectAttempt >= maxReconnectAttempt)
                            return
                        reconnectAttempt += 1
                        reconnectTimer.interval = Math.min(2000, 250 * reconnectAttempt)
                        reconnectTimer.restart()
                    }
                    // 디지털 줌 설정 함수
                    function setDigitalZoom(scale, focusX, focusY) {
                        // 무동작 처리(줌 기능 QML 변환 구현)
                    }
                    // 재생 상태 변경 처리 함수
                    onIsPlayingChanged: {
                        root.cameraStateChanged(root.cameraIndex, isPlaying)
                        if (!isPlaying) {
                            root.cameraFpsChanged(root.cameraIndex, 0)
                            frameCounter = 0
                            fps = 0
                            hasRecentFrames = false
                            noFrameSeconds = 0
                        }
                    }
                    // FPS 변경 처리 함수
                    onFpsChanged: {
                        root.cameraFpsChanged(root.cameraIndex, Math.max(0, Math.round(fps)))
                        root.cameraStateChanged(root.cameraIndex, isPlaying)
                    }

                    MediaPlayer {
                        id: mediaPlayer
                        audioOutput: null
                        videoOutput: videoOutput
                        // 플레이어 상태 변경 처리 함수
                        onPlaybackStateChanged: {
                            vlc.isPlaying = (playbackState === MediaPlayer.PlayingState)
                            if (vlc.isPlaying) {
                                vlc.reconnectAttempt = 0
                                reconnectTimer.stop()
                            }
                        }
                        // 오류 발생 처리 함수
                        onErrorOccurred: function(error, errorString) {
                            vlc.isPlaying = false
                            console.warn("MediaPlayer error:", error, errorString)
                            if (vlc.userPaused) {
                                return
                            }
                            vlc.scheduleReconnect()
                        }
                    }

                    VideoOutput {
                        id: videoOutput
                        anchors.fill: parent
                        fillMode: VideoOutput.PreserveAspectFit

                        transform: [
                            Scale {
                                origin.x: videoViewport.width / 2
                                origin.y: videoViewport.height / 2
                                xScale: root.dptzEnabled ? root.dptzScale : 1.0
                                yScale: root.dptzEnabled ? root.dptzScale : 1.0
                            },
                            Translate {
                                x: root.dptzEnabled ? root.dptzPanX : 0
                                y: root.dptzEnabled ? root.dptzPanY : 0
                            }
                        ]
                    }

                    Connections {
                        target: videoOutput.videoSink
                        ignoreUnknownSignals: true
                        // 비디오 프레임 변경 처리 함수
                        function onVideoFrameChanged(frame) {
                            if (!vlc.isPlaying)
                                return
                            vlc.frameCounter += 1
                        }
                    }

                    Timer {
                        interval: 1000
                        running: true
                        repeat: true
                        // 트리거 처리 함수
                        onTriggered: {
                            if (!vlc.isPlaying) {
                                if (vlc.fps !== 0)
                                    vlc.fps = 0
                                vlc.frameCounter = 0
                                vlc.hasRecentFrames = false
                                vlc.noFrameSeconds = 0
                                return
                            }
                            vlc.fps = vlc.frameCounter
                            if (vlc.frameCounter > 0) {
                                vlc.hasRecentFrames = true
                                vlc.noFrameSeconds = 0
                            } else {
                                vlc.noFrameSeconds += 1
                                if (vlc.noFrameSeconds >= 2)
                                    vlc.hasRecentFrames = false
                            }
                            vlc.frameCounter = 0
                        }
                    }

                    Timer {
                        id: reconnectTimer
                        interval: 250
                        repeat: false
                        // 트리거 처리 함수
                        onTriggered: {
                            if (vlc.url === "")
                                return
                            mediaPlayer.stop()
                            mediaPlayer.source = ""
                            mediaPlayer.source = vlc.url
                            mediaPlayer.play()
                        }
                    }
                }

                Rectangle {
                    anchors.fill: parent
                    color: "black"
                    z: -1
                    Text {
                        anchors.centerIn: parent
                        text: "NO SIGNAL"
                        color: "#3f3f46"
                        visible: !vlc.isPlaying
                        font.bold: true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: false
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton
                    cursorShape: enabled ? Qt.OpenHandCursor : Qt.ArrowCursor
                    property real lastX: 0
                    property real lastY: 0
                    // 누름 이벤트 처리 함수
                    onPressed: (mouse) => {
                        lastX = mouse.x
                        lastY = mouse.y
                        root.dptzPointerX = mouse.x
                        root.dptzPointerY = mouse.y
                        cursorShape = Qt.ClosedHandCursor
                    }
                    // 해제 이벤트 처리 함수
                    onReleased: cursorShape = Qt.OpenHandCursor
                    // 위치 변경 처리 함수
                    onPositionChanged: (mouse) => {
                        root.dptzPointerX = mouse.x
                        root.dptzPointerY = mouse.y
                        if (!pressed || root.dptzScale <= 1.0) return
                        root.dptzPanX += mouse.x - lastX
                        root.dptzPanY += mouse.y - lastY
                        lastX = mouse.x
                        lastY = mouse.y
                        root.dptzClampPan()
                    }
                    // 더블 클릭 처리 함수
                    onDoubleClicked: (mouse) => { mouse.accepted = false }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.dptzEnabled
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    // 위치 변경 처리 함수
                    onPositionChanged: (mouse) => {
                        root.dptzPointerX = mouse.x
                        root.dptzPointerY = mouse.y
                    }
                    // 휠 입력 처리 함수
                    onWheel: (wheel) => {
                        if (!root.dptzFocusLocked) {
                            root.dptzLockedFocusX = wheel.x
                            root.dptzLockedFocusY = wheel.y
                            root.dptzFocusLocked = true
                        }
                        focusUnlockTimer.restart()
                        root.dptzPointerX = root.dptzLockedFocusX
                        root.dptzPointerY = root.dptzLockedFocusY
                        if (wheel.angleDelta.y > 0) root.dptzZoomAt(0.2, root.dptzLockedFocusX, root.dptzLockedFocusY)
                        else if (wheel.angleDelta.y < 0) root.dptzZoomAt(-0.2, root.dptzLockedFocusX, root.dptzLockedFocusY)
                        wheel.accepted = true
                    }
                }

                Timer {
                    id: focusUnlockTimer
                    interval: 180
                    repeat: false
                    // 트리거 처리 함수
                    onTriggered: root.dptzFocusLocked = false
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: "transparent"

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8

                    RowLayout {
                        spacing: 4
                        Rectangle { width: 6; height: 6; radius: 3; color: theme ? theme.accent : "#f97316"; visible: true }
                        Text { text: "REC"; color: theme ? theme.accent : "#f97316"; font.bold: true; font.pixelSize: 10 }
                    }

                    Text {
                        text: "1080P | " + Math.max(0, Math.round(vlc.fps)) + "FPS"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 10
                    }

                    Item { Layout.fillWidth: true }

                    RowLayout {
                        visible: root.dptzEnabled
                        spacing: 6

                        Rectangle {
                            Layout.preferredWidth: 116
                            Layout.preferredHeight: 24
                            radius: 6
                            color: theme ? theme.bgComponent : "#18181b"
                            border.width: 1
                            border.color: theme ? theme.border : "#27272a"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 2
                                anchors.rightMargin: 2
                                spacing: 2

                                Rectangle {
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 20
                                    radius: 4
                                    color: minusMouse.pressed
                                           ? (theme ? theme.bgSecondary : "#09090b")
                                           : (minusMouse.containsMouse ? (theme ? theme.border : "#3f3f46") : "transparent")
                                    border.width: 1
                                    border.color: theme ? theme.border : "#27272a"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "-"
                                        color: theme ? theme.textPrimary : "white"
                                        font.bold: true
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        id: minusMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        // 클릭 이벤트 처리 함수
                                        onClicked: {
                                            var fx = root.dptzPointerX > 0 ? root.dptzPointerX : videoViewport.width / 2
                                            var fy = root.dptzPointerY > 0 ? root.dptzPointerY : videoViewport.height / 2
                                            root.dptzZoomAt(-0.2, fx, fy)
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 20
                                    radius: 4
                                    color: theme ? theme.bgSecondary : "#09090b"
                                    border.width: 1
                                    border.color: theme ? theme.border : "#27272a"
                                    Text {
                                        anchors.centerIn: parent
                                        text: Number(root.dptzScale).toFixed(1) + "x"
                                        color: theme ? theme.textPrimary : "white"
                                        font.bold: true
                                        font.pixelSize: 11
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        // 클릭 이벤트 처리 함수
                                        onClicked: root.dptzReset()
                                    }
                                }

                                Rectangle {
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 20
                                    radius: 4
                                    color: plusMouse.pressed
                                           ? (theme ? theme.bgSecondary : "#09090b")
                                           : (plusMouse.containsMouse ? (theme ? theme.border : "#3f3f46") : "transparent")
                                    border.width: 1
                                    border.color: theme ? theme.border : "#27272a"
                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        color: theme ? theme.textPrimary : "white"
                                        font.bold: true
                                        font.pixelSize: 12
                                    }
                                    MouseArea {
                                        id: plusMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        // 클릭 이벤트 처리 함수
                                        onClicked: {
                                            var fx = root.dptzPointerX > 0 ? root.dptzPointerX : videoViewport.width / 2
                                            var fy = root.dptzPointerY > 0 ? root.dptzPointerY : videoViewport.height / 2
                                            root.dptzZoomAt(0.2, fx, fy)
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Text {
                        id: timeLabel
                        text: Qt.formatTime(new Date(), "hh:mm:ss")
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.family: "Courier New"
                        font.pixelSize: 10
                    }

                    Timer {
                        interval: 1000
                        running: true
                        repeat: true
                        // 트리거 처리 함수
                        onTriggered: timeLabel.text = Qt.formatTime(new Date(), "hh:mm:ss")
                    }
                }
            }
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            enabled: true
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            // 클릭 이벤트 처리 함수
            onClicked: parent.forceActiveFocus()
            // 더블 클릭 처리 함수
            onDoubleClicked: root.doubleClicked()
        }

    }
    // 소스 변경 처리 함수
    onSourceChanged: {
        if (root.source && root.source.length > 0) {
            console.log("[VIDEO][SOURCE]", root.source)
        }
        vlc.url = root.source
        root.dptzReset()
        startPlayTimer.stop()

        if (root.source !== "") {
            if (startDelayMs > 0) {
                startPlayTimer.interval = startDelayMs
                startPlayTimer.start()
            } else {
                vlc.play()
            }
        } else {
            vlc.stop()
        }
    }

    Timer {
        id: startPlayTimer
        interval: 0
        repeat: false
        // 트리거 처리 함수
        onTriggered: {
            if (root.source !== "") {
                vlc.play()
            }
        }
    }
}
