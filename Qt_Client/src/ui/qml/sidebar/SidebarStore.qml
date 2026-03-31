import QtQuick

Item {
    id: store
    property var backend: null

    property var cameraNames: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
    property bool showCameraControls: false
    property bool showPlaybackControls: false
    property bool showThermalControls: false
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
    // 두 자리 숫자 포맷 함수
    function pad2(v) { return (v < 10 ? "0" : "") + v }
    // 재생 날짜 포맷 함수
    function formatPlaybackDate() {
        return playbackSelectedDate.getFullYear() + "-" + pad2(playbackSelectedDate.getMonth() + 1) + "-" + pad2(playbackSelectedDate.getDate())
    }
    // 재생 시간 포맷 함수
    function formatPlaybackTime() {
        var s = playbackCurrentSeconds >= 0 ? playbackCurrentSeconds : 0
        return pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }
    // 초 기준 시간 동기화 함수
    function syncTimeFromSeconds(sec) {
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        playbackCurrentSeconds = s
        playbackTimeText = pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }
    // 입력 필드 시간 동기화 함수
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
    // 입력 필드 날짜 동기화 함수
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
    // 시분초 텍스트 유효성 확인 함수
    function isValidHmsText(t) {
        var m = /^(\d{2}):(\d{2}):(\d{2})$/.exec((t || "").trim())
        if (!m) return false
        var h = parseInt(m[1], 10)
        var mm = parseInt(m[2], 10)
        var s = parseInt(m[3], 10)
        return h >= 0 && h <= 23 && mm >= 0 && mm <= 59 && s >= 0 && s <= 59
    }
    // 초 시분초 변환 함수
    function secToHms(sec) {
        var s = Math.max(0, Math.min(86399, Math.floor(sec)))
        return pad2(Math.floor(s / 3600)) + ":" + pad2(Math.floor((s % 3600) / 60)) + ":" + pad2(s % 60)
    }
    // 내보내기 범위 적용 함수
    function applyExportRangeFromSecond(sec) {
        var startSec = Math.max(0, Math.min(86399, Math.floor(sec)))
        var endSec = Math.max(startSec, Math.min(86399, startSec + 300))

        if (playbackSegments && playbackSegments.length > 0) {
            var ranges = []
            for (var i = 0; i < playbackSegments.length; i++) {
                var seg = playbackSegments[i]
                var a = Math.max(0, Math.min(86399, Number(seg.start || 0)))
                var b = Math.max(0, Math.min(86399, Number(seg.end || 0)))
                ranges.push({ start: Math.min(a, b), end: Math.max(a, b) })
            }

            ranges.sort(function(lhs, rhs) { return lhs.start - rhs.start })

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

            for (var k = 0; k < merged.length; k++) {
                if (startSec >= merged[k].start && startSec <= merged[k].end) {
                    endSec = Math.min(endSec, merged[k].end)
                    break
                }
            }
        }

        playbackExportStartText = secToHms(startSec)
        playbackExportEndText = secToHms(endSec)
    }
    // 내보내기 시작 시간 동기화 함수
    function syncExportStartFromFields(hh, mm, ss) {
        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
            return false
        playbackExportStartText = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss)
        return true
    }
    // 내보내기 종료 시간 동기화 함수
    function syncExportEndFromFields(hh, mm, ss) {
        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59)
            return false
        playbackExportEndText = pad2(hh) + ":" + pad2(mm) + ":" + pad2(ss)
        return true
    }
    // 유효한 타임라인 요청 함수
    function requestTimelineIfValid() {
        if (!syncDateFromField())
            return
        requestPlaybackTimeline(playbackChannelIndex, playbackDateText)
    }
    // 월간 일자 요청 함수
    function requestMonthDays() {
        playbackRecordedDays = []
        requestPlaybackMonthDays(playbackChannelIndex, playbackViewYear, playbackViewMonth + 1)
    }
    // 재생 채널 선택 함수
    function selectPlaybackChannel(index) {
        var i = Math.max(0, Math.min(3, Number(index)))
        if (playbackChannelIndex === i)
            return
        playbackChannelIndex = i
        requestTimelineIfValid()
        requestMonthDays()
    }
    // 녹화 일자 여부 확인 함수
    function isRecordedDay(day) {
        if (!playbackRecordedDays || playbackRecordedDays.length === 0)
            return false
        for (var i = 0; i < playbackRecordedDays.length; i++) {
            if (Number(playbackRecordedDays[i]) === day)
                return true
        }
        return false
    }
    // 현재 뷰 오늘 일자 확인 함수
    function isTodayInViewDay(day) {
        var now = new Date()
        return now.getFullYear() === playbackViewYear
            && now.getMonth() === playbackViewMonth
            && now.getDate() === day
    }
    // 초 단위 녹화 여부 확인 함수
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
    // 타임라인 정보 갱신 함수
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
    // 재생 시작 적용 함수
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
    // 현재 뷰 월간 일자 수 계산 함수
    function daysInViewMonth() {
        return new Date(playbackViewYear, playbackViewMonth + 1, 0).getDate()
    }
    // 첫 요일 오프셋 계산 함수
    function firstDayOffset() {
        return new Date(playbackViewYear, playbackViewMonth, 1).getDay()
    }
    // 월간 라벨 생성 함수
    function monthLabel() {
        var m = playbackViewMonth + 1
        return playbackViewYear + "-" + pad2(m)
    }
    // 선택 카메라 제목 생성 함수
    function selectedCameraTitle() {
        if (selectedCameraIndex < 0 || selectedCameraIndex >= cameraNames.length)
            return "Camera"
        return "Cam " + (selectedCameraIndex + 1) + " - " + cameraNames[selectedCameraIndex]
    }
    // 표시 설정 적용 함수
    function applyDisplaySettings() {
        if (!showCameraControls || selectedCameraIndex < 0 || !backend)
            return
        backend.sunapiSetDisplaySettings(
                    selectedCameraIndex,
                    displayContrast,
                    displayBrightness,
                    displaySharpnessLevel,
                    displayColorLevel,
                    displaySharpnessEnabled)
    }
    // 표시 설정 갱신 함수
    function refreshDisplaySettings() {
        if (!showCameraControls || selectedCameraIndex < 0 || !backend)
            return
        backend.sunapiLoadDisplaySettings(selectedCameraIndex)
    }
    // 카메라 제어 상태 타이머 재시작 함수
    function restartCameraControlStatusTimer() {
        controlStatusTimer.restart()
    }

    Connections {
        target: store.backend ? store.backend : null
        // 카메라 제어 메시지 처리 함수
        function onCameraControlMessage(message, isError) {
            if (!store.showCameraControls)
                return
            store.cameraControlStatus = message
            store.cameraControlError = isError
            controlStatusTimer.restart()
        }
        // 표시 설정 변경 처리 함수
        function onDisplaySettingsChanged() {
            store.displayContrast = backend.displayContrast
            store.displayBrightness = backend.displayBrightness
            store.displaySharpnessLevel = backend.displaySharpnessLevel
            store.displaySharpnessEnabled = backend.displaySharpnessEnabled
            store.displayColorLevel = backend.displayColorLevel
        }
    }

    Timer {
        id: controlStatusTimer
        interval: 3000
        repeat: false
        // 트리거 처리 함수
        onTriggered: {
            store.cameraControlStatus = ""
            store.cameraControlError = false
        }
    }

    Timer {
        id: displaySettingsRefreshTimer
        interval: 80
        repeat: false
        // 트리거 처리 함수
        onTriggered: store.refreshDisplaySettings()
    }

    Timer {
        id: playbackTick
        interval: 1000
        repeat: true
        running: store.showPlaybackControls && store.playbackRunning && !store.playbackPending
        // 트리거 처리 함수
        onTriggered: store.syncTimeFromSeconds(store.playbackCurrentSeconds + 1)
    }
    // 카메라 제어 패널 표시 상태 변경 처리 함수
    onShowCameraControlsChanged: {
        if (!showCameraControls && mapModeEnabled && backend) {
            backend.stopCctv3dMapSequence()
            mapModeEnabled = false
        }
        if (showCameraControls && selectedCameraIndex >= 0) {
            store.refreshDisplaySettings()
            displaySettingsRefreshTimer.restart()
        }
    }
    // 선택 카메라 변경 처리 함수
    onSelectedCameraIndexChanged: {
        if (mapModeEnabled && backend) {
            backend.stopCctv3dMapSequence()
            mapModeEnabled = false
        }
        if (showCameraControls && selectedCameraIndex >= 0) {
            supportZoom = true
            supportFocus = true
            store.refreshDisplaySettings()
            displaySettingsRefreshTimer.restart()
        }
    }
    // 재생 제어 패널 표시 상태 변경 처리 함수
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
    // 재생 보기 연도 변경 처리 함수
    onPlaybackViewYearChanged: if (showPlaybackControls) requestMonthDays()
    // 재생 보기 월 변경 처리 함수
    onPlaybackViewMonthChanged: if (showPlaybackControls) requestMonthDays()
    // 재생 현재 시간 변경 처리 함수
    onPlaybackCurrentSecondsChanged: {
        playbackTimeInRange = isSecondRecorded(playbackCurrentSeconds >= 0 ? playbackCurrentSeconds : 0)
        updateTimelineInfo()
    }
    // 재생 시간 텍스트 변경 처리 함수
    onPlaybackTimeTextChanged: {
        if (!store.playbackRunning && !store.playbackPending && store.isValidHmsText(store.playbackTimeText)) {
            if (!store.isValidHmsText(store.playbackExportStartText))
                store.playbackExportStartText = store.playbackTimeText
            if (!store.isValidHmsText(store.playbackExportEndText))
                store.playbackExportEndText = store.playbackTimeText
        }
    }
    // 재생 구간 변경 처리 함수
    onPlaybackSegmentsChanged: updateTimelineInfo()
}
