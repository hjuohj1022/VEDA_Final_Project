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
    property bool passwordVisible: false
    property bool capsLockOn: false
    
    property bool isLoggedIn: backend.isLoggedIn
    property bool twoFactorRequired: backend.twoFactorRequired
    signal requestReturnLiveView()

    // Windows API 기반 Lock 상태 즉시 조회
    function refreshKeyboardLockIndicators() {
        capsLockOn = backend.isCapsLockOn()
    }
    
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

    // 회원가입 비밀번호 복잡도 검증 함수
    function validateSignupPasswordComplexity(password) {
        if (password.length < 8) {
            return "비밀번호는 8자 이상이어야 합니다."
        }
        if (password.length > 16) {
            return "비밀번호는 16자 이하여야 합니다."
        }
        if (/\s/.test(password)) {
            return "비밀번호에는 공백을 사용할 수 없습니다."
        }
        if (!/[0-9]/.test(password)) {
            return "비밀번호에는 숫자가 1개 이상 포함되어야 합니다."
        }
        if (!/[^A-Za-z0-9]/.test(password)) {
            return "비밀번호에는 특수문자가 1개 이상 포함되어야 합니다."
        }
        return ""
    }

    Timer {
        id: keyboardLockTimer
        interval: 150
        repeat: true
        running: !root.isLoggedIn
        onTriggered: root.refreshKeyboardLockIndicators()
    }

    Component.onCompleted: refreshKeyboardLockIndicators()
    
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
                    Layout.preferredHeight: 40
                    height: 40
                    rightPadding: 40
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
                
                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    enabled: !backend.twoFactorRequired
                    opacity: backend.twoFactorRequired ? 0.65 : 1.0

                    TextField {
                        id: pwField
                        anchors.fill: parent
                        rightPadding: 40
                        placeholderText: "Password"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                        echoMode: root.passwordVisible ? TextInput.Normal : TextInput.Password
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: pwField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        onAccepted: triggerSignIn()
                    }

                    ToolButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        onClicked: root.passwordVisible = !root.passwordVisible
                        contentItem: Item {
                            Text {
                                anchors.centerIn: parent
                                text: "\uE890"
                                font.family: "Segoe MDL2 Assets"
                                font.pixelSize: 16
                                color: theme ? theme.textSecondary : "#d4d4d8"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            Rectangle {
                                visible: !root.passwordVisible
                                anchors.centerIn: parent
                                width: 2
                                height: 18
                                radius: 1
                                rotation: -45
                                color: theme ? theme.textSecondary : "#d4d4d8"
                            }
                        }
                        background: Rectangle {
                            color: "transparent"
                            border.color: "transparent"
                        }
                    }
                }

                ColumnLayout {
                    visible: root.signupMode && !backend.twoFactorRequired
                    Layout.fillWidth: true
                    spacing: 2

                    TextMetrics {
                        id: passwordRulePrefixMetrics
                        font.pixelSize: 11
                        font.bold: true
                        text: "비밀번호 규칙: "
                    }

                    Text {
                        Layout.fillWidth: true
                        color: theme ? theme.textPrimary : "#e4e4e7"
                        font.pixelSize: 11
                        font.bold: true
                        text: "비밀번호 규칙: 8~16자 · 숫자 1개 이상 · 특수문자 1개 이상"
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: passwordRulePrefixMetrics.width
                        color: theme ? theme.textPrimary : "#e4e4e7"
                        font.pixelSize: 11
                        font.bold: true
                        text: "공백 불가"
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    visible: !backend.twoFactorRequired && root.capsLockOn

                    Text {
                        visible: root.capsLockOn
                        text: "CAPS LOCK ON"
                        color: "#f97316"
                        font.bold: true
                        font.pixelSize: 12
                    }

                    Item { Layout.fillWidth: true }
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
                            text: "비밀번호 인증이 완료되었습니다. Authenticator 앱의 6자리 OTP를 입력해 로그인하세요."
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
                            root.passwordVisible = false
                            root.capsLockOn = false
                            return
                        }
                        if (idField.text.trim().length === 0 || pwField.text.length === 0) {
                            errorDialog.title = "Sign Up Failed"
                            errorDialog.text = "ID와 비밀번호를 모두 입력해 주세요."
                            errorDialog.open()
                            return
                        }
                        const complexityError = validateSignupPasswordComplexity(pwField.text)
                        if (complexityError.length > 0) {
                            errorDialog.title = "Sign Up Failed"
                            errorDialog.text = complexityError
                            errorDialog.open()
                            return
                        }
                        backend.registerUser(idField.text, pwField.text)
                    }
                }

                Button {
                    text: "Cancel OTP"
                    Layout.fillWidth: true
                    height: 36
                    visible: backend.twoFactorRequired && !root.signupMode
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    background: Rectangle {
                        color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                        border.color: theme ? theme.border : "#52525b"
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: theme ? theme.textPrimary : "#f4f4f5"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
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
                        color: theme ? theme.textPrimary : "#f4f4f5"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: backend.skipLoginTemporarily()
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
                        color: theme ? theme.textPrimary : "#e4e4e7"
                        font.bold: true
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
                        root.passwordVisible = false
                        root.capsLockOn = false
                    }
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
                        color: theme ? theme.textPrimary : "#f4f4f5"
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: {
                        root.signupMode = false
                        idField.text = ""
                        pwField.text = ""
                        otpField.text = ""
                        root.passwordVisible = false
                        root.capsLockOn = false
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
                root.passwordVisible = false
                root.capsLockOn = false
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










