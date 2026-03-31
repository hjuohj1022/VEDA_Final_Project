import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
Window {
    id: root
    property var theme
    property var hostWindow
    property string statusText: "이벤트 대기 중"
    property bool statusError: false
    property int selectedHistoryIndex: 0
    property bool historyDetailOpen: false
    readonly property bool darkChrome: hostWindow ? hostWindow.isDarkMode : true

    transientParent: hostWindow
    width: 860
    height: contentColumn.implicitHeight + 28
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"
    // 상태 메시지 정리 함수
    function simplifyStatusMessage(message) {
        var text = String(message || "").replace(/\s+/g, " ").trim()
        if (text.length === 0)
            return "이벤트 제어 응답"
        if (text.length > 120)
            return text.substring(0, 120) + "..."
        return text
    }
    // 현재 심각도 조회 함수
    function currentSeverityValue() {
        if (!backend.eventAlertActive)
            return "safe"

        var value = String(backend.eventAlertSeverity || "").trim().toLowerCase()
        if (value.length === 0)
            return "info"
        return value
    }
    // 심각도 라벨 계산 함수
    function severityLabel() {
        var value = root.currentSeverityValue()
        if (value === "safe")
            return "SAFE"
        return value.toUpperCase()
    }
    // 심각도 색상 계산 함수
    function severityColor() {
        var value = root.currentSeverityValue()
        if (value === "safe")
            return "#16a34a"
        if (value === "critical")
            return "#ef4444"
        if (value === "warning")
            return "#f59e0b"
        if (value === "high")
            return "#f97316"
        return theme ? theme.accent : "#f97316"
    }
    // 이벤트 이력 조회 함수
    function eventHistory() {
        return backend.eventAlertHistory || []
    }
    // 선택 이벤트 조회 함수
    function selectedEvent() {
        var history = eventHistory()
        if (history.length === 0)
            return null
        var index = Math.max(0, Math.min(selectedHistoryIndex, history.length - 1))
        return history[index]
    }
    // 선택 문자열 조회 함수
    function selectedString(key, fallback) {
        var item = selectedEvent()
        if (!item || item[key] === undefined || item[key] === null)
            return String(fallback || "")
        return String(item[key])
    }
    // 선택 불리언 조회 함수
    function selectedBool(key, fallback) {
        var item = selectedEvent()
        if (!item || item[key] === undefined || item[key] === null)
            return !!fallback
        return !!item[key]
    }
    // 제어 상태 요약 함수
    function activeControlSummary() {
        if (!backend.eventAlertActive)
            return "현재 감지된 이벤트가 없어 안전 상태입니다."

        return "현재 이벤트 적용 시 레이저 ON 후 비상 대피 시퀀스를 실행합니다."
    }
    // 다이얼로그 열기 함수
    function openDialog() {
        selectedHistoryIndex = 0
        historyDetailOpen = false
        statusText = backend.eventAlertActive
                   ? "현재 이벤트 정보를 확인하세요."
                   : "현재 수신된 이벤트가 없습니다."
        statusError = false
        visible = true
        backend.markEventAlertRead()
    }
    // 다이얼로그 닫기 함수
    function closeDialog() {
        visible = false
    }
    // 가시성 변경 처리 함수
    onVisibleChanged: {
        if (!visible)
            return
        if (hostWindow) {
            x = hostWindow.x + (hostWindow.width - width) / 2
            y = hostWindow.y + (hostWindow.height - height) / 2
        }
        backend.markEventAlertRead()
    }

    component ActionButton: Button {
        id: actionButton
        property bool accentStyle: false
        property bool dangerStyle: false
        property bool compact: false

        implicitHeight: compact ? 34 : 42
        hoverEnabled: enabled

        background: Rectangle {
            radius: 8
            border.width: 1
            border.color: {
                if (!actionButton.enabled)
                    return root.darkChrome ? "#2b3240" : "#d4d4d8"
                if (actionButton.accentStyle)
                    return actionButton.hovered ? "#fb923c" : (root.theme ? root.theme.accent : "#f97316")
                if (actionButton.dangerStyle)
                    return root.darkChrome ? "#7f1d1d" : "#ef4444"
                return root.darkChrome ? "#30384a" : "#d4d4d8"
            }
            color: {
                if (!actionButton.enabled)
                    return root.darkChrome ? "#141924" : "#f1f5f9"
                if (actionButton.accentStyle)
                    return actionButton.down ? "#ea580c"
                                             : (actionButton.hovered ? "#fb923c" : (root.theme ? root.theme.accent : "#f97316"))
                if (actionButton.dangerStyle) {
                    if (actionButton.down)
                        return root.darkChrome ? "#3a1010" : "#fee2e2"
                    return actionButton.hovered
                         ? (root.darkChrome ? "#311014" : "#fff1f2")
                         : (root.darkChrome ? "#1a1114" : "#ffffff")
                }
                if (actionButton.down)
                    return root.darkChrome ? "#0b0f17" : "#eef2f7"
                return actionButton.hovered
                     ? (root.darkChrome ? "#151b27" : "#f8fafc")
                     : (root.darkChrome ? "#101522" : "#ffffff")
            }
        }

        contentItem: Text {
            text: actionButton.text
            color: {
                if (!actionButton.enabled)
                    return root.darkChrome ? "#64748b" : "#94a3b8"
                if (actionButton.accentStyle)
                    return "white"
                if (actionButton.dangerStyle)
                    return root.darkChrome ? "#fda4af" : "#b91c1c"
                return root.darkChrome ? "#f8fafc" : "#0f172a"
            }
            font.pixelSize: actionButton.compact ? 12 : 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    Component {
        id: historyListComponent

        ListView {
            id: eventHistoryList
            clip: true
            spacing: 4
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
            // 최신 이벤트를 위에 두고, 더블클릭하면 상세 로그로 전환
            model: root.eventHistory()
            currentIndex: Math.max(0, Math.min(root.selectedHistoryIndex, count - 1))

            delegate: Rectangle {
                required property int index
                required property var modelData
                width: eventHistoryList.width - 8
                height: 30
                radius: 6
                color: ListView.isCurrentItem
                       ? (root.theme ? root.theme.accent : "#f97316")
                       : (root.darkChrome ? "#101522" : "#ffffff")
                border.width: 1
                border.color: ListView.isCurrentItem
                              ? (root.theme ? root.theme.accent : "#f97316")
                              : (root.theme ? root.theme.border : "#d1d5db")

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 9
                    anchors.rightMargin: 9
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: String(modelData.receivedAt || "-")
                    color: ListView.isCurrentItem ? "white" : (root.theme ? root.theme.textPrimary : "#111827")
                    font.pixelSize: 12
                    font.bold: ListView.isCurrentItem
                }

                MouseArea {
                    anchors.fill: parent
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        root.selectedHistoryIndex = index
                        eventHistoryList.currentIndex = index
                    }
                    // 더블 클릭 처리 함수
                    onDoubleClicked: {
                        root.selectedHistoryIndex = index
                        eventHistoryList.currentIndex = index
                        root.historyDetailOpen = true
                    }
                }
            }
        }
    }

    Component {
        id: historyDetailComponent

        ScrollView {
            clip: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Text {
                width: parent.width
                text: selectedEvent()
                      ? selectedString("message", "로그 내용이 없습니다.")
                      : "이벤트 목록에서 항목을 선택하면 상세 로그가 표시됩니다."
                color: theme ? theme.textSecondary : "#374151"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: theme ? theme.bgComponent : "#18181b"
        border.color: theme ? theme.border : "#27272a"
        border.width: 1

        ColumnLayout {
            id: contentColumn
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: "이벤트 알림"
                    color: theme ? theme.textPrimary : "white"
                    font.pixelSize: 16
                    font.bold: true
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 340
                    Layout.preferredHeight: 228
                    radius: 8
                    color: theme && hostWindow && hostWindow.isDarkMode ? "#0b0f1a" : "#f8fafc"
                    border.color: theme ? theme.border : "#d1d5db"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                text: root.historyDetailOpen ? "선택한 로그" : "이벤트 목록"
                                color: theme ? theme.textPrimary : "#111827"
                                font.pixelSize: 14
                                font.bold: true
                            }

                            Item { Layout.fillWidth: true }

                            ActionButton {
                                visible: !root.historyDetailOpen
                                text: "새로고침"
                                compact: true
                                Layout.preferredWidth: 84
                                // 클릭 이벤트 처리 함수
                                onClicked: {
                                    backend.resetSessionTimer()
                                    if (backend.refreshEventAlertHistory()) {
                                        statusText = "이벤트 목록 새로고침 요청을 전송했습니다."
                                        statusError = false
                                    } else {
                                        statusText = "이벤트 목록을 새로고침하지 못했습니다."
                                        statusError = true
                                    }
                                }
                            }

                            ActionButton {
                                visible: !root.historyDetailOpen
                                text: "삭제"
                                compact: true
                                dangerStyle: true
                                Layout.preferredWidth: 72
                                enabled: !!selectedEvent() && Number(selectedEvent().id || 0) > 0
                                // 클릭 이벤트 처리 함수
                                onClicked: {
                                    var item = selectedEvent()
                                    var eventLogId = item ? Number(item.id || 0) : 0
                                    backend.resetSessionTimer()
                                    if (eventLogId <= 0) {
                                        statusText = "삭제할 이벤트를 먼저 선택해주세요."
                                        statusError = true
                                    } else if (backend.deleteEventAlertItem(eventLogId)) {
                                        statusText = "선택한 이벤트 삭제 요청을 전송했습니다."
                                        statusError = false
                                    } else {
                                        statusText = "선택한 이벤트 삭제 요청을 시작하지 못했습니다."
                                        statusError = true
                                    }
                                }
                            }

                            ActionButton {
                                visible: root.historyDetailOpen
                                text: "목록"
                                compact: true
                                Layout.preferredWidth: 72
                                // 클릭 이벤트 처리 함수
                                onClicked: root.historyDetailOpen = false
                            }
                        }

                        Text {
                            visible: root.historyDetailOpen
                            text: selectedEvent()
                                  ? ("수신 시각: " + selectedString("receivedAt", "-")
                                     + " / 출처: " + selectedString("source", "-"))
                                  : "선택한 이벤트가 없습니다."
                            color: theme ? theme.textSecondary : "#6b7280"
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }

                        Loader {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            sourceComponent: root.historyDetailOpen ? historyDetailComponent : historyListComponent
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 470
                    Layout.preferredHeight: 228
                    radius: 8
                    color: theme && hostWindow && hostWindow.isDarkMode ? "#0b0f1a" : "#f8fafc"
                    border.color: theme ? theme.border : "#d1d5db"
                    border.width: 1

                    ColumnLayout {
                        id: eventSummaryColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Text {
                            text: backend.eventAlertActive
                                  ? (backend.eventAlertTitle.length > 0 ? backend.eventAlertTitle : "이벤트 알림")
                                  : "안전 상태"
                            color: theme ? theme.textPrimary : "#111827"
                            font.pixelSize: 15
                            font.bold: true
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Text {
                            // 최신 이벤트 카드에서 갱신 시점을 바로 확인
                            visible: backend.eventAlertActive && backend.eventAlertReceivedAtText.length > 0
                            text: "수신 시각: " + backend.eventAlertReceivedAtText
                            color: theme ? theme.textSecondary : "#6b7280"
                            font.pixelSize: 11
                            Layout.fillWidth: true
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.preferredHeight: 24
                                radius: 12
                                color: theme ? theme.bgSecondary : "#e5e7eb"
                                border.color: theme ? theme.border : "#d1d5db"
                                border.width: 1
                                Layout.preferredWidth: Math.max(86, sourceText.implicitWidth + 20)

                                Text {
                                    id: sourceText
                                    anchors.centerIn: parent
                                    text: backend.eventAlertActive
                                          ? ("출처: " + (backend.eventAlertSource.length > 0 ? backend.eventAlertSource : "-"))
                                          : "상태: 정상"
                                    color: theme ? theme.textSecondary : "#374151"
                                    font.pixelSize: 11
                                }
                            }

                            Rectangle {
                                Layout.preferredHeight: 24
                                radius: 12
                                color: root.severityColor()
                                Layout.preferredWidth: Math.max(70, severityText.implicitWidth + 20)

                                Text {
                                    id: severityText
                                    anchors.centerIn: parent
                                    text: root.severityLabel()
                                    color: "white"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }

                            Rectangle {
                                Layout.preferredHeight: 24
                                radius: 12
                                color: backend.eventAlertAutoControl ? "#16a34a" : "#6b7280"
                                Layout.preferredWidth: Math.max(84, autoText.implicitWidth + 20)

                                Text {
                                    id: autoText
                                    anchors.centerIn: parent
                                    text: backend.eventAlertAutoControl ? "자동 제어 ON" : "자동 제어 OFF"
                                    color: "white"
                                    font.pixelSize: 11
                                    font.bold: true
                                }
                            }

                            Item { Layout.fillWidth: true }
                        }

                        Text {
                            id: eventMessageText
                            visible: false
                            text: backend.eventAlertActive
                                  ? (backend.eventAlertMessage.length > 0 ? backend.eventAlertMessage : "이벤트 설명이 없습니다.")
                                  : "MQTT 이벤트를 받으면 여기에서 제목과 설명을 확인할 수 있습니다."
                            color: theme ? theme.textSecondary : "#374151"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Text {
                            visible: backend.eventAlertActive
                            text: root.activeControlSummary()
                            color: theme ? theme.textSecondary : "#374151"
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            visible: !backend.eventAlertActive
                            Layout.fillWidth: true
                            Layout.preferredHeight: safeStateText.implicitHeight + 16
                            radius: 8
                            color: root.darkChrome ? "#0f2f1f" : "#dcfce7"
                            border.width: 1
                            border.color: root.darkChrome ? "#166534" : "#86efac"

                            Text {
                                id: safeStateText
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                verticalAlignment: Text.AlignVCenter
                                text: "현재 감지된 이벤트가 없어 안전 상태입니다."
                                color: root.darkChrome ? "#bbf7d0" : "#166534"
                                font.pixelSize: 12
                                font.bold: true
                                wrapMode: Text.WordWrap
                            }
                        }

                        ScrollView {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                            Text {
                                width: parent.width
                                text: backend.eventAlertActive
                                      ? (backend.eventAlertMessage.length > 0 ? backend.eventAlertMessage : "이벤트 설명이 없습니다.")
                                      : "MQTT 이벤트를 받으면 여기에서 제목과 설명을 확인할 수 있습니다."
                                color: theme ? theme.textSecondary : "#374151"
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ActionButton {
                    text: "현재 이벤트 적용"
                    accentStyle: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    enabled: backend.eventAlertActive
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.applyEventAlertControl()
                    }
                }

                ActionButton {
                    text: "비상 동작 정지"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.stopEventAlertControl()
                        statusText = "비상 동작 정지 요청을 전송했습니다."
                        statusError = false
                    }
                }

                ActionButton {
                    text: "이벤트 초기화"
                    dangerStyle: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.clearEventAlert()
                        statusText = "현재 이벤트를 초기화했습니다."
                        statusError = false
                    }
                }

                ActionButton {
                    text: "닫기"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    // 클릭 이벤트 처리 함수
                    onClicked: root.closeDialog()
                }
            }

            Item {
                Layout.fillHeight: true
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                radius: 8
                color: theme && hostWindow && hostWindow.isDarkMode ? "#0b0f1a" : "#f8fafc"
                border.color: statusError ? "#ef4444" : (theme ? theme.border : "#d1d5db")
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    text: root.statusText
                    elide: Text.ElideRight
                    wrapMode: Text.NoWrap
                    verticalAlignment: Text.AlignVCenter
                    color: statusError ? "#ef4444" : (theme ? theme.textSecondary : "#111827")
                    font.pixelSize: 11
                }
            }
        }
    }
    Connections {
        target: backend
        // 카메라 제어 메시지 처리 함수
        function onCameraControlMessage(message, isError) {
            if (!root.visible)
                return
            if (message.indexOf("Motor ") !== 0
                    && message.indexOf("Laser ") !== 0
                    && message.indexOf("Event alert ") !== 0)
                return
            root.statusText = root.simplifyStatusMessage(message)
            root.statusError = isError
        }
        // 이벤트 알림 상태 변경 처리 함수
        function onEventAlertStateChanged() {
            if (!root.visible)
                return
            if (backend.eventAlertUnread)
                backend.markEventAlertRead()
            root.statusText = backend.eventAlertActive
                            ? (backend.eventAlertAutoControl
                               ? "새 이벤트 수신. 자동제어 요청이 전송되었습니다."
                               : "새 이벤트를 수신했습니다.")
                            : "현재 수신된 이벤트가 없습니다."
            root.statusError = false
        }
        // 이벤트 알림 이력 변경 처리 함수
        function onEventAlertHistoryChanged() {
            root.selectedHistoryIndex = 0
            root.historyDetailOpen = false
        }
    }
}
