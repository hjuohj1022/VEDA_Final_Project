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
        // 메인 뷰 열기 요청 처리 함수
        onOpenMainViewRequested: function(cameraIndex) {
            root.openMainViewRequested(cameraIndex)
        }
    }
}