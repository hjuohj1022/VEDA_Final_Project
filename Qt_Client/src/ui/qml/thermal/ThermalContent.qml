import QtQuick
import "../common"

Item {
    id: root
    property var theme
    signal requestReturnLiveView()

    LoginScreen {
        anchors.fill: parent
        theme: root.theme
        // 실시간 화면 복귀 요청 처리 함수
        onRequestReturnLiveView: {
            root.requestReturnLiveView()
        }
    }
}
