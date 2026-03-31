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
    property int controlElementHeight: 36
    property int controlTextPixelSize: 12
    property int comboHorizontalPadding: 28
    property color mutedTextColor: theme && theme.textSecondary ? Qt.lighter(theme.textSecondary, 1.18) : "#c7c9d3"
    property color statusTextColor: theme && theme.textSecondary ? Qt.lighter(theme.textSecondary, 1.3) : "#e0e2ea"

    visible: active
    Layout.fillWidth: true
    Layout.fillHeight: visible
    Layout.preferredHeight: visible ? 1 : 0
    Layout.minimumHeight: visible ? 1 : 0
    // 선택 모터 조회 함수
    function selectedMotor() {
        return Number(motorCombo.currentText)
    }
    // 선택 각도 조회 함수
    function selectedAngle() {
        return Number(angleField.text)
    }
    // 선택 속도 조회 함수
    function selectedSpeed() {
        return Number(speedField.text)
    }
    // 선택 방향 조회 함수
    function selectedDirection() {
        return directionCombo.currentIndex === 0 ? "left" : "right"
    }
    // 각도 입력값 정리 함수
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
    // 속도 입력값 정리 함수
    function sanitizeSpeed(raw) {
        var t = String(raw || "").replace(/[^0-9]/g, "")
        if (t.length === 0)
            return "5"
        var n = Number(t)
        if (n < 1)
            n = 1
        if (n > 10)
            n = 10
        return String(n)
    }
    // 제어 상태 메시지 정리 함수
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
    // 누름 유지 방향 조회 함수
    function holdDirection(motor) {
        return holdDirectionByMotor[motor] || ""
    }
    // 누름 유지 동작 여부 확인 함수
    function hasActiveHold() {
        return holdDirection(1).length > 0
                || holdDirection(2).length > 0
                || holdDirection(3).length > 0
    }
    // 누름 유지 방향 설정 함수
    function setHoldDirection(motor, direction) {
        var next = {
            1: holdDirectionByMotor[1] || "",
            2: holdDirectionByMotor[2] || "",
            3: holdDirectionByMotor[3] || ""
        }
        next[motor] = direction
        holdDirectionByMotor = next
    }
    // 모터 누름 유지 시작 함수
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
    // 모터 누름 유지 중지 함수
    function stopMotorHold(motor) {
        if (holdDirection(motor).length === 0 || !motorBackend)
            return
        motorBackend.resetSessionTimer()
        motorBackend.motorRelease(motor)
        setHoldDirection(motor, "")
    }
    // 전체 모터 누름 유지 중지 함수
    function stopAllMotorHolds() {
        stopMotorHold(1)
        stopMotorHold(2)
        stopMotorHold(3)
        pressedHoldMotors = []
    }
    // 선택 모터 누름 유지 시작 함수
    function startSelectedMotorHolds() {
        var motorId = selectedMotor()
        var direction = selectedDirection()
        startMotorHold(motorId, direction)
        pressedHoldMotors = [motorId]
        statusText = "Motor hold active - M" + motorId + ":" + (direction === "left" ? "L" : "R")
        statusError = false
    }
    // 누름 모터 유지 해제 함수
    function releasePressedMotorHolds() {
        var motors = pressedHoldMotors || []
        for (var i = 0; i < motors.length; ++i)
            stopMotorHold(motors[i])
        pressedHoldMotors = []
    }
    // 레이저 활성 상태 동기화 함수
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
    // 가시성 변경 처리 함수
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
                Layout.preferredHeight: 336
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
                        text: "Hold / stop / angle / speed control"
                        color: root.mutedTextColor
                        font.pixelSize: 12
                    }

                    RowLayout {
                        id: targetDirectionRow
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: (targetDirectionRow.width - targetDirectionRow.spacing) / 2
                            spacing: 4

                            Text {
                                text: "Target"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            ComboBox {
                                id: motorCombo
                                Layout.fillWidth: true
                                implicitHeight: root.controlElementHeight
                                model: ["1", "2", "3"]
                                currentIndex: 0
                                leftPadding: root.comboHorizontalPadding
                                rightPadding: root.comboHorizontalPadding
                                font.pixelSize: root.controlTextPixelSize
                                font.bold: true

                                contentItem: Text {
                                    text: motorCombo.displayText
                                    color: theme ? theme.textPrimary : "white"
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    font: motorCombo.font
                                    elide: Text.ElideRight
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
                                    horizontalAlignment: Text.AlignHCenter
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
                            Layout.preferredWidth: (targetDirectionRow.width - targetDirectionRow.spacing) / 2
                            spacing: 4

                            Text {
                                text: "Direction"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            ComboBox {
                                id: directionCombo
                                Layout.fillWidth: true
                                implicitHeight: root.controlElementHeight
                                model: ["Left", "Right"]
                                currentIndex: 0
                                leftPadding: root.comboHorizontalPadding
                                rightPadding: root.comboHorizontalPadding
                                font.pixelSize: root.controlTextPixelSize
                                font.bold: true

                                contentItem: Text {
                                    text: directionCombo.displayText
                                    color: theme ? theme.textPrimary : "white"
                                    verticalAlignment: Text.AlignVCenter
                                    horizontalAlignment: Text.AlignHCenter
                                    font: directionCombo.font
                                    elide: Text.ElideRight
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
                                    horizontalAlignment: Text.AlignHCenter
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
                        id: holdStopRow
                        Layout.fillWidth: true
                        spacing: 8

                        C.SidebarControlButton {
                            text: "Hold"
                            accentStyle: root.hasActiveHold()
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            Layout.preferredWidth: (holdStopRow.width - holdStopRow.spacing) / 2
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 누름 이벤트 처리 함수
                            onPressed: root.startSelectedMotorHolds()
                            // 해제 이벤트 처리 함수
                            onReleased: root.releasePressedMotorHolds()
                            // 취소 이벤트 처리 함수
                            onCanceled: root.releasePressedMotorHolds()
                        }

                        C.SidebarControlButton {
                            text: "Stop"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            Layout.preferredWidth: (holdStopRow.width - holdStopRow.spacing) / 2
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
                        id: speedRow
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: (speedRow.width - speedRow.spacing) / 2
                            spacing: 4

                            Text {
                                text: "Speed"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            TextField {
                                id: speedField
                                Layout.fillWidth: true
                                implicitHeight: root.controlElementHeight
                                text: "5"
                                color: theme ? theme.textPrimary : "white"
                                font.pixelSize: root.controlTextPixelSize
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
                                    color: theme ? theme.bgSecondary : "#09090b"
                                    border.color: speedField.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#27272a")
                                    border.width: 1
                                    radius: 6
                                }
                                // 텍스트 편집 처리 함수
                                onTextEdited: {
                                    var s = root.sanitizeSpeed(text)
                                    if (s !== text) {
                                        text = s
                                        cursorPosition = text.length
                                    }
                                }
                            }
                        }

                        C.SidebarControlButton {
                            text: "Apply"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            Layout.preferredWidth: (speedRow.width - speedRow.spacing) / 2
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            Layout.alignment: Qt.AlignBottom
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
                            onClicked: {
                                if (!motorBackend)
                                    return
                                speedField.text = root.sanitizeSpeed(speedField.text)
                                motorBackend.resetSessionTimer()
                                motorBackend.motorSetSpeed(root.selectedMotor(), root.selectedSpeed())
                            }
                        }
                    }

                    RowLayout {
                        id: angleRow
                        Layout.fillWidth: true
                        spacing: 8

                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: (angleRow.width - angleRow.spacing) / 2
                            spacing: 4

                            Text {
                                text: "Angle"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }

                            TextField {
                                id: angleField
                                Layout.fillWidth: true
                                implicitHeight: root.controlElementHeight
                                text: "90"
                                color: theme ? theme.textPrimary : "white"
                                font.pixelSize: root.controlTextPixelSize
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
                                    color: theme ? theme.bgSecondary : "#09090b"
                                    border.color: angleField.activeFocus
                                                  ? (theme ? theme.accent : "#f97316")
                                                  : (theme ? theme.border : "#27272a")
                                    border.width: 1
                                    radius: 6
                                }
                                // 텍스트 편집 처리 함수
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
                            Layout.preferredWidth: (angleRow.width - angleRow.spacing) / 2
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            Layout.alignment: Qt.AlignBottom
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
                        id: allStopRow
                        Layout.fillWidth: true
                        spacing: 8

                        C.SidebarControlButton {
                            text: "Center All"
                            compact: true
                            theme: root.theme
                            Layout.fillWidth: true
                            Layout.preferredWidth: (allStopRow.width - allStopRow.spacing) / 2
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
                            Layout.preferredWidth: (allStopRow.width - allStopRow.spacing) / 2
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
                Layout.preferredHeight: 112
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
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
                            Layout.preferredHeight: root.controlElementHeight
                            Layout.minimumHeight: root.controlElementHeight
                            enabled: !!motorBackend
                            // 클릭 이벤트 처리 함수
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
        // 카메라 제어 메시지 처리 함수
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
