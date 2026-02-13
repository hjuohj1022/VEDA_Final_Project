import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import Team3VideoReceiver 1.0

Item {
    id: root
    property string source: ""
    // property alias status: statusLabel.text // 별칭 제거됨, 이제 내부 로직 사용
    property string locationName: "Camera"
    property string bitrateText: (vlc.bitrate / 1000).toFixed(0) + " kbps"
    property var theme
    
    // 컨테이너 스타일 (테두리, 둥근 모서리)
    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgSecondary : "#09090b"
        radius: 8
        border.color: mouseArea.containsMouse ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
        border.width: 2
        clip: true 
        
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 2 // 테두리 제외
            spacing: 0

            // 1. 헤더 바
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 36 // 컴팩트 헤더
                color: "transparent" // 부모 배경 사용
                
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    
                    // 카메라 아이콘 및 정보
                    RowLayout {
                        spacing: 6
                        Text { text: "📷"; font.pixelSize: 12 }
                        Column {
                            spacing: 0
                            Text { text: "Cam " + (index + 1); color: theme ? theme.textPrimary : "white"; font.bold: true; font.pixelSize: 12 }
                            Text { text: root.locationName; color: theme ? theme.textSecondary : "#a1a1aa"; font.pixelSize: 10 }
                        }
                    }

                    Item { Layout.fillWidth: true }
                    
                    // 상태 배지
                    Rectangle {
                        Layout.preferredHeight: 18
                        Layout.preferredWidth: 46
                        color: statusLabel.text === "LIVE" ? (theme ? theme.accent + "33" : "#33f97316") : "#3371717a" 
                        radius: 4
                        border.width: 1
                        border.color: statusLabel.text === "LIVE" ? (theme ? theme.accent : "#f97316") : "#71717a"
                        
                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 4
                            Rectangle {
                                width: 6; height: 6; radius: 3
                                color: statusLabel.text === "LIVE" ? (theme ? theme.accent : "#f97316") : "#71717a"
                                visible: statusLabel.text === "LIVE"
                            }
                            Text {
                                id: statusLabel
                                text: vlc.isPlaying ? "LIVE" : "OFFLINE"
                                color: text === "LIVE" ? (theme ? theme.accent : "#f97316") : "#71717a"
                                font.bold: true
                                font.pixelSize: 9
                            }
                        }
                    }
                }
                
                // 구분선
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }
            }

            // 2. 비디오 영역 (네이티브 윈도우 자리 표시자)
            // 이 아이템은 중간 공간을 채웁니다. 네이티브 윈도우가 위치를 추적합니다.
            VlcPlayer {
                id: vlc
                Layout.fillWidth: true
                Layout.fillHeight: true
                // url: root.source // 바인딩 제거 (순서 보장을 위해 onSourceChanged에서 수동 설정)
                
                // 비디오 로딩 중이거나 오프라인일 때의 배경
                Rectangle {
                    anchors.fill: parent
                    color: "black"
                    z: -1
                    
                    Text {
                        anchors.centerIn: parent
                        text: "NO SIGNAL"
                        color: "#3f3f46"
                        visible: !vlc.isPlaying
                        font.bold: true
                    }
                }
            }
// ...


            // 3. 바닥글 바
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                color: "transparent"
                
                // 상단 구분선
                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: 1
                    color: theme ? theme.border : "#27272a"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 8
                    
                    RowLayout {
                        spacing: 4
                        Rectangle { width: 6; height: 6; radius: 3; color: theme ? theme.accent : "#f97316"; visible: true }
                        Text { text: "REC"; color: theme ? theme.accent : "#f97316"; font.bold: true; font.pixelSize: 10 }
                    }
                    
                    Text { text: "1080P • 30FPS"; color: theme ? theme.textSecondary : "#a1a1aa"; font.pixelSize: 10 }
                    
                    // 사용자 요청에 따라 비트레이트 제거됨
                    
                    Item { Layout.fillWidth: true }
                    
                    Text {
                        id: timeLabel
                        text: Qt.formatTime(new Date(), "hh:mm:ss")
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.family: "Courier New"
                        font.pixelSize: 10
                    }

                    Timer {
                        interval: 1000
                        running: true
                        repeat: true
                        onTriggered: timeLabel.text = Qt.formatTime(new Date(), "hh:mm:ss")
                    }
                }
            }
        }
        
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.forceActiveFocus()
            onDoubleClicked: root.doubleClicked()
            // 참고: 네이티브 윈도우가 비디오 영역의 클릭을 가로챌 수 있음
        }
    }

    signal doubleClicked()

    Component.onCompleted: {
        if (root.source !== "") vlc.play()
    }
    onSourceChanged: {
        console.log(root.source === "" ? "VideoPlayer source reset" : "VideoPlayer source changed: " + root.source)
        vlc.url = root.source

        if (root.source !== "") {
            console.log("Source valid, calling play()")
            vlc.play()
        } else {
            console.log("Source empty, calling stop()")
            vlc.stop()
        }
    }
}
