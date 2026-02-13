import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Dialogs

Item {
    // 로그인/녹화 목록/재생 화면 루트
    id: root
    property var theme
    property string pendingDeleteFileName: ""
    property string adminUnlockCode: ""
    property bool darkTheme: theme ? ((theme.bgPrimary.r + theme.bgPrimary.g + theme.bgPrimary.b) < 1.5) : true
    
    property bool isLoggedIn: backend.isLoggedIn
    
    // 로그인 전 화면
    Rectangle {
        anchors.fill: parent
        color: theme ? theme.bgSecondary : "#09090b"
        visible: !root.isLoggedIn
        z: 10
        
        ColumnLayout {
            anchors.centerIn: parent
            width: 320
            spacing: 24
            
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 48; height: 48
                    color: theme ? theme.accent : "#f97316"
                    radius: 12
                    Text {
                        anchors.centerIn: parent
                        text: "\uE72E"
                        color: "white"
                        font.family: "Segoe MDL2 Assets"
                        font.pixelSize: 20
                    }
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
                    enabled: !backend.loginLocked
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    
                    background: Rectangle {
                        color: !parent.enabled
                               ? (theme ? theme.border : "#3f3f46")
                               : (parent.down ? "#ea580c" : (theme ? theme.accent : "#f97316"))
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

                Text {
                    Layout.fillWidth: true
                    color: backend.loginLocked ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    text: backend.loginLocked
                          ? "Account locked after " + backend.loginMaxAttempts + " failed attempts."
                          : ("Failed attempts: " + backend.loginFailedAttempts + " / " + backend.loginMaxAttempts)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    visible: backend.loginLocked

                    TextField {
                        id: adminUnlockField
                        placeholderText: "Admin unlock key"
                        echoMode: TextInput.Password
                        Layout.fillWidth: true
                        height: 40
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: adminUnlockField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        onTextChanged: root.adminUnlockCode = text
                        onAccepted: {
                            if (backend.adminUnlock(text)) {
                                text = ""
                                root.adminUnlockCode = ""
                            }
                        }
                    }

                Button {
                    text: "Admin Unlock"
                    Layout.fillWidth: true
                    height: 38
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    onClicked: {
                            if (backend.adminUnlock(adminUnlockField.text)) {
                                adminUnlockField.text = ""
                                root.adminUnlockCode = ""
                            }
                        }
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : (theme ? theme.bgComponent : "#18181b")
                        border.color: theme ? theme.border : "#27272a"
                        radius: 6
                    }
                        contentItem: Text {
                            text: parent.text
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                
                Button {
                    text: "Clear"
                    Layout.fillWidth: true
                    height: 40
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                        border.color: theme ? theme.border : "#27272a"
                        radius: 6
                    }
                    
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    onClicked: {
                        idField.text = ""
                        pwField.text = ""
                    }
                }
            }
        }
    }
    
    // 로그인 후 화면 (좌: 녹화 목록, 우: 플레이어)
    RowLayout {
        anchors.fill: parent
        visible: root.isLoggedIn
        spacing: 16
        
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
                        text: "Refresh"
                        scale: down ? 0.97 : 1.0
                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                        background: Rectangle {
                            color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                            radius: 4
                            border.color: theme ? theme.border : "#27272a"
                        }
                        contentItem: Text { text: parent.text; color: theme ? theme.textSecondary : "#a1a1aa"; anchors.centerIn: parent }
                        onClicked: backend.refreshRecordings()
                        ToolTip.visible: hovered
                        ToolTip.text: "Refresh recordings"
                    }
                }
                
                Button {
                    Layout.fillWidth: true
                    height: 36
                    text: "Return to Live View"
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    
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

                // 녹화 파일 목록
                ListView {
                    id: recordList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    
                    model: ListModel { id: filesModel }
                    
                    delegate: Rectangle {
                        width: recordList.width
                        height: 40
                        color: itemMouseArea.pressed
                               ? (recordList.currentIndex === index
                                  ? "#ea580c"
                                  : (darkTheme ? "#3f3f46" : "#e2e8f0"))
                               : (recordList.currentIndex === index
                                  ? (theme ? theme.accent : "#f97316")
                                  : (itemMouseArea.containsMouse
                                     ? (darkTheme ? "#27272a" : "#f1f5f9")
                                     : (index % 2 == 0 ? "transparent" : (theme ? theme.bgComponent : "#18181b"))))
                        radius: 4
                        scale: itemMouseArea.pressed ? 0.985 : 1.0
                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                        
                            MouseArea {
                                id: itemMouseArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                
                                
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
                                
                                // 항목 우클릭 메뉴
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
                                    MenuItem {
                                        text: "Delete"
                                        onTriggered: {
                                            root.pendingDeleteFileName = model.fileName
                                            deleteDialog.open()
                                        }
                                    }
                                }
                            }
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            
                            Text {
                                text: "F"
                                font.pixelSize: 14
                            }
                            Text {
                                text: model.fileName
                                color: recordList.currentIndex === index ? "white" : (theme ? theme.textPrimary : "white")
                                font.bold: recordList.currentIndex === index
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            
                            Text {
                                text: model.fileSize > 0 ? formatBytes(model.fileSize) : ""
                                color: recordList.currentIndex === index ? "#fff7ed" : (theme ? theme.textSecondary : "#a1a1aa")
                                font.pixelSize: 12
                                visible: model.fileSize > 0
                            }


                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: "No recordings found"
                        visible: recordList.count === 0 && backend.isLoggedIn
                        color: theme ? theme.textSecondary : "#71717a"
                    }

                    Connections {
                        target: backend
                        function onRecordingsLoaded(files) {
                            filesModel.clear()
                            for(var i=0; i<files.length; i++) {
                                var file = files[i]
                                var name = file.name || file
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
            }
            
            Rectangle {
                width: parent.width
                height: 80
                anchors.bottom: parent.bottom
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16
                    
                    Rectangle {
                        width: 40; height: 40
                        radius: 20
                        color: "#27272a"
                        border.color: "#3f3f46"
                        border.width: 1
                        
                        Text {
                            anchors.centerIn: parent
                            text: player.playbackState === MediaPlayer.PlayingState ? "II" : ">"
                            color: "white"
                            font.pixelSize: 18
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() : player.play()
                        }
                    }
                    
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

                }
            }
        }
    }
    
    // 백엔드 시그널 연결
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
                
                var currentTime = new Date().getTime()
                var elapsed = (currentTime - downloadStartTime) / 1000
                var speed = elapsed > 0 ? received / elapsed : 0
                
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

        function onDeleteFailed(error) {
            errorDialog.title = "Delete Failed"
            errorDialog.text = error
            errorDialog.open()
        }
    }

    // 공통 에러 다이얼로그
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        title: "Error"
        property string text: ""
        modal: true
        width: 360
        closePolicy: Popup.NoAutoClose

        header: Rectangle {
            implicitHeight: 44
            color: theme ? theme.bgSecondary : "#0f172a"
            border.color: theme ? theme.border : "#27272a"

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 14
                text: errorDialog.title
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
                text: errorDialog.text
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
                    onClicked: errorDialog.close()
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
                    color: theme ? theme.border : "#27272a"
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
                scale: down ? 0.97 : 1.0
                Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                onClicked: {
                    backend.cancelDownload()
                    downloadPopup.close()
                }
                
                background: Rectangle {
                    color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                    border.color: theme ? theme.border : "#52525b"
                    radius: 4
                }
                contentItem: Text {
                    text: parent.text
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    anchors.centerIn: parent
                }
            }
        }
    }
    
    // 파일 내보내기 경로 선택
    FileDialog {
        id: fileDialog
        title: "Export Recording"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Video files (*.mp4)", "All files (*)"]
        property string selectedFileName
        
        onAccepted: {
            var path = selectedFile.toString()
            if (path.startsWith("file:///")) path = path.slice(8)
            
            backend.exportRecording(selectedFileName, path)
        }
    }
    function playVideo(fileName) {
        console.log("Requesting download and play:", fileName)
        backend.downloadAndPlay(fileName)
    }

    // 파일명 변경 팝업 (다크 테마 + 드래그 이동)
    Popup {
        id: renameDialog
        width: 420
        height: 210
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose
        property string currentFileName: ""

        onOpened: {
            x = (root.width - width) / 2
            y = (root.height - height) / 2
            renameField.text = currentFileName
            renameField.forceActiveFocus()
            renameField.selectAll()
        }

        background: Rectangle {
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            radius: 10
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: "transparent"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    text: "Rename Recording"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 15
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    property real pressRootX: 0
                    property real pressRootY: 0
                    property real startPopupX: 0
                    property real startPopupY: 0
                    onPressed: (mouse) => {
                        var p = mapToItem(root, mouse.x, mouse.y)
                        pressRootX = p.x
                        pressRootY = p.y
                        startPopupX = renameDialog.x
                        startPopupY = renameDialog.y
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        var p = mapToItem(root, mouse.x, mouse.y)
                        var nx = startPopupX + (p.x - pressRootX)
                        var ny = startPopupY + (p.y - pressRootY)
                        renameDialog.x = Math.max(0, Math.min(root.width - renameDialog.width, nx))
                        renameDialog.y = Math.max(0, Math.min(root.height - renameDialog.height, ny))
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 10

                    Label {
                        text: "Enter new name:"
                        color: theme ? theme.textPrimary : "white"
                    }

                    TextField {
                        id: renameField
                        Layout.fillWidth: true
                        selectByMouse: true
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#0f172a"
                            border.color: theme ? theme.border : "#374151"
                            radius: 6
                        }
                        onAccepted: {
                            if (text !== renameDialog.currentFileName && text.length > 0) {
                                backend.renameRecording(renameDialog.currentFileName, text)
                            }
                            renameDialog.close()
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    text: "Cancel"
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 34
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    onClicked: renameDialog.close()
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                        border.color: theme ? theme.border : "#52525b"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: "Rename"
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 34
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    onClicked: {
                        if (renameField.text !== renameDialog.currentFileName && renameField.text.length > 0) {
                            backend.renameRecording(renameDialog.currentFileName, renameField.text)
                        }
                        renameDialog.close()
                    }
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
                }

                Item { Layout.preferredWidth: 12 }
            }
        }
    }

    // 파일 삭제 확인 팝업 (다크 테마 + 드래그 이동)
    Popup {
        id: deleteDialog
        width: 360
        height: 190
        modal: true
        focus: true
        closePolicy: Popup.NoAutoClose

        onOpened: {
            x = (root.width - width) / 2
            y = (root.height - height) / 2
        }

        background: Rectangle {
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            radius: 10
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: "transparent"

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    text: "Delete Recording"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 15
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.SizeAllCursor
                    property real pressRootX: 0
                    property real pressRootY: 0
                    property real startPopupX: 0
                    property real startPopupY: 0
                    onPressed: (mouse) => {
                        var p = mapToItem(root, mouse.x, mouse.y)
                        pressRootX = p.x
                        pressRootY = p.y
                        startPopupX = deleteDialog.x
                        startPopupY = deleteDialog.y
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed) return
                        var p = mapToItem(root, mouse.x, mouse.y)
                        var nx = startPopupX + (p.x - pressRootX)
                        var ny = startPopupY + (p.y - pressRootY)
                        deleteDialog.x = Math.max(0, Math.min(root.width - deleteDialog.width, nx))
                        deleteDialog.y = Math.max(0, Math.min(root.height - deleteDialog.height, ny))
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "transparent"

                Text {
                    anchors.fill: parent
                    anchors.margins: 14
                    text: root.pendingDeleteFileName.length > 0
                          ? "Delete this file?\n" + root.pendingDeleteFileName
                          : "Delete this file?"
                    color: theme ? theme.textPrimary : "white"
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.preferredHeight: 54
                spacing: 10

                Item { Layout.fillWidth: true }

                Button {
                    text: "Cancel"
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 34
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    onClicked: {
                        root.pendingDeleteFileName = ""
                        deleteDialog.close()
                    }
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                        border.color: theme ? theme.border : "#52525b"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                Button {
                    text: "Delete"
                    Layout.preferredWidth: 96
                    Layout.preferredHeight: 34
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    onClicked: {
                        if (root.pendingDeleteFileName.length > 0) {
                            backend.deleteRecording(root.pendingDeleteFileName)
                        }
                        root.pendingDeleteFileName = ""
                        deleteDialog.close()
                    }
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
                }

                Item { Layout.preferredWidth: 12 }
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










