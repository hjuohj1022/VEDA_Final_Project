import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property bool isLoggedIn: false
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

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                VideoPlayer {
                    id: inlineMainPlayer
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
                    onDoubleClicked: {
                        root.requestClose()
                    }
                }
            }
        }
    }
}
