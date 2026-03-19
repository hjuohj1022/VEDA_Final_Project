import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Window {
    id: root
    property var theme
    property var hostWindow
    property string statusText: "모터 제어 대기"
    property bool statusError: false
    property var holdDirectionByMotor: ({1: "", 2: "", 3: ""})
    property var pressedHoldMotors: []

    transientParent: hostWindow
    width: 420
    height: 420
    visible: false
    modality: Qt.NonModal
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"

    function openDialog() {
        statusText = "모터 제어 대기"
        statusError = false
        visible = true
    }

    function closeDialog() {
        stopAllMotorHolds()
        visible = false
    }

    function selectedMotor() {
        return Number(motorCombo.currentText)
    }

    function selectedAngle() {
        return Number(angleField.text)
    }

    // 선택된 방향 반환 함수
    function selectedDirection() {
        return directionCombo.currentIndex === 0 ? "left" : "right"
    }

    // 선택된 Target/Direction 기준 Hold 적용 함수
    function startSelectedMotorHolds() {
        var motorId = selectedMotor()
        var direction = selectedDirection()
        startMotorHold(motorId, direction)
        pressedHoldMotors = [motorId]
        statusText = "모터 hold 적용 - M" + motorId + ":" + (direction === "left" ? "L" : "R")
        statusError = false
        return true
    }

    // Hold 버튼 손떼기 시 자동 release 처리 함수
    function releasePressedMotorHolds() {
        var motors = pressedHoldMotors || []
        for (var i = 0; i < motors.length; ++i)
            stopMotorHold(motors[i])
        pressedHoldMotors = []
    }

    function sanitizeAngle(raw) {
        var t = String(raw || "").replace(/[^0-9]/g, "")
        if (t.length === 0)
            return "90"
        var n = Number(t)
        if (n < 0) n = 0
        if (n > 180) n = 180
        return String(n)
    }

    function simplifyMotorStatusMessage(message) {
        var text = String(message || "").replace(/\s+/g, " ").trim()
        if (text.length === 0)
            return "Motor command response"

        var successIdx = text.indexOf(" success:")
        if (successIdx >= 0) {
            var action = text.substring(0, successIdx)
            var detail = text.substring(successIdx + 9).trim()
            if (detail.indexOf("OK ") === 0)
                return action + " success (" + detail + ")"
            return action + " success"
        }

        var failedIdx = text.indexOf(" failed")
        if (failedIdx >= 0) {
            var base = text.substring(0, failedIdx)
            if (text.indexOf("(HTTP ") >= 0) {
                var httpStart = text.indexOf("(HTTP ")
                var httpEnd = text.indexOf(")", httpStart)
                if (httpEnd > httpStart) {
                    var httpPart = text.substring(httpStart, httpEnd + 1)
                    return base + " failed " + httpPart
                }
            }
            return base + " failed"
        }

        if (text.indexOf("{") >= 0)
            text = text.substring(0, text.indexOf("{")).trim()

        if (text.length > 96)
            text = text.substring(0, 96) + "..."

        return text
    }

    function holdDirection(motor) {
        return holdDirectionByMotor[motor] || ""
    }

    function setHoldDirection(motor, direction) {
        var next = {
            1: holdDirectionByMotor[1] || "",
            2: holdDirectionByMotor[2] || "",
            3: holdDirectionByMotor[3] || ""
        }
        next[motor] = direction
        holdDirectionByMotor = next
    }

    function startMotorHold(motor, direction) {
        var current = holdDirection(motor)
        if (current === direction)
            return

        if (current.length > 0) {
            backend.resetSessionTimer()
            backend.motorRelease(motor)
        }

        backend.resetSessionTimer()
        backend.motorPress(motor, direction)
        setHoldDirection(motor, direction)
    }

    function stopMotorHold(motor) {
        if (holdDirection(motor).length === 0)
            return
        backend.resetSessionTimer()
        backend.motorRelease(motor)
        setHoldDirection(motor, "")
    }

    function stopAllMotorHolds() {
        stopMotorHold(1)
        stopMotorHold(2)
        stopMotorHold(3)
        pressedHoldMotors = []
    }

    onVisibleChanged: {
        if (!visible) {
            stopAllMotorHolds()
            return
        }
        backend.resetSessionTimer()
        if (hostWindow) {
            x = hostWindow.x + (hostWindow.width - width) / 2
            y = hostWindow.y + (hostWindow.height - height) / 2
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
                    text: "Motor Control (임시)"
                    color: theme ? theme.textPrimary : "white"
                    font.pixelSize: 15
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

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Target"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    Layout.preferredWidth: 44
                }

                ComboBox {
                    id: motorCombo
                    Layout.preferredWidth: 80
                    model: ["1", "2", "3"]
                    currentIndex: 0
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "Dir"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    Layout.preferredWidth: 44
                }

                ComboBox {
                    id: directionCombo
                    Layout.preferredWidth: 100
                    model: ["Left", "Right"]
                    currentIndex: 0
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "Hold"
                    Layout.fillWidth: true
                    onPressed: {
                        root.startSelectedMotorHolds()
                    }
                    onReleased: {
                        root.releasePressedMotorHolds()
                    }
                    onCanceled: {
                        root.releasePressedMotorHolds()
                    }
                }

                Button {
                    text: "Stop"
                    Layout.fillWidth: true
                    onClicked: {
                        var motorId = root.selectedMotor()
                        root.stopMotorHold(motorId)
                        backend.resetSessionTimer()
                        backend.motorStop(motorId)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    text: "Angle"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    Layout.preferredWidth: 44
                }

                TextField {
                    id: angleField
                    Layout.preferredWidth: 72
                    text: "90"
                    inputMethodHints: Qt.ImhDigitsOnly
                    onTextEdited: {
                        var s = sanitizeAngle(text)
                        if (s !== text) {
                            text = s
                            cursorPosition = text.length
                        }
                    }
                }

                Button {
                    text: "Set"
                    Layout.fillWidth: true
                    onClicked: {
                        backend.resetSessionTimer()
                        angleField.text = sanitizeAngle(angleField.text)
                        backend.motorSetAngle(selectedMotor(), selectedAngle())
                    }
                }

                Button {
                    text: "Center All"
                    Layout.fillWidth: true
                    onClicked: {
                        backend.resetSessionTimer()
                        angleField.text = sanitizeAngle(angleField.text)
                        backend.motorCenter(selectedAngle())
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "Stop All"
                    Layout.fillWidth: true
                    onClicked: {
                        root.stopAllMotorHolds()
                        backend.resetSessionTimer()
                        backend.motorStopAll()
                    }
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
            if (message.indexOf("Motor ") !== 0)
                return
            root.statusText = root.simplifyMotorStatusMessage(message)
            root.statusError = isError
        }
    }
}

