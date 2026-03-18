import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../dialogs"

Item {
    // 로그인/열화상 목록 화면 루트
    id: root
    property var theme
    property string adminUnlockCode: ""
    property bool signupMode: false
    property bool darkTheme: theme ? ((theme.bgPrimary.r + theme.bgPrimary.g + theme.bgPrimary.b) < 1.5) : true
    
    property bool isLoggedIn: backend.isLoggedIn
    property bool twoFactorRequired: backend.twoFactorRequired
    signal requestReturnLiveView()
    
    function triggerSignIn() {
        if (root.signupMode || backend.loginLocked) {
            return
        }
        if (backend.twoFactorRequired) {
            backend.verifyTwoFactorOtp(otpField.text)
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
                    enabled: !backend.twoFactorRequired
                    opacity: backend.twoFactorRequired ? 0.65 : 1.0
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
                    enabled: !backend.twoFactorRequired
                    opacity: backend.twoFactorRequired ? 0.65 : 1.0
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

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: backend.twoFactorRequired && !root.signupMode

                    Rectangle {
                        Layout.fillWidth: true
                        color: theme ? theme.bgComponent : "#18181b"
                        border.color: theme ? theme.border : "#27272a"
                        radius: 8
                        implicitHeight: otpHint.implicitHeight + 24

                        Text {
                            id: otpHint
                            anchors.fill: parent
                            anchors.margins: 12
                            color: theme ? theme.textSecondary : "#d4d4d8"
                            wrapMode: Text.WordWrap
                            text: "비밀번호 확인이 완료되었습니다. Authenticator 앱의 6자리 OTP를 입력해 로그인하세요."
                        }
                    }

                    TextField {
                        id: otpField
                        placeholderText: "6-digit OTP"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                        Layout.fillWidth: true
                        height: 40
                        maximumLength: 6
                        inputMethodHints: Qt.ImhDigitsOnly
                        validator: RegularExpressionValidator { regularExpression: /[0-9]{0,6}/ }
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: otpField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        onAccepted: triggerSignIn()
                    }
                }
                
                Button {
                    text: backend.twoFactorRequired ? "Verify OTP" : "Sign In"
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
                    visible: !backend.twoFactorRequired
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
                    text: root.signupMode ? "Back to Sign In" : "Cancel OTP"
                    Layout.fillWidth: true
                    height: 36
                    visible: root.signupMode || backend.twoFactorRequired
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
                        if (root.signupMode) {
                            root.signupMode = false
                            idField.text = ""
                            pwField.text = ""
                            pwConfirmField.text = ""
                            otpField.text = ""
                            return
                        }
                        backend.cancelTwoFactorLogin()
                        otpField.text = ""
                    }
                }

                Button {
                    text: "Skip (Temporary)"
                    Layout.fillWidth: true
                    height: 36
                    visible: !root.signupMode && !backend.twoFactorRequired
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
                    visible: !root.signupMode && !backend.twoFactorRequired
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
                    visible: backend.loginLocked && !root.signupMode && !backend.twoFactorRequired

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
                    text: backend.twoFactorRequired ? "Clear OTP" : "Clear"
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
                        if (backend.twoFactorRequired) {
                            otpField.text = ""
                            return
                        }
                        idField.text = ""
                        pwField.text = ""
                        pwConfirmField.text = ""
                    }
                }
            }
        }
    }
    
    // 로그인 이후 화면 (열화상 뷰)
    Item {
        anchors.fill: parent
        visible: root.isLoggedIn

        Rectangle {
            anchors.fill: parent
            anchors.margins: 10
            color: theme ? theme.bgSecondary : "#09090b"
            radius: 8
            border.color: theme ? theme.border : "#27272a"
            border.width: 2
            clip: true

            Item {
                anchors.fill: parent
                anchors.margins: 2

                Rectangle {
                    anchors.fill: parent
                    color: "black"
                }

                Image {
                    anchors.fill: parent
                    anchors.margins: 8
                    fillMode: Image.PreserveAspectFit
                    retainWhileLoading: true
                    smooth: false
                    cache: false
                    source: backend.thermalFrameDataUrl
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 30
                    color: "transparent"

                    Rectangle {
                        anchors.top: parent.top
                        width: parent.width
                        height: 1
                        color: theme ? theme.border : "#27272a"
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                        text: backend.thermalInfoText
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 12
                    }
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
                otpField.text = ""
                adminUnlockField.text = ""
                root.adminUnlockCode = ""
                backend.stopThermalStream()
            }
        }

        function onTwoFactorRequiredChanged() {
            if (backend.twoFactorRequired) {
                otpField.text = ""
                otpField.forceActiveFocus()
            } else {
                otpField.text = ""
            }
        }
        
        function onLoginFailed(error) {
            errorDialog.title = backend.twoFactorRequired ? "OTP Verification Failed" : "Login Failed"
            errorDialog.text = error
            errorDialog.open()
        }
        
        function onLoginSuccess() {
            otpField.text = ""
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
            otpField.text = ""
        }

        function onRegisterFailed(error) {
            errorDialog.title = "Sign Up Failed"
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

}










