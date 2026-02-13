import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property bool isDarkMode: true
    property bool isLoggedIn: false
    property int sessionRemainingSeconds: 0
    signal toggleTheme()
    signal requestLogin()
    signal requestLogout()
    signal requestHome()
    signal requestRtspSettings()

    function formatSession(seconds) {
        var s = Math.max(0, seconds)
        var m = Math.floor(s / 60)
        var r = s % 60
        return (m < 10 ? "0" + m : "" + m) + ":" + (r < 10 ? "0" + r : "" + r)
    }

    color: theme ? theme.bgSecondary : "#09090b"

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme ? theme.border : "#27272a"
        opacity: 0.9
    }

    component IconButton: Rectangle {
        id: iconBtn
        property string label: ""
        property color fg: theme ? theme.textSecondary : "#a1a1aa"
        signal clicked()

        width: 32
        height: 32
        radius: 9
        color: mouse.pressed
               ? (theme ? theme.border : "#27272a")
               : (mouse.containsMouse ? (theme ? theme.bgComponent : "#18181b") : "transparent")
        border.color: mouse.containsMouse ? (theme ? theme.border : "#27272a") : "transparent"
        border.width: 1
        scale: mouse.pressed ? 0.96 : 1.0
        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

        Text {
            anchors.centerIn: parent
            text: iconBtn.label
            color: iconBtn.fg
            font.family: "Segoe MDL2 Assets"
            font.pixelSize: 13
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                backend.resetSessionTimer()
                iconBtn.clicked()
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 14

        RowLayout {
            spacing: 10

            Rectangle {
                id: homeBtn
                width: 34
                height: 34
                radius: 10
                color: homeMouse.pressed ? "#ea580c" : (theme ? theme.accent : "#f97316")
                scale: homeMouse.pressed ? 0.96 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    radius: 5
                    color: "white"
                }

                MouseArea {
                    id: homeMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        backend.resetSessionTimer()
                        root.requestHome()
                    }
                }
            }

            Column {
                Layout.alignment: Qt.AlignVCenter
                spacing: 1

                Text {
                    text: "Vision VMS"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 32/2
                }

                Text {
                    text: "AI Surveillance"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: 250
            Layout.preferredHeight: 34
            Layout.leftMargin: 8
            radius: 9
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8

                Text {
                    text: "O"
                    color: theme ? theme.accent : "#f97316"
                    font.bold: true
                    font.pixelSize: 12
                }

                Text {
                    text: "Search..."
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
        }

        Item { Layout.fillWidth: true }

        RowLayout {
            spacing: 6

            IconButton {
                label: "\uE7F4"
                fg: theme ? theme.textSecondary : "#a1a1aa"
            }

            Rectangle {
                width: 6
                height: 6
                radius: 3
                color: theme ? theme.accent : "#f97316"
                anchors.verticalCenter: parent.verticalCenter
                Layout.leftMargin: -10
                Layout.rightMargin: 6
            }

            IconButton {
                label: "\uE713"
                fg: theme ? theme.textSecondary : "#a1a1aa"
                onClicked: root.requestRtspSettings()
            }

            IconButton {
                label: root.isDarkMode ? "\uE708" : "\uE706"
                fg: theme ? theme.accent : "#f97316"
                onClicked: root.toggleTheme()
            }

            Rectangle {
                Layout.preferredHeight: 34
                Layout.preferredWidth: 96
                visible: root.isLoggedIn
                radius: 10
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: formatSession(root.sessionRemainingSeconds)
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 12
                }
            }

            Rectangle {
                id: authBtn
                Layout.preferredHeight: 34
                Layout.preferredWidth: 92
                radius: 10
                color: authMouse.pressed
                       ? (theme ? theme.border : "#27272a")
                       : (theme ? theme.bgComponent : "#18181b")
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                scale: authMouse.pressed ? 0.97 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 8

                    Rectangle {
                        width: 24
                        height: 24
                        radius: 12
                        color: theme ? theme.accent : "#f97316"

                        Text {
                            anchors.centerIn: parent
                            text: "\uE77B"
                            color: "white"
                            font.family: "Segoe MDL2 Assets"
                            font.pixelSize: 10
                        }
                    }

                    Text {
                        text: root.isLoggedIn ? "Logout" : "Login"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                MouseArea {
                    id: authMouse
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        backend.resetSessionTimer()
                        if (root.isLoggedIn) {
                            root.requestLogout()
                        } else {
                            root.requestLogin()
                        }
                    }
                }
            }
        }
    }
}
