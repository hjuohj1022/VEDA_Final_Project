import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Team3VideoReceiver 1.0

Item {
    id: root
    property var theme
    property string playbackSource: ""
    property string playbackTitle: "Playback Stream"
    property int playbackCurrentSeconds: 0
    property var playbackSegments: []
    property string playbackTimelineInfoText: "녹화 구간 없음"

    // 줌 레벨 단계(총 23단계: 0~22)
    // 0: 24h, 1~4: 점진 확대, 5~6: 15분 라벨, 7~10: 10분 라벨,
    // 11~19: 5분 라벨, 20~22: 1분 라벨
    property int timelineZoomLevel: 0
    property var timelineWindows: [
        86400, 64800, 48600, 36000, 25200,
        18000, 12600,
        9000, 7200, 6000, 4800,
        3600, 3000, 2400, 2100, 1800, 1500, 1200, 900, 720,
        600, 480, 360
    ]
    property int timelineViewStartSec: 0

    signal seekRequested(int seconds)

    function pad2(v) { return (v < 10 ? "0" : "") + v }
    function fmt(sec) {
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        return pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }

    function currentWindowSec() {
        return Number(timelineWindows[Math.max(0, Math.min(timelineZoomLevel, timelineWindows.length - 1))])
    }

    function clampViewStart(startSec, windowSec) {
        var s = Math.floor(startSec)
        var w = Math.max(60, Math.floor(windowSec))
        var maxStart = Math.max(0, 86400 - w)
        if (s < 0) s = 0
        if (s > maxStart) s = maxStart
        return s
    }

    function viewEndSec() {
        return Math.min(86400, timelineViewStartSec + currentWindowSec())
    }

    function ensureCurrentInView() {
        var w = currentWindowSec()
        var v0 = timelineViewStartSec
        var v1 = v0 + w
        if (playbackCurrentSeconds < v0 || playbackCurrentSeconds >= v1) {
            timelineViewStartSec = clampViewStart(playbackCurrentSeconds - (w / 2), w)
        }
    }

    function labelStepSec() {
        var lv = timelineZoomLevel
        if (lv <= 4) return 3600
        if (lv <= 6) return 900
        if (lv <= 10) return 600
        if (lv <= 19) return 300
        return 60
    }

    function labelCount() {
        var step = labelStepSec()
        var range = Math.max(1, viewEndSec() - timelineViewStartSec)
        return Math.floor((range - 1) / step) + 1
    }

    function labelSecondAt(i) {
        return Math.min(viewEndSec() - 1, timelineViewStartSec + (i * labelStepSec()))
    }

    function labelTextAt(i) {
        var sec = labelSecondAt(i)
        var hh = Math.floor(sec / 3600)
        var mm = Math.floor((sec % 3600) / 60)
        return pad2(hh) + ":" + pad2(mm)
    }

    function snapToZoomUnit(sec) {
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        if (timelineZoomLevel <= 0)
            return s
        var unit = (timelineZoomLevel >= 20) ? 60 : 300
        return Math.max(0, Math.min(86399, Math.round(s / unit) * unit))
    }

    function displaySegments() {
        if (!playbackSegments || playbackSegments.length === 0)
            return []
        var arr = []
        for (var i = 0; i < playbackSegments.length; i++) {
            var seg = playbackSegments[i]
            var a = Math.max(0, Math.min(86399, Number(seg.start || 0)))
            var b = Math.max(0, Math.min(86399, Number(seg.end || 0)))
            arr.push({ start: Math.min(a, b), end: Math.max(a, b) })
        }
        arr.sort(function(lhs, rhs) { return lhs.start - rhs.start })
        var merged = []
        for (var j = 0; j < arr.length; j++) {
            var cur = arr[j]
            if (merged.length === 0) {
                merged.push({ start: cur.start, end: cur.end })
                continue
            }
            var last = merged[merged.length - 1]
            // 렌더링 기준에서는 2초 이하 간격은 연결 구간으로 처리
            if (cur.start <= (last.end + 2)) {
                if (cur.end > last.end)
                    last.end = cur.end
            } else {
                merged.push({ start: cur.start, end: cur.end })
            }
        }
        return merged
    }

    function isSecondRecorded(sec) {
        if (!playbackSegments || playbackSegments.length === 0)
            return false
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        for (var i = 0; i < playbackSegments.length; i++) {
            var seg = playbackSegments[i]
            var a = Math.max(0, Math.min(86399, Number(seg.start || 0)))
            var b = Math.max(0, Math.min(86399, Number(seg.end || 0)))
            var lo = Math.min(a, b)
            var hi = Math.max(a, b)
            if (s >= lo && s <= hi)
                return true
        }
        return false
    }

    function pausePlayback() {
        playbackPlayer.pauseStream()
    }

    function resumePlayback() {
        playbackPlayer.resumeStream()
    }

    onPlaybackCurrentSecondsChanged: ensureCurrentInView()

    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgSecondary : "#09090b"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"
                border.width: 0
                radius: 8

                VideoPlayer {
                    id: playbackPlayer
                    anchors.fill: parent
                    theme: root.theme
                    titleText: root.playbackTitle
                    locationName: "Playback"
                    cameraIndex: -1
                    dptzEnabled: false
                    startDelayMs: 0
                    source: root.playbackSource
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 108
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6

                    Item {
                        id: timelineScale
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26

                        WheelHandler {
                            target: timelineScale
                            onWheel: function(event) {
                                var oldLevel = root.timelineZoomLevel
                                var newLevel = oldLevel
                                if (event.angleDelta.y > 0)
                                    newLevel = Math.min(oldLevel + 1, root.timelineWindows.length - 1)
                                else if (event.angleDelta.y < 0)
                                    newLevel = Math.max(oldLevel - 1, 0)

                                if (newLevel === oldLevel) {
                                    event.accepted = true
                                    return
                                }

                                var oldWindow = root.currentWindowSec()
                                var px = timelineScale.width / 2
                                if (event && event.x !== undefined) {
                                    px = event.x
                                } else if (event && event.point && event.point.position && event.point.position.x !== undefined) {
                                    px = event.point.position.x
                                }
                                var ratio = Math.max(0, Math.min(1, px / Math.max(1, timelineScale.width)))
                                var anchorSec = root.timelineViewStartSec + (ratio * oldWindow)

                                root.timelineZoomLevel = newLevel
                                var newWindow = root.currentWindowSec()
                                var newStart = anchorSec - (ratio * newWindow)
                                if (newLevel > 0)
                                    newStart = Math.round(newStart / 300) * 300
                                root.timelineViewStartSec = root.clampViewStart(newStart, newWindow)
                                event.accepted = true
                            }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: theme ? theme.border : "#3f3f46"
                        }

                        Repeater {
                            model: root.timelineZoomLevel === 0 ? 24 : 0
                            delegate: Item {
                                property real colW: parent.width / 24.0
                                x: index * colW
                                width: colW
                                height: parent.height

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 1
                                    height: 6
                                    color: theme ? theme.border : "#52525b"
                                }

                                Text {
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 8
                                    text: (index < 10 ? "0" : "") + index + ":00"
                                    color: theme ? theme.textSecondary : "#a1a1aa"
                                    font.pixelSize: 10
                                    x: {
                                        if (index === 0) return 0
                                        if (index === 23) return parent.width - implicitWidth
                                        return (parent.width - implicitWidth) / 2
                                    }
                                }
                            }
                        }

                        Repeater {
                            model: root.timelineZoomLevel !== 0 ? root.labelCount() : 0
                            delegate: Item {
                                width: 1
                                height: parent.height
                                property real sec: root.labelSecondAt(index)
                                property real range: Math.max(1, root.viewEndSec() - root.timelineViewStartSec)
                                property real mapped: Math.round(((sec - root.timelineViewStartSec) / range) * (parent.width - 1))
                                x: mapped

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: 1
                                    height: 6
                                    color: theme ? theme.border : "#52525b"
                                }

                                Text {
                                    anchors.bottom: parent.bottom
                                    anchors.bottomMargin: 8
                                    x: -Math.floor(implicitWidth / 2)
                                    text: root.labelTextAt(index)
                                    color: theme ? theme.textSecondary : "#a1a1aa"
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26

                        Slider {
                            id: timelineSlider
                            anchors.fill: parent
                            from: root.timelineViewStartSec
                            to: Math.max(root.timelineViewStartSec + 1, root.viewEndSec() - 1)
                            stepSize: 1
                            value: Math.max(from, Math.min(to, root.playbackCurrentSeconds))

                            background: Item {
                                implicitHeight: 12
                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    height: 8
                                    radius: 4
                                    color: "#3f3f46"
                                }
                                Repeater {
                                    model: root.displaySegments()
                                    delegate: Rectangle {
                                        property real startSecRaw: Math.max(0, Math.min(86399, modelData.start || 0))
                                        property real endSecRaw: Math.max(0, Math.min(86399, modelData.end || 0))
                                        property real segLo: Math.min(startSecRaw, endSecRaw)
                                        property real segHi: Math.max(startSecRaw, endSecRaw)
                                        property real viewLo: root.timelineViewStartSec
                                        property real viewHi: root.viewEndSec()
                                        property real clipLo: Math.max(segLo, viewLo)
                                        property real clipHi: Math.min(segHi, viewHi)
                                        property real range: Math.max(1, viewHi - viewLo)
                                        property real leftPos: ((clipLo - viewLo) / range) * parent.width
                                        property real rightPos: ((clipHi - viewLo) / range) * parent.width
                                        visible: clipHi >= clipLo
                                        x: leftPos
                                        width: Math.max(1, rightPos - leftPos)
                                        y: (parent.height - 8) / 2
                                        height: 8
                                        radius: 2
                                        color: "#65a30d"
                                        opacity: 0.95
                                    }
                                }
                            }

                            handle: Rectangle {
                                x: timelineSlider.visualPosition * (timelineSlider.availableWidth - width)
                                y: timelineSlider.topPadding + (timelineSlider.availableHeight - height) / 2
                                width: 18
                                height: 18
                                radius: 9
                                color: "#f3f4f6"
                                border.color: "#d4d4d8"
                            }

                            onMoved: root.seekRequested(root.snapToZoomUnit(value))
                            onPressedChanged: {
                                if (!pressed) {
                                    if (!root.isSecondRecorded(value))
                                        return
                                    root.seekRequested(root.snapToZoomUnit(value))
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: root.playbackTimelineInfoText
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 11
                        }
                        Item { Layout.fillWidth: true }
                        Label {
                            text: root.fmt(root.playbackCurrentSeconds)
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 11
                        }
                    }
                }
            }
        }
    }
}
