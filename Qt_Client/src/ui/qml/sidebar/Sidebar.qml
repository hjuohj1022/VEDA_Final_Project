import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property var backendObject: null

    property alias cameraNames: store.cameraNames
    property alias showCameraControls: store.showCameraControls
    property alias showPlaybackControls: store.showPlaybackControls
    property alias showThermalControls: store.showThermalControls
    property alias selectedCameraIndex: store.selectedCameraIndex
    property alias cameraControlStatus: store.cameraControlStatus
    property alias cameraControlError: store.cameraControlError
    property alias mapModeEnabled: store.mapModeEnabled
    property alias supportZoom: store.supportZoom
    property alias supportFocus: store.supportFocus
    property alias displayContrast: store.displayContrast
    property alias displayBrightness: store.displayBrightness
    property alias displaySharpnessLevel: store.displaySharpnessLevel
    property alias displaySharpnessEnabled: store.displaySharpnessEnabled
    property alias displayColorLevel: store.displayColorLevel
    property alias playbackChannelIndex: store.playbackChannelIndex
    property alias playbackRunning: store.playbackRunning
    property alias playbackPending: store.playbackPending
    property alias playbackCurrentSeconds: store.playbackCurrentSeconds
    property alias playbackSelectedDate: store.playbackSelectedDate
    property alias playbackDateText: store.playbackDateText
    property alias playbackTimeText: store.playbackTimeText
    property alias playbackExportStartText: store.playbackExportStartText
    property alias playbackExportEndText: store.playbackExportEndText
    property alias playbackSegments: store.playbackSegments
    property alias playbackTimeInRange: store.playbackTimeInRange
    property alias playbackTimelineInfoText: store.playbackTimelineInfoText
    property alias playbackViewYear: store.playbackViewYear
    property alias playbackViewMonth: store.playbackViewMonth
    property alias playbackCalendarVisible: store.playbackCalendarVisible
    property alias playbackRecordedDays: store.playbackRecordedDays

    signal requestCameraNameChange(int cameraIndex, string name)
    signal requestPlayback(int channelIndex, string dateText, string timeText)
    signal requestPlaybackTimeline(int channelIndex, string dateText)
    signal requestPlaybackMonthDays(int channelIndex, int year, int month)
    signal requestPlaybackExport(int channelIndex, string dateText, string startTimeText, string endTimeText)
    signal requestPlaybackPause()
    signal requestPlaybackResume()

    color: theme ? theme.bgSecondary : "#09090b"

    function pad2(v) { return store.pad2(v) }
    function formatPlaybackDate() { return store.formatPlaybackDate() }
    function formatPlaybackTime() { return store.formatPlaybackTime() }
    function syncTimeFromSeconds(sec) { store.syncTimeFromSeconds(sec) }
    function syncSecondsFromFields() { return store.syncSecondsFromFields() }
    function syncDateFromField() { return store.syncDateFromField() }
    function isValidHmsText(t) { return store.isValidHmsText(t) }
    function secToHms(sec) { return store.secToHms(sec) }
    function applyExportRangeFromSecond(sec) { store.applyExportRangeFromSecond(sec) }
    function syncExportStartFromFields(hh, mm, ss) { return store.syncExportStartFromFields(hh, mm, ss) }
    function syncExportEndFromFields(hh, mm, ss) { return store.syncExportEndFromFields(hh, mm, ss) }
    function requestTimelineIfValid() { store.requestTimelineIfValid() }
    function requestMonthDays() { store.requestMonthDays() }
    function selectPlaybackChannel(index) { store.selectPlaybackChannel(index) }
    function isRecordedDay(day) { return store.isRecordedDay(day) }
    function isTodayInViewDay(day) { return store.isTodayInViewDay(day) }
    function isSecondRecorded(sec) { return store.isSecondRecorded(sec) }
    function updateTimelineInfo() { store.updateTimelineInfo() }
    function applyPlaybackStart(dateText, timeText) { store.applyPlaybackStart(dateText, timeText) }
    function daysInViewMonth() { return store.daysInViewMonth() }
    function firstDayOffset() { return store.firstDayOffset() }
    function monthLabel() { return store.monthLabel() }
    function selectedCameraTitle() { return store.selectedCameraTitle() }
    function applyDisplaySettings() { store.applyDisplaySettings() }
    function refreshDisplaySettings() { store.refreshDisplaySettings() }
    function restartCameraControlStatusTimer() { store.restartCameraControlStatusTimer() }

    SidebarStore {
        id: store
        backend: root.backendObject
    }

    Connections {
        target: store
        function onRequestCameraNameChange(cameraIndex, name) {
            root.requestCameraNameChange(cameraIndex, name)
        }
        function onRequestPlayback(channelIndex, dateText, timeText) {
            root.requestPlayback(channelIndex, dateText, timeText)
        }
        function onRequestPlaybackTimeline(channelIndex, dateText) {
            root.requestPlaybackTimeline(channelIndex, dateText)
        }
        function onRequestPlaybackMonthDays(channelIndex, year, month) {
            root.requestPlaybackMonthDays(channelIndex, year, month)
        }
        function onRequestPlaybackExport(channelIndex, dateText, startTimeText, endTimeText) {
            root.requestPlaybackExport(channelIndex, dateText, startTimeText, endTimeText)
        }
        function onRequestPlaybackPause() {
            root.requestPlaybackPause()
        }
        function onRequestPlaybackResume() {
            root.requestPlaybackResume()
        }
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

        Text {
            text: store.showCameraControls
                  ? "Camera Controls"
                  : (store.showPlaybackControls
                     ? "Playback Controls"
                     : (store.showThermalControls ? "Thermal Panel" : "System Metrics"))
            color: theme ? theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14
        }

        SidebarPlaybackControlsPanel {
            Layout.fillWidth: true
            theme: root.theme
            store: store
        }

        SidebarCameraControlsPanel {
            Layout.fillWidth: true
            theme: root.theme
            store: store
        }

        SidebarThermalControlsPanel {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            theme: root.theme
            store: store
        }

        SidebarSystemMetricsPanel {
            Layout.fillWidth: true
            theme: root.theme
            store: store
        }

        Item {
            Layout.fillHeight: store.showPlaybackControls || store.showCameraControls || store.showThermalControls
        }
    }
}
