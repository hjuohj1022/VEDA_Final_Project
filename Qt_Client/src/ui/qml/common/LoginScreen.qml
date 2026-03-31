import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import "../dialogs"

Item {
    // 로그인/열화상 목록 화면 루트
    id: root
    property var theme
    property string adminUnlockCode: ""
    property bool signupMode: false
    property bool darkTheme: theme ? ((theme.bgPrimary.r + theme.bgPrimary.g + theme.bgPrimary.b) < 1.5) : true
    property bool primaryPasswordVisible: false
    property bool confirmPasswordVisible: false
    property bool capsLockOn: false
    property bool signupEmailCodeVisible: false
    property bool signupEmailVerified: false
    property string signupEmailVerifiedId: ""
    property string signupEmailVerifiedAddress: ""
    property string signupEmailStatusText: ""
    property string certDirectoryStatusText: ""
    property bool certDirectoryStatusIsError: false
    property int authFieldWidth: 320
    property int authInlineButtonWidth: 78
    property int authInlineSpacing: 8
    property int authInlineRowWidth: authFieldWidth + authInlineButtonWidth + authInlineSpacing
    
    property bool isLoggedIn: backend.isLoggedIn
    property bool twoFactorRequired: backend.twoFactorRequired
    signal requestReturnLiveView()
    // 키보드 잠금 표시 갱신 함수
    function refreshKeyboardLockIndicators() {
        capsLockOn = backend.isCapsLockOn()
    }
    // 로그인 실행 함수
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
    // 회원가입 실행 함수
    function triggerSignUp() {
        if (!root.signupMode || backend.twoFactorRequired) {
            return
        }
        if (idField.text.trim().length === 0
                || emailField.text.trim().length === 0
                || pwField.text.length === 0
                || pwConfirmField.text.length === 0) {
            errorDialog.title = "회원가입 실패"
            errorDialog.text = "ID, 이메일, 비밀번호, 비밀번호 확인을 모두 입력해 주세요."
            errorDialog.open()
            return
        }
        const emailError = validateSignupEmail(emailField.text)
        if (emailError.length > 0) {
            errorDialog.title = "회원가입 실패"
            errorDialog.text = emailError
            errorDialog.open()
            return
        }
        if (!root.isCurrentSignupEmailVerified()) {
            errorDialog.title = "회원가입 실패"
            errorDialog.text = "이메일 인증을 완료해 주세요."
            errorDialog.open()
            return
        }
        const complexityError = validateSignupPasswordComplexity(pwField.text)
        if (complexityError.length > 0) {
            errorDialog.title = "회원가입 실패"
            errorDialog.text = complexityError
            errorDialog.open()
            return
        }
        if (pwField.text !== pwConfirmField.text) {
            errorDialog.title = "회원가입 실패"
            errorDialog.text = "비밀번호와 비밀번호 확인이 일치하지 않습니다."
            errorDialog.open()
            return
        }
        backend.registerUser(idField.text, pwField.text, emailField.text)
    }
    // URL 로컬 경로 변환 함수
    function urlToLocalPath(u) {
        var s = String(u || "")
        if (s.indexOf("file:///") === 0) {
            return decodeURIComponent(s.slice(8))
        }
        if (s.indexOf("file://") === 0) {
            return decodeURIComponent(s.slice(7))
        }
        if (s.indexOf("file:/") === 0) {
            return decodeURIComponent(s.slice(6))
        }
        return s
    }
    // 로컬 경로 파일 URL 변환 함수
    function localPathToFileUrl(path) {
        var s = String(path || "")
        if (s.length === 0)
            return ""
        var normalized = s.replace(/\\/g, "/")
        if (/^[A-Za-z]:\//.test(normalized))
            return "file:///" + encodeURI(normalized)
        if (normalized.indexOf("/") === 0)
            return "file://" + encodeURI(normalized)
        return normalized
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
    // 회원가입 이메일 형식 검증 함수
    function validateSignupEmail(email) {
        const trimmed = email.trim()
        if (trimmed.length === 0) {
            return "이메일을 입력해 주세요."
        }
        if (/\s/.test(trimmed)) {
            return "이메일에는 공백을 사용할 수 없습니다."
        }
        if (!/^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)+$/.test(trimmed)) {
            return "이메일 형식이 올바르지 않습니다."
        }
        return ""
    }
    // 회원가입 이메일 인증 상태 초기화 함수
    function resetSignupEmailVerificationState() {
        signupEmailCodeVisible = false
        signupEmailVerified = false
        signupEmailVerifiedId = ""
        signupEmailVerifiedAddress = ""
        signupEmailStatusText = ""
        if (emailCodeField) {
            emailCodeField.text = ""
        }
    }
    // 현재 회원가입 이메일 인증 상태 확인 함수
    function isCurrentSignupEmailVerified() {
        return signupEmailVerified
                && signupEmailVerifiedId === idField.text.trim()
                && signupEmailVerifiedAddress === emailField.text.trim()
    }

    Timer {
        id: keyboardLockTimer
        interval: 150
        repeat: true
        running: !root.isLoggedIn
        // 트리거 처리 함수
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
            anchors.verticalCenterOffset: root.signupMode ? 0 : -12
            width: root.authFieldWidth
            spacing: 24
            
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: 8
                
                Image {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                    source: "qrc:/qt/qml/Team3VideoReceiver/icons/AEGIS_logo.png"
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: 112
                    sourceSize.height: 112
                    smooth: true
                    mipmap: true
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: root.signupMode ? "회원가입" : "AEGIS Vision VMS"
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 24
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    visible: root.signupMode
                    text: root.signupMode
                          ? "새 계정을 만들려면 아래 정보를 입력하세요"
                          : ""
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
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    // 입력 확정 처리 함수
                    onAccepted: triggerSignIn()
                    // 텍스트 변경 처리 함수
                    onTextChanged: {
                        if (root.signupMode && (root.signupEmailCodeVisible || root.signupEmailVerified)) {
                            root.resetSignupEmailVerificationState()
                        }
                    }
                }

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    Layout.preferredHeight: 40
                    visible: root.signupMode && !backend.twoFactorRequired
                    enabled: !backend.twoFactorRequired
                    opacity: backend.twoFactorRequired ? 0.65 : 1.0

                    TextField {
                        id: emailField
                        width: root.authFieldWidth
                        height: parent.height
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        placeholderText: "이메일"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: emailField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        // 텍스트 변경 처리 함수
                        onTextChanged: {
                            if (root.signupEmailCodeVisible || root.signupEmailVerified) {
                                root.resetSignupEmailVerificationState()
                            }
                        }
                    }

                    Button {
                        id: requestEmailVerifyButton
                        width: root.authInlineButtonWidth
                        height: parent.height
                        x: parent.width + root.authInlineSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.isCurrentSignupEmailVerified() ? "완료" : "인증"
                        enabled: !root.isCurrentSignupEmailVerified()
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
                        // 클릭 이벤트 처리 함수
                        onClicked: {
                            if (idField.text.trim().length === 0) {
                                errorDialog.title = "회원가입 실패"
                                errorDialog.text = "이메일 인증 전에 ID를 먼저 입력해 주세요."
                                errorDialog.open()
                                return
                            }
                            const emailError = validateSignupEmail(emailField.text)
                            if (emailError.length > 0) {
                                errorDialog.title = "회원가입 실패"
                                errorDialog.text = emailError
                                errorDialog.open()
                                return
                            }
                            root.signupEmailCodeVisible = true
                            root.signupEmailVerified = false
                            root.signupEmailVerifiedId = ""
                            root.signupEmailVerifiedAddress = ""
                            root.signupEmailStatusText = "인증 코드를 전송 중입니다..."
                            emailCodeField.text = ""
                            backend.requestEmailVerification(idField.text, emailField.text)
                        }
                    }
                }

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    Layout.preferredHeight: 40
                    visible: root.signupMode && !backend.twoFactorRequired && root.signupEmailCodeVisible

                    TextField {
                        id: emailCodeField
                        width: root.authFieldWidth
                        height: parent.height
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        placeholderText: "인증 코드 입력"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: emailCodeField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        // 입력 확정 처리 함수
                        onAccepted: {
                            if (text.trim().length > 0) {
                                backend.confirmEmailVerification(idField.text, emailField.text, text.trim())
                            }
                        }
                    }

                    Button {
                        id: verifyEmailCodeButton
                        width: root.authInlineButtonWidth
                        height: parent.height
                        x: parent.width + root.authInlineSpacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: "확인"
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
                        // 클릭 이벤트 처리 함수
                        onClicked: {
                            if (emailCodeField.text.trim().length === 0) {
                                errorDialog.title = "회원가입 실패"
                                errorDialog.text = "이메일 인증 코드를 입력해 주세요."
                                errorDialog.open()
                                return
                            }
                            backend.confirmEmailVerification(idField.text, emailField.text, emailCodeField.text.trim())
                        }
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    visible: root.signupMode && root.signupEmailStatusText.length > 0
                    color: root.isCurrentSignupEmailVerified()
                           ? "#22c55e"
                           : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                    text: root.signupEmailStatusText
                }
                
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    Layout.preferredHeight: 40
                    enabled: !backend.twoFactorRequired
                    opacity: backend.twoFactorRequired ? 0.65 : 1.0

                    TextField {
                        id: pwField
                        anchors.fill: parent
                        rightPadding: 40
                        placeholderText: "비밀번호"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                        echoMode: root.primaryPasswordVisible ? TextInput.Normal : TextInput.Password
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: pwField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        // 입력 확정 처리 함수
                        onAccepted: {
                            if (root.signupMode) {
                                pwConfirmField.forceActiveFocus()
                                return
                            }
                            triggerSignIn()
                        }
                    }

                    ToolButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        // 클릭 이벤트 처리 함수
                        onClicked: root.primaryPasswordVisible = !root.primaryPasswordVisible
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
                                visible: !root.primaryPasswordVisible
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

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    Layout.preferredHeight: 40
                    visible: root.signupMode && !backend.twoFactorRequired
                    enabled: !backend.twoFactorRequired
                    opacity: backend.twoFactorRequired ? 0.65 : 1.0

                    TextField {
                        id: pwConfirmField
                        anchors.fill: parent
                        rightPadding: 40
                        placeholderText: "비밀번호 확인"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
                        echoMode: root.confirmPasswordVisible ? TextInput.Normal : TextInput.Password
                        color: theme ? theme.textPrimary : "white"
                        background: Rectangle {
                            color: theme ? theme.bgComponent : "#18181b"
                            border.color: pwConfirmField.activeFocus ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#27272a")
                            radius: 6
                        }
                        // 입력 확정 처리 함수
                        onAccepted: triggerSignUp()
                    }

                    ToolButton {
                        anchors.right: parent.right
                        anchors.rightMargin: 4
                        anchors.verticalCenter: parent.verticalCenter
                        width: 32
                        height: 32
                        // 클릭 이벤트 처리 함수
                        onClicked: root.confirmPasswordVisible = !root.confirmPasswordVisible
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
                                visible: !root.confirmPasswordVisible
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

                Item {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    Layout.preferredHeight: forgotPasswordLink.implicitHeight
                    visible: !root.signupMode && !backend.twoFactorRequired

                    Text {
                        id: forgotPasswordLink
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: "비밀번호를 잊으셨나요?"
                        color: forgotPasswordMouse.containsMouse
                               ? (theme ? theme.accent : "#f97316")
                               : (theme ? theme.textSecondary : "#a1a1aa")
                        font.pixelSize: 12
                        font.underline: forgotPasswordMouse.containsMouse
                    }

                    MouseArea {
                        id: forgotPasswordMouse
                        x: forgotPasswordLink.x
                        y: forgotPasswordLink.y
                        width: forgotPasswordLink.implicitWidth
                        height: forgotPasswordLink.implicitHeight
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        // 클릭 이벤트 처리 함수
                        onClicked: passwordForgotDialog.openDialog(idField.text)
                    }
                }

                ColumnLayout {
                    visible: root.signupMode && !backend.twoFactorRequired
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                        horizontalAlignment: Text.AlignLeft
                        text: "비밀번호 규칙: 8~16자 · 숫자 1개 이상 · 특수문자 1개 이상"
                    }

                    Text {
                        Layout.fillWidth: true
                        Layout.leftMargin: passwordRulePrefixMetrics.width
                        color: theme ? theme.textPrimary : "#e4e4e7"
                        font.pixelSize: 11
                        font.bold: true
                        horizontalAlignment: Text.AlignLeft
                        text: "공백 불가"
                    }
                }

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    spacing: 10
                    visible: backend.twoFactorRequired && !root.signupMode

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: root.authFieldWidth
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
                        Layout.alignment: Qt.AlignHCenter
                        Layout.preferredWidth: root.authFieldWidth
                        placeholderText: "6자리 OTP"
                        placeholderTextColor: theme ? theme.textSecondary : "#a1a1aa"
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
                        // 입력 확정 처리 함수
                        onAccepted: triggerSignIn()
                    }
                }
                
                Button {
                    text: backend.twoFactorRequired ? "OTP 확인" : "로그인"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        triggerSignIn()
                    }
                }

                Button {
                    text: root.signupMode ? "가입하기" : "회원가입"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        if (!root.signupMode) {
                            root.signupMode = true
                            idField.text = ""
                            emailField.text = ""
                            pwField.text = ""
                            pwConfirmField.text = ""
                            root.resetSignupEmailVerificationState()
                            root.primaryPasswordVisible = false
                            root.confirmPasswordVisible = false
                            root.capsLockOn = false
                            return
                        }
                        triggerSignUp()
                    }
                }

                Button {
                    text: "OTP 취소"
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.cancelTwoFactorLogin()
                        otpField.text = ""
                    }
                }

                ColumnLayout {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    spacing: 8
                    visible: backend.loginLocked && !root.signupMode && !backend.twoFactorRequired

                    TextField {
                        id: adminUnlockField
                        placeholderText: "관리자 해제 키"
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
                        // 텍스트 변경 처리 함수
                        onTextChanged: root.adminUnlockCode = text
                        // 입력 확정 처리 함수
                        onAccepted: {
                            backend.adminUnlock(text)
                        }
                    }

                Button {
                    text: "Admin Unlock"
                    Layout.fillWidth: true
                    height: 38
                    scale: down ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        backend.adminUnlock(adminUnlockField.text)
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
                    text: backend.twoFactorRequired ? "OTP 초기화" : "초기화"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        if (backend.twoFactorRequired) {
                            otpField.text = ""
                            return
                        }
                        idField.text = ""
                        emailField.text = ""
                        pwField.text = ""
                        pwConfirmField.text = ""
                        root.resetSignupEmailVerificationState()
                        root.primaryPasswordVisible = false
                        root.confirmPasswordVisible = false
                        root.capsLockOn = false
                    }
                }

                Button {
                    text: "인증서 경로 설정"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        certFolderDialog.currentFolder = root.localPathToFileUrl(backend.certDirectoryPath)
                        certFolderDialog.open()
                    }
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    visible: !root.signupMode && !backend.twoFactorRequired
                    color: theme ? theme.textSecondary : "#a1a1aa"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    text: "인증서 폴더: " + backend.certDirectoryPath
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    visible: !root.signupMode
                             && !backend.twoFactorRequired
                             && root.certDirectoryStatusText.length > 0
                    color: root.certDirectoryStatusIsError
                           ? "#ef4444"
                           : "#22c55e"
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    text: root.certDirectoryStatusText
                }

                Text {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
                    visible: backend.loginLocked && !root.signupMode && !backend.twoFactorRequired
                    color: backend.loginLocked ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    text: "로그인 시도 횟수를 초과해 계정이 잠겼습니다."
                }

                Button {
                    text: "로그인으로 돌아가기"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: root.authFieldWidth
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
                    // 클릭 이벤트 처리 함수
                    onClicked: {
                        root.signupMode = false
                        idField.text = ""
                        emailField.text = ""
                        pwField.text = ""
                        pwConfirmField.text = ""
                        otpField.text = ""
                        root.resetSignupEmailVerificationState()
                        root.primaryPasswordVisible = false
                        root.confirmPasswordVisible = false
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

                Rectangle {
                    id: thermalStatusBar
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 34
                    color: theme ? theme.bgComponent : "#111827"

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

                Image {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: thermalStatusBar.top
                    anchors.margins: 8
                    fillMode: Image.PreserveAspectFit
                    retainWhileLoading: true
                    smooth: false
                    cache: false
                    source: backend.thermalFrameDataUrl
                }
            }
        }
    }
    
    // 백엔드 시그널 연결
    Connections {
        target: backend
        // 로그인 상태 변경 처리 함수
        function onIsLoggedInChanged() {
            if (!backend.isLoggedIn) {
                idField.text = ""
                pwField.text = ""
                pwConfirmField.text = ""
                otpField.text = ""
                adminUnlockField.text = ""
                root.adminUnlockCode = ""
                root.resetSignupEmailVerificationState()
                root.primaryPasswordVisible = false
                root.confirmPasswordVisible = false
                root.capsLockOn = false
                backend.stopThermalStream()
            }
        }
        // 이중 인증 필요 상태 변경 처리 함수
        function onTwoFactorRequiredChanged() {
            if (backend.twoFactorRequired) {
                otpField.text = ""
                otpField.forceActiveFocus()
            } else {
                otpField.text = ""
            }
        }
        // 로그인 잠금 상태 변경 처리 함수
        function onLoginLockChanged() {
            if (!backend.loginLocked && backend.loginFailedAttempts === 0) {
                adminUnlockField.text = ""
                root.adminUnlockCode = ""
            }
        }
        // 로그인 실패 처리 함수
        function onLoginFailed(error) {
            errorDialog.title = backend.twoFactorRequired ? "OTP Verification Failed" : "Login Failed"
            errorDialog.text = error
            errorDialog.open()
        }
        // 로그인 성공 처리 함수
        function onLoginSuccess() {
            otpField.text = ""
            backend.startThermalStream()
        }
        // 회원가입 성공 처리 함수
        function onRegisterSuccess(message) {
            errorDialog.title = "회원가입"
            errorDialog.text = message
            errorDialog.open()
            root.signupMode = false
            idField.text = ""
            emailField.text = ""
            pwField.text = ""
            pwConfirmField.text = ""
            otpField.text = ""
            root.resetSignupEmailVerificationState()
            root.primaryPasswordVisible = false
            root.confirmPasswordVisible = false
        }
        // 회원가입 실패 처리 함수
        function onRegisterFailed(error) {
            errorDialog.title = "회원가입 실패"
            errorDialog.text = error
            errorDialog.open()
        }
        // 이메일 인증 요청 처리 함수
        function onEmailVerificationRequested(message, _debugToken) {
            root.signupEmailCodeVisible = true
            root.signupEmailStatusText = message
        }
        // 이메일 인증 요청 실패 처리 함수
        function onEmailVerificationRequestFailed(error) {
            root.signupEmailStatusText = ""
            errorDialog.title = "Email Verification Failed"
            errorDialog.text = error
            errorDialog.open()
        }
        // 이메일 인증 완료 처리 함수
        function onEmailVerificationConfirmed(message) {
            root.signupEmailVerified = true
            root.signupEmailVerifiedId = idField.text.trim()
            root.signupEmailVerifiedAddress = emailField.text.trim()
            root.signupEmailStatusText = message
        }
        // 이메일 인증 완료 실패 처리 함수
        function onEmailVerificationConfirmFailed(error) {
            root.signupEmailVerified = false
            root.signupEmailVerifiedId = ""
            root.signupEmailVerifiedAddress = ""
            errorDialog.title = "Email Verification Failed"
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

    PasswordForgotDialog {
        id: passwordForgotDialog
        theme: root.theme
        backendObject: backend
        // 재설정 요청 처리 함수
        onResetRequested: function(userId, email, debugCode, message) {
            passwordResetDialog.openDialog(userId, email, debugCode, message)
        }
    }

    PasswordResetDialog {
        id: passwordResetDialog
        theme: root.theme
        backendObject: backend
        // 재설정 완료 처리 함수
        onResetCompleted: function(message) {
            errorDialog.title = "비밀번호 재설정"
            errorDialog.text = message && message.length > 0
                             ? message
                             : "비밀번호가 재설정되었습니다. 새 비밀번호로 로그인해 주세요."
            errorDialog.open()
        }
    }

    FolderDialog {
        id: certFolderDialog
        title: "인증서 폴더 선택"
        // 입력 확정 처리 함수
        onAccepted: {
            var selectedPath = root.urlToLocalPath(selectedFolder)
            if (!selectedPath || selectedPath.length === 0)
                return

            if (backend.updateCertDirectoryPath(selectedPath)) {
                root.certDirectoryStatusIsError = false
                root.certDirectoryStatusText = "인증서 폴더를 적용했습니다."
                return
            }

            root.certDirectoryStatusIsError = true
            root.certDirectoryStatusText = "인증서 폴더를 적용하지 못했습니다."
            errorDialog.title = "인증서 경로 설정 실패"
            errorDialog.text = "선택한 폴더를 인증서 경로로 적용하지 못했습니다."
            errorDialog.open()
        }
    }

}
