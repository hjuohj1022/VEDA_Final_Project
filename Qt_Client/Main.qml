import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1280
    height: 720
    minimumWidth: 1280
    minimumHeight: 720
    maximumWidth: 1280
    maximumHeight: 720
    visible: true
    property bool isDarkMode: true
    property alias appTheme: theme

    QtObject {
        id: theme
        property color bgPrimary: window.isDarkMode ? "#000000" : "#f4f4f5" // 검정 : zinc-100
        property color bgSecondary: window.isDarkMode ? "#09090b" : "#ffffff" // zinc-950 : 흰색
        property color bgComponent: window.isDarkMode ? "#18181b" : "#ffffff" // zinc-900 : 흰색
        property color border: window.isDarkMode ? "#27272a" : "#e4e4e7" // zinc-800 : zinc-200
        property color textPrimary: window.isDarkMode ? "#ffffff" : "#09090b" // 흰색 : zinc-950
        property color textSecondary: window.isDarkMode ? "#a1a1aa" : "#71717a" // zinc-400 : zinc-500
        property color accent: "#f97316" // orange-500
    }

    title: "Vision VMS"
    color: theme.bgPrimary // bg-black
    
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // 1. 헤더 (상단)
        Header {
            Layout.fillWidth: true
            Layout.preferredHeight: 56 // h-14 (56px)
            z: 10
            theme: window.appTheme
            isDarkMode: window.isDarkMode
            isLoggedIn: backend.isLoggedIn
            onToggleTheme: window.isDarkMode = !window.isDarkMode
            onRequestLogin: stackLayout.currentIndex = 1
            onRequestHome: stackLayout.currentIndex = 0
        }

        // 2. 메인 콘텐츠 영역 (헤더 아래)
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 메인 뷰 (현재 왼쪽, VideoGrid 포함)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                
                // 콘텐츠 패딩 래퍼
                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    
                    // 내부 컨테이너 (bg-zinc-950)
                    Rectangle {
                        anchors.fill: parent
                        color: theme.bgSecondary // bg-zinc-950
                        
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8 // p-2
                            spacing: 8 // gap-2

                            // 제목 표시줄 (라이브 모니터링)
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                
                                Text {
                                    text: "Live Monitoring"
                                    color: theme.textPrimary // text-white
                                    font.pixelSize: 18 // text-lg
                                    font.bold: true
                                    Layout.alignment: Qt.AlignVCenter
                                }
                                
                                Item { Layout.fillWidth: true }
                                
                                // 버튼
                                RowLayout {
                                    spacing: 8
                                    
                                    // 그리드 버튼 (라이브 모드: 0)
                                    Rectangle {
                                        width: 50; height: 28
                                        color: stackLayout.currentIndex === 0 ? theme.accent : theme.bgComponent
                                        border.color: theme.border
                                        border.width: stackLayout.currentIndex === 0 ? 0 : 1
                                        radius: 6
                                        
                                        Text { 
                                            anchors.centerIn: parent
                                            text: "Grid"
                                            color: stackLayout.currentIndex === 0 ? "white" : theme.textSecondary
                                            font.pixelSize: 12
                                            font.bold: true
                                        }
                                        
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: stackLayout.currentIndex = 0
                                        }
                                    }

                                    // 내보내기/파일목록 버튼 (녹화 모드: 1)
                                    Rectangle {
                                        width: 60; height: 28
                                        color: stackLayout.currentIndex === 1 ? theme.accent : theme.bgComponent
                                        radius: 6
                                        border.color: theme.border
                                        border.width: stackLayout.currentIndex === 1 ? 0 : 1
                                        
                                        Text {
                                            anchors.centerIn: parent
                                            text: "Export"
                                            color: stackLayout.currentIndex === 1 ? "white" : theme.textSecondary
                                            font.bold: true
                                            font.pixelSize: 12
                                        }

                                        MouseArea {
                                            id: exportMouseArea
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (backend.isLoggedIn) {
                                                    stackLayout.currentIndex = 1
                                                } else {
                                                    // 로그인 필요 시 로그인 화면으로 (현재 LoginScreen이 index 1이므로 이동하되, 로그인 안되어 있으면 로그인 폼이 뜸)
                                                    stackLayout.currentIndex = 1
                                                }
                                            }
                                        }
                                    }
                                }
                            }

                            // 콘텐츠 페이지 (VideoGrid / 녹화)
                            StackLayout {
                                id: stackLayout
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                currentIndex: 0 

                                VideoGrid {
                                    id: videoGrid
                                    theme: window.appTheme // 테마 주입
                                    isActive: stackLayout.currentIndex === 0 // 라이브 탭일 때만 활성화
                                }

                                LoginScreen {
                                    id: loginScreen
                                    theme: window.appTheme // 테마 주입
                                }
                            }
                        }
                    }
                }
            }

            // 사이드바 (오른쪽)
            Sidebar {
                Layout.preferredWidth: 256 // w-64 (256px)
                Layout.fillHeight: true
                theme: window.appTheme // 테마 주입
            }
        }
    }
}
// Force Rebuild 02

