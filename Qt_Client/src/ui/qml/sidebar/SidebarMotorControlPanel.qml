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
    property string statusText: "Motor control ready"
    property bool statusError: false
    property bool laserEnabled: false
    property var holdDirectionByMotor: ({1: "", 2: "", 3: ""})
    property var pressedHoldMotors: []
    property color mutedTextColor: theme && theme.textSecondary ? Qt.lighter(theme.textSecondary, 1.18) : "#c7c9d3"
    property color statusTextColor: theme && theme.textSecondary ? Qt.lighter(theme.textSecondary, 1.3) : "#e0e2ea"

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

    function simplifyControlStatusMessage(message) {
        var text = String(message || "").replace(/\s+/g, " ").trim()
        if (text.length === 0)
            return "Control command response"

        var successIdx = text.indexOf(" success:")
        if (successIdx >= 0) {
            var action = text.substring(0, successIdx)
            var detail = text.substring(successIdx + 9).trim()
            if (detail.length > 0 && (detail.indexOf("OK ") === 0 || detail.length <= 24))
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

    function hasActiveHold() {
        return holdDirection(1).length > 0
                || holdDirection(2).length > 0
                || holdDirection(3).length > 0
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
        statusText = "Motor hold active - M" + motorId + ":" + (direction === "left" ? "L" : "R")
        statusError = false
    }

    function releasePressedMotorHolds() {
        var motors = pressedHoldMotors || []
        for (var i = 0; i < motors.length; ++i)
            stopMotorHold(motors[i])
        pressedHoldMotors = []
    }

    function syncLaserEnabledFromMessage(message) {
        var text = String(message || "").toLowerCase()
        if (text.indexOf("laser on success") === 0) {
            laserEnabled = true
            return
        }
        if (text.indexOf("laser off success") === 0) {
            laserEnabled = false
            return
        }
        if (text.indexOf("laser bridge:") !== 0)
            return

        if (text.indexOf("last=laser on") >= 0 || text.indexOf("response=led on") >= 0) {
            laserEnabled = true
            return
        }
        if (text.indexOf("last=laser off") >= 0 || text.indexOf("response=led off") >= 0) {
            laserEnabled = false
        }
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
            height: Math.max(root.height, implicitHeight)
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 268
                radius: 8
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: "Motor Control"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    Text {
                        text: "Hold / stop / angle control"
                        color: root.mutedTextColor
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Target"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            ComboBox {
                                id: motorCombo
                                Layout.fillWidth: true
                                implicitHeight: 36
                                model: ["1", "2", "3"]
                                currentIndex: 0
                                leftPadding: 12
                                rightPadding: 28
                                font.pixelSize: 13

                                contentItem: Text {
                                    text: motorCombo.displayText
                                    color: theme ? theme.textPrimary : "white"
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: motorCombo.leftPadding
                                    rightPadding: motorCombo.rightPadding
                                    font: motorCombo.font
                                }

                                background: Rectangle {
                                    radius: 6
                                    color: theme ? theme.bgSecondary : "#09090b"
                                    border.width: 1
                                    border.color: motorCombo.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#27272a")
                                }

                                indicator: Text {
                                    text: "\u25BE"
                                    color: root.mutedTextColor
                                    font.pixelSize: 13
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                popup: Popup {
                                    y: motorCombo.height + 4
                                    width: motorCombo.width
                                    implicitHeight: contentItem.implicitHeight
                                    padding: 4

                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: motorCombo.popup.visible ? motorCombo.delegateModel : null
                                        currentIndex: motorCombo.highlightedIndex
                                    }

                                    background: Rectangle {
                                        radius: 8
                                        color: theme ? theme.bgComponent : "#18181b"
                                        border.width: 1
                                        border.color: theme ? theme.border : "#27272a"
                                    }
                                }

                                delegate: ItemDelegate {
                                    required property var modelData
                                    required property int index
                                    width: motorCombo.width - 8
                                    highlighted: motorCombo.highlightedIndex === index

                                    contentItem: Text {
                                        text: modelData
                                        color: highlighted
                                               ? "white"
                                               : (theme ? theme.textPrimary : "white")
                                        font.pixelSize: 13
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    background: Rectangle {
                                        radius: 6
                                        color: highlighted
                                               ? (theme ? theme.accent : "#f97316")
                                               : "transparent"
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Text {
                                text: "Direction"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            ComboBox {
                                id: directionCombo
                                Layout.fillWidth: true
                                implicitHeight: 36
                                model: ["Left", "Right"]
                                currentIndex: 0
                                leftPadding: 12
                                rightPadding: 28
                                font.pixelSize: 13

                                contentItem: Text {
                                    text: directionCombo.displayText
                                    color: theme ? theme.textPrimary : "white"
                                    verticalAlignment: Text.AlignVCenter
                                    leftPadding: directionCombo.leftPadding
                                    rightPadding: directionCombo.rightPadding
                                    font: directionCombo.font
                                }

                                background: Rectangle {
                                    radius: 6
                                    color: theme ? theme.bgSecondary : "#09090b"
                                    border.width: 1
                                    border.color: directionCombo.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#27272a")
                                }

                                indicator: Text {
                                    text: "\u25BE"
                                    color: root.mutedTextColor
                                    font.pixelSize: 13
                                    anchors.right: parent.right
                                    anchors.rightMargin: 10
                                    anchors.verticalCenter: parent.verticalCenter
                                }

                                popup: Popup {
                                    y: directionCombo.height + 4
                                    width: directionCombo.width
                                    implicitHeight: contentItem.implicitHeight
                                    padding: 4

                                    contentItem: ListView {
                                        clip: true
                                        implicitHeight: contentHeight
                                        model: directionCombo.popup.visible ? directionCombo.delegateModel : null
                                        currentIndex: directionCombo.highlightedIndex
                                    }

                                    background: Rectangle {
                                        radius: 8
                                        color: theme ? theme.bgComponent : "#18181b"
                                        border.width: 1
                                        border.color: theme ? theme.border : "#27272a"
                                    }
                                }

                                delegate: ItemDelegate {
                                    required property var modelData
                                    required property int index
                                    width: directionCombo.width - 8
                                    highlighted: directionCombo.highlightedIndex === index

                                    contentItem: Text {
                                        text: modelData
                                        color: highlighted
                                               ? "white"
                                               : (theme ? theme.textPrimary : "white")
                                        font.pixelSize: 13
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    background: Rectangle {
                                        radius: 6
                                        color: highlighted
                                               ? (theme ? theme.accent : "#f97316")
                                               : "transparent"
                                    }
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        C.SidebarControlButton {
                            text: "Hold"
                            accentStyle: root.hasActiveHold()
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

                        ColumnLayout {
                            Layout.preferredWidth: 88
                            spacing: 4

                            Text {
                                text: "Angle"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            TextField {
                                id: angleField
                                Layout.fillWidth: true
                                implicitHeight: 36
                                text: "90"
                                color: theme ? theme.textPrimary : "white"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                inputMethodHints: Qt.ImhDigitsOnly
                                selectByMouse: true
                                background: Rectangle {
                                    color: theme ? theme.bgSecondary : "#09090b"
                                    border.color: angleField.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#27272a")
                                    border.width: 1
                                    radius: 6
                                }
                                onTextEdited: {
                                    var s = root.sanitizeAngle(text)
                                    if (s !== text) {
                                        text = s
                                        cursorPosition = text.length
                                    }
                                }
                            }
                        }

                        C.SidebarControlButton {
                            text: "Set"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignBottom
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
                Layout.preferredHeight: 104
                radius: 8
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        text: "Laser Control"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 13
                    }

                    Text {
                        text: "Quick laser control via Crow API"
                        color: root.mutedTextColor
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        C.SidebarControlButton {
                            text: "On"
                            accentStyle: root.laserEnabled
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                motorBackend.resetSessionTimer()
                                motorBackend.laserOn()
                            }
                        }

                        C.SidebarControlButton {
                            text: "Off"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                motorBackend.resetSessionTimer()
                                motorBackend.laserOff()
                            }
                        }

                        C.SidebarControlButton {
                            text: "Status"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            enabled: !!motorBackend
                            onClicked: {
                                if (!motorBackend)
                                    return
                                motorBackend.resetSessionTimer()
                                motorBackend.laserStatus()
                            }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 118
                Layout.preferredHeight: 118
                radius: 8
                color: theme ? theme.bgComponent : "#18181b"
                border.color: statusError ? "#ef4444" : (theme ? theme.border : "#27272a")
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.margins: 12
                    text: root.statusText
                    wrapMode: Text.WordWrap
                    color: statusError ? "#ef4444" : root.statusTextColor
                    font.pixelSize: 13
                    font.weight: Font.Medium
                }
            }
        }
    }

    Connections {
        target: backend
        function onCameraControlMessage(message, isError) {
            if (!root.visible)
                return
            if (message.indexOf("Motor ") !== 0 && message.indexOf("Laser ") !== 0)
                return
            if (!isError)
                root.syncLaserEnabledFromMessage(message)
            root.statusText = root.simplifyControlStatusMessage(message)
            root.statusError = isError
        }
    }
}
