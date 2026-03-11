import QtQuick

Item {
    id: root
    property var theme
    signal requestReturnLiveView()

    LoginScreen {
        anchors.fill: parent
        theme: root.theme
        onRequestReturnLiveView: {
            root.requestReturnLiveView()
        }
    }
}