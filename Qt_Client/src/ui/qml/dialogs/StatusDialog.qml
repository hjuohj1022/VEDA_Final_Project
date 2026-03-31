import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property string text: ""

    modal: true
    parent: Overlay.overlay
    width: 360
    closePolicy: Popup.NoAutoClose
    x: Math.round((((parent ? parent.width : 0) - width) / 2))
    y: Math.round((((parent ? parent.height : 0) - implicitHeight) / 2))

    header: Rectangle {
        implicitHeight: 44
        color: theme ? theme.bgSecondary : "#0f172a"
        border.color: theme ? theme.border : "#27272a"

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 14
            text: root.title
            color: theme ? theme.textPrimary : "white"
            font.bold: true
            font.pixelSize: 14
        }
    }

    contentItem: Rectangle {
        implicitHeight: 86
        color: "transparent"

        Text {
            anchors.fill: parent
            anchors.margins: 14
            text: root.text
            color: theme ? theme.textPrimary : "white"
            wrapMode: Text.WordWrap
            verticalAlignment: Text.AlignVCenter
        }
    }

    footer: Rectangle {
        implicitHeight: 58
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12

            Item { Layout.fillWidth: true }

            Button {
                text: "확인"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 34
                scale: down ? 0.97 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                // 클릭 이벤트 처리 함수
                onClicked: root.close()
                background: Rectangle {
                    color: parent.down ? (theme ? theme.border : "#374151") : (theme ? theme.bgSecondary : "#1f2937")
                    border.color: theme ? theme.border : "#374151"
                    radius: 6
                }
                contentItem: Text {
                    text: parent.text
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    background: Rectangle {
        color: theme ? theme.bgComponent : "#18181b"
        border.color: theme ? theme.border : "#27272a"
        radius: 8
    }
}
