import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var store

    visible: store ? (!store.showCameraControls && !store.showPlaybackControls) : false
    Layout.preferredHeight: visible ? 440 : 0
    Layout.maximumHeight: visible ? 440 : 0
    Layout.minimumHeight: visible ? 440 : 0

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

        GridLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 168
            columns: 2
            columnSpacing: 8
            rowSpacing: 8

            Repeater {
                model: [
                    { label: "Active Cameras", value: backend.activeCameras + "", sub: "of 4 total", icon: "\uD83C\uDFA5" },
                    { label: "Detected Objects", value: "-", sub: "Connection Needed", icon: "\uD83E\uDDE0" },
                    { label: "Storage Used", value: backend.storagePercent + "%", sub: backend.storageUsed + " / " + backend.storageTotal, icon: "\uD83D\uDCBE" },
                    { label: "Network Status", value: "-", sub: "Connection Needed", icon: "\uD83D\uDCE1" }
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
                                width: 20
                                height: 20
                                color: theme ? theme.border : "#27272a"
                                radius: 4
                                Text { anchors.centerIn: parent; text: modelData.icon; font.pixelSize: 10 }
                            }
                            Text {
                                text: modelData.label.split(" ")[0]
                                color: theme ? theme.textSecondary : "#71717a"
                                font.bold: true
                                font.pixelSize: 9
                                font.capitalization: Font.AllUppercase
                            }
                        }

                        Text { text: modelData.value; color: theme ? theme.textPrimary : "white"; font.bold: true; font.pixelSize: 16 }
                        Text { text: modelData.sub; color: theme ? theme.textSecondary : "#52525b"; font.pixelSize: 9 }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: theme ? theme.bgComponent : "#18181b"
            radius: 8
            border.color: "#4e2c0e"
            border.width: 1

            ColumnLayout {
                anchors.centerIn: parent
                spacing: 2

                RowLayout {
                    spacing: 6
                    Rectangle { width: 6; height: 6; radius: 3; color: theme ? theme.accent : "#f97316" }
                    Text { text: "AI STATUS"; color: theme ? theme.accent : "#f97316"; font.bold: true; font.pixelSize: 9 }
                }
                Text { text: "Active on all feeds"; color: theme ? theme.textSecondary : "#71717a"; font.pixelSize: 10 }
            }
        }
    }
}
