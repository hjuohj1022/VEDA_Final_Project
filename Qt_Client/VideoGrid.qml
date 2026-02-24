import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

Item {
    id: root
    property int gridColumns: 2
    property int maximizedIndex: -1
    property var theme
    property bool isActive: true
    property var cameraStates: [false, false, false, false]
    property var cameraFpsValues: [0, 0, 0, 0]

    function recountActiveCameras() {
        var count = 0
        for (var i = 0; i < cameraStates.length; i++) {
            if (cameraStates[i]) count++
        }
        backend.activeCameras = count
    }

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

    function resetCameraStates() {
        cameraStates = [false, false, false, false]
        cameraFpsValues = [0, 0, 0, 0]
        recountActiveCameras()
        recountCurrentFps()
    }

    onIsActiveChanged: {
        if (!isActive) {
            maximizedIndex = -1
            resetCameraStates()
        }
    }

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
                theme: root.theme
                dptzEnabled: root.maximizedIndex === index
                tileIndex: index
                cameraIndex: index
                startDelayMs: 0
                locationName: ["Main Entrance", "Parking Lot A", "Loading Bay", "Reception Area"][index]

                source: (root.isActive && (root.maximizedIndex === -1 || root.maximizedIndex === index))
                        ? backend.buildRtspUrl(index, root.maximizedIndex === -1)
                        : ""

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

                onDoubleClicked: {
                    if (root.maximizedIndex === cameraIndex) {
                        root.maximizedIndex = -1
                    } else {
                        root.maximizedIndex = cameraIndex
                    }
                }
            }
        }
    }
}
