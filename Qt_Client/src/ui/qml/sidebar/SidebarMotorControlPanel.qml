import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as C

Item {
    id: root
    property var theme
    property var store
    property bool active: true
    property var motorBackend: store ? store.backend : null
    property string statusText: "모터 제어 대기"
    property bool statusError: false
    property var holdDirectionByMotor: ({1: "", 2: "", 3: ""})
    property var pressedHoldMotors: []

    visible: active
    Layout.fillWidth: true
    Layout.fillHeight: visible
    Layout.preferredHeight: visible ? 1 : 0
    Layout.minimumHeight: visible ? 1 : 0

    function selectedMotor() {
        return Number(motorCombo.currentText)
    }

    function selectedAngle() {
        return Number(angleField.text)
    }

    function selectedDirection() {
        return directionCombo.currentIndex === 0 ? "left" : "right"
    }

    function sanitizeAngle(raw) {
        var t = String(raw || "").replace(/[^0-9]/g, "")
        if (t.length === 0)
            return "90"
        var n = Number(t)
        if (n < 0)
            n = 0
        if (n > 180)
            n = 180
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
        if (!motorBackend)
            return

        var current = holdDirection(motor)
        if (current === direction)
            return

        if (current.length > 0) {
            motorBackend.resetSessionTimer()
            motorBackend.motorRelease(motor)
        }

        motorBackend.resetSessionTimer()
        motorBackend.motorPress(motor, direction)
        setHoldDirection(motor, direction)
    }

    function stopMotorHold(motor) {
        if (holdDirection(motor).length === 0 || !motorBackend)
            return
        motorBackend.resetSessionTimer()
        motorBackend.motorRelease(motor)
        setHoldDirection(motor, "")
    }

    function stopAllMotorHolds() {
        stopMotorHold(1)
        stopMotorHold(2)
        stopMotorHold(3)
        pressedHoldMotors = []
    }

    function startSelectedMotorHolds() {
        var motorId = selectedMotor()
        var direction = selectedDirection()
        startMotorHold(motorId, direction)
        pressedHoldMotors = [motorId]
        statusText = "모터 hold 적용 - M" + motorId + ":" + (direction === "left" ? "L" : "R")
        statusError = false
    }

    function releasePressedMotorHolds() {
        var motors = pressedHoldMotors || []
        for (var i = 0; i < motors.length; ++i)
            stopMotorHold(motors[i])
        pressedHoldMotors = []
    }

    onVisibleChanged: {
        if (!visible)
            stopAllMotorHolds()
    }

    ScrollView {
        anchors.fill: parent
        clip: true
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            width: root.width
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 118
                radius: 8
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 6

                    Text {
                        text: "Motor Control"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    Text {
                        text: "방향 hold / 정지 / 각도 이동 제어"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 11
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
                            Layout.preferredWidth: 84
                            model: ["1", "2", "3"]
                            currentIndex: 0
                        }

                        Item { Layout.fillWidth: true }

                        Text {
                            text: "Dir"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            Layout.preferredWidth: 24
                        }

                        ComboBox {
                            id: directionCombo
                            Layout.preferredWidth: 96
                            model: ["Left", "Right"]
                            currentIndex: 0
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 138
                radius: 8
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        C.SidebarControlButton {
                            text: "Hold"
                            accentStyle: true
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onPressed: root.startSelectedMotorHolds()
                            onReleased: root.releasePressedMotorHolds()
                            onCanceled: root.releasePressedMotorHolds()
                        }

                        C.SidebarControlButton {
                            text: "Stop"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                var motorId = root.selectedMotor()
                                root.stopMotorHold(motorId)
                                motorBackend.resetSessionTimer()
                                motorBackend.motorStop(motorId)
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
                                var s = root.sanitizeAngle(text)
                                if (s !== text) {
                                    text = s
                                    cursorPosition = text.length
                                }
                            }
                        }

                        C.SidebarControlButton {
                            text: "Set"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                angleField.text = root.sanitizeAngle(angleField.text)
                                motorBackend.resetSessionTimer()
                                motorBackend.motorSetAngle(root.selectedMotor(), root.selectedAngle())
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        C.SidebarControlButton {
                            text: "Center All"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                angleField.text = root.sanitizeAngle(angleField.text)
                                motorBackend.resetSessionTimer()
                                motorBackend.motorCenter(root.selectedAngle())
                            }
                        }

                        C.SidebarControlButton {
                            text: "Stop All"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                root.stopAllMotorHolds()
                                motorBackend.resetSessionTimer()
                                motorBackend.motorStopAll()
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 110
                radius: 8
                color: theme ? theme.bgComponent : "#18181b"
                border.color: statusError ? "#ef4444" : (theme ? theme.border : "#27272a")
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.margins: 12
                    text: root.statusText
                    wrapMode: Text.WordWrap
                    color: statusError ? "#ef4444" : (theme ? theme.textSecondary : "#d4d4d8")
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
