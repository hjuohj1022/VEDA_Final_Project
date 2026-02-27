import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme

    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgSecondary : "#09090b"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            Text {
                text: "Playback"
                color: theme ? theme.textPrimary : "white"
                font.bold: true
                font.pixelSize: 20
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "black"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                radius: 8

                Text {
                    anchors.centerIn: parent
                    text: "Use the right sidebar to choose channel/time and start playback"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 14
                }
            }
        }
    }
}
