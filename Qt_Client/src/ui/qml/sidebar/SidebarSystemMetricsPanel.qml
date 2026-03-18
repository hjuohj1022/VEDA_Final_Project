import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var store
    property var clientInfo: ({})
    property bool clientInfoReady: false
    property int lastCpuPercent: -1

    visible: store ? (!store.showCameraControls && !store.showPlaybackControls && !store.showThermalControls) : false
    Layout.fillHeight: visible
    Layout.preferredHeight: visible ? 1 : 0
    Layout.minimumHeight: visible ? 1 : 0

    function refreshClientInfo() {
        var info = backend.getClientSystemInfo()
        if (!info) {
            return
        }
        clientInfo = info
        clientInfoReady = true
        var cpu = Number(info.cpuUsagePercent)
        if (!isNaN(cpu) && cpu >= 0) {
            lastCpuPercent = Math.round(cpu)
        }
    }

    function clientMainValue() {
        if (!clientInfoReady && lastCpuPercent < 0) {
            return "Initializing"
        }
        if (lastCpuPercent < 0) {
            return "Initializing"
        }
        return "CPU " + lastCpuPercent + "%"
    }

    function parseGiB(text) {
        if (!text || typeof text !== "string") {
            return NaN
        }
        var m = text.match(/([0-9]+(?:\.[0-9]+)?)/)
        if (!m || m.length < 2) {
            return NaN
        }
        return Number(m[1])
    }

    function clientSubText() {
        if (!clientInfoReady) {
            return "Initializing"
        }
        var totalGiB = parseGiB(clientInfo.ramTotal)
        var availGiB = parseGiB(clientInfo.ramAvailable)
        var ramText = "RAM Unknown"
        if (!isNaN(totalGiB) && totalGiB > 0 && !isNaN(availGiB)) {
            var usedPercent = ((totalGiB - availGiB) / totalGiB) * 100.0
            if (usedPercent < 0) {
                usedPercent = 0
            }
            if (usedPercent > 100) {
                usedPercent = 100
            }
            ramText = "RAM " + Math.round(usedPercent) + "%"
        }
        var gpu = (clientInfo.gpuModel && clientInfo.gpuModel.length > 0) ? clientInfo.gpuModel : "GPU Unknown"
        return ramText + " · " + gpu
    }

    // 상태 텍스트별 카드 색상 계산
    function statusColor(status) {
        if (status === "GOOD") {
            return "#22c55e"
        }
        if (status === "DOWN") {
            return "#ef4444"
        }
        if (status === "DEGRADED") {
            return "#f97316"
        }
        return (theme ? theme.textSecondary : "#a1a1aa")
    }

    // API 상태 판정
    function apiStatusText() {
        if (backend.isLoggedIn) {
            return "GOOD"
        }
        if (backend.storageTotal && backend.storageTotal !== "0 GB") {
            return "GOOD"
        }
        return "DOWN"
    }

    // RTSP 상태 판정
    function rtspStatusText() {
        if (backend.activeCameras <= 0) {
            return "DOWN"
        }
        if (backend.currentFps >= 5) {
            return "GOOD"
        }
        if (backend.currentFps > 0) {
            return "DEGRADED"
        }
        return "DEGRADED"
    }

    // MQTT 상태 판정
    function mqttStatusText() {
        var status = (backend.networkStatus || "").toLowerCase().trim()
        if (status.length === 0) {
            return "DEGRADED"
        }
        if (status.indexOf("connected") >= 0 || status.indexOf("good") >= 0 || status.indexOf("ok") >= 0 || status.indexOf("up") >= 0) {
            return "GOOD"
        }
        if (status.indexOf("error") >= 0 || status.indexOf("disconnect") >= 0 || status.indexOf("down") >= 0 || status.indexOf("tls config error") >= 0) {
            return "DOWN"
        }
        return "DEGRADED"
    }

    onVisibleChanged: {
        if (visible) {
            refreshClientInfo()
        }
    }

    Component.onCompleted: refreshClientInfo()

    Timer {
        interval: 2000
        repeat: true
        running: root.visible
        onTriggered: root.refreshClientInfo()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 208
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

                Timer {
                    interval: 1000
                    running: true
                    repeat: true
                    onTriggered: {
                        var arr = chartRoot.dataHistory
                        if (arr.length > 20) arr.shift()
                        arr.push(chartRoot.value)

                        var max = 0
                        var sum = 0
                        for (var i = 0; i < arr.length; i++) {
                            if (arr[i] > max) max = arr[i]
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
                dataHistory: []
            }

            SystemChart {
                title: "LATENCY"
                unit: "ms"
                value: backend.latency
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: theme ? theme.accent : "#f97316"
                dataHistory: []
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8

            Repeater {
                model: [
                    { label: "ACTIVE", value: backend.activeCameras + "", sub: "of 4 total", icon: "\uD83C\uDFA5" },
                    {
                        label: "STORAGE",
                        value: (backend.storageTotal && backend.storageTotal !== "0 GB") ? (backend.storagePercent + "%") : "Initializing",
                        sub: (backend.storageTotal && backend.storageTotal !== "0 GB") ? (backend.storageUsed + " / " + backend.storageTotal) : "Unknown",
                        icon: "\uD83D\uDCBE"
                    },
                    { label: "CLIENT", value: root.clientMainValue(), sub: root.clientSubText(), icon: "\uD83D\uDCBB" },
                    {
                        label: "SERVER",
                        icon: "\uD83D\uDDA5",
                        parts: [
                            { name: "API", status: root.apiStatusText() },
                            { name: "RTSP", status: root.rtspStatusText() },
                            { name: "MQTT", status: root.mqttStatusText() }
                        ]
                    }
                ]

                delegate: Rectangle {
                    property var cardData: modelData
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
                                width: 20
                                height: 20
                                color: theme ? theme.border : "#27272a"
                                radius: 4
                                Text { anchors.centerIn: parent; text: cardData.icon; font.pixelSize: 10 }
                            }
                            Text {
                                text: cardData.label
                                color: theme ? theme.textSecondary : "#71717a"
                                font.bold: true
                                font.pixelSize: 9
                                font.capitalization: Font.AllUppercase
                            }
                        }

                        RowLayout {
                            visible: cardData.label === "SERVER"
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            spacing: 4

                            Repeater {
                                model: cardData.parts ? cardData.parts : []
                                delegate: ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Text {
                                        text: modelData.status
                                        color: root.statusColor(modelData.status)
                                        font.bold: true
                                        font.pixelSize: 15
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: modelData.name
                                        color: theme ? theme.textSecondary : "#52525b"
                                        font.pixelSize: 9
                                        horizontalAlignment: Text.AlignHCenter
                                        Layout.fillWidth: true
                                    }
                                }
                            }
                        }

                        ColumnLayout {
                            visible: cardData.label !== "SERVER"
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: (cardData && cardData.value !== undefined) ? String(cardData.value) : ""
                                color: cardData.valueColor ? cardData.valueColor : (theme ? theme.textPrimary : "white")
                                font.bold: true
                                font.pixelSize: 16
                            }
                            Text {
                                text: (cardData && cardData.sub !== undefined) ? String(cardData.sub) : ""
                                color: theme ? theme.textSecondary : "#52525b"
                                font.pixelSize: 9
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }
}
