import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    modal: true
    focus: true
    closePolicy: Popup.NoAutoClose
    width: 900
    height: 680
    padding: 0

    property string frameSource: ""
    property string infoText: "열화상 대기 중"
    signal closeRequested()

    background: Rectangle {
        color: "#111111"
        radius: 8
        border.color: "#2a2a2a"
        border.width: 1
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "열화상 스트림"
                font.pixelSize: 20
                color: "#f3f3f3"
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "닫기"
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#000000"
            border.color: "#343434"
            border.width: 1

            Image {
                anchors.fill: parent
                fillMode: Image.PreserveAspectFit
                retainWhileLoading: true
                smooth: false
                cache: false
                source: root.frameSource
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.infoText
            color: "#d0d0d0"
            font.pixelSize: 14
        }
    }
}
