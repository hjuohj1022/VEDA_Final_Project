import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    color: theme ? theme.bgSecondary : "#09090b"
    
    // 왼쪽 테두리
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

        // 시스템 메트릭 제목
        Text {
            text: "System Metrics"
            color: theme ? theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14 // text-sm
        }

        // 차트 영역
        ColumnLayout {
            spacing: 8
            
            // 재사용 가능한 차트 구성 요소 (인라인)
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
                    chartRoot.calculatedMax = max > 0 ? max : 100 // 0 스케일 방지
                    chartRoot.calculatedAvg = Math.round(sum / len)
                }

                Timer {
                    interval: 1000; running: true; repeat: true
                    onTriggered: {
                        var newVal = chartRoot.value
                        var arr = chartRoot.dataHistory
                        if (arr.length > 20) arr.shift()
                        arr.push(newVal)
                        
                        // 최대값 및 평균값 계산
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
                            
                            var maxVal = chartRoot.calculatedMax * 1.2 // 약간의 여백
                            
                            ctx.beginPath()
                            ctx.lineWidth = 1.5
                            ctx.strokeStyle = chartRoot.graphColor
                            ctx.fillStyle = Qt.rgba(chartRoot.graphColor.r, chartRoot.graphColor.g, chartRoot.graphColor.b, 0.2)
                            
                            var stepX = width / (data.length - 1)
                            
                            // 선 그리기
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
            
            // FPS 차트 인스턴스
            SystemChart {
                title: "FPS"
                unit: "FPS"
                value: backend.currentFps
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: "#0d9488"
                dataHistory: [30, 29, 30, 31, 30, 28, 29, 30, 30, 29, 30, 31, 29, 30, 30]
            }
            
            // 지연 시간 차트 인스턴스
            SystemChart {
                title: "LATENCY"
                unit: "ms"
                value: backend.latency
                valueColor: theme ? theme.textPrimary : "white"
                graphColor: theme ? theme.accent : "#f97316"
                dataHistory: [40, 42, 45, 41, 39, 40, 42, 44, 45, 43, 41, 40, 42, 41]
            }
        }

        // 통계 그리드
        GridLayout {
            columns: 2
            columnSpacing: 8
            rowSpacing: 8
            
            Repeater {
                model: [
                    { label: "Active Cameras", value: "4", sub: "of 4 total", icon: "📊", color: theme ? theme.accent : "#f97316" },
                    { label: "Detected Objects", value: "-", sub: "Connection Needed", icon: "👥", color: theme ? theme.textSecondary : "#71717a" },
                    { label: "Storage Used", value: backend.storagePercent + "%", sub: backend.storageUsed + " / " + backend.storageTotal, icon: "💾", color: theme ? theme.textSecondary : "#71717a" },
                    { label: "Network Status", value: "-", sub: "Connection Needed", icon: "📶", color: theme ? theme.textSecondary : "#71717a" }
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

        Item { Layout.fillHeight: true } // 스페이서

            // AI 상태 카드
        Rectangle {
            Layout.fillWidth: true
            height: 50
            color: theme ? theme.bgComponent : "#18181b"
            radius: 8
            border.color: "#4e2c0e" // 상태에 주황색 틴트 유지? 아니면 theme.accent 사용? 로직을 위해 고정 유지
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
