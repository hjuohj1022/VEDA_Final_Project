import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root
    property int gridColumns: 2 // 기본 2x2
    property int maximizedIndex: -1 // -1은 없음 의미
    property var theme // Main에서 주입됨
    property bool isActive: true // 활성화 여부 (Main에서 제어)

    GridLayout {
        anchors.fill: parent
        anchors.margins: 10
        columns: root.maximizedIndex !== -1 ? 1 : root.gridColumns
        rowSpacing: 10
        columnSpacing: 10

        Repeater {
            model: 4
            delegate: VideoPlayer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.maximizedIndex === -1 || root.maximizedIndex === index
                theme: root.theme // 테마 전달
                
                required property int index
                
                locationName: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"][index]
                
                // RTSP URL 동적 생성 (비활성 시 빈 문자열로 연결 해제)
                source: root.isActive ? "rtsp://" + backend.rtspIp + ":" + backend.rtspPort + "/" + index : ""
                
                onDoubleClicked: {
                    if (root.maximizedIndex === index) {
                        root.maximizedIndex = -1 // 복구
                    } else {
                        root.maximizedIndex = index // 최대화
                    }
                }
                
                Component.onCompleted: {
                    console.log("Video source set to:", source)
                }
            }
        }
    }
}
