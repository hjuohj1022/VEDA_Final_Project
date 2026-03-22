import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components" as C

Item {
    id: root
    property var theme
    property var store
    property bool active: true

    visible: store ? (active && store.showCameraControls) : false
    Layout.fillHeight: visible
    Layout.preferredHeight: visible ? 1 : 0

    function syncCameraName() {
        if (!store)
            return
        if (store.selectedCameraIndex >= 0 && store.selectedCameraIndex < store.cameraNames.length) {
            cameraNameField.text = store.cameraNames[store.selectedCameraIndex]
        } else {
            cameraNameField.text = ""
        }
    }

    function syncDisplayFields() {
        contrastValueField.text = String(store.displayContrast)
        brightnessValueField.text = String(store.displayBrightness)
        sharpnessValueField.text = String(store.displaySharpnessLevel)
        colorValueField.text = String(store.displayColorLevel)
    }

    Component.onCompleted: {
        syncCameraName()
        syncDisplayFields()
    }

    Connections {
        target: store
        function onSelectedCameraIndexChanged() { syncCameraName() }
        function onShowCameraControlsChanged() {
            if (store.showCameraControls)
                syncCameraName()
        }
        function onDisplayContrastChanged() { contrastValueField.text = String(store.displayContrast) }
        function onDisplayBrightnessChanged() { brightnessValueField.text = String(store.displayBrightness) }
        function onDisplaySharpnessLevelChanged() { sharpnessValueField.text = String(store.displaySharpnessLevel) }
        function onDisplayColorLevelChanged() { colorValueField.text = String(store.displayColorLevel) }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8
            implicitHeight: topControlCardContent.implicitHeight + 20

            ColumnLayout {
                id: topControlCardContent
                anchors.fill: parent
                anchors.margins: 10
                spacing: 4

                Text {
                    text: store.selectedCameraTitle()
                    color: theme ? theme.textPrimary : "white"
                    font.bold: true
                    font.pixelSize: 13
                }
                Text {
                    text: "확대 화면 제어 패널"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 11
                }

                Text {
                    text: "휠 업/다운: 줌 인/아웃"
                    color: theme ? theme.textSecondary : "#71717a"
                    font.pixelSize: 10
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    TextField {
                        id: cameraNameField
                        Layout.fillWidth: true
                        placeholderText: "위치 이름"
                        color: theme ? theme.textPrimary : "white"
                        placeholderTextColor: theme ? theme.textSecondary : "#71717a"
                        enabled: store.selectedCameraIndex >= 0
                        background: Rectangle {
                            color: theme ? theme.bgSecondary : "#09090b"
                            border.color: cameraNameField.activeFocus ? theme.accent : theme.border
                            border.width: 1
                            radius: 6
                        }
                        onAccepted: {
                            if (store.selectedCameraIndex < 0)
                                return
                            var trimmed = text.trim()
                            if (trimmed.length === 0)
                                return
                            store.requestCameraNameChange(store.selectedCameraIndex, trimmed)
                            store.cameraControlStatus = "카메라 이름이 변경되었습니다."
                            store.cameraControlError = false
                            store.restartCameraControlStatusTimer()
                        }
                    }

                    C.SidebarControlButton {
                        text: "저장"
                        compact: true
                        theme: root.theme
                        Layout.preferredWidth: 56
                        enabled: store.selectedCameraIndex >= 0
                        onClicked: {
                            if (store.selectedCameraIndex < 0)
                                return
                            var trimmed = cameraNameField.text.trim()
                            if (trimmed.length === 0)
                                return
                            store.requestCameraNameChange(store.selectedCameraIndex, trimmed)
                            store.cameraControlStatus = "카메라 이름이 변경되었습니다."
                            store.cameraControlError = false
                            store.restartCameraControlStatusTimer()
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    C.SidebarControlButton {
                        text: "줌- + 오토포커스"
                        compact: true
                        theme: root.theme
                        Layout.fillWidth: true
                        enabled: store.selectedCameraIndex >= 0 && store.supportZoom && store.supportFocus
                        onClicked: {
                            if (store.selectedCameraIndex < 0)
                                return
                            var prepared = backend.startCctv3dMapPrepareSequence(store.selectedCameraIndex)
                            if (prepared) {
                                store.cameraControlStatus = "줌-/오토포커스 준비 시퀀스를 시작했습니다."
                                store.cameraControlError = false
                                store.restartCameraControlStatusTimer()
                            }
                        }
                    }

                    C.SidebarControlButton {
                        text: store.mapModeEnabled ? "3D Map 모드 ON" : "3D Map 모드 OFF"
                        compact: true
                        accentStyle: store.mapModeEnabled
                        theme: root.theme
                        Layout.fillWidth: true
                        enabled: store.selectedCameraIndex >= 0
                        onClicked: {
                            if (store.selectedCameraIndex < 0)
                                return

                            if (!store.mapModeEnabled) {
                                var started = backend.startCctv3dMapSequence(store.selectedCameraIndex)
                                store.mapModeEnabled = started
                                if (started) {
                                    store.cameraControlStatus = "3D Map 시작 요청을 보냈습니다."
                                    store.cameraControlError = false
                                    store.restartCameraControlStatusTimer()
                                }
                            } else {
                                backend.stopCctv3dMapSequence()
                                store.mapModeEnabled = false
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: store.mapModeEnabled

                    C.SidebarControlButton {
                        text: "3D Map 일시정지"
                        compact: true
                        theme: root.theme
                        Layout.fillWidth: true
                        enabled: store.selectedCameraIndex >= 0 && store.mapModeEnabled
                        onClicked: {
                            if (backend.pauseCctv3dMapSequence()) {
                                store.cameraControlStatus = "3D Map pause 요청을 보냈습니다."
                                store.cameraControlError = false
                                store.restartCameraControlStatusTimer()
                            }
                        }
                    }

                    C.SidebarControlButton {
                        text: "3D Map 재개"
                        compact: true
                        theme: root.theme
                        Layout.fillWidth: true
                        enabled: store.selectedCameraIndex >= 0 && store.mapModeEnabled
                        onClicked: {
                            if (backend.resumeCctv3dMapSequence()) {
                                store.cameraControlStatus = "3D Map resume 요청을 보냈습니다."
                                store.cameraControlError = false
                                store.restartCameraControlStatusTimer()
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 360
            color: theme ? theme.bgComponent : "#18181b"
            border.color: theme ? theme.border : "#27272a"
            border.width: 1
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    C.SidebarControlButton {
                        text: "줌 +"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportZoom
                        onClicked: backend.sunapiZoomIn(store.selectedCameraIndex)
                    }
                    C.SidebarControlButton {
                        text: "줌 -"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportZoom
                        onClicked: backend.sunapiZoomOut(store.selectedCameraIndex)
                    }
                    C.SidebarControlButton {
                        text: "줌 정지"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportZoom
                        onClicked: backend.sunapiZoomStop(store.selectedCameraIndex)
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: theme ? theme.border : "#27272a" }

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 6
                    rowSpacing: 6

                    C.SidebarControlButton {
                        text: "포커스 Near"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportFocus
                        onClicked: backend.sunapiFocusNear(store.selectedCameraIndex)
                    }
                    C.SidebarControlButton {
                        text: "포커스 Far"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportFocus
                        onClicked: backend.sunapiFocusFar(store.selectedCameraIndex)
                    }
                    C.SidebarControlButton {
                        text: "포커스 정지"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportFocus
                        onClicked: backend.sunapiFocusStop(store.selectedCameraIndex)
                    }
                    C.SidebarControlButton {
                        text: "오토포커스"
                        Layout.fillWidth: true
                        compact: true
                        theme: root.theme
                        enabled: store.selectedCameraIndex >= 0 && store.supportFocus
                        onClicked: backend.sunapiSimpleAutoFocus(store.selectedCameraIndex)
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: theme ? theme.border : "#27272a" }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    spacing: 4
                    Label {
                        text: "표시"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Item { Layout.fillWidth: true }
                    C.SidebarControlButton {
                        text: "초기화"
                        compact: true
                        theme: root.theme
                        Layout.preferredHeight: 22
                        Layout.maximumHeight: 22
                        enabled: store.selectedCameraIndex >= 0
                        onClicked: backend.sunapiResetDisplaySettings(store.selectedCameraIndex)
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: -8
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label { text: "대비"; color: theme ? theme.textPrimary : "white"; Layout.preferredWidth: 34; font.pixelSize: 11 }
                        C.SidebarControlButton {
                            text: "-"
                            compact: true
                            theme: root.theme
                            Layout.preferredWidth: 16
                            enabled: store.selectedCameraIndex >= 0
                            onClicked: {
                                store.displayContrast = Math.max(1, store.displayContrast - 1)
                                store.applyDisplaySettings()
                            }
                        }
                        C.SidebarDisplaySlider {
                            id: contrastSlider
                            Layout.fillWidth: true
                            theme: root.theme
                            from: 1
                            to: 100
                            stepSize: 1
                            value: store.displayContrast
                            property bool dragged: false
                            onMoved: {
                                store.displayContrast = Math.round(value)
                                contrastValueField.text = String(store.displayContrast)
                            }
                            onPressedChanged: {
                                if (pressed) dragged = true
                                else if (dragged) { dragged = false; store.applyDisplaySettings() }
                            }
                        }
                        C.SidebarControlButton {
                            text: "+"
                            compact: true
                            theme: root.theme
                            Layout.preferredWidth: 16
                            enabled: store.selectedCameraIndex >= 0
                            onClicked: {
                                store.displayContrast = Math.min(100, store.displayContrast + 1)
                                store.applyDisplaySettings()
                            }
                        }
                        TextField {
                            id: contrastValueField
                            Layout.preferredWidth: 38
                            text: String(store.displayContrast)
                            horizontalAlignment: Text.AlignHCenter
                            color: theme ? theme.textPrimary : "white"
                            validator: IntValidator { bottom: 1; top: 100 }
                            background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: theme ? theme.border : "#27272a"; radius: 4 }
                            onEditingFinished: {
                                var n = parseInt(text, 10)
                                if (isNaN(n)) n = store.displayContrast
                                store.displayContrast = Math.max(1, Math.min(100, n))
                                text = String(store.displayContrast)
                                store.applyDisplaySettings()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label { text: "밝기"; color: theme ? theme.textPrimary : "white"; Layout.preferredWidth: 34; font.pixelSize: 11 }
                        C.SidebarControlButton { text: "-"; compact: true; theme: root.theme; Layout.preferredWidth: 16; enabled: store.selectedCameraIndex >= 0; onClicked: { store.displayBrightness = Math.max(1, store.displayBrightness - 1); store.applyDisplaySettings() } }
                        C.SidebarDisplaySlider {
                            id: brightnessSlider
                            Layout.fillWidth: true
                            theme: root.theme
                            from: 1
                            to: 100
                            stepSize: 1
                            value: store.displayBrightness
                            property bool dragged: false
                            onMoved: {
                                store.displayBrightness = Math.round(value)
                                brightnessValueField.text = String(store.displayBrightness)
                            }
                            onPressedChanged: {
                                if (pressed) dragged = true
                                else if (dragged) { dragged = false; store.applyDisplaySettings() }
                            }
                        }
                        C.SidebarControlButton { text: "+"; compact: true; theme: root.theme; Layout.preferredWidth: 16; enabled: store.selectedCameraIndex >= 0; onClicked: { store.displayBrightness = Math.min(100, store.displayBrightness + 1); store.applyDisplaySettings() } }
                        TextField {
                            id: brightnessValueField
                            Layout.preferredWidth: 38
                            text: String(store.displayBrightness)
                            horizontalAlignment: Text.AlignHCenter
                            color: theme ? theme.textPrimary : "white"
                            validator: IntValidator { bottom: 1; top: 100 }
                            background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: theme ? theme.border : "#27272a"; radius: 4 }
                            onEditingFinished: {
                                var n = parseInt(text, 10)
                                if (isNaN(n)) n = store.displayBrightness
                                store.displayBrightness = Math.max(1, Math.min(100, n))
                                text = String(store.displayBrightness)
                                store.applyDisplaySettings()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Row {
                            Layout.preferredWidth: sharpnessLabel.implicitWidth + sharpnessToggle.implicitWidth + 2
                            spacing: 0
                            Label {
                                id: sharpnessLabel
                                text: "윤곽 조정"
                                color: theme ? theme.textPrimary : "white"
                                font.pixelSize: 11
                            }
                            CheckBox {
                                id: sharpnessToggle
                                text: ""
                                scale: 0.55
                                padding: 0
                                leftPadding: 0
                                rightPadding: 0
                                topPadding: 0
                                bottomPadding: 0
                                checked: store.displaySharpnessEnabled
                                enabled: store.selectedCameraIndex >= 0
                                onToggled: {
                                    store.displaySharpnessEnabled = checked
                                    store.applyDisplaySettings()
                                }
                            }
                        }
                        C.SidebarControlButton { text: "-"; compact: true; theme: root.theme; Layout.preferredWidth: 16; enabled: store.selectedCameraIndex >= 0; onClicked: { store.displaySharpnessLevel = Math.max(1, store.displaySharpnessLevel - 1); store.applyDisplaySettings() } }
                        C.SidebarDisplaySlider {
                            id: sharpnessSlider
                            Layout.fillWidth: true
                            theme: root.theme
                            from: 1
                            to: 32
                            stepSize: 1
                            value: store.displaySharpnessLevel
                            property bool dragged: false
                            onMoved: {
                                store.displaySharpnessLevel = Math.round(value)
                                sharpnessValueField.text = String(store.displaySharpnessLevel)
                            }
                            onPressedChanged: {
                                if (pressed) dragged = true
                                else if (dragged) { dragged = false; store.applyDisplaySettings() }
                            }
                        }
                        C.SidebarControlButton { text: "+"; compact: true; theme: root.theme; Layout.preferredWidth: 16; enabled: store.selectedCameraIndex >= 0; onClicked: { store.displaySharpnessLevel = Math.min(32, store.displaySharpnessLevel + 1); store.applyDisplaySettings() } }
                        TextField {
                            id: sharpnessValueField
                            Layout.preferredWidth: 38
                            text: String(store.displaySharpnessLevel)
                            horizontalAlignment: Text.AlignHCenter
                            color: theme ? theme.textPrimary : "white"
                            validator: IntValidator { bottom: 1; top: 32 }
                            background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: theme ? theme.border : "#27272a"; radius: 4 }
                            onEditingFinished: {
                                var n = parseInt(text, 10)
                                if (isNaN(n)) n = store.displaySharpnessLevel
                                store.displaySharpnessLevel = Math.max(1, Math.min(32, n))
                                text = String(store.displaySharpnessLevel)
                                store.applyDisplaySettings()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Label { text: "컬러 레벨"; color: theme ? theme.textPrimary : "white"; font.pixelSize: 11; Layout.preferredWidth: 52; Layout.minimumWidth: 52 }
                        C.SidebarControlButton { text: "-"; compact: true; theme: root.theme; Layout.preferredWidth: 16; enabled: store.selectedCameraIndex >= 0; onClicked: { store.displayColorLevel = Math.max(1, store.displayColorLevel - 1); store.applyDisplaySettings() } }
                        C.SidebarDisplaySlider {
                            id: colorSlider
                            Layout.fillWidth: true
                            theme: root.theme
                            from: 1
                            to: 100
                            stepSize: 1
                            value: store.displayColorLevel
                            property bool dragged: false
                            onMoved: {
                                store.displayColorLevel = Math.round(value)
                                colorValueField.text = String(store.displayColorLevel)
                            }
                            onPressedChanged: {
                                if (pressed) dragged = true
                                else if (dragged) { dragged = false; store.applyDisplaySettings() }
                            }
                        }
                        C.SidebarControlButton { text: "+"; compact: true; theme: root.theme; Layout.preferredWidth: 16; enabled: store.selectedCameraIndex >= 0; onClicked: { store.displayColorLevel = Math.min(100, store.displayColorLevel + 1); store.applyDisplaySettings() } }
                        TextField {
                            id: colorValueField
                            Layout.preferredWidth: 38
                            text: String(store.displayColorLevel)
                            horizontalAlignment: Text.AlignHCenter
                            color: theme ? theme.textPrimary : "white"
                            validator: IntValidator { bottom: 1; top: 100 }
                            background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: theme ? theme.border : "#27272a"; radius: 4 }
                            onEditingFinished: {
                                var n = parseInt(text, 10)
                                if (isNaN(n)) n = store.displayColorLevel
                                store.displayColorLevel = Math.max(1, Math.min(100, n))
                                text = String(store.displayColorLevel)
                                store.applyDisplaySettings()
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 2
                    Layout.preferredHeight: store.cameraControlStatus.length > 0 ? 18 : 0
                    visible: store.cameraControlStatus.length > 0
                    text: store.cameraControlStatus.length > 0 ? store.cameraControlStatus : " "
                    color: store.cameraControlError ? "#ef4444" : (theme ? theme.textSecondary : "#a1a1aa")
                    font.pixelSize: 10
                    wrapMode: Text.NoWrap
                    elide: Text.ElideRight
                }
            }
        }
    }
}
