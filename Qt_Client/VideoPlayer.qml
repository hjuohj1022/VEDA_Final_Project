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

    function restartStream() {
        if (root.source === "")
            return
        startPlayTimer.stop()
        vlc.stop()
        // Small delay helps camera-side stream reconfiguration settle.
        startPlayTimer.interval = 350
        startPlayTimer.start()
    }

    function dptzReset() {
        dptzScale = 1.0
        dptzPanX = 0
        dptzPanY = 0
        vlc.setDigitalZoom(dptzScale, 0.5, 0.5)
    }

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

    function dptzZoom(step) {
        dptzZoomAt(step, videoViewport.width / 2, videoViewport.height / 2)
    }

    function dptzZoomAt(step, focusX, focusY) {
        if (!dptzEnabled) return

        var oldScale = dptzScale
        var newScale = Math.max(dptzMinScale, Math.min(dptzMaxScale, dptzScale + step))
        if (Math.abs(newScale - oldScale) < 0.0001) return

        dptzScale = newScale

        var viewW = Math.max(1, videoViewport.width)
        var viewH = Math.max(1, videoViewport.height)
        var srcW = vlc.videoWidth > 0 ? vlc.videoWidth : 1920
        var srcH = vlc.videoHeight > 0 ? vlc.videoHeight : 1080
        var srcAspect = srcW / Math.max(1, srcH)
        var viewAspect = viewW / Math.max(1, viewH)

        var contentX = 0
        var contentY = 0
        var contentW = viewW
        var contentH = viewH

        if (viewAspect > srcAspect) {
            contentW = viewH * srcAspect
            contentX = (viewW - contentW) / 2
        } else if (viewAspect < srcAspect) {
            contentH = viewW / srcAspect
            contentY = (viewH - contentH) / 2
        }

        var nx = (focusX - contentX) / Math.max(1, contentW)
        var ny = (focusY - contentY) / Math.max(1, contentH)
        nx = Math.max(0, Math.min(1, nx))
        ny = Math.max(0, Math.min(1, ny))
        if (dptzDebugLog) {
            console.log("[DPTZ][QML]",
                        "step=", step,
                        "scale=", newScale,
                        "focus=", focusX, focusY,
                        "view=", viewW, viewH,
                        "src=", srcW, srcH,
                        "content=", contentX, contentY, contentW, contentH,
                        "norm=", nx, ny)
        }
        vlc.setDigitalZoom(dptzScale, nx, ny)
    }

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
                                text: vlc.isPlaying ? "LIVE" : "OFFLINE"
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

                VlcPlayer {
                    id: vlc
                    width: videoViewport.width
                    height: videoViewport.height
                    x: 0
                    y: 0

                    onIsPlayingChanged: {
                        root.cameraStateChanged(root.cameraIndex, vlc.isPlaying)
                        if (!vlc.isPlaying) root.cameraFpsChanged(root.cameraIndex, 0)
                    }
                    onFpsChanged: {
                        var roundedFps = Math.max(0, Math.round(vlc.fps))
                        root.cameraFpsChanged(root.cameraIndex, roundedFps)
                        root.cameraStateChanged(root.cameraIndex, vlc.isPlaying)
                    }

                    onVideoDrag: function(dx, dy) {
                        // Native VLC window mode: drag-pan is intentionally disabled to keep
                        // zoom strictly inside the camera frame.
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
                    onPressed: (mouse) => {
                        lastX = mouse.x
                        lastY = mouse.y
                        root.dptzPointerX = mouse.x
                        root.dptzPointerY = mouse.y
                        cursorShape = Qt.ClosedHandCursor
                    }
                    onReleased: cursorShape = Qt.OpenHandCursor
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
                    onDoubleClicked: (mouse) => { mouse.accepted = false }
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.dptzEnabled
                    hoverEnabled: true
                    acceptedButtons: Qt.NoButton
                    onPositionChanged: (mouse) => {
                        root.dptzPointerX = mouse.x
                        root.dptzPointerY = mouse.y
                    }
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
                        spacing: 4

                        Button {
                            text: "-"
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 22
                            onClicked: {
                                var fx = root.dptzPointerX > 0 ? root.dptzPointerX : videoViewport.width / 2
                                var fy = root.dptzPointerY > 0 ? root.dptzPointerY : videoViewport.height / 2
                                root.dptzZoomAt(-0.2, fx, fy)
                            }
                        }

                        Button {
                            text: Number(root.dptzScale).toFixed(1) + "x"
                            Layout.preferredWidth: 42
                            Layout.preferredHeight: 22
                            onClicked: root.dptzReset()
                        }

                        Button {
                            text: "+"
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 22
                            onClicked: {
                                var fx = root.dptzPointerX > 0 ? root.dptzPointerX : videoViewport.width / 2
                                var fy = root.dptzPointerY > 0 ? root.dptzPointerY : videoViewport.height / 2
                                root.dptzZoomAt(0.2, fx, fy)
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
            onClicked: parent.forceActiveFocus()
            onDoubleClicked: root.doubleClicked()
        }

    }

    onSourceChanged: {
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
        onTriggered: {
            if (root.source !== "") {
                vlc.play()
            }
        }
    }
}
