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
    readonly property bool darkChrome: hostWindow ? hostWindow.isDarkMode : true

    transientParent: hostWindow
    width: 500
    height: 600
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"

    function sanitizeAngle(raw, fallback) {
        var text = String(raw || "").replace(/[^0-9]/g, "")
        if (text.length === 0)
            return String(fallback)
        var value = Number(text)
        if (value < 0)
            value = 0
        if (value > 180)
            value = 180
        return String(value)
    }

    function syncPresetFromBackend() {
        motor1Field.text = String(backend.eventAlertPresetMotor1Angle)
        motor2Field.text = String(backend.eventAlertPresetMotor2Angle)
        motor3Field.text = String(backend.eventAlertPresetMotor3Angle)
        laserCheck.checked = backend.eventAlertPresetLaserEnabled
    }

    function savePreset(showMessage) {
        motor1Field.text = sanitizeAngle(motor1Field.text, backend.eventAlertPresetMotor1Angle)
        motor2Field.text = sanitizeAngle(motor2Field.text, backend.eventAlertPresetMotor2Angle)
        motor3Field.text = sanitizeAngle(motor3Field.text, backend.eventAlertPresetMotor3Angle)
        backend.resetSessionTimer()
        backend.updateEventAlertPreset(Number(motor1Field.text),
                                       Number(motor2Field.text),
                                       Number(motor3Field.text),
                                       laserCheck.checked)
        if (showMessage) {
            statusText = "이벤트 preset을 저장했습니다."
            statusError = false
        }
    }

    function simplifyStatusMessage(message) {
        var text = String(message || "").replace(/\s+/g, " ").trim()
        if (text.length === 0)
            return "이벤트 제어 응답"
        if (text.length > 120)
            return text.substring(0, 120) + "..."
        return text
    }

    function severityLabel() {
        var value = String(backend.eventAlertSeverity || "").trim().toLowerCase()
        if (value.length === 0)
            return "INFO"
        return value.toUpperCase()
    }

    function severityColor() {
        var value = String(backend.eventAlertSeverity || "").trim().toLowerCase()
        if (value === "critical")
            return "#ef4444"
        if (value === "warning")
            return "#f59e0b"
        if (value === "high")
            return "#f97316"
        return theme ? theme.accent : "#f97316"
    }

    function activeControlSummary() {
        if (!backend.eventAlertActive)
            return "현재 수신된 이벤트가 없습니다."

        if (backend.eventAlertHasControlOverride) {
            return "Payload 제어값: M1 " + backend.eventAlertMotor1Angle
                + " / M2 " + backend.eventAlertMotor2Angle
                + " / M3 " + backend.eventAlertMotor3Angle
                + " / Laser " + (backend.eventAlertLaserEnabled ? "ON" : "OFF")
        }

        return "Payload 제어값 없음. 저장 preset 사용"
    }

    function presetSummary() {
        return "저장 preset: M1 " + backend.eventAlertPresetMotor1Angle
            + " / M2 " + backend.eventAlertPresetMotor2Angle
            + " / M3 " + backend.eventAlertPresetMotor3Angle
            + " / Laser " + (backend.eventAlertPresetLaserEnabled ? "ON" : "OFF")
    }

    function openDialog() {
        syncPresetFromBackend()
        statusText = backend.eventAlertActive
                   ? "현재 이벤트 정보를 확인하세요."
                   : "현재 수신된 이벤트가 없습니다."
        statusError = false
        visible = true
        backend.markEventAlertRead()
    }

    function closeDialog() {
        visible = false
    }

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

    Rectangle {
        anchors.fill: parent
        radius: 10
        color: theme ? theme.bgComponent : "#18181b"
        border.color: theme ? theme.border : "#27272a"
        border.width: 1

        ColumnLayout {
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

                ActionButton {
                    text: "닫기"
                    compact: true
                    Layout.preferredWidth: 72
                    onClicked: root.closeDialog()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 170
                radius: 8
                color: theme && hostWindow && hostWindow.isDarkMode ? "#0b0f1a" : "#f8fafc"
                border.color: theme ? theme.border : "#d1d5db"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: backend.eventAlertActive
                              ? (backend.eventAlertTitle.length > 0 ? backend.eventAlertTitle : "이벤트 알림")
                              : "현재 이벤트 없음"
                        color: theme ? theme.textPrimary : "#111827"
                        font.pixelSize: 15
                        font.bold: true
                        wrapMode: Text.WordWrap
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
                                text: "Source: " + (backend.eventAlertSource.length > 0 ? backend.eventAlertSource : "-")
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
                                text: backend.eventAlertAutoControl ? "AUTO ON" : "AUTO OFF"
                                color: "white"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        text: backend.eventAlertActive
                              ? (backend.eventAlertMessage.length > 0 ? backend.eventAlertMessage : "이벤트 설명이 없습니다.")
                              : "MQTT 이벤트를 받으면 여기에서 제목과 설명을 확인할 수 있습니다."
                        color: theme ? theme.textSecondary : "#374151"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                    }

                    Text {
                        text: root.activeControlSummary()
                        color: theme ? theme.textSecondary : "#374151"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 182
                radius: 8
                color: theme && hostWindow && hostWindow.isDarkMode ? "#0b0f1a" : "#f8fafc"
                border.color: theme ? theme.border : "#d1d5db"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Text {
                        text: "자동제어 프리셋"
                        color: theme ? theme.textPrimary : "#111827"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 8
                        rowSpacing: 6

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Motor 1"
                                color: theme ? theme.textSecondary : "#374151"
                                font.pixelSize: 12
                            }

                            TextField {
                                id: motor1Field
                                Layout.fillWidth: true
                                implicitHeight: 38
                                text: "90"
                                color: theme ? theme.textPrimary : "#111827"
                                font.pixelSize: 13
                                font.bold: true
                                leftPadding: 0
                                rightPadding: 0
                                topPadding: 0
                                bottomPadding: 0
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                selectByMouse: true
                                background: Rectangle {
                                    color: theme ? theme.bgSecondary : "#ffffff"
                                    border.color: motor1Field.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#d1d5db")
                                    border.width: 1
                                    radius: 6
                                }
                                onTextEdited: {
                                    var s = root.sanitizeAngle(text, backend.eventAlertPresetMotor1Angle)
                                    if (s !== text) {
                                        text = s
                                        cursorPosition = text.length
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Motor 2"
                                color: theme ? theme.textSecondary : "#374151"
                                font.pixelSize: 12
                            }

                            TextField {
                                id: motor2Field
                                Layout.fillWidth: true
                                implicitHeight: 38
                                text: "90"
                                color: theme ? theme.textPrimary : "#111827"
                                font.pixelSize: 13
                                font.bold: true
                                leftPadding: 0
                                rightPadding: 0
                                topPadding: 0
                                bottomPadding: 0
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                selectByMouse: true
                                background: Rectangle {
                                    color: theme ? theme.bgSecondary : "#ffffff"
                                    border.color: motor2Field.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#d1d5db")
                                    border.width: 1
                                    radius: 6
                                }
                                onTextEdited: {
                                    var s = root.sanitizeAngle(text, backend.eventAlertPresetMotor2Angle)
                                    if (s !== text) {
                                        text = s
                                        cursorPosition = text.length
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Motor 3"
                                color: theme ? theme.textSecondary : "#374151"
                                font.pixelSize: 12
                            }

                            TextField {
                                id: motor3Field
                                Layout.fillWidth: true
                                implicitHeight: 38
                                text: "90"
                                color: theme ? theme.textPrimary : "#111827"
                                font.pixelSize: 13
                                font.bold: true
                                leftPadding: 0
                                rightPadding: 0
                                topPadding: 0
                                bottomPadding: 0
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                selectByMouse: true
                                background: Rectangle {
                                    color: theme ? theme.bgSecondary : "#ffffff"
                                    border.color: motor3Field.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#d1d5db")
                                    border.width: 1
                                    radius: 6
                                }
                                onTextEdited: {
                                    var s = root.sanitizeAngle(text, backend.eventAlertPresetMotor3Angle)
                                    if (s !== text) {
                                        text = s
                                        cursorPosition = text.length
                                    }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "레이저 포함"
                            color: theme ? theme.textSecondary : "#374151"
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Switch {
                            id: laserCheck
                            checked: false
                            padding: 0

                            indicator: Rectangle {
                                implicitWidth: 46
                                implicitHeight: 26
                                radius: 13
                                color: laserCheck.checked
                                       ? (theme ? theme.accent : "#f97316")
                                       : (theme ? theme.bgSecondary : "#ffffff")
                                border.color: laserCheck.checked
                                              ? (theme ? theme.accent : "#f97316")
                                              : (theme ? theme.border : "#d1d5db")
                                border.width: 1

                                Rectangle {
                                    width: 20
                                    height: 20
                                    radius: 10
                                    y: 3
                                    x: laserCheck.checked ? 23 : 3
                                    color: "white"

                                    Behavior on x {
                                        NumberAnimation {
                                            duration: 120
                                            easing.type: Easing.OutCubic
                                        }
                                    }
                                }
                            }

                            contentItem: Text {
                                text: laserCheck.checked ? "ON" : "OFF"
                                color: theme ? theme.textSecondary : "#374151"
                                font.pixelSize: 12
                                font.bold: true
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: laserCheck.indicator.width + 10
                            }

                            background: Rectangle {
                                color: "transparent"
                            }
                        }
                    }

                    Text {
                        text: root.presetSummary()
                        color: theme ? theme.textSecondary : "#374151"
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
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
                    onClicked: {
                        root.savePreset(false)
                        backend.resetSessionTimer()
                        backend.applyEventAlertControl()
                    }
                }

                ActionButton {
                    text: "프리셋 저장"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
                    onClicked: root.savePreset(true)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                ActionButton {
                    text: "이벤트 초기화"
                    dangerStyle: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.minimumWidth: 0
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
    }
}
