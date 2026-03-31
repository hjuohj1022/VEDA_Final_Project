import QtQuick
import QtQuick.Layouts
import "../playback"

Item {
    id: root
    property var theme
    property bool isLoggedIn: false
    property bool mapModeEnabled: false
    property int cameraIndex: -1
    property string locationName: "Camera"
    signal requestClose()

    Rectangle {
        anchors.fill: parent
        color: root.theme.bgSecondary

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                VideoPlayer {
                    id: inlineMainPlayer
                    anchors.fill: parent
                    theme: root.theme
                    tileIndex: root.cameraIndex
                    titleText: root.cameraIndex >= 0
                               ? ("Cam " + (root.cameraIndex + 1) + " - Main Stream")
                               : "Main Stream"
                    cameraIndex: root.cameraIndex
                    dptzEnabled: true
                    locationName: root.locationName
                    startDelayMs: 0
                    source: (root.isLoggedIn && root.cameraIndex >= 0)
                            ? ((backend.rtspIp, backend.rtspPort),
                               backend.buildRtspUrl(root.cameraIndex, false))
                            : ""
                    // 더블 클릭 처리 함수
                    onDoubleClicked: {
                        root.requestClose()
                    }
                }

                Item {
                    anchors.fill: parent
                    z: 5
                    visible: root.mapModeEnabled

                    Image {
                        id: cctv3dMapImage
                        anchors.fill: parent
                        source: root.mapModeEnabled ? backend.cctv3dMapFrameDataUrl : ""
                        fillMode: Image.PreserveAspectFit
                        cache: false
                        smooth: true
                        mipmap: false
                        visible: source.length > 0
                    }

                    Rectangle {
                        anchors.centerIn: parent
                        visible: root.mapModeEnabled && cctv3dMapImage.source.length === 0
                        color: "#66000000"
                        radius: 8
                        width: 220
                        height: 36

                        Text {
                            anchors.centerIn: parent
                            text: "3D Map 스트림 수신 대기 중..."
                            color: "white"
                            font.pixelSize: 12
                        }
                    }
                }
            }
        }
    }
}
