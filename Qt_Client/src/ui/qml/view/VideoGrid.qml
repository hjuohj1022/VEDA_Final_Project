import QtQuick
import QtQuick.Layouts
import QtQuick.Controls
import "../playback"

Item {
    id: root
    property int gridColumns: 2
    property var theme
    property var cameraNames: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"]
    property bool isActive: true
    property var cameraStates: [false, false, false, false]
    property var cameraFpsValues: [0, 0, 0, 0]
    signal openMainViewRequested(int cameraIndex)
    // 활성 카메라 수 재계산 함수
    function recountActiveCameras() {
        var count = 0
        for (var i = 0; i < cameraStates.length; i++) {
            if (cameraStates[i]) count++
        }
        backend.activeCameras = count
    }
    // 현재 FPS 재계산 함수
    function recountCurrentFps() {
        var sum = 0
        var count = 0
        for (var i = 0; i < cameraFpsValues.length; i++) {
            if (!cameraStates[i]) continue
            var fps = cameraFpsValues[i]
            if (fps > 0) {
                sum += fps
                count++
            }
        }
        backend.currentFps = count > 0 ? Math.round(sum / count) : 0
    }
    // 카메라 상태 초기화 함수
    function resetCameraStates() {
        cameraStates = [false, false, false, false]
        cameraFpsValues = [0, 0, 0, 0]
        recountActiveCameras()
        recountCurrentFps()
    }
    // 활성 상태 변경 처리 함수
    onIsActiveChanged: {
        if (!isActive) {
            resetCameraStates()
        }
    }

    GridLayout {
        anchors.fill: parent
        anchors.margins: 10
        columns: root.gridColumns
        rowSpacing: 10
        columnSpacing: 10

        Repeater {
            model: 4
            delegate: VideoPlayer {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: true
                theme: root.theme
                dptzEnabled: false
                tileIndex: index
                cameraIndex: index
                startDelayMs: 0
                locationName: (root.cameraNames && index >= 0 && index < root.cameraNames.length)
                              ? root.cameraNames[index]
                              : ("Cam " + (index + 1))

                source: root.isActive
                        ? ((backend.rtspIp, backend.rtspPort),
                           backend.buildRtspUrl(index, true))
                        : ""
                // 카메라 상태 변경 처리 함수
                onCameraStateChanged: function(cameraIndex, isLive) {
                    if (cameraIndex < 0 || cameraIndex >= root.cameraStates.length) {
                        return
                    }
                    if (root.cameraStates[cameraIndex] === isLive) {
                        return
                    }
                    root.cameraStates[cameraIndex] = isLive
                    if (!isLive) {
                        root.cameraFpsValues[cameraIndex] = 0
                    }
                    root.recountActiveCameras()
                    root.recountCurrentFps()
                }
                // 카메라 FPS 변경 처리 함수
                onCameraFpsChanged: function(cameraIndex, fps) {
                    if (cameraIndex < 0 || cameraIndex >= root.cameraFpsValues.length) {
                        return
                    }
                    if (root.cameraFpsValues[cameraIndex] === fps) {
                        return
                    }
                    root.cameraFpsValues[cameraIndex] = fps
                    root.recountCurrentFps()
                }
                // 더블 클릭 처리 함수
                onDoubleClicked: {
                    root.openMainViewRequested(cameraIndex)
                }
            }
        }
    }
}
