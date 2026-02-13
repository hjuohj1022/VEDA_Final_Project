import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Dialogs

Item {
    // 강제 재빌드 2026-02-11
    id: root
    property var theme
    
    // 로그인 상태
    property bool isLoggedIn: backend.isLoggedIn
    
    // 로그인 화면
    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgSecondary : "#09090b"
        visible: !root.isLoggedIn
        z: 10
        
        ColumnLayout {
            anchors.centerIn: parent
            width: 320
            spacing: 24
            
            // 로고 / 헤더
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 48; height: 48
                    color: theme ? theme.accent : "#f97316"
                    radius: 12
                    Text { anchors.centerIn: parent; text: "🛡️"; font.pixelSize: 24 }
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Welcome Back"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 24
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Sign in to access surveillance system"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 14
                }
            }
            
            // 양식
            ColumnLayout {
                spacing: 16
                Layout.fillWidth: true
                
                TextField {
                    id: idField
                    placeholderText: "ID"
                    Layout.fillWidth: true
                    height: 40
                    color: theme ? theme.textPrimary : "white"
                    background: Rectangle {
                        color: theme ? theme.bgComponent : "#18181b"
                        border.color: idField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                }
                
                TextField {
                    id: pwField
                    placeholderText: "Password"
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    height: 40
                    color: theme ? theme.textPrimary : "white"
                    background: Rectangle {
                        color: theme ? theme.bgComponent : "#18181b"
                        border.color: pwField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                }
                
                Button {
                    text: "Sign In"
                    Layout.fillWidth: true
                    height: 40
                    
                    background: Rectangle {
                        color: parent.down ? "#ea580c" : (theme ? theme.accent : "#f97316")
                        radius: 6
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        backend.login(idField.text, pwField.text)
                    }
                }
                
                Button {
                    text: "Cancel"
                    Layout.fillWidth: true
                    height: 40
                    
                    background: Rectangle {
                        color: "transparent"
                        border.color: theme ? theme.border : "#27272a"
                        radius: 6
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: stackLayout.currentIndex = 0 // 라이브로 돌아가기
                }
            }
        }
    }
    
    // 녹화 목록 보기 (로그인 시 표시)
    RowLayout {
        anchors.fill: parent
        visible: root.isLoggedIn
        spacing: 16
        
        // 목록 사이드
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: theme ? theme.bgComponent : "#18181b"
            radius: 8
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                
                RowLayout {
                    Layout.fillWidth: true
                    
                    Text {
                        text: "Recordings"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 18
                    }
                    
                    Item { Layout.fillWidth: true }
                    
                    Button {
                        text: "🔄"
                        background: Rectangle { color: "transparent"; radius: 4; border.color: theme ? theme.border : "#27272a" }
                        contentItem: Text { text: parent.text; color: theme ? theme.textSecondary : "#a1a1aa"; anchors.centerIn: parent }
                        onClicked: backend.refreshRecordings()
                        ToolTip.visible: hovered
                        ToolTip.text: "목록 새로고침"
                    }
                }
                
                // 라이브 뷰 버튼 (상단에 눈에 띄게)
                Button {
                    Layout.fillWidth: true
                    height: 36
                    text: "📺 Return to Live View"
                    
                    background: Rectangle {
                        color: parent.down ? "#ea580c" : (theme ? theme.accent : "#f97316")
                        radius: 6
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: "white"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: stackLayout.currentIndex = 0
                }

                ListView {
                    id: recordList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    
                    model: ListModel { id: filesModel }
                    
                    delegate: Rectangle {
                        width: recordList.width
                        height: 40
                        color: itemMouseArea.pressed ? (theme ? theme.accent : "#f97316") : (index % 2 == 0 ? "transparent" : (theme ? theme.bgComponent : "#18181b"))
                        radius: 4
                        
                            MouseArea {
                                id: itemMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                
                                // 더블 클릭 우선 처리를 위해 onClicked 로직 조정 필요할 수 있음
                                // 하지만 여기서는 우클릭 메뉴와 좌클릭 선택이 필요함.
                                // 가장 빠른 반응을 위해 onPress 시점에 인덱스 변경을 고려할 수도 있으나, 드래그 등과 충돌 가능.
                                // 더블 클릭 시 즉시 로그 출력
                                
                                onPressed: (mouse) => {
                                    if (mouse.button === Qt.LeftButton) {
                                        recordList.currentIndex = index
                                    }
                                }
                                onClicked: (mouse) => {
                                    if (mouse.button === Qt.RightButton) {
                                        recordList.currentIndex = index
                                        contextMenu.popup()
                                    }
                                }
                                onDoubleClicked: {
                                    console.log("Double click detected for:", model.fileName)
                                    root.playVideo(model.fileName)
                                }
                                
                                Menu {
                                    id: contextMenu
                                    MenuItem {
                                        text: "Rename"
                                        onTriggered: {
                                            renameDialog.currentFileName = model.fileName
                                            renameDialog.open()
                                        }
                                    }
                                    MenuItem {
                                        text: "Export"
                                        onTriggered: {
                                            fileDialog.currentFile = "file:///" + model.fileName
                                            fileDialog.selectedFileName = model.fileName
                                            fileDialog.open()
                                        }
                                    }
                                }
                            }
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            
                            Text {
                                text: "🎬"
                                font.pixelSize: 14
                            }
                            Text {
                                text: model.fileName
                                color: itemMouseArea.containsMouse || recordList.currentIndex === index ? "white" : (theme ? theme.textPrimary : "white")
                                font.bold: recordList.currentIndex === index
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            
                            Text {
                                text: model.fileSize > 0 ? formatBytes(model.fileSize) : ""
                                color: itemMouseArea.containsMouse || recordList.currentIndex === index ? "#e4e4e7" : (theme ? theme.textSecondary : "#a1a1aa")
                                font.pixelSize: 12
                                visible: model.fileSize > 0
                            }


                        }
                    }

                    // 빈 상태
                    Text {
                        anchors.centerIn: parent
                        text: "No recordings found"
                        visible: recordList.count === 0 && backend.isLoggedIn
                        color: theme ? theme.textSecondary : "#71717a"
                    }
                    
                    // 모델 업데이트를 위한 연결
                    Connections {
                        target: backend
                        function onRecordingsLoaded(files) {
                            filesModel.clear()
                            for(var i=0; i<files.length; i++) {
                                var file = files[i]
                                // Backend에서 QVariantMap으로 전달됨 (name, size 등)
                                var name = file.name || file // 호환성 (문자열만 올 경우)
                                var size = file.size || 0
                                filesModel.append({
                                    fileName: name,
                                    fileSize: size
                                })
                            }
                        }
                    }
                }
            }
        }
        
        // 오른쪽 패널: 플레이어
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"
            
            VideoOutput {
                id: recordVideoOut
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectFit
            }
            
            MediaPlayer {
                id: player
                videoOutput: recordVideoOut
                // audioOutput: AudioOutput { volume: 0.5 } // 제거됨
            }
            
            // 플레이어 컨트롤 오버레이
            Rectangle {
                width: parent.width
                height: 80
                anchors.bottom: parent.bottom
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1 // Top border mainly
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16
                    
                    // 재생/일시정지
                    Rectangle {
                        width: 40; height: 40
                        radius: 20
                        color: "#27272a"
                        border.color: "#3f3f46"
                        border.width: 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: player.playbackState === MediaPlayer.PlayingState ? "⏸" : "▶"
                            color: "white"
                            font.pixelSize: 18
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                        }
                    }
                    
                    // 시간 및 슬라이더
                    Text {
                        text: formatTime(player.position)
                        color: "#a1a1aa"
                        font.family: "monospace"
                    }
                    
                    Slider {
                        id: seekSlider
                        Layout.fillWidth: true
                        from: 0
                        to: player.duration
                        value: player.position
                        onMoved: player.position = value
                        
                        handle: Rectangle {
                            x: seekSlider.leftPadding + seekSlider.visualPosition * (seekSlider.availableWidth - width)
                            y: seekSlider.topPadding + seekSlider.availableHeight / 2 - height / 2
                            width: 16; height: 16
                            radius: 8
                            color: "#f97316"
                        }
                    }
                    
                    Text {
                        text: formatTime(player.duration)
                        color: "#a1a1aa"
                        font.family: "monospace"
                    }
                    
                    // 볼륨 제거됨 - 오디오 없음
                    
                    /*
                    Text { text: "🔊"; font.pixelSize: 16 }
                    Slider {
                        Layout.preferredWidth: 100
                        from: 0; to: 1.0; value: 0.5
                        onMoved: player.audioOutput.volume = value
                    }
                    */
                }
            }
        }
    }
    
    // 백엔드 신호 연결
    Connections {
        target: backend
        
        function onLoginFailed(error) {
            errorDialog.title = "Login Failed"
            errorDialog.text = error
            errorDialog.open()
        }
        
        function onLoginSuccess() {
            backend.refreshRecordings()
        }
        
        property var downloadStartTime: 0
        
        function onDownloadProgress(received, total) {
            if (!downloadPopup.opened) {
                downloadPopup.open()
                downloadStartTime = new Date().getTime()
            }
            
            if (total > 0) {
                downloadBar.value = received / total
                
                // ETA 계산 (단순 평균 속도 기반)
                var currentTime = new Date().getTime()
                var elapsed = (currentTime - downloadStartTime) / 1000 // 초 단위 경과 시간
                var speed = elapsed > 0 ? received / elapsed : 0 // bytes/sec
                
                var etaText = ""
                if (speed > 0) {
                    var remaining = total - received
                    var etaSeconds = Math.ceil(remaining / speed)
                    etaText = " (" + etaSeconds + "s remaining)"
                }
                
                downloadText.text = formatBytes(received) + " / " + formatBytes(total) + etaText
            }
        }
        
        function onDownloadFinished(path) {
            downloadPopup.close()
            if (path.toString().startsWith("file:///")) {
                console.log("Playing local file:", path)
                player.source = path
                player.play()
            } else {
                errorDialog.title = "Export Success"
                errorDialog.text = "File saved successfully to:\n" + path
                errorDialog.open()
            }
        }
        
        function onDownloadError(error) {
            downloadPopup.close()
            errorDialog.title = "Download Error"
            errorDialog.text = error
            errorDialog.open()
        }
    }

    // 오류 대화상자
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        title: "Error"
        property string text: ""
        modal: true
        standardButtons: Dialog.Ok
        
        contentItem: Text {
            text: errorDialog.text
            color: theme ? theme.textPrimary : "white"
            wrapMode: Text.WordWrap
        }
        
        background: Rectangle {
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            radius: 8
        }
    }

    // 다운로드 진행 팝업
    Popup {
        id: downloadPopup
        anchors.centerIn: parent
        width: 300
        height: 120
        modal: true
        closePolicy: Popup.NoAutoClose
        
        background: Rectangle {
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            radius: 8
        }
        
        ColumnLayout {
            anchors.centerIn: parent
            width: parent.width - 40
            spacing: 16
            
            Text {
                text: "Downloading Video..."
                color: theme ? theme.textPrimary : "white"
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }
            
            Text {
                id: downloadText
                text: "0 MB / 0 MB"
                color: theme ? theme.textSecondary : "#a1a1aa"
                font.pixelSize: 12
                Layout.alignment: Qt.AlignHCenter
            }
            
            ProgressBar {
                id: downloadBar
                Layout.fillWidth: true
                from: 0
                to: 1.0
                value: 0
                
                background: Rectangle {
                    implicitWidth: 200
                    implicitHeight: 6
                    color: "#27272a"
                    radius: 3
                }
                
                contentItem: Item {
                    implicitWidth: 200
                    implicitHeight: 6
                    
                    Rectangle {
                        width: downloadBar.visualPosition * parent.width
                        height: parent.height
                        radius: 2
                        color: theme ? theme.accent : "#f97316"
                    }
                }
            }
            
            Button {
                text: "Cancel"
                Layout.alignment: Qt.AlignHCenter
                onClicked: {
                    backend.cancelDownload()
                    downloadPopup.close()
                }
                
                background: Rectangle {
                    color: "transparent"
                    border.color: "#52525b"
                    radius: 4
                }
                contentItem: Text { text: parent.text; color: "#a1a1aa"; anchors.centerIn: parent }
            }
        }
    }
    
    FileDialog {
        id: fileDialog
        title: "Export Recording"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Video files (*.mp4)", "All files (*)"]
        property string selectedFileName
        
        onAccepted: {
            // 선택된 경로에서 파일명 추출 또는 그대로 사용?
            // FileDialog는 selectedFile (url)을 반환함.
            // backend.exportRecording expects: fileName (server), savePath (local)
            var path = selectedFile.toString()
            // Remove file:/// prefix if present (Windows specific handling might be needed in C++, but verify)
            if (path.startsWith("file:///")) path = path.slice(8) // file:/// -> /... (On Windows 8 chars?) 
            // Actually QUrl.toLocalFile() logic is better handled in C++? 
            // Let's pass the raw string and let C++ handle or simpler: Backend.exportRecording should take the path.
            // Backend takes QString savePath. If passed "file:///C:/...", QFile might handle it or need stripping.
            // Let's strip "file:///" manually to be safe for QFile.
            
            backend.exportRecording(selectedFileName, path)
        }
    }

    function playVideo(fileName) {
        console.log("Requesting download and play:", fileName)
        backend.downloadAndPlay(fileName)
    }

    Dialog {
        id: renameDialog
        title: "Rename Recording"
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string currentFileName
        
        ColumnLayout {
            spacing: 10
            Label { text: "Enter new name:" }
            TextField {
                id: renameField
                text: renameDialog.currentFileName
                selectByMouse: true
                onAccepted: renameDialog.accept()
            }
        }
        
        onOpened: {
            renameField.text = currentFileName
            renameField.forceActiveFocus()
        }
        
        onAccepted: {
            if (renameField.text !== currentFileName && renameField.text.length > 0) {
                // 확장자 유지 로직이 필요할 수 있음
                // 여기서는 입력된 텍스트 그대로 서버에 요청
                backend.renameRecording(currentFileName, renameField.text)
            }
        }
    }
    
    function formatTime(ms) {
        var seconds = Math.floor((ms / 1000) % 60);
        var minutes = Math.floor((ms / 60000) % 60);
        var hours = Math.floor(ms / 3600000);

        return (hours > 0 ? hours + ":" : "") + 
               (minutes < 10 ? "0" + minutes : minutes) + ":" + 
               (seconds < 10 ? "0" + seconds : seconds);
    }
    
    function formatBytes(bytes) {
        if (bytes === 0) return "0 B";
        var k = 1024;
        var sizes = ["B", "KB", "MB", "GB", "TB"];
        var i = Math.floor(Math.log(bytes) / Math.log(k));
        return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + " " + sizes[i];
    }
}
