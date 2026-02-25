import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property bool showCameraControls: false
    property int selectedCameraIndex: -1
    property string cameraControlStatus: ""
    property bool cameraControlError: false
    property bool mapModeEnabled: false
    property bool supportZoom: true
    property bool supportFocus: true
    property bool horizontalFlipEnabled: false
    property bool verticalFlipEnabled: false
    signal mainCameraReconnectRequested(int cameraIndex)
    color: theme ? theme.bgSecondary : "#09090b"

    function selectedCameraTitle() {
        var names = ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
        if (selectedCameraIndex < 0 || selectedCameraIndex >= names.length)
            return "Camera"
        return "Cam " + (selectedCameraIndex + 1) + " - " + names[selectedCameraIndex]
    }

    Connections {
        target: backend
        function onCameraControlMessage(message, isError) {
            if (!root.showCameraControls)
                return
            root.cameraControlStatus = message
            root.cameraControlError = isError
            controlStatusTimer.restart()
            if (!isError
                    && root.selectedCameraIndex >= 0
                    && message.indexOf("Flip/Rotate") === 0) {
                // Reconnect only selected main camera after encoder params changed.
                root.mainCameraReconnectRequested(root.selectedCameraIndex)
            }
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

    onShowCameraControlsChanged: {
        if (showCameraControls && selectedCameraIndex >= 0) {
            backend.sunapiLoadSupportedPtzActions(selectedCameraIndex)
        }
    }

    onSelectedCameraIndexChanged: {
        if (showCameraControls && selectedCameraIndex >= 0) {
            supportZoom = true
            supportFocus = true
            backend.sunapiLoadSupportedPtzActions(selectedCameraIndex)
        }
    }
    
    // ?쇱そ ?뚮몢由?
    Rectangle {
        anchors.left: parent.left
        width: 1
        height: parent.height
        color: theme ? theme.border : "#27272a"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8 // p-2
        spacing: 8 // space-y-2

        // ?쒖뒪??硫뷀듃由??쒕ぉ
        Text {
            text: root.showCameraControls ? "Camera Controls" : "System Metrics"
            color: theme ? theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14 // text-sm
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

                Button {
                    text: root.mapModeEnabled ? "3D Map 모드 ON (미구현)" : "3D Map 모드 OFF (미구현)"
                    Layout.fillWidth: true
                    onClicked: root.mapModeEnabled = !root.mapModeEnabled
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
            Layout.preferredHeight: 420

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 6

                Button {
                    text: "줌 +"
                    Layout.fillWidth: true
                    enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                    onClicked: backend.sunapiZoomIn(root.selectedCameraIndex)
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: theme ? theme.border : "#27272a"
                        border.width: 1
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#71717a"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.bold: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        text: "줌 -"
                        Layout.fillWidth: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomOut(root.selectedCameraIndex)
                    }
                    Button {
                        text: "줌 정지"
                        Layout.fillWidth: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportZoom
                        onClicked: backend.sunapiZoomStop(root.selectedCameraIndex)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        text: "포커스 Near"
                        Layout.fillWidth: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusNear(root.selectedCameraIndex)
                    }
                    Button {
                        text: "포커스 Far"
                        Layout.fillWidth: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusFar(root.selectedCameraIndex)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Button {
                        text: "포커스 정지"
                        Layout.fillWidth: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiFocusStop(root.selectedCameraIndex)
                    }
                    Button {
                        text: "오토포커스"
                        Layout.fillWidth: true
                        enabled: root.selectedCameraIndex >= 0 && root.supportFocus
                        onClicked: backend.sunapiSimpleAutoFocus(root.selectedCameraIndex)
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    ComboBox {
                        id: wbModeCombo
                        Layout.fillWidth: true
                        model: ["ATW", "Indoor", "Outdoor", "Manual", "AWC"]
                        enabled: root.selectedCameraIndex >= 0
                    }
                    Button {
                        text: "화이트밸런스 적용"
                        Layout.preferredWidth: 120
                        enabled: root.selectedCameraIndex >= 0
                        onClicked: backend.sunapiSetWhiteBalanceMode(root.selectedCameraIndex, wbModeCombo.currentText)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    CheckBox {
                        id: hFlipCheck
                        text: "좌우 반전"
                        checked: root.horizontalFlipEnabled
                        onToggled: root.horizontalFlipEnabled = checked
                    }

                    CheckBox {
                        id: vFlipCheck
                        text: "상하 반전"
                        checked: root.verticalFlipEnabled
                        onToggled: root.verticalFlipEnabled = checked
                    }
                }

                Button {
                    text: "반전 적용"
                    Layout.fillWidth: true
                    enabled: root.selectedCameraIndex >= 0
                    onClicked: {
                        backend.sunapiSetFlipAndRotate(
                            root.selectedCameraIndex,
                            root.horizontalFlipEnabled,
                            root.verticalFlipEnabled,
                            0
                        )
                    }
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.cameraControlStatus.length > 0
                    text: root.cameraControlStatus
                    color: root.cameraControlError ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                }
            }
        }

        // 李⑦듃 ?곸뿭
        ColumnLayout {
            visible: !root.showCameraControls
            spacing: 8
            
            // ?ъ궗??媛?ν븳 李⑦듃 援ъ꽦 ?붿냼 (?몃씪??
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
                    chartRoot.calculatedMax = max > 0 ? max : 100 // 0 ?ㅼ???諛⑹?
                    chartRoot.calculatedAvg = Math.round(sum / len)
                }

                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: {
                        var newVal = chartRoot.value
                        var arr = chartRoot.dataHistory
                        if (arr.length > 20) arr.shift()
                        arr.push(newVal)
                        
                        // 理쒕?媛?諛??됯퇏媛?怨꾩궛
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
                            
                            var maxVal = chartRoot.calculatedMax * 1.2 // ?쎄컙???щ갚
                            
                            ctx.beginPath()
                            ctx.lineWidth = 1.5
                            ctx.strokeStyle = chartRoot.graphColor
                            ctx.fillStyle = Qt.rgba(chartRoot.graphColor.r, chartRoot.graphColor.g, chartRoot.graphColor.b, 0.2)
                            
                            var stepX = width / (data.length - 1)
                            
                            // ??洹몃━湲?
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
            
            // FPS 李⑦듃 ?몄뒪?댁뒪
            SystemChart {
                title: "FPS"
                unit: "FPS"
                value: backend.currentFps
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: "#0d9488"
                dataHistory: [30, 29, 30, 31, 30, 28, 29, 30, 30, 29, 30, 31, 29, 30, 30]
            }
            
            // 吏???쒓컙 李⑦듃 ?몄뒪?댁뒪
            SystemChart {
                title: "LATENCY"
                unit: "ms"
                value: backend.latency
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: theme ? theme.accent : "#f97316"
                dataHistory: [40, 42, 45, 41, 39, 40, 42, 44, 45, 43, 41, 40, 42, 41]
            }
        }

        // ?듦퀎 洹몃━??
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

        Item { Layout.fillHeight: true } // ?ㅽ럹?댁꽌

            // AI ?곹깭 移대뱶
        Rectangle {
            visible: !root.showCameraControls
            Layout.fillWidth: true
            height: 50
            color: theme ? theme.bgComponent : "#18181b"
            radius: 8
            border.color: "#4e2c0e" // ?곹깭??二쇳솴???댄듃 ?좎?? ?꾨땲硫?theme.accent ?ъ슜? 濡쒖쭅???꾪빐 怨좎젙 ?좎?
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

