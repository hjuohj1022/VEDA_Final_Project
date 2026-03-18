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

    transientParent: hostWindow
    width: 420
    height: 330
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
        visible = false
    }

    function selectedMotor() {
        return Number(motorCombo.currentText)
    }

    function selectedDirection() {
        return directionCombo.currentText
    }

    function selectedAngle() {
        return Number(angleField.text)
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

    onVisibleChanged: {
        if (!visible)
            return
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
                    text: "Motor"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    Layout.preferredWidth: 44
                }

                ComboBox {
                    id: motorCombo
                    Layout.preferredWidth: 80
                    model: ["1", "2", "3"]
                    currentIndex: 0
                }

                Text {
                    text: "Direction"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    Layout.preferredWidth: 62
                }

                ComboBox {
                    id: directionCombo
                    Layout.preferredWidth: 90
                    model: ["left", "right"]
                    currentIndex: 0
                }

                Item { Layout.fillWidth: true }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "Press"
                    Layout.fillWidth: true
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.motorPress(selectedMotor(), selectedDirection())
                    }
                }

                Button {
                    text: "Release"
                    Layout.fillWidth: true
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.motorRelease(selectedMotor())
                    }
                }

                Button {
                    text: "Stop"
                    Layout.fillWidth: true
                    onClicked: {
                        backend.resetSessionTimer()
                        backend.motorStop(selectedMotor())
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
            root.statusText = message
            root.statusError = isError
        }
    }
}
