import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property var backendObject: null
    property int liveSidebarTab: 0
    property int cameraSidebarTab: 0

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
    // 두 자리 숫자 포맷 함수
    function pad2(v) { return store.pad2(v) }
    // 재생 날짜 포맷 함수
    function formatPlaybackDate() { return store.formatPlaybackDate() }
    // 재생 시간 포맷 함수
    function formatPlaybackTime() { return store.formatPlaybackTime() }
    // 초 기준 시간 동기화 함수
    function syncTimeFromSeconds(sec) { store.syncTimeFromSeconds(sec) }
    // 입력 필드 시간 동기화 함수
    function syncSecondsFromFields() { return store.syncSecondsFromFields() }
    // 입력 필드 날짜 동기화 함수
    function syncDateFromField() { return store.syncDateFromField() }
    // 시분초 텍스트 유효성 확인 함수
    function isValidHmsText(t) { return store.isValidHmsText(t) }
    // 초 시분초 변환 함수
    function secToHms(sec) { return store.secToHms(sec) }
    // 내보내기 범위 적용 함수
    function applyExportRangeFromSecond(sec) { store.applyExportRangeFromSecond(sec) }
    // 내보내기 시작 시간 동기화 함수
    function syncExportStartFromFields(hh, mm, ss) { return store.syncExportStartFromFields(hh, mm, ss) }
    // 내보내기 종료 시간 동기화 함수
    function syncExportEndFromFields(hh, mm, ss) { return store.syncExportEndFromFields(hh, mm, ss) }
    // 유효한 타임라인 요청 함수
    function requestTimelineIfValid() { store.requestTimelineIfValid() }
    // 월간 일자 요청 함수
    function requestMonthDays() { store.requestMonthDays() }
    // 재생 채널 선택 함수
    function selectPlaybackChannel(index) { store.selectPlaybackChannel(index) }
    // 녹화 일자 여부 확인 함수
    function isRecordedDay(day) { return store.isRecordedDay(day) }
    // 현재 뷰 오늘 일자 확인 함수
    function isTodayInViewDay(day) { return store.isTodayInViewDay(day) }
    // 초 단위 녹화 여부 확인 함수
    function isSecondRecorded(sec) { return store.isSecondRecorded(sec) }
    // 타임라인 정보 갱신 함수
    function updateTimelineInfo() { store.updateTimelineInfo() }
    // 재생 시작 적용 함수
    function applyPlaybackStart(dateText, timeText) { store.applyPlaybackStart(dateText, timeText) }
    // 현재 뷰 월간 일자 수 계산 함수
    function daysInViewMonth() { return store.daysInViewMonth() }
    // 첫 요일 오프셋 계산 함수
    function firstDayOffset() { return store.firstDayOffset() }
    // 월간 라벨 생성 함수
    function monthLabel() { return store.monthLabel() }
    // 선택 카메라 제목 생성 함수
    function selectedCameraTitle() { return store.selectedCameraTitle() }
    // 표시 설정 적용 함수
    function applyDisplaySettings() { store.applyDisplaySettings() }
    // 표시 설정 갱신 함수
    function refreshDisplaySettings() { store.refreshDisplaySettings() }
    // 카메라 제어 상태 타이머 재시작 함수
    function restartCameraControlStatusTimer() { store.restartCameraControlStatusTimer() }
    // 실시간 탭 표시 여부 확인 함수
    function showLiveTabs() {
        return !store.showCameraControls && !store.showPlaybackControls && !store.showThermalControls
    }
    // 카메라 탭 표시 여부 확인 함수
    function showCameraTabs() {
        return store.showCameraControls
    }
    // 사이드바 제목 생성 함수
    function sidebarTitle() {
        if (showCameraTabs())
            return "Side Panel"
        if (store.showPlaybackControls)
            return "Playback Controls"
        if (store.showThermalControls)
            return "Thermal Panel"
        return "Side Panel"
    }
    // 탭 선택 색상 반환 함수
    function tabSelectedColor(selected) {
        return selected ? (theme ? theme.accent : "#f97316") : (theme ? theme.bgComponent : "#18181b")
    }
    // 탭 테두리 색상 반환 함수
    function tabBorderColor(selected) {
        return selected ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
    }
    // 탭 텍스트 색상 반환 함수
    function tabTextColor(selected) {
        return selected ? "white" : (theme ? theme.textSecondary : "#a1a1aa")
    }

    SidebarStore {
        id: store
        backend: root.backendObject
    }

    Connections {
        target: store
        // 카메라 이름 변경 요청 처리 함수
        function onRequestCameraNameChange(cameraIndex, name) {
            root.requestCameraNameChange(cameraIndex, name)
        }
        // 재생 요청 처리 함수
        function onRequestPlayback(channelIndex, dateText, timeText) {
            root.requestPlayback(channelIndex, dateText, timeText)
        }
        // 재생 타임라인 요청 처리 함수
        function onRequestPlaybackTimeline(channelIndex, dateText) {
            root.requestPlaybackTimeline(channelIndex, dateText)
        }
        // 재생 월간 녹화 일자 요청 처리 함수
        function onRequestPlaybackMonthDays(channelIndex, year, month) {
            root.requestPlaybackMonthDays(channelIndex, year, month)
        }
        // 재생 내보내기 요청 처리 함수
        function onRequestPlaybackExport(channelIndex, dateText, startTimeText, endTimeText) {
            root.requestPlaybackExport(channelIndex, dateText, startTimeText, endTimeText)
        }
        // 재생 일시정지 요청 처리 함수
        function onRequestPlaybackPause() {
            root.requestPlaybackPause()
        }
        // 재생 재개 요청 처리 함수
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

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6

            Text {
                text: root.sidebarTitle()
                color: theme ? theme.textPrimary : "white"
                font.bold: true
                font.pixelSize: 14
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.showLiveTabs()

                Repeater {
                    model: [
                        { label: "System Metrics", index: 0 },
                        { label: "H/W Control", index: 1 }
                    ]

                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        hoverEnabled: true

                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: root.tabBorderColor(root.liveSidebarTab === parent.modelData.index)
                            color: root.tabSelectedColor(root.liveSidebarTab === parent.modelData.index)
                        }

                        contentItem: Text {
                            text: parent.modelData.label
                            color: root.tabTextColor(root.liveSidebarTab === parent.modelData.index)
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        // 클릭 이벤트 처리 함수
                        onClicked: root.liveSidebarTab = modelData.index
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: root.showCameraTabs()

                Repeater {
                    model: [
                        { label: "Camera Controls", index: 0 },
                        { label: "H/W Control", index: 1 }
                    ]

                    delegate: Button {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        hoverEnabled: true

                        background: Rectangle {
                            radius: 6
                            border.width: 1
                            border.color: root.tabBorderColor(root.cameraSidebarTab === parent.modelData.index)
                            color: root.tabSelectedColor(root.cameraSidebarTab === parent.modelData.index)
                        }

                        contentItem: Text {
                            text: parent.modelData.label
                            color: root.tabTextColor(root.cameraSidebarTab === parent.modelData.index)
                            font.pixelSize: 11
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        // 클릭 이벤트 처리 함수
                        onClicked: root.cameraSidebarTab = modelData.index
                    }
                }
            }
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
            active: root.cameraSidebarTab === 0
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
            active: root.liveSidebarTab === 0
        }

        SidebarMotorControlPanel {
            Layout.fillWidth: true
            theme: root.theme
            store: store
            active: (root.showCameraTabs() && root.cameraSidebarTab === 1)
                    || (root.showLiveTabs() && root.liveSidebarTab === 1)
        }

        Item {
            Layout.fillHeight: store.showPlaybackControls || store.showThermalControls
        }
    }
}
