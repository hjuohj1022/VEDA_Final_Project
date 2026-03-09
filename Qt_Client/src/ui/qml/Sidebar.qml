import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property var cameraNames: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
    property bool showCameraControls: false
    property bool showPlaybackControls: false
    property int selectedCameraIndex: -1
    property string cameraControlStatus: ""
    property bool cameraControlError: false
    property bool mapModeEnabled: false
    property bool supportZoom: true
    property bool supportFocus: true
    property int displayContrast: 50
    property int displayBrightness: 50
    property int displaySharpnessLevel: 12
    property bool displaySharpnessEnabled: true
    property int displayColorLevel: 50
    property int playbackChannelIndex: 0
    property bool playbackRunning: false
    property bool playbackPending: false
    property int playbackCurrentSeconds: -1
    property date playbackSelectedDate: new Date()
    property string playbackDateText: ""
    property string playbackTimeText: ""
    property string playbackExportStartText: ""
    property string playbackExportEndText: ""
    property var playbackSegments: []
    property bool playbackTimeInRange: false
    property string playbackTimelineInfoText: "녹화 구간 없음"
    property int playbackViewYear: playbackSelectedDate.getFullYear()
    property int playbackViewMonth: playbackSelectedDate.getMonth()
    property bool playbackCalendarVisible: false
    property var playbackRecordedDays: []
    signal requestCameraNameChange(int cameraIndex, string name)
    signal requestPlayback(int channelIndex, string dateText, string timeText)
    signal requestPlaybackTimeline(int channelIndex, string dateText)
    signal requestPlaybackMonthDays(int channelIndex, int year, int month)
    signal requestPlaybackExport(int channelIndex, string dateText, string startTimeText, string endTimeText)
    signal requestPlaybackPause()
    signal requestPlaybackResume()
    color: theme ? theme.bgSecondary : "#09090b"

    function pad2(v) { return (v < 10 ? "0" : "") + v }
    function formatPlaybackDate() {
        return playbackSelectedDate.getFullYear() + "-" + pad2(playbackSelectedDate.getMonth() + 1) + "-" + pad2(playbackSelectedDate.getDate())
    }
    function formatPlaybackTime() {
        var s = playbackCurrentSeconds >= 0 ? playbackCurrentSeconds : 0
        return pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }
    function syncTimeFromSeconds(sec) {
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        playbackCurrentSeconds = s
        playbackTimeText = pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }
    function syncSecondsFromFields() {
        var parts = playbackTimeText.split(":")
        if (parts.length !== 3)
            return false
        var h = parseInt(parts[0], 10)
        var m = parseInt(parts[1], 10)
        var s = parseInt(parts[2], 10)
        if (isNaN(h) || isNaN(m) || isNaN(s))
            return false
        if (h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
            return false
        playbackCurrentSeconds = (h * 3600) + (m * 60) + s
        playbackTimeText = pad2(h) + ":" + pad2(m) + ":" + pad2(s)
        return true
    }
    function syncDateFromField() {
        var t = playbackDateText.trim()
        var m = /^(\d{4})-(\d{2})-(\d{2})$/.exec(t)
        if (!m)
            return false
        var y = parseInt(m[1], 10)
        var mo = parseInt(m[2], 10)
        var d = parseInt(m[3], 10)
        var dt = new Date(y, mo - 1, d)
        if (dt.getFullYear() !== y || (dt.getMonth() + 1) !== mo || dt.getDate() !== d)
            return false
        playbackSelectedDate = dt
        playbackViewYear = dt.getFullYear()
        playbackViewMonth = dt.getMonth()
        playbackDateText = formatPlaybackDate()
        return true
    }
    function isValidHmsText(t) {
        var m = /^(\d{2}):(\d{2}):(\d{2})$/.exec((t || "").trim())
        if (!m) return false
        var h = parseInt(m[1], 10)
        var mm = parseInt(m[2], 10)
        var s = parseInt(m[3], 10)
        return h >= 0 && h <= 23 && mm >= 0 && mm <= 59 && s >= 0 && s <= 59
    }
    function secToHms(sec) {
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        return pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }
    function applyExportRangeFromSecond(sec) {
        var startSec = Math.max(0, Math.min(86399, Math.floor(sec)))
        var endSec = Math.max(startSec, Math.min(86399, startSec + 300))
        playbackExportStartText = secToHms(startSec)
        playbackExportEndText = secToHms(endSec)
    }
    function syncExportStartFromFields() {
        var hh = parseInt(playbackExportStartHourField.text, 10)
        var mm = parseInt(playbackExportStartMinuteField.text, 10)
        var ss = parseInt(playbackExportStartSecondField.text, 10)
        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
            return false
        playbackExportStartText = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss)
        return true
    }
    function syncExportEndFromFields() {
        var hh = parseInt(playbackExportEndHourField.text, 10)
        var mm = parseInt(playbackExportEndMinuteField.text, 10)
        var ss = parseInt(playbackExportEndSecondField.text, 10)
        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
            return false
        playbackExportEndText = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss)
        return true
    }
    function requestTimelineIfValid() {
        if (!syncDateFromField())
            return
        requestPlaybackTimeline(playbackChannelIndex, playbackDateText)
    }
    function requestMonthDays() {
        playbackRecordedDays = []
        requestPlaybackMonthDays(playbackChannelIndex, playbackViewYear, playbackViewMonth + 1)
    }
    function selectPlaybackChannel(index) {
        var i = Math.max(0, Math.min(3, Number(index)))
        if (playbackChannelIndex === i)
            return
        playbackChannelIndex = i
        requestTimelineIfValid()
        requestMonthDays()
    }
    function isRecordedDay(day) {
        if (!playbackRecordedDays || playbackRecordedDays.length === 0)
            return false
        for (var i = 0; i < playbackRecordedDays.length; i++) {
            if (Number(playbackRecordedDays[i]) === day)
                return true
        }
        return false
    }
    function isTodayInViewDay(day) {
        var now = new Date()
        return now.getFullYear() === playbackViewYear
            && now.getMonth() === playbackViewMonth
            && now.getDate() === day
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
    function updateTimelineInfo() {
        if (!playbackSegments || playbackSegments.length === 0) {
            playbackTimelineInfoText = "녹화 구간 없음"
            playbackTimeInRange = false
            return
        }
        var ranges = []
        for (var i = 0; i < playbackSegments.length; i++) {
            var seg = playbackSegments[i]
            var a = Math.max(0, Math.min(86399, Number(seg.start || 0)))
            var b = Math.max(0, Math.min(86399, Number(seg.end || 0)))
            var lo = Math.min(a, b)
            var hi = Math.max(a, b)
            ranges.push({ start: lo, end: hi })
        }
        if (ranges.length === 0) {
            playbackTimelineInfoText = "녹화 구간 없음"
            playbackTimeInRange = false
            return
        }

        ranges.sort(function(lhs, rhs) { return lhs.start - rhs.start })

        // 인접/겹침 구간 병합(1초 이내 간격은 연결 구간으로 처리)
        var merged = []
        for (var j = 0; j < ranges.length; j++) {
            var cur = ranges[j]
            if (merged.length === 0) {
                merged.push({ start: cur.start, end: cur.end })
                continue
            }
            var last = merged[merged.length - 1]
            if (cur.start <= (last.end + 1)) {
                if (cur.end > last.end)
                    last.end = cur.end
            } else {
                merged.push({ start: cur.start, end: cur.end })
            }
        }

        var fmt = function(v) {
            return pad2(Math.floor(v / 3600)) + ":" + pad2(Math.floor((v % 3600) / 60)) + ":" + pad2(v % 60)
        }
        var cursorSec = (playbackCurrentSeconds >= 0 ? playbackCurrentSeconds : 0)
        var selectedSeg = null
        for (var k = 0; k < merged.length; k++) {
            if (cursorSec >= merged[k].start && cursorSec <= merged[k].end) {
                selectedSeg = merged[k]
                break
            }
        }

        if (selectedSeg) {
            playbackTimelineInfoText = "녹화 구간: " + fmt(selectedSeg.start) + " ~ " + fmt(selectedSeg.end)
            playbackTimeInRange = true
        } else {
            playbackTimelineInfoText = "녹화 구간 없음 (선택 시간)"
            playbackTimeInRange = false
        }
    }
    function applyPlaybackStart(dateText, timeText) {
        var d = new Date(dateText + "T00:00:00")
        if (!isNaN(d.getTime())) {
            playbackSelectedDate = d
            playbackViewYear = d.getFullYear()
            playbackViewMonth = d.getMonth()
            playbackDateText = formatPlaybackDate()
        }
        var parts = timeText.split(":")
        if (parts.length === 3) {
            var hh = Math.max(0, Math.min(23, parseInt(parts[0], 10) || 0))
            var mm = Math.max(0, Math.min(59, parseInt(parts[1], 10) || 0))
            var ss = Math.max(0, Math.min(59, parseInt(parts[2], 10) || 0))
            playbackTimeText = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss)
        }
        syncSecondsFromFields()
        if (isValidHmsText(playbackTimeText)) {
            playbackExportStartText = playbackTimeText
            applyExportRangeFromSecond(playbackCurrentSeconds)
        }
    }
    function daysInViewMonth() {
        return new Date(playbackViewYear, playbackViewMonth + 1, 0).getDate()
    }
    function firstDayOffset() {
        return new Date(playbackViewYear, playbackViewMonth, 1).getDay()
    }
    function monthLabel() {
        var m = playbackViewMonth + 1
        return playbackViewYear + "-" + pad2(m)
    }

    function selectedCameraTitle() {
        if (selectedCameraIndex < 0 || selectedCameraIndex >= cameraNames.length)
            return "Camera"
        return "Cam " + (selectedCameraIndex + 1) + " - " + cameraNames[selectedCameraIndex]
    }

    function applyDisplaySettings() {
        if (!showCameraControls || selectedCameraIndex < 0)
            return
        backend.sunapiSetDisplaySettings(
                    selectedCameraIndex,
                    displayContrast,
                    displayBrightness,
                    displaySharpnessLevel,
                    displayColorLevel,
                    displaySharpnessEnabled)
    }

    Connections {
        target: backend
        function onCameraControlMessage(message, isError) {
            if (!root.showCameraControls)
                return
            root.cameraControlStatus = message
            root.cameraControlError = isError
            controlStatusTimer.restart()
        }
        function onDisplaySettingsChanged() {
            root.displayContrast = backend.displayContrast
            root.displayBrightness = backend.displayBrightness
            root.displaySharpnessLevel = backend.displaySharpnessLevel
            root.displaySharpnessEnabled = backend.displaySharpnessEnabled
            root.displayColorLevel = backend.displayColorLevel
        }
    }

    Timer {
        id: controlStatusTimer
        interval: 3000
        repeat: false
        onTriggered: {
            root.cameraControlStatus = ""
            root.cameraControlError = false
        }
    }

    component ControlButton: Button {
        id: controlBtn
        property bool accentStyle: false
        property bool compact: false
        implicitHeight: compact ? 30 : 40
        hoverEnabled: enabled
        scale: controlBtn.down ? 0.97 : 1.0

        Behavior on scale {
            NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
        }

        background: Rectangle {
            radius: 6
            border.width: 1
            border.color: controlBtn.accentStyle
                          ? (controlBtn.enabled ? (controlBtn.hovered ? "#fb923c" : theme.accent) : theme.border)
                          : (controlBtn.hovered ? "#3f3f46" : theme.border)
            color: !controlBtn.enabled
                   ? (theme ? theme.bgComponent : "#18181b")
                   : controlBtn.down
                     ? (controlBtn.accentStyle ? "#ea580c" : (theme ? theme.bgSecondary : "#09090b"))
                     : controlBtn.accentStyle
                       ? (theme ? theme.accent : "#f97316")
                       : (theme ? theme.bgSecondary : "#09090b")
        }

        contentItem: Text {
            text: controlBtn.text
            color: !controlBtn.enabled
                   ? (theme ? theme.textSecondary : "#71717a")
                   : (controlBtn.accentStyle ? "white" : (theme ? theme.textPrimary : "white"))
            font.pixelSize: compact ? 12 : 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    onShowCameraControlsChanged: {
        if (showCameraControls && selectedCameraIndex >= 0) {
            cameraNameField.text = (selectedCameraIndex < cameraNames.length) ? cameraNames[selectedCameraIndex] : ""
            backend.sunapiLoadDisplaySettings(selectedCameraIndex)
        }
    }

    Timer {
        id: playbackTick
        interval: 1000
        repeat: true
        running: root.showPlaybackControls && root.playbackRunning && !root.playbackPending
        onTriggered: root.syncTimeFromSeconds(root.playbackCurrentSeconds + 1)
    }

    onSelectedCameraIndexChanged: {
        if (showCameraControls && selectedCameraIndex >= 0) {
            supportZoom = true
            supportFocus = true
            cameraNameField.text = (selectedCameraIndex < cameraNames.length) ? cameraNames[selectedCameraIndex] : ""
            backend.sunapiLoadDisplaySettings(selectedCameraIndex)
        }
    }
    onShowPlaybackControlsChanged: {
        if (showPlaybackControls && playbackCurrentSeconds < 0) {
            var now = new Date()
            playbackSelectedDate = new Date(now.getFullYear(), now.getMonth(), now.getDate())
            playbackViewYear = playbackSelectedDate.getFullYear()
            playbackViewMonth = playbackSelectedDate.getMonth()
            playbackDateText = formatPlaybackDate()
            playbackTimeText = "00:00:00"
            playbackCurrentSeconds = 0
            playbackExportStartText = "00:00:00"
            playbackExportEndText = "00:00:00"
        }
        if (showPlaybackControls) {
            requestTimelineIfValid()
            requestMonthDays()
        }
    }
    onPlaybackViewYearChanged: {
        if (showPlaybackControls)
            requestMonthDays()
    }
    onPlaybackViewMonthChanged: {
        if (showPlaybackControls)
            requestMonthDays()
    }
    onPlaybackCurrentSecondsChanged: {
        playbackTimeInRange = isSecondRecorded(playbackCurrentSeconds >= 0 ? playbackCurrentSeconds : 0)
        updateTimelineInfo()
    }
    onPlaybackDateTextChanged: {
        if (playbackDateTextField && playbackDateTextField.text !== playbackDateText)
            playbackDateTextField.text = playbackDateText
    }
    onPlaybackTimeTextChanged: {
        var parts = playbackTimeText.split(":")
        if (parts.length !== 3)
            return
        if (playbackHourField && playbackHourField.text !== parts[0])
            playbackHourField.text = parts[0]
        if (playbackMinuteField && playbackMinuteField.text !== parts[1])
            playbackMinuteField.text = parts[1]
        if (playbackSecondField && playbackSecondField.text !== parts[2])
            playbackSecondField.text = parts[2]
        if (!root.playbackRunning && !root.playbackPending && root.isValidHmsText(root.playbackTimeText)) {
            if (!root.isValidHmsText(root.playbackExportStartText))
                root.playbackExportStartText = root.playbackTimeText
            if (!root.isValidHmsText(root.playbackExportEndText))
                root.playbackExportEndText = root.playbackTimeText
        }
    }
    onPlaybackSegmentsChanged: {
        updateTimelineInfo()
    }
    onPlaybackExportStartTextChanged: {
        var parts = playbackExportStartText.split(":")
        if (parts.length !== 3)
            return
        if (playbackExportStartHourField && playbackExportStartHourField.text !== parts[0])
            playbackExportStartHourField.text = parts[0]
        if (playbackExportStartMinuteField && playbackExportStartMinuteField.text !== parts[1])
            playbackExportStartMinuteField.text = parts[1]
        if (playbackExportStartSecondField && playbackExportStartSecondField.text !== parts[2])
            playbackExportStartSecondField.text = parts[2]
    }
    onPlaybackExportEndTextChanged: {
        var parts = playbackExportEndText.split(":")
        if (parts.length !== 3)
            return
        if (playbackExportEndHourField && playbackExportEndHourField.text !== parts[0])
            playbackExportEndHourField.text = parts[0]
        if (playbackExportEndMinuteField && playbackExportEndMinuteField.text !== parts[1])
            playbackExportEndMinuteField.text = parts[1]
        if (playbackExportEndSecondField && playbackExportEndSecondField.text !== parts[2])
            playbackExportEndSecondField.text = parts[2]
    }
    
    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: theme ? theme.border : "#27272a"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // 우측 패널 상단 제목
        Text {
            text: root.showCameraControls
                  ? "Camera Controls"
                  : (root.showPlaybackControls ? "Playback Controls" : "System Metrics")
            color: theme ? theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14
        }

        Rectangle {
            visible: root.showPlaybackControls
            Layout.fillWidth: true
            Layout.fillHeight: root.showPlaybackControls
            Layout.minimumHeight: root.showPlaybackControls ? 500 : 0
            clip: true
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Label {
                        text: "CH"
                        color: theme ? theme.textPrimary : "white"
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignVCenter
                    }

                    Repeater {
                        model: 4
                        delegate: Button {
                            id: channelBtn
                            required property int index
                            Layout.preferredWidth: 34
                            Layout.preferredHeight: 30
                            text: String(index + 1)
                            hoverEnabled: enabled

                            contentItem: Text {
                                text: parent.text
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                color: (root.playbackChannelIndex === index)
                                       ? "white"
                                       : (theme ? theme.textPrimary : "white")
                                font.pixelSize: 12
                                font.bold: (root.playbackChannelIndex === index)
                            }

                            background: Rectangle {
                                radius: 4
                                border.width: 1
                                border.color: (root.playbackChannelIndex === index)
                                              ? (theme ? theme.accent : "#f97316")
                                              : (theme ? theme.border : "#52525b")
                                color: (root.playbackChannelIndex === index)
                                       ? (channelBtn.down ? "#ea580c" : (theme ? theme.accent : "#f97316"))
                                       : (channelBtn.down ? (theme ? theme.bgSecondary : "#09090b")
                                                       : (theme ? theme.bgComponent : "#18181b"))
                            }

                            onClicked: root.selectPlaybackChannel(index)
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    TextField {
                        id: playbackDateTextField
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: root.playbackDateText
                        enabled: !root.playbackRunning && !root.playbackPending
                        inputMask: "0000-00-00;_"
                        placeholderText: "YYYY-MM-DD"
                        color: theme ? theme.textPrimary : "white"
                        placeholderTextColor: theme ? theme.textSecondary : "#71717a"
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackDateTextField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncDateFromField()) {
                                text = root.playbackDateText
                                return
                            }
                            root.playbackCalendarVisible = false
                            root.requestTimelineIfValid()
                        }
                    }

                    ControlButton {
                        text: "날짜"
                        compact: true
                        Layout.preferredWidth: 50
                        Layout.minimumWidth: 50
                        Layout.maximumWidth: 50
                        enabled: !root.playbackRunning && !root.playbackPending
                        onClicked: {
                            root.playbackCalendarVisible = !root.playbackCalendarVisible
                        }
                    }

                    ControlButton {
                        text: "새로고침"
                        compact: true
                        Layout.preferredWidth: 58
                        Layout.minimumWidth: 58
                        Layout.maximumWidth: 58
                        onClicked: {
                            root.requestTimelineIfValid()
                            root.requestMonthDays()
                        }
                    }
                }

                Rectangle {
                    visible: root.playbackCalendarVisible
                    Layout.fillWidth: true
                    Layout.preferredHeight: 190
                    color: theme ? theme.bgSecondary : "#09090b"
                    border.color: theme ? theme.border : "#27272a"
                    border.width: 1
                    radius: 6

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true

                            ControlButton {
                                text: "<"
                                compact: true
                                Layout.preferredWidth: 34
                                onClicked: {
                                    if (root.playbackViewMonth === 0) {
                                        root.playbackViewMonth = 11
                                        root.playbackViewYear -= 1
                                    } else {
                                        root.playbackViewMonth -= 1
                                    }
                                }
                            }

                            Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: root.monthLabel()
                                color: theme ? theme.textPrimary : "white"
                                font.bold: true
                            }

                            ControlButton {
                                text: ">"
                                compact: true
                                Layout.preferredWidth: 34
                                onClicked: {
                                    if (root.playbackViewMonth === 11) {
                                        root.playbackViewMonth = 0
                                        root.playbackViewYear += 1
                                    } else {
                                        root.playbackViewMonth += 1
                                    }
                                }
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 7
                            rowSpacing: 4
                            columnSpacing: 4

                            Repeater {
                                model: ["일", "월", "화", "수", "목", "금", "토"]
                                delegate: Label {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: modelData
                                    color: theme ? theme.textSecondary : "#a1a1aa"
                                    font.pixelSize: 11
                                }
                            }

                            Repeater {
                                model: 42
                                delegate: Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 20
                                    radius: 4
                                    border.width: {
                                        var first = root.firstDayOffset()
                                        var d = index - first + 1
                                        if (d < 1 || d > root.daysInViewMonth())
                                            return 0
                                        var selected = (root.playbackSelectedDate.getFullYear() === root.playbackViewYear
                                                    && root.playbackSelectedDate.getMonth() === root.playbackViewMonth
                                                    && root.playbackSelectedDate.getDate() === d)
                                        return (!selected && root.isTodayInViewDay(d)) ? 1 : 0
                                    }
                                    border.color: {
                                        var first = root.firstDayOffset()
                                        var d = index - first + 1
                                        if (d < 1 || d > root.daysInViewMonth())
                                            return "transparent"
                                        return theme ? theme.textPrimary : "#ffffff"
                                    }
                                    color: {
                                        var first = root.firstDayOffset()
                                        var d = index - first + 1
                                        if (d < 1 || d > root.daysInViewMonth())
                                            return "transparent"
                                        var selected = (root.playbackSelectedDate.getFullYear() === root.playbackViewYear
                                                    && root.playbackSelectedDate.getMonth() === root.playbackViewMonth
                                                    && root.playbackSelectedDate.getDate() === d)
                                        if (selected)
                                            return theme.accent
                                        return "transparent"
                                    }

                                    Text {
                                        anchors.centerIn: parent
                                        text: {
                                            var first = root.firstDayOffset()
                                            var d = index - first + 1
                                            if (d < 1 || d > root.daysInViewMonth())
                                                return ""
                                            return d
                                        }
                                        color: {
                                            var first = root.firstDayOffset()
                                            var d = index - first + 1
                                            if (d < 1 || d > root.daysInViewMonth())
                                                return (theme ? theme.textPrimary : "white")
                                            var selected = (root.playbackSelectedDate.getFullYear() === root.playbackViewYear
                                                        && root.playbackSelectedDate.getMonth() === root.playbackViewMonth
                                                        && root.playbackSelectedDate.getDate() === d)
                                            if (selected)
                                                return "white"
                                            if (root.isRecordedDay(d))
                                                return "#f97316"
                                            return (theme ? theme.textPrimary : "white")
                                        }
                                        font.pixelSize: 11
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: {
                                            var first = root.firstDayOffset()
                                            var d = index - first + 1
                                            return d >= 1 && d <= root.daysInViewMonth()
                                        }
                                        onClicked: {
                                            var first = root.firstDayOffset()
                                            var d = index - first + 1
                                            root.playbackSelectedDate = new Date(root.playbackViewYear, root.playbackViewMonth, d)
                                            root.playbackDateText = root.formatPlaybackDate()
                                            root.playbackCalendarVisible = false
                                            root.requestTimelineIfValid()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    TextField {
                        id: playbackHourField
                        Layout.fillWidth: true
                        enabled: !root.playbackRunning && !root.playbackPending
                        inputMask: "00;_"
                        placeholderText: "시"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackHourField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            var hh = parseInt(playbackHourField.text, 10)
                            var mm = parseInt(playbackMinuteField.text, 10)
                            var ss = parseInt(playbackSecondField.text, 10)
                            if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
                                playbackHourField.text = root.pad2(Math.floor((root.playbackCurrentSeconds >= 0 ? root.playbackCurrentSeconds : 0) / 3600))
                                return
                            }
                            root.playbackTimeText = root.pad2(hh) + ":" + root.pad2(mm) + ":" + root.pad2(ss)
                            root.syncSecondsFromFields()
                        }
                    }

                    Label {
                        text: ":"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                    }

                    TextField {
                        id: playbackMinuteField
                        Layout.fillWidth: true
                        enabled: !root.playbackRunning && !root.playbackPending
                        inputMask: "00;_"
                        placeholderText: "분"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackMinuteField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            var hh = parseInt(playbackHourField.text, 10)
                            var mm = parseInt(playbackMinuteField.text, 10)
                            var ss = parseInt(playbackSecondField.text, 10)
                            if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
                                playbackMinuteField.text = root.pad2(Math.floor(((root.playbackCurrentSeconds >= 0 ? root.playbackCurrentSeconds : 0) % 3600) / 60))
                                return
                            }
                            root.playbackTimeText = root.pad2(hh) + ":" + root.pad2(mm) + ":" + root.pad2(ss)
                            root.syncSecondsFromFields()
                        }
                    }

                    Label {
                        text: ":"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                    }

                    TextField {
                        id: playbackSecondField
                        Layout.fillWidth: true
                        enabled: !root.playbackRunning && !root.playbackPending
                        inputMask: "00;_"
                        placeholderText: "초"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackSecondField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            var hh = parseInt(playbackHourField.text, 10)
                            var mm = parseInt(playbackMinuteField.text, 10)
                            var ss = parseInt(playbackSecondField.text, 10)
                            if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
                                playbackSecondField.text = root.pad2((root.playbackCurrentSeconds >= 0 ? root.playbackCurrentSeconds : 0) % 60)
                                return
                            }
                            root.playbackTimeText = root.pad2(hh) + ":" + root.pad2(mm) + ":" + root.pad2(ss)
                            root.syncSecondsFromFields()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    ControlButton {
                        text: "Play"
                        Layout.fillWidth: true
                        accentStyle: true
                        enabled: !root.playbackRunning && !root.playbackPending && root.playbackTimeInRange
                        onClicked: {
                            if (!root.syncDateFromField() || !root.syncSecondsFromFields())
                                return
                            if (backend.playbackWsPlay()) {
                                root.playbackRunning = true
                                root.playbackPending = false
                                root.requestPlaybackResume()
                            } else {
                                var d = root.formatPlaybackDate()
                                var t = root.formatPlaybackTime()
                                root.playbackPending = true
                                root.requestPlayback(root.playbackChannelIndex, d, t)
                            }
                        }
                    }

                    ControlButton {
                        text: "Pause"
                        Layout.fillWidth: true
                        enabled: root.playbackRunning && !root.playbackPending
                        onClicked: {
                            root.playbackRunning = false
                            root.playbackPending = false
                            root.requestPlaybackPause()
                            backend.playbackWsPause()
                        }
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.playbackTimelineInfoText
                    color: root.playbackTimeInRange
                           ? (theme ? theme.textSecondary : "#a1a1aa")
                           : "#f59e0b"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 11
                }

                Label {
                    Layout.fillWidth: true
                    text: root.formatPlaybackDate() + " " + root.formatPlaybackTime()
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    horizontalAlignment: Text.AlignHCenter
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: theme ? theme.border : "#27272a"
                    opacity: 0.9
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        text: "시작"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 11
                        Layout.preferredWidth: 30
                    }

                    TextField {
                        id: playbackExportStartHourField
                        Layout.fillWidth: true
                        enabled: !root.playbackPending
                        inputMask: "00;_"
                        text: "00"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackExportStartHourField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncExportStartFromFields()) {
                                var p = root.playbackExportStartText.split(":")
                                if (p.length === 3) text = p[0]
                            }
                        }
                    }
                    Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                    TextField {
                        id: playbackExportStartMinuteField
                        Layout.fillWidth: true
                        enabled: !root.playbackPending
                        inputMask: "00;_"
                        text: "00"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackExportStartMinuteField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncExportStartFromFields()) {
                                var p = root.playbackExportStartText.split(":")
                                if (p.length === 3) text = p[1]
                            }
                        }
                    }
                    Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                    TextField {
                        id: playbackExportStartSecondField
                        Layout.fillWidth: true
                        enabled: !root.playbackPending
                        inputMask: "00;_"
                        text: "00"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackExportStartSecondField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncExportStartFromFields()) {
                                var p = root.playbackExportStartText.split(":")
                                if (p.length === 3) text = p[2]
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Label {
                        text: "끝"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 11
                        Layout.preferredWidth: 30
                    }

                    TextField {
                        id: playbackExportEndHourField
                        Layout.fillWidth: true
                        enabled: !root.playbackPending
                        inputMask: "00;_"
                        text: "00"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackExportEndHourField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncExportEndFromFields()) {
                                var p = root.playbackExportEndText.split(":")
                                if (p.length === 3) text = p[0]
                            }
                        }
                    }
                    Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                    TextField {
                        id: playbackExportEndMinuteField
                        Layout.fillWidth: true
                        enabled: !root.playbackPending
                        inputMask: "00;_"
                        text: "00"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackExportEndMinuteField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncExportEndFromFields()) {
                                var p = root.playbackExportEndText.split(":")
                                if (p.length === 3) text = p[1]
                            }
                        }
                    }
                    Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                    TextField {
                        id: playbackExportEndSecondField
                        Layout.fillWidth: true
                        enabled: !root.playbackPending
                        inputMask: "00;_"
                        text: "00"
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: playbackExportEndSecondField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onEditingFinished: {
                            if (!root.syncExportEndFromFields()) {
                                var p = root.playbackExportEndText.split(":")
                                if (p.length === 3) text = p[2]
                            }
                        }
                    }
                }

                ControlButton {
                    text: "내보내기"
                    Layout.fillWidth: true
                    compact: true
                    enabled: !root.playbackPending
                             && /^\d{4}-\d{2}-\d{2}$/.test(root.playbackDateText)
                             && root.isValidHmsText(root.playbackExportStartText)
                             && root.isValidHmsText(root.playbackExportEndText)
                             && (root.playbackExportStartText <= root.playbackExportEndText)
                    onClicked: {
                        if (!root.syncDateFromField())
                            return
                        if (!root.isValidHmsText(root.playbackExportStartText) || !root.isValidHmsText(root.playbackExportEndText))
                            return
                        if (root.playbackExportStartText > root.playbackExportEndText)
                            return
                        root.requestPlaybackExport(root.playbackChannelIndex,
                                                   root.formatPlaybackDate(),
                                                   root.playbackExportStartText,
                                                   root.playbackExportEndText)
                    }
                }

            }
        }

        Rectangle {
            visible: root.showCameraControls
            Layout.fillWidth: true
            Layout.preferredHeight: root.showCameraControls ? 150 : 0
            Layout.maximumHeight: root.showCameraControls ? 150 : 0
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                Text {
                    text: root.selectedCameraTitle()
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 13
                }
                Text {
                    text: "확대 화면 제어 패널"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 11
                }

                Text {
                    text: "휠 업/다운: 줌 인/아웃"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: cameraNameField
                        Layout.fillWidth: true
                        placeholderText: "위치 이름"
                        color: theme ? theme.textPrimary : "white"
                        placeholderTextColor: theme ? theme.textSecondary : "#71717a"
                        enabled: root.selectedCameraIndex >= 0
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: cameraNameField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onAccepted: {
                            if (root.selectedCameraIndex < 0)
                                return
                            var trimmed = text.trim()
                            if (trimmed.length === 0)
                                return
                            root.requestCameraNameChange(root.selectedCameraIndex, trimmed)
                            root.cameraControlStatus = "카메라 이름이 변경되었습니다."
                            root.cameraControlError = false
                            controlStatusTimer.restart()
                        }
                    }
                    ControlButton {
                        text: "저장"
                        compact: true
                        Layout.preferredWidth: 56
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            if (root.selectedCameraIndex < 0)
                                return
                            var trimmed = cameraNameField.text.trim()
                            if (trimmed.length === 0)
                                return
                            root.requestCameraNameChange(root.selectedCameraIndex, trimmed)
                            root.cameraControlStatus = "카메라 이름이 변경되었습니다."
                            root.cameraControlError = false
                            controlStatusTimer.restart()
                        }
                    }
                }

                ControlButton {
                    text: root.mapModeEnabled ? "3D Map 모드 ON (미구현)" : "3D Map 모드 OFF (미구현)"
                    Layout.fillWidth: true
                    compact: true
                    onClicked: {
                        root.mapModeEnabled = !root.mapModeEnabled
                        if (root.mapModeEnabled && root.selectedCameraIndex >= 0) {
                            backend.sunapiSimpleAutoFocus(root.selectedCameraIndex)
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: root.showCameraControls
            Layout.fillWidth: true
            Layout.preferredHeight: root.showCameraControls ? 460 : 0
            Layout.maximumHeight: root.showCameraControls ? 460 : 0
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    ControlButton {
                        text: "줌 +"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomIn(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "줌 -"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomOut(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "줌 정지"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomStop(root.selectedCameraIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6

                    ControlButton {
                        text: "포커스 Near"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusNear(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "포커스 Far"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusFar(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "포커스 정지"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusStop(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "오토포커스"
                        Layout.fillWidth: true
                        compact: true
                        accentStyle: root.mapModeEnabled
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiSimpleAutoFocus(root.selectedCameraIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                Label {
                    text: "표시"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 12
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 6
                    columnSpacing: 6
                    rowSpacing: 4

                    Label { text: "대비"; color: theme ? theme.textSecondary : "#a1a1aa"; Layout.preferredWidth: 42; font.pixelSize: 11 }
                    ControlButton {
                        text: "-"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displayContrast = Math.max(1, root.displayContrast - 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 1
                        to: 100
                        stepSize: 1
                        value: root.displayContrast
                        onMoved: root.displayContrast = Math.round(value)
                        onPressedChanged: if (!pressed) root.applyDisplaySettings()
                    }
                    ControlButton {
                        text: "+"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displayContrast = Math.min(100, root.displayContrast + 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Label { text: String(root.displayContrast); color: theme ? theme.textPrimary : "white"; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "(1~100)"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }

                    Label { text: "밝기"; color: theme ? theme.textSecondary : "#a1a1aa"; Layout.preferredWidth: 42; font.pixelSize: 11 }
                    ControlButton {
                        text: "-"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displayBrightness = Math.max(1, root.displayBrightness - 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 1
                        to: 100
                        stepSize: 1
                        value: root.displayBrightness
                        onMoved: root.displayBrightness = Math.round(value)
                        onPressedChanged: if (!pressed) root.applyDisplaySettings()
                    }
                    ControlButton {
                        text: "+"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displayBrightness = Math.min(100, root.displayBrightness + 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Label { text: String(root.displayBrightness); color: theme ? theme.textPrimary : "white"; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "(1~100)"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }

                    CheckBox {
                        text: "윤곽"
                        font.pixelSize: 11
                        Layout.preferredWidth: 42
                        checked: root.displaySharpnessEnabled
                        enabled: root.selectedCameraIndex >= 0
                        onToggled: {
                            root.displaySharpnessEnabled = checked
                            root.applyDisplaySettings()
                        }
                    }
                    ControlButton {
                        text: "-"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displaySharpnessLevel = Math.max(1, root.displaySharpnessLevel - 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 1
                        to: 32
                        stepSize: 1
                        value: root.displaySharpnessLevel
                        onMoved: root.displaySharpnessLevel = Math.round(value)
                        onPressedChanged: if (!pressed) root.applyDisplaySettings()
                    }
                    ControlButton {
                        text: "+"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displaySharpnessLevel = Math.min(32, root.displaySharpnessLevel + 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Label { text: String(root.displaySharpnessLevel); color: theme ? theme.textPrimary : "white"; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "(1~32)"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }

                    Label { text: "컬러 레벨"; color: theme ? theme.textSecondary : "#a1a1aa"; Layout.preferredWidth: 42; font.pixelSize: 11 }
                    ControlButton {
                        text: "-"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displayColorLevel = Math.max(1, root.displayColorLevel - 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 1
                        to: 100
                        stepSize: 1
                        value: root.displayColorLevel
                        onMoved: root.displayColorLevel = Math.round(value)
                        onPressedChanged: if (!pressed) root.applyDisplaySettings()
                    }
                    ControlButton {
                        text: "+"
                        compact: true
                        Layout.preferredWidth: 32
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            root.displayColorLevel = Math.min(100, root.displayColorLevel + 1)
                            root.applyDisplaySettings()
                        }
                    }
                    Label { text: String(root.displayColorLevel); color: theme ? theme.textPrimary : "white"; Layout.preferredWidth: 30; horizontalAlignment: Text.AlignHCenter }
                    Label { text: "(1~100)"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ControlButton {
                        text: "초기화"
                        compact: true
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: backend.sunapiResetDisplaySettings(root.selectedCameraIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                Text {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 14
                    Layout.minimumHeight: 14
                    Layout.maximumHeight: 14
                    text: root.cameraControlStatus.length > 0 ? root.cameraControlStatus : " "
                    color: root.cameraControlError ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 10
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                    opacity: root.cameraControlStatus.length > 0 ? 1.0 : 0.0
                }

            }
        }

        // 시스템 메트릭 차트 영역
        ColumnLayout {
            visible: !root.showCameraControls && !root.showPlaybackControls
            Layout.fillWidth: true
            Layout.preferredHeight: (root.showCameraControls || root.showPlaybackControls) ? 0 : 208
            Layout.maximumHeight: (root.showCameraControls || root.showPlaybackControls) ? 0 : 208
            spacing: 8
            
            component SystemChart : Rectangle {
                id: chartRoot
                property string title: "Metric"
                property string unit: ""
                property real value: 0
                property string valueColor: theme ? theme.textPrimary : "white"
                property color graphColor: "#f97316"
                property var dataHistory: []
                property real calculatedMax: 100
                property real calculatedAvg: 0
                
                Layout.fillWidth: true
                height: 100
                color: theme ? theme.bgComponent : "#18181b"
                radius: 8
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                
                function updateStats() {
                    var max = 0
                    var sum = 0
                    var len = chatRoot.dataHistory.length
                    if (len === 0) return

                    for(var i=0; i<len; i++) {
                        var v = chatRoot.dataHistory[i]
                        if(v > max) max = v
                        sum += v
                    }
                    chartRoot.calculatedMax = max > 0 ? max : 100
                    chartRoot.calculatedAvg = Math.round(sum / len)
                }

                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: {
                        var newVal = chartRoot.value
                        var arr = chartRoot.dataHistory
                        if (arr.length > 20) arr.shift()
                        arr.push(newVal)
                        
                        var max = 0
                        var sum = 0
                        for(var i=0; i<arr.length; i++) {
                            if(arr[i] > max) max = arr[i]
                            sum += arr[i]
                        }
                        chartRoot.calculatedMax = max > 0 ? max : (chartRoot.title === "FPS" ? 60 : 100)
                        chartRoot.calculatedAvg = Math.round(sum / arr.length)
                        
                        chartCanvas.requestPaint()
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: chartRoot.title; color: theme ? theme.textSecondary : "#71717a"; font.bold: true; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Text { text: chartRoot.value; color: chartRoot.valueColor; font.bold: true; font.pixelSize: 18 }
                        Text { text: chartRoot.unit; color: theme ? theme.textSecondary : "#71717a"; font.bold: true; font.pixelSize: 10 }
                    }
                    
                    Canvas {
                        id: chartCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            
                            var data = chartRoot.dataHistory
                            if (data.length < 2) return
                            var maxVal = chartRoot.calculatedMax * 1.2
                            
                            ctx.beginPath()
                            ctx.lineWidth = 1.5
                            ctx.strokeStyle = chartRoot.graphColor
                            ctx.fillStyle = Qt.rgba(chartRoot.graphColor.r, chartRoot.graphColor.g, chartRoot.graphColor.b, 0.2)
                            
                            var stepX = width / (data.length - 1)
                            
                            ctx.moveTo(0, height - (data[0] / maxVal) * height)
                            for (var i = 1; i < data.length; i++) {
                                ctx.lineTo(i * stepX, height - (data[i] / maxVal) * height)
                            }
                            ctx.stroke()
                            
                            ctx.lineTo(width, height)
                            ctx.lineTo(0, height)
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Avg:"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }
                        Text { text: chartRoot.calculatedAvg; color: theme ? theme.textSecondary : "#d4d4d8"; font.bold: true; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: "Max:"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }
                        Text { text: chartRoot.calculatedMax; color: theme ? theme.textSecondary : "#d4d4d8"; font.bold: true; font.pixelSize: 10 }
                    }
                }
            }
            
            SystemChart {
                title: "FPS"
                unit: "FPS"
                value: backend.currentFps
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: "#0d9488"
                dataHistory: []
            }
            
            SystemChart {
                title: "LATENCY"
                unit: "ms"
                value: backend.latency
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: theme ? theme.accent : "#f97316"
                dataHistory: []
            }
        }

        GridLayout {
            visible: !root.showCameraControls && !root.showPlaybackControls
            Layout.fillWidth: true
            Layout.preferredHeight: (root.showCameraControls || root.showPlaybackControls) ? 0 : 168
            Layout.maximumHeight: (root.showCameraControls || root.showPlaybackControls) ? 0 : 168
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            
            Repeater {
                model: [
                    { label: "Active Cameras", value: backend.activeCameras + "", sub: "of 4 total", icon: "\uD83C\uDFA5", color: theme ? theme.accent : "#f97316" },
                    { label: "Detected Objects", value: "-", sub: "Connection Needed", icon: "\uD83E\uDDE0", color: theme ? theme.textSecondary : "#71717a" },
                    { label: "Storage Used", value: backend.storagePercent + "%", sub: backend.storageUsed + " / " + backend.storageTotal, icon: "\uD83D\uDCBE", color: theme ? theme.textSecondary : "#71717a" },
                    { label: "Network Status", value: "-", sub: "Connection Needed", icon: "\uD83D\uDCE1", color: theme ? theme.textSecondary : "#71717a" }
                ]
                
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    color: theme ? theme.bgComponent : "#18181b"
                    radius: 8
                    border.color: theme ? theme.border : "#27272a"
                    border.width: 1
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4
                        
                        RowLayout {
                            spacing: 6
                            Rectangle {
                                width: 20; height: 20
                                color: theme ? theme.border : "#27272a"
                                radius: 4
                                Text { anchors.centerIn: parent; text: modelData.icon; font.pixelSize: 10 }
                            }
                            Text {
                                text: modelData.label.split(' ')[0]
                                color: theme ? theme.textSecondary : "#71717a"
                                font.bold: true
                                font.pixelSize: 9
                                font.capitalization: Font.AllUppercase
                            }
                        }
                        
                        Text {
                            text: modelData.value
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        
                        Text {
                            text: modelData.sub
                            color: theme ? theme.textSecondary : "#52525b"
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }


        Rectangle {
            visible: !root.showCameraControls && !root.showPlaybackControls
            Layout.fillWidth: true
            Layout.preferredHeight: (root.showCameraControls || root.showPlaybackControls) ? 0 : 50
            Layout.maximumHeight: (root.showCameraControls || root.showPlaybackControls) ? 0 : 50
            height: 50
            color: theme ? theme.bgComponent : "#18181b"
            radius: 8
            border.color: "#4e2c0e"
            border.width: 1
            
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 2
                
                RowLayout {
                    spacing: 6
                    Rectangle {
                        width: 6; height: 6; radius: 3
                        color: theme ? theme.accent : "#f97316"
                    }
                    Text {
                        text: "AI STATUS"
                        color: theme ? theme.accent : "#f97316"
                        font.bold: true
                        font.pixelSize: 9
                    }
                }
                Text {
                    text: "Active on all feeds"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }
            }
        }

        Item {
            Layout.fillHeight: !root.showPlaybackControls
        }
    }
}


