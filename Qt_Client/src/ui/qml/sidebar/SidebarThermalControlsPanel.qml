import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    property var theme
    property var store
    property var thermalBackend: store ? store.backend : null

    visible: store ? store.showThermalControls : false
    Layout.preferredHeight: visible ? 500 : 0
    Layout.maximumHeight: visible ? 500 : 0
    Layout.minimumHeight: visible ? 500 : 0
    // 팔레트 선택 동기화 함수
    function syncPalette() {
        if (!thermalBackend)
            return
        var options = ["Gray", "Iron", "Jet"]
        var idx = options.indexOf(thermalBackend.thermalPalette)
        paletteCombo.currentIndex = idx >= 0 ? idx : 2
    }

    Component.onCompleted: syncPalette()

    Connections {
        target: thermalBackend ? thermalBackend : null
        // 열화상 팔레트 변경 처리 함수
        function onThermalPaletteChanged() { root.syncPalette() }
    }

    Rectangle {
        anchors.fill: parent
        clip: true
        color: theme ? theme.bgComponent : "#18181b"
        border.color: theme ? theme.border : "#27272a"
        border.width: 1
        radius: 8

        Rectangle {
            anchors.fill: parent
            anchors.margins: 10
            color: theme ? theme.bgSecondary : "#0f172a"
            radius: 8
            border.color: theme ? theme.border : "#27272a"
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true

                    Text {
                        text: "Thermal Controls"
                        color: theme ? theme.textPrimary : "white"
                        font.bold: true
                        font.pixelSize: 14
                    }

                    Item { Layout.fillWidth: true }

                    Button {
                        text: thermalBackend && thermalBackend.thermalStreaming ? "Stop" : "Start"
                        enabled: !!thermalBackend
                        Layout.preferredHeight: 30
                        Layout.preferredWidth: 52
                        scale: down ? 0.97 : 1.0
                        Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }
                        background: Rectangle {
                            color: parent.down ? (theme ? theme.border : "#27272a") : "transparent"
                            radius: 4
                            border.color: theme ? theme.border : "#27272a"
                        }
                        contentItem: Text {
                            text: parent.text
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 12
                            anchors.centerIn: parent
                        }
                        // 클릭 이벤트 처리 함수
                        onClicked: {
                            if (!thermalBackend)
                                return
                            if (thermalBackend.thermalStreaming)
                                thermalBackend.stopThermalStream()
                            else
                                thermalBackend.startThermalStream()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: "Start/Stop thermal stream"
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

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
                        implicitHeight: 34
                        model: ["Gray", "Iron", "Jet"]
                        leftPadding: 10
                        rightPadding: 26
                        font.pixelSize: 13

                        contentItem: Text {
                            text: paletteCombo.displayText
                            color: theme ? theme.textPrimary : "white"
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: paletteCombo.leftPadding
                            rightPadding: paletteCombo.rightPadding
                            font: paletteCombo.font
                        }

                        background: Rectangle {
                            radius: 4
                            color: theme ? theme.bgComponent : "#18181b"
                            border.width: 1
                            border.color: paletteCombo.activeFocus
                                          ? (theme ? theme.accent : "#f97316")
                                          : (theme ? theme.border : "#27272a")
                        }

                        indicator: Text {
                            text: "\u25BE"
                            color: theme ? theme.textSecondary : "#a1a1aa"
                            font.pixelSize: 12
                            anchors.right: parent.right
                            anchors.rightMargin: 9
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        // 활성화 처리 함수
                        onActivated: {
                            if (thermalBackend)
                                thermalBackend.setThermalPalette(currentText)
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    Text {
                        text: "Auto Range"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        Layout.preferredWidth: 72
                    }

                    Item { Layout.fillWidth: true }

                    Switch {
                        id: autoRangeSwitch
                        checked: thermalBackend ? thermalBackend.thermalAutoRange : true
                        enabled: !!thermalBackend
                        leftPadding: 0
                        rightPadding: 0
                        topPadding: 0
                        bottomPadding: 0
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

                        indicator: Rectangle {
                            implicitWidth: 42
                            implicitHeight: 22
                            radius: 11
                            color: autoRangeSwitch.checked ? (theme ? theme.accent + "66" : "#33f97316") : "#3f3f46"
                            border.width: 1
                            border.color: autoRangeSwitch.checked ? (theme ? theme.accent : "#f97316") : "#52525b"

                            Rectangle {
                                width: 18
                                height: 18
                                radius: 9
                                y: 2
                                x: autoRangeSwitch.checked ? parent.width - width - 2 : 2
                                color: "#e4e4e7"
                                border.width: 1
                                border.color: "#a1a1aa"
                                Behavior on x { NumberAnimation { duration: 120 } }
                            }
                        }
                        // 토글 처리 함수
                        onToggled: {
                            if (thermalBackend)
                                thermalBackend.setThermalAutoRange(checked)
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: thermalBackend ? thermalBackend.thermalAutoRange : false

                    Text {
                        text: "Auto Window: " + (thermalBackend ? thermalBackend.thermalAutoRangeWindowPercent : 0) + "%"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                    }

                    Slider {
                        id: autoWindowSlider
                        Layout.fillWidth: true
                        implicitHeight: 22
                        from: 50
                        to: 100
                        stepSize: 1
                        enabled: thermalBackend ? thermalBackend.thermalAutoRange : false
                        value: thermalBackend ? thermalBackend.thermalAutoRangeWindowPercent : 96
                        // 이동 처리 함수
                        onMoved: {
                            if (thermalBackend)
                                thermalBackend.setThermalAutoRangeWindowPercent(Math.round(value))
                        }

                        background: Rectangle {
                            x: autoWindowSlider.leftPadding
                            y: autoWindowSlider.topPadding + autoWindowSlider.availableHeight / 2 - height / 2
                            width: autoWindowSlider.availableWidth
                            height: 4
                            radius: 2
                            color: "#3f3f46"
                            Rectangle {
                                width: autoWindowSlider.visualPosition * parent.width
                                height: parent.height
                                color: theme ? theme.accent : "#f97316"
                                radius: 2
                            }
                        }

                        handle: Rectangle {
                            x: autoWindowSlider.leftPadding + autoWindowSlider.visualPosition * (autoWindowSlider.availableWidth - width)
                            y: autoWindowSlider.topPadding + autoWindowSlider.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: "#e4e4e7"
                            border.width: 1
                            border.color: "#a1a1aa"
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                    Layout.minimumHeight: 8
                    visible: thermalBackend ? thermalBackend.thermalAutoRange : false
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "Min: " + (thermalBackend ? thermalBackend.thermalManualMin : 0)
                        color: theme ? theme.textSecondary : "#a1a1aa"
                    }

                    Slider {
                        id: minSlider
                        Layout.fillWidth: true
                        implicitHeight: 22
                        from: 0
                        to: 30000
                        stepSize: 10
                        enabled: thermalBackend ? !thermalBackend.thermalAutoRange : false
                        value: thermalBackend ? thermalBackend.thermalManualMin : 7000
                        // 이동 처리 함수
                        onMoved: {
                            if (thermalBackend)
                                thermalBackend.setThermalManualRange(value, maxSlider.value)
                        }

                        background: Rectangle {
                            x: minSlider.leftPadding
                            y: minSlider.topPadding + minSlider.availableHeight / 2 - height / 2
                            width: minSlider.availableWidth
                            height: 4
                            radius: 2
                            color: "#3f3f46"
                            Rectangle {
                                width: minSlider.visualPosition * parent.width
                                height: parent.height
                                color: theme ? theme.accent : "#f97316"
                                radius: 2
                            }
                        }

                        handle: Rectangle {
                            x: minSlider.leftPadding + minSlider.visualPosition * (minSlider.availableWidth - width)
                            y: minSlider.topPadding + minSlider.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: "#e4e4e7"
                            border.width: 1
                            border.color: "#a1a1aa"
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    Text {
                        text: "Max: " + (thermalBackend ? thermalBackend.thermalManualMax : 0)
                        color: theme ? theme.textSecondary : "#a1a1aa"
                    }

                    Slider {
                        id: maxSlider
                        Layout.fillWidth: true
                        implicitHeight: 22
                        from: 100
                        to: 35000
                        stepSize: 10
                        enabled: thermalBackend ? !thermalBackend.thermalAutoRange : false
                        value: thermalBackend ? thermalBackend.thermalManualMax : 10000
                        // 이동 처리 함수
                        onMoved: {
                            if (thermalBackend)
                                thermalBackend.setThermalManualRange(minSlider.value, value)
                        }

                        background: Rectangle {
                            x: maxSlider.leftPadding
                            y: maxSlider.topPadding + maxSlider.availableHeight / 2 - height / 2
                            width: maxSlider.availableWidth
                            height: 4
                            radius: 2
                            color: "#3f3f46"
                            Rectangle {
                                width: maxSlider.visualPosition * parent.width
                                height: parent.height
                                color: theme ? theme.accent : "#f97316"
                                radius: 2
                            }
                        }

                        handle: Rectangle {
                            x: maxSlider.leftPadding + maxSlider.visualPosition * (maxSlider.availableWidth - width)
                            y: maxSlider.topPadding + maxSlider.availableHeight / 2 - height / 2
                            implicitWidth: 16
                            implicitHeight: 16
                            radius: 8
                            color: "#e4e4e7"
                            border.width: 1
                            border.color: "#a1a1aa"
                        }
                    }
                }

                Item { Layout.fillHeight: true; Layout.minimumHeight: 8 }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    radius: 6
                    color: "transparent"
                    border.color: theme ? theme.border : "#27272a"
                    border.width: 1

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        text: "Emissivity/Ambient (TBD)"
                        color: theme ? theme.textSecondary : "#a1a1aa"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
