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

    transientParent: hostWindow
    width: 500
    height: 560
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

                Button {
                    text: "닫기"
                    Layout.preferredWidth: 72
                    onClicked: root.closeDialog()
                    background: Rectangle {
                        color: parent.down ? "#dc2626" : (theme ? theme.bgSecondary : "#111827")
                        border.color: theme ? theme.border : "#27272a"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textPrimary : "white"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
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
                        text: "자동제어 preset"
                        color: theme ? theme.textPrimary : "#111827"
                        font.pixelSize: 14
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "Motor 1"
                            color: theme ? theme.textSecondary : "#374151"
                            Layout.preferredWidth: 58
                        }

                        TextField {
                            id: motor1Field
                            Layout.fillWidth: true
                            text: "90"
                            inputMethodHints: Qt.ImhDigitsOnly
                            onTextEdited: {
                                var s = root.sanitizeAngle(text, backend.eventAlertPresetMotor1Angle)
                                if (s !== text) {
                                    text = s
                                    cursorPosition = text.length
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "Motor 2"
                            color: theme ? theme.textSecondary : "#374151"
                            Layout.preferredWidth: 58
                        }

                        TextField {
                            id: motor2Field
                            Layout.fillWidth: true
                            text: "90"
                            inputMethodHints: Qt.ImhDigitsOnly
                            onTextEdited: {
                                var s = root.sanitizeAngle(text, backend.eventAlertPresetMotor2Angle)
                                if (s !== text) {
                                    text = s
                                    cursorPosition = text.length
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            text: "Motor 3"
                            color: theme ? theme.textSecondary : "#374151"
                            Layout.preferredWidth: 58
                        }

                        TextField {
                            id: motor3Field
                            Layout.fillWidth: true
                            text: "90"
                            inputMethodHints: Qt.ImhDigitsOnly
                            onTextEdited: {
                                var s = root.sanitizeAngle(text, backend.eventAlertPresetMotor3Angle)
                                if (s !== text) {
                                    text = s
                                    cursorPosition = text.length
                                }
                            }
                        }
                    }

                    CheckBox {
                        id: laserCheck
                        text: "레이저 ON 포함"
                        checked: false
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

                Button {
                    text: "Preset 저장"
                    Layout.fillWidth: true
                    onClicked: root.savePreset(true)
                }

                Button {
                    text: "현재 이벤트 적용"
                    Layout.fillWidth: true
                    enabled: backend.eventAlertActive
                    onClicked: {
                        root.savePreset(false)
                        backend.resetSessionTimer()
                        backend.applyEventAlertControl()
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "이벤트 초기화"
                    Layout.fillWidth: true
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.clearEventAlert()
                        statusText = "현재 이벤트를 초기화했습니다."
                        statusError = false
                    }
                }

                Button {
                    text: "닫기"
                    Layout.fillWidth: true
                    onClicked: root.closeDialog()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: theme && hostWindow && hostWindow.isDarkMode ? "#0b0f1a" : "#f8fafc"
                border.color: statusError ? "#ef4444" : (theme ? theme.border : "#d1d5db")
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.statusText
                    wrapMode: Text.WordWrap
                    color: statusError ? "#ef4444" : (theme ? theme.textSecondary : "#111827")
                    font.pixelSize: 12
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
