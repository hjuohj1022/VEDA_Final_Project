import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import QtQuick.Dialogs
import "../dialogs"

Item {
    // 로그인/열화상 목록 화면 루트
    id: root
    property var theme
    property string pendingDeleteFileName: ""
    property string adminUnlockCode: ""
    property bool signupMode: false
    property bool darkTheme: theme ? ((theme.bgPrimary.r + theme.bgPrimary.g + theme.bgPrimary.b) < 1.5) : true
    
    property bool isLoggedIn: backend.isLoggedIn
    signal requestReturnLiveView()
    
    function triggerSignIn() {
        if (root.signupMode || backend.loginLocked) {
            return
        }
        backend.login(idField.text, pwField.text)
    }
    
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
                    text: root.signupMode ? "Create Account" : "Welcome Back"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 24
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.signupMode
                          ? "Sign up to create a new account"
                          : "Sign in to access surveillance system"
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
                    placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                    Layout.fillWidth: true
                    height: 40
                    color: theme ? theme.textPrimary : "white"
                    background: Rectangle {
                        color: theme ? theme.bgComponent : "#18181b"
                        border.color: idField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                    onAccepted: triggerSignIn()
                }
                
                TextField {
                    id: pwField
                    placeholderText: "Password"
                    placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                    echoMode: TextInput.Password
                    Layout.fillWidth: true
                    height: 40
                    color: theme ? theme.textPrimary : "white"
                    background: Rectangle {
                        color: theme ? theme.bgComponent : "#18181b"
                        border.color: pwField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                    onAccepted: triggerSignIn()
                }

                TextField {
                    id: pwConfirmField
                    placeholderText: "Confirm Password"
                    placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                    echoMode: TextInput.Password
                    visible: root.signupMode
                    Layout.fillWidth: true
                    height: root.signupMode ? 40 : 0
                    color: theme ? theme.textPrimary : "white"
                    background: Rectangle {
                        color: theme ? theme.bgComponent : "#18181b"
                        border.color: pwConfirmField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                        radius: 6
                    }
                }
                
                Button {
                    text: "Sign In"
                    Layout.fillWidth: true
                    height: 40
                    visible: !root.signupMode
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
                        triggerSignIn()
                    }
                }

                Button {
                    text: root.signupMode ? "Create Account" : "Sign Up"
                    Layout.fillWidth: true
                    height: 40
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

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

                    onClicked: {
                        if (!root.signupMode) {
                            root.signupMode = true
                            idField.text = ""
                            pwField.text = ""
                            pwConfirmField.text = ""
                            return
                        }
                        if (idField.text.trim().length === 0 || pwField.text.length === 0 || pwConfirmField.text.length === 0) {
                            errorDialog.title = "Sign Up Failed"
                            errorDialog.text = "ID/비밀번호/비밀번호 확인을 모두 입력해 주세요."
                            errorDialog.open()
                            return
                        }
                        if (pwField.text !== pwConfirmField.text) {
                            errorDialog.title = "Sign Up Failed"
                            errorDialog.text = "비밀번호와 비밀번호 확인이 일치하지 않습니다."
                            errorDialog.open()
                            return
                        }
                        backend.registerUser(idField.text, pwField.text)
                    }
                }

                Button {
                    text: "Back to Sign In"
                    Layout.fillWidth: true
                    height: 36
                    visible: root.signupMode
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
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
                    onClicked: {
                        root.signupMode = false
                        idField.text = ""
                        pwField.text = ""
                        pwConfirmField.text = ""
                    }
                }

                Button {
                    text: "Skip (Temporary)"
                    Layout.fillWidth: true
                    height: 36
                    visible: !root.signupMode
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
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
                    onClicked: backend.skipLoginTemporarily()
                }

                Text {
                    Layout.fillWidth: true
                    visible: !root.signupMode
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
                    visible: backend.loginLocked && !root.signupMode

                    TextField {
                        id: adminUnlockField
                        placeholderText: "Admin unlock key"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
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
                        pwConfirmField.text = ""
                    }
                }
            }
        }
    }
    
    // 로그인 이후 화면 (열화상 패널)
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
                        text: "Thermal Panel"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 18
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: backend.thermalStreaming ? "Stop" : "Start"
                        scale: down ? 0.97 : 1.0
                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                        background: Rectangle {
                            color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                            radius: 4
                            border.color: theme ? theme.border : "#27272a"
                        }
                        contentItem: Text { text: parent.text; color: theme ? theme.textSecondary : "#a1a1aa"; anchors.centerIn: parent }
                        onClicked: {
                            if (backend.thermalStreaming)
                                backend.stopThermalStream()
                            else
                                backend.startThermalStream()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "열화상 스트리밍 시작/중지"
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

                    onClicked: root.requestReturnLiveView()
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: theme ? theme.bgSecondary : "#0f172a"
                    radius: 8
                    border.color: theme ? theme.border : "#27272a"
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 10

                        Text {
                            text: "Thermal Controls"
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                            font.pixelSize: 14
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                text: "Palette"
                                color: theme ? theme.textSecondary : "#a1a1aa"
                                Layout.preferredWidth: 72
                            }
                            ComboBox {
                                id: paletteCombo
                                Layout.fillWidth: true
                                model: ["Gray", "Iron", "Jet"]
                                Component.onCompleted: {
                                    var idx = model.indexOf(backend.thermalPalette)
                                    currentIndex = idx >= 0 ? idx : 2
                                }
                                onActivated: backend.setThermalPalette(currentText)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "Auto Range"
                                color: theme ? theme.textSecondary : "#a1a1aa"
                                Layout.fillWidth: true
                            }
                            Switch {
                                checked: backend.thermalAutoRange
                                onToggled: backend.setThermalAutoRange(checked)
                            }
                        }

                        Text {
                            text: "Auto Window: " + backend.thermalAutoRangeWindowPercent + "%"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            visible: backend.thermalAutoRange
                        }
                        Slider {
                            id: autoWindowSlider
                            Layout.fillWidth: true
                            from: 50
                            to: 100
                            stepSize: 1
                            enabled: backend.thermalAutoRange
                            visible: backend.thermalAutoRange
                            value: backend.thermalAutoRangeWindowPercent
                            onMoved: backend.setThermalAutoRangeWindowPercent(Math.round(value))
                        }

                        Text {
                            text: "Min: " + backend.thermalManualMin
                            color: theme ? theme.textSecondary : "#a1a1aa"
                        }
                        Slider {
                            id: minSlider
                            Layout.fillWidth: true
                            from: 0
                            to: 30000
                            stepSize: 10
                            enabled: !backend.thermalAutoRange
                            value: backend.thermalManualMin
                            onMoved: backend.setThermalManualRange(value, maxSlider.value)
                        }

                        Text {
                            text: "Max: " + backend.thermalManualMax
                            color: theme ? theme.textSecondary : "#a1a1aa"
                        }
                        Slider {
                            id: maxSlider
                            Layout.fillWidth: true
                            from: 100
                            to: 35000
                            stepSize: 10
                            enabled: !backend.thermalAutoRange
                            value: backend.thermalManualMax
                            onMoved: backend.setThermalManualRange(minSlider.value, value)
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 6
                            color: "transparent"
                            border.color: theme ? theme.border : "#27272a"
                            border.width: 1
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 4
                                Text {
                                    text: "Emissivity (추후 확장)"
                                    color: theme ? theme.textSecondary : "#a1a1aa"
                                    font.pixelSize: 12
                                }
                                Text {
                                    text: "Ambient (추후 확장)"
                                    color: theme ? theme.textSecondary : "#a1a1aa"
                                    font.pixelSize: 12
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "black"

            Image {
                anchors.fill: parent
                anchors.margins: 8
                fillMode: Image.PreserveAspectFit
                smooth: false
                cache: false
                source: backend.thermalFrameDataUrl
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 40
                color: "#18181b"
                border.color: "#27272a"
                border.width: 1
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    text: backend.thermalInfoText
                    color: "#d4d4d8"
                    font.pixelSize: 12
                }
            }
        }
    }
    
    // 백엔드 시그널 연결
    Connections {
        target: backend

        function onIsLoggedInChanged() {
            if (!backend.isLoggedIn) {
                idField.text = ""
                pwField.text = ""
                adminUnlockField.text = ""
                root.adminUnlockCode = ""
                backend.stopThermalStream()
            }
        }
        
        function onLoginFailed(error) {
            errorDialog.title = "Login Failed"
            errorDialog.text = error
            errorDialog.open()
        }
        
        function onLoginSuccess() {
            backend.startThermalStream()
        }

        function onRegisterSuccess(message) {
            errorDialog.title = "Sign Up"
            errorDialog.text = message
            errorDialog.open()
            root.signupMode = false
            idField.text = ""
            pwField.text = ""
            pwConfirmField.text = ""
        }

        function onRegisterFailed(error) {
            errorDialog.title = "Sign Up Failed"
            errorDialog.text = error
            errorDialog.open()
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
            errorDialog.title = "Export Success"
            errorDialog.text = "File saved successfully to:\n" + path
            errorDialog.open()
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

    // 공통 상태/에러 다이얼로그
    StatusDialog {
        id: errorDialog
        anchors.centerIn: parent
        theme: root.theme
        title: "Error"
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

    // 파일명 변경 팝업 (스크롤 방지 + 드래그 이동)
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

    // 파일 삭제 확인 팝업 (스크롤 방지 + 드래그 이동)
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










