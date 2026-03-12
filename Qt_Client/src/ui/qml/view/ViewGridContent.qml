import QtQuick

Item {
    id: root
    property var theme
    property var cameraNames
    property bool isActive: true
    signal openMainViewRequested(int cameraIndex)

    VideoGrid {
        anchors.fill: parent
        theme: root.theme
        cameraNames: root.cameraNames
        isActive: root.isActive
        onOpenMainViewRequested: function(cameraIndex) {
            root.openMainViewRequested(cameraIndex)
        }
    }
}