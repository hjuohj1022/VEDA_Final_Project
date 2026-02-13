import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property var theme
    property bool isDarkMode: true
    property bool isLoggedIn: false
    signal toggleTheme()
    signal requestLogin()
    signal requestHome()

    color: theme ? theme.bgSecondary : "#09090b"
    
    // 하단 테두리
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: theme ? theme.border : "#27272a"
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16 // px-4
        anchors.rightMargin: 16
        spacing: 16

        // 1. 로고 & 제목 영역
        RowLayout {
            spacing: 8
            
            // 방패 아이콘 상자
            Rectangle {
                width: 32; height: 32
                color: theme ? theme.accent : "#f97316"
                radius: 8 // rounded-lg
                
                Text {
                    anchors.centerIn: parent
                    text: "🛡️" // 방패 아이콘 대체
                    font.pixelSize: 16
                    color: "white"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.requestHome()
                }
            }
            
            Column {
                Layout.alignment: Qt.AlignVCenter
                Text {
                    text: "Vision VMS"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 18 // text-lg
                }
                Text {
                    text: "AI Surveillance"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10 // text-[10px]
                }
            }
        }
        
        // 2. 검색 바
        Rectangle {
            Layout.preferredWidth: 192 // w-48 (192px)
            Layout.preferredHeight: 32
            Layout.leftMargin: 16
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8 // rounded-lg
            
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                
                Text {
                    text: "🔍" // 검색 아이콘
                    font.pixelSize: 14
                    color: theme ? theme.textSecondary : "#71717a"
                }
                
                Text {
                    text: "Search..."
                    color: theme ? theme.textSecondary : "#52525b"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                }
            }
        }
        
        Item { Layout.fillWidth: true } // 스페이서
        
        // 3. 오른쪽 작업
        RowLayout {
            spacing: 8
            
            // 알림 버튼
            Rectangle {
                width: 32; height: 32
                color: "transparent"
                radius: 8
                
                Text {
                    anchors.centerIn: parent
                    text: "🔔"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    font.pixelSize: 16
                }
                
                // 빨간 점
                Rectangle {
                    width: 6; height: 6
                    radius: 3
                    color: theme ? theme.accent : "#f97316"
                    anchors.top: parent.top
                    anchors.right: parent.right
                    anchors.margins: 6
                }
            }
            
            // 설정 버튼
            Rectangle {
                width: 32; height: 32
                color: "transparent"
                radius: 8
                Text {
                    anchors.centerIn: parent
                    text: "⚙️"
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    font.pixelSize: 16
                }
            }
            
            // 테마 전환
            Rectangle {
                width: 32; height: 32
                color: "transparent"
                radius: 8
                Text {
                    anchors.centerIn: parent
                    text: root.isDarkMode ? "☀️" : "🌙"
                    color: theme ? theme.accent : "#f97316"
                    font.pixelSize: 16
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.toggleTheme()
                }
            }
            
            // 관리자 / 로그인 프로필
            Rectangle {
                Layout.preferredHeight: 32
                Layout.preferredWidth: 80
                color: theme ? theme.bgComponent : "#18181b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                radius: 8
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 8
                    
                    Rectangle {
                        width: 24; height: 24
                        radius: 12
                        color: theme ? theme.accent : "#f97316"
                        Text {
                            anchors.centerIn: parent
                            text: root.isLoggedIn ? (backend.userId ? backend.userId.substring(0, 1).toUpperCase() : "U") : "👤"
                            color: "white"
                            font.bold: true
                            font.pixelSize: 12 // text-xs
                        }
                    }
                    
                    Text {
                        text: root.isLoggedIn ? (backend.userId ? backend.userId : "User") : "Login"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 12
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.requestLogin()
                }
            }
        }
    }
}
