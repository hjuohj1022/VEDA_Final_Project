import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property var cameraNames: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
    property bool showCameraControls: false
    property int selectedCameraIndex: -1
    property string cameraControlStatus: ""
    property bool cameraControlError: false
    property bool mapModeEnabled: false
    property bool supportZoom: true
    property bool supportFocus: true
    signal requestCameraNameChange(int cameraIndex, string name)
    color: theme ? theme.bgSecondary : "#09090b"

    function selectedCameraTitle() {
        if (selectedCameraIndex < 0 || selectedCameraIndex >= cameraNames.length)
            return "Camera"
        return "Cam " + (selectedCameraIndex + 1) + " - " + cameraNames[selectedCameraIndex]
    }

    Connections {
        target: backend
        function onCameraControlMessage(message, isError) {
            if (!root.showCameraControls)
                return
            root.cameraControlStatus = message
            root.cameraControlError = isError
            controlStatusTimer.restart()
        }
        function onSunapiSupportedPtzActionsLoaded(cameraIndex, actions) {
            if (cameraIndex !== root.selectedCameraIndex)
                return
            root.supportZoom = actions.zoom !== false
            root.supportFocus = actions.focus !== false
        }
    }

    Timer {
        id: controlStatusTimer
        interval: 3000
        repeat: false
        onTriggered: {
            root.cameraControlStatus = ""
            root.cameraControlError = false
        }
    }

    component ControlButton: Button {
        id: controlBtn
        property bool accentStyle: false
        property bool compact: false
        implicitHeight: compact ? 30 : 40
        hoverEnabled: enabled

        background: Rectangle {
            radius: 6
            border.width: 1
            border.color: controlBtn.accentStyle
                          ? (controlBtn.enabled ? (controlBtn.hovered ? "#fb923c" : theme.accent) : theme.border)
                          : (controlBtn.hovered ? "#3f3f46" : theme.border)
            color: !controlBtn.enabled
                   ? (theme ? theme.bgComponent : "#18181b")
                   : controlBtn.down
                     ? (controlBtn.accentStyle ? "#ea580c" : (theme ? theme.bgSecondary : "#09090b"))
                     : controlBtn.accentStyle
                       ? (theme ? theme.accent : "#f97316")
                       : (theme ? theme.bgSecondary : "#09090b")
        }

        contentItem: Text {
            text: controlBtn.text
            color: !controlBtn.enabled
                   ? (theme ? theme.textSecondary : "#71717a")
                   : (controlBtn.accentStyle ? "white" : (theme ? theme.textPrimary : "white"))
            font.pixelSize: compact ? 12 : 13
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    onShowCameraControlsChanged: {
        if (showCameraControls && selectedCameraIndex >= 0) {
            backend.sunapiLoadSupportedPtzActions(selectedCameraIndex)
            cameraNameField.text = (selectedCameraIndex < cameraNames.length) ? cameraNames[selectedCameraIndex] : ""
        }
    }

    onSelectedCameraIndexChanged: {
        if (showCameraControls && selectedCameraIndex >= 0) {
            supportZoom = true
            supportFocus = true
            backend.sunapiLoadSupportedPtzActions(selectedCameraIndex)
            cameraNameField.text = (selectedCameraIndex < cameraNames.length) ? cameraNames[selectedCameraIndex] : ""
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

        // 우측 패널 상단 제목
        Text {
            text: root.showCameraControls ? "Camera Controls" : "System Metrics"
            color: theme ? theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14
        }

        Rectangle {
            visible: root.showCameraControls
            Layout.fillWidth: true
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8
            Layout.preferredHeight: 150

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                Text {
                    text: root.selectedCameraTitle()
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 13
                }
                Text {
                    text: "확대 화면 제어 패널"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 11
                }

                Text {
                    text: "휠 업/다운: 줌 인/아웃"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: cameraNameField
                        Layout.fillWidth: true
                        placeholderText: "위치 이름"
                        color: theme ? theme.textPrimary : "white"
                        placeholderTextColor: theme ? theme.textSecondary : "#71717a"
                        enabled: root.selectedCameraIndex >= 0
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: cameraNameField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onAccepted: {
                            if (root.selectedCameraIndex < 0)
                                return
                            var trimmed = text.trim()
                            if (trimmed.length === 0)
                                return
                            root.requestCameraNameChange(root.selectedCameraIndex, trimmed)
                            root.cameraControlStatus = "카메라 이름이 변경되었습니다."
                            root.cameraControlError = false
                            controlStatusTimer.restart()
                        }
                    }
                    ControlButton {
                        text: "저장"
                        compact: true
                        Layout.preferredWidth: 56
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: {
                            if (root.selectedCameraIndex < 0)
                                return
                            var trimmed = cameraNameField.text.trim()
                            if (trimmed.length === 0)
                                return
                            root.requestCameraNameChange(root.selectedCameraIndex, trimmed)
                            root.cameraControlStatus = "카메라 이름이 변경되었습니다."
                            root.cameraControlError = false
                            controlStatusTimer.restart()
                        }
                    }
                }

                ControlButton {
                    text: root.mapModeEnabled ? "3D Map 모드 ON (미구현)" : "3D Map 모드 OFF (미구현)"
                    Layout.fillWidth: true
                    compact: true
                    onClicked: {
                        root.mapModeEnabled = !root.mapModeEnabled
                        if (root.mapModeEnabled && root.selectedCameraIndex >= 0) {
                            backend.sunapiSimpleAutoFocus(root.selectedCameraIndex)
                        }
                    }
                }
            }
        }

        Rectangle {
            visible: root.showCameraControls
            Layout.fillWidth: true
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8
            Layout.preferredHeight: 260

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    ControlButton {
                        text: "줌 +"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomIn(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "줌 -"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomOut(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "줌 정지"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomStop(root.selectedCameraIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6

                    ControlButton {
                        text: "포커스 Near"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusNear(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "포커스 Far"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusFar(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "포커스 정지"
                        Layout.fillWidth: true
                        compact: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusStop(root.selectedCameraIndex)
                    }
                    ControlButton {
                        text: "오토포커스"
                        Layout.fillWidth: true
                        compact: true
                        accentStyle: root.mapModeEnabled
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiSimpleAutoFocus(root.selectedCameraIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.cameraControlStatus.length > 0
                    text: root.cameraControlStatus
                    color: root.cameraControlError ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }

                Item { Layout.fillHeight: true }
            }
        }

        // 시스템 메트릭 차트 영역
        ColumnLayout {
            visible: !root.showCameraControls
            spacing: 8
            
            component SystemChart : Rectangle {
                id: chartRoot
                property string title: "Metric"
                property string unit: ""
                property real value: 0
                property string valueColor: theme ? theme.textPrimary : "white"
                property color graphColor: "#f97316"
                property var dataHistory: []
                property real calculatedMax: 100
                property real calculatedAvg: 0
                
                Layout.fillWidth: true
                height: 100
                color: theme ? theme.bgComponent : "#18181b"
                radius: 8
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                
                function updateStats() {
                    var max = 0
                    var sum = 0
                    var len = chatRoot.dataHistory.length
                    if (len === 0) return

                    for(var i=0; i<len; i++) {
                        var v = chatRoot.dataHistory[i]
                        if(v > max) max = v
                        sum += v
                    }
                    chartRoot.calculatedMax = max > 0 ? max : 100
                    chartRoot.calculatedAvg = Math.round(sum / len)
                }

                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: {
                        var newVal = chartRoot.value
                        var arr = chartRoot.dataHistory
                        if (arr.length > 20) arr.shift()
                        arr.push(newVal)
                        
                        var max = 0
                        var sum = 0
                        for(var i=0; i<arr.length; i++) {
                            if(arr[i] > max) max = arr[i]
                            sum += arr[i]
                        }
                        chartRoot.calculatedMax = max > 0 ? max : (chartRoot.title === "FPS" ? 60 : 100)
                        chartRoot.calculatedAvg = Math.round(sum / arr.length)
                        
                        chartCanvas.requestPaint()
                    }
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: chartRoot.title; color: theme ? theme.textSecondary : "#71717a"; font.bold: true; font.pixelSize: 11 }
                        Item { Layout.fillWidth: true }
                        Text { text: chartRoot.value; color: chartRoot.valueColor; font.bold: true; font.pixelSize: 18 }
                        Text { text: chartRoot.unit; color: theme ? theme.textSecondary : "#71717a"; font.bold: true; font.pixelSize: 10 }
                    }
                    
                    Canvas {
                        id: chartCanvas
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            
                            var data = chartRoot.dataHistory
                            if (data.length < 2) return
                            var maxVal = chartRoot.calculatedMax * 1.2
                            
                            ctx.beginPath()
                            ctx.lineWidth = 1.5
                            ctx.strokeStyle = chartRoot.graphColor
                            ctx.fillStyle = Qt.rgba(chartRoot.graphColor.r, chartRoot.graphColor.g, chartRoot.graphColor.b, 0.2)
                            
                            var stepX = width / (data.length - 1)
                            
                            ctx.moveTo(0, height - (data[0] / maxVal) * height)
                            for (var i = 1; i < data.length; i++) {
                                ctx.lineTo(i * stepX, height - (data[i] / maxVal) * height)
                            }
                            ctx.stroke()
                            
                            ctx.lineTo(width, height)
                            ctx.lineTo(0, height)
                            ctx.closePath()
                            ctx.fill()
                        }
                    }
                    
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: "Avg:"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }
                        Text { text: chartRoot.calculatedAvg; color: theme ? theme.textSecondary : "#d4d4d8"; font.bold: true; font.pixelSize: 10 }
                        Item { Layout.fillWidth: true }
                        Text { text: "Max:"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }
                        Text { text: chartRoot.calculatedMax; color: theme ? theme.textSecondary : "#d4d4d8"; font.bold: true; font.pixelSize: 10 }
                    }
                }
            }
            
            SystemChart {
                title: "FPS"
                unit: "FPS"
                value: backend.currentFps
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: "#0d9488"
                dataHistory: [30, 29, 30, 31, 30, 28, 29, 30, 30, 29, 30, 31, 29, 30, 30]
            }
            
            SystemChart {
                title: "LATENCY"
                unit: "ms"
                value: backend.latency
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: theme ? theme.accent : "#f97316"
                dataHistory: [40, 42, 45, 41, 39, 40, 42, 44, 45, 43, 41, 40, 42, 41]
            }
        }

        GridLayout {
            visible: !root.showCameraControls
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            
            Repeater {
                model: [
                    { label: "Active Cameras", value: backend.activeCameras + "", sub: "of 4 total", icon: "\uD83C\uDFA5", color: theme ? theme.accent : "#f97316" },
                    { label: "Detected Objects", value: "-", sub: "Connection Needed", icon: "\uD83E\uDDE0", color: theme ? theme.textSecondary : "#71717a" },
                    { label: "Storage Used", value: backend.storagePercent + "%", sub: backend.storageUsed + " / " + backend.storageTotal, icon: "\uD83D\uDCBE", color: theme ? theme.textSecondary : "#71717a" },
                    { label: "Network Status", value: "-", sub: "Connection Needed", icon: "\uD83D\uDCE1", color: theme ? theme.textSecondary : "#71717a" }
                ]
                
                delegate: Rectangle {
                    Layout.fillWidth: true
                    height: 80
                    color: theme ? theme.bgComponent : "#18181b"
                    radius: 8
                    border.color: theme ? theme.border : "#27272a"
                    border.width: 1
                    
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4
                        
                        RowLayout {
                            spacing: 6
                            Rectangle {
                                width: 20; height: 20
                                color: theme ? theme.border : "#27272a"
                                radius: 4
                                Text { anchors.centerIn: parent; text: modelData.icon; font.pixelSize: 10 }
                            }
                            Text {
                                text: modelData.label.split(' ')[0]
                                color: theme ? theme.textSecondary : "#71717a"
                                font.bold: true
                                font.pixelSize: 9
                                font.capitalization: Font.AllUppercase
                            }
                        }
                        
                        Text {
                            text: modelData.value
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                            font.pixelSize: 16
                        }
                        
                        Text {
                            text: modelData.sub
                            color: theme ? theme.textSecondary : "#52525b"
                            font.pixelSize: 9
                        }
                    }
                }
            }
        }


        Rectangle {
            visible: !root.showCameraControls
            Layout.fillWidth: true
            height: 50
            color: theme ? theme.bgComponent : "#18181b"
            radius: 8
            border.color: "#4e2c0e"
            border.width: 1
            
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 2
                
                RowLayout {
                    spacing: 6
                    Rectangle {
                        width: 6; height: 6; radius: 3
                        color: theme ? theme.accent : "#f97316"
                    }
                    Text {
                        text: "AI STATUS"
                        color: theme ? theme.accent : "#f97316"
                        font.bold: true
                        font.pixelSize: 9
                    }
                }
                Text {
                    text: "Active on all feeds"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }
            }
        }
    }
}


