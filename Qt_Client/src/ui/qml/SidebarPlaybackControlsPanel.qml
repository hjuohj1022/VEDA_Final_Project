import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "components" as C

Item {
    id: root
    property var theme
    property var store

    visible: store ? store.showPlaybackControls : false
    Layout.preferredHeight: visible ? 500 : 0
    Layout.maximumHeight: visible ? 500 : 0
    Layout.minimumHeight: visible ? 500 : 0

    function syncTimeFields() {
        if (!store)
            return
        var parts = String(store.playbackTimeText || "").split(":")
        if (parts.length !== 3)
            return
        playbackHourField.text = parts[0]
        playbackMinuteField.text = parts[1]
        playbackSecondField.text = parts[2]
    }

    function syncExportStartFields() {
        if (!store)
            return
        var parts = String(store.playbackExportStartText || "").split(":")
        if (parts.length !== 3)
            return
        playbackExportStartHourField.text = parts[0]
        playbackExportStartMinuteField.text = parts[1]
        playbackExportStartSecondField.text = parts[2]
    }

    function syncExportEndFields() {
        if (!store)
            return
        var parts = String(store.playbackExportEndText || "").split(":")
        if (parts.length !== 3)
            return
        playbackExportEndHourField.text = parts[0]
        playbackExportEndMinuteField.text = parts[1]
        playbackExportEndSecondField.text = parts[2]
    }

    Component.onCompleted: {
        if (!store)
            return
        playbackDateTextField.text = store.playbackDateText
        syncTimeFields()
        syncExportStartFields()
        syncExportEndFields()
    }

    Connections {
        target: store
        function onPlaybackDateTextChanged() {
            if (playbackDateTextField.text !== store.playbackDateText)
                playbackDateTextField.text = store.playbackDateText
        }
        function onPlaybackTimeTextChanged() { syncTimeFields() }
        function onPlaybackExportStartTextChanged() { syncExportStartFields() }
        function onPlaybackExportEndTextChanged() { syncExportEndFields() }
    }

    Rectangle {
        anchors.fill: parent
        clip: true
        color: theme ? theme.bgComponent : "#18181b"
        border.color: theme ? theme.border : "#27272a"
        border.width: 1
        radius: 8

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "CH"
                    color: theme ? theme.textPrimary : "white"
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignVCenter
                }

                Repeater {
                    model: 4
                    delegate: Button {
                        id: channelBtn
                        required property int index
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 30
                        text: String(index + 1)
                        hoverEnabled: enabled

                        contentItem: Text {
                            text: parent.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: (store.playbackChannelIndex === index) ? "white" : (theme ? theme.textPrimary : "white")
                            font.pixelSize: 12
                            font.bold: (store.playbackChannelIndex === index)
                        }

                        background: Rectangle {
                            radius: 4
                            border.width: 1
                            border.color: (store.playbackChannelIndex === index) ? (theme ? theme.accent : "#f97316") : (theme ? theme.border : "#52525b")
                            color: (store.playbackChannelIndex === index) ? (channelBtn.down ? "#ea580c" : (theme ? theme.accent : "#f97316")) : (channelBtn.down ? (theme ? theme.bgSecondary : "#09090b") : (theme ? theme.bgComponent : "#18181b"))
                        }

                        onClicked: store.selectPlaybackChannel(index)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                TextField {
                    id: playbackDateTextField
                    Layout.fillWidth: true
                    enabled: !store.playbackRunning && !store.playbackPending
                    inputMask: "0000-00-00;_"
                    placeholderText: "YYYY-MM-DD"
                    color: theme ? theme.textPrimary : "white"
                    placeholderTextColor: theme ? theme.textSecondary : "#71717a"
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: playbackDateTextField.activeFocus ? theme.accent : theme.border
                        border.width: 1
                        radius: 6
                    }
                    onEditingFinished: {
                        store.playbackDateText = text
                        if (!store.syncDateFromField()) {
                            text = store.playbackDateText
                            return
                        }
                        store.playbackCalendarVisible = false
                        store.requestTimelineIfValid()
                    }
                }

                C.SidebarControlButton {
                    text: "날짜"
                    compact: true
                    theme: root.theme
                    Layout.preferredWidth: 50
                    enabled: !store.playbackRunning && !store.playbackPending
                    onClicked: store.playbackCalendarVisible = !store.playbackCalendarVisible
                }

                C.SidebarControlButton {
                    text: "새로고침"
                    compact: true
                    theme: root.theme
                    Layout.preferredWidth: 58
                    onClicked: {
                        store.requestTimelineIfValid()
                        store.requestMonthDays()
                    }
                }
            }

            Rectangle {
                visible: store.playbackCalendarVisible
                Layout.fillWidth: true
                Layout.preferredHeight: 190
                color: theme ? theme.bgSecondary : "#09090b"
                border.color: theme ? theme.border : "#27272a"
                border.width: 1
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true

                        C.SidebarControlButton {
                            text: "<"
                            compact: true
                            theme: root.theme
                            Layout.preferredWidth: 34
                            onClicked: {
                                if (store.playbackViewMonth === 0) {
                                    store.playbackViewMonth = 11
                                    store.playbackViewYear -= 1
                                } else {
                                    store.playbackViewMonth -= 1
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            text: store.monthLabel()
                            color: theme ? theme.textPrimary : "white"
                            font.bold: true
                        }

                        C.SidebarControlButton {
                            text: ">"
                            compact: true
                            theme: root.theme
                            Layout.preferredWidth: 34
                            onClicked: {
                                if (store.playbackViewMonth === 11) {
                                    store.playbackViewMonth = 0
                                    store.playbackViewYear += 1
                                } else {
                                    store.playbackViewMonth += 1
                                }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 7
                        rowSpacing: 4
                        columnSpacing: 4

                        Repeater {
                            model: ["일", "월", "화", "수", "목", "금", "토"]
                            delegate: Label {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignHCenter
                                text: modelData
                                color: theme ? theme.textSecondary : "#a1a1aa"
                                font.pixelSize: 11
                            }
                        }

                        Repeater {
                            model: 42
                            delegate: Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 20
                                radius: 4
                                color: {
                                    var first = store.firstDayOffset()
                                    var d = index - first + 1
                                    if (d < 1 || d > store.daysInViewMonth()) return "transparent"
                                    var selected = (store.playbackSelectedDate.getFullYear() === store.playbackViewYear && store.playbackSelectedDate.getMonth() === store.playbackViewMonth && store.playbackSelectedDate.getDate() === d)
                                    return selected ? theme.accent : "transparent"
                                }

                                Text {
                                    anchors.centerIn: parent
                                    text: {
                                        var first = store.firstDayOffset()
                                        var d = index - first + 1
                                        if (d < 1 || d > store.daysInViewMonth()) return ""
                                        return d
                                    }
                                    color: {
                                        var first = store.firstDayOffset()
                                        var d = index - first + 1
                                        if (d < 1 || d > store.daysInViewMonth()) return (theme ? theme.textPrimary : "white")
                                        var selected = (store.playbackSelectedDate.getFullYear() === store.playbackViewYear && store.playbackSelectedDate.getMonth() === store.playbackViewMonth && store.playbackSelectedDate.getDate() === d)
                                        if (selected) return "white"
                                        if (store.isRecordedDay(d)) return "#f97316"
                                        return (theme ? theme.textPrimary : "white")
                                    }
                                    font.pixelSize: 11
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    enabled: {
                                        var first = store.firstDayOffset()
                                        var d = index - first + 1
                                        return d >= 1 && d <= store.daysInViewMonth()
                                    }
                                    onClicked: {
                                        var first = store.firstDayOffset()
                                        var d = index - first + 1
                                        store.playbackSelectedDate = new Date(store.playbackViewYear, store.playbackViewMonth, d)
                                        store.playbackDateText = store.formatPlaybackDate()
                                        store.playbackCalendarVisible = false
                                        store.requestTimelineIfValid()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 4

                TextField {
                    id: playbackHourField
                    Layout.fillWidth: true
                    enabled: !store.playbackRunning && !store.playbackPending
                    inputMask: "00;_"
                    placeholderText: "시"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: playbackHourField.activeFocus ? theme.accent : theme.border
                        border.width: 1
                        radius: 6
                    }
                    onEditingFinished: {
                        var hh = parseInt(playbackHourField.text, 10)
                        var mm = parseInt(playbackMinuteField.text, 10)
                        var ss = parseInt(playbackSecondField.text, 10)
                        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
                            playbackHourField.text = store.pad2(Math.floor((store.playbackCurrentSeconds >= 0 ? store.playbackCurrentSeconds : 0) / 3600))
                            return
                        }
                        store.playbackTimeText = store.pad2(hh) + ":" + store.pad2(mm) + ":" + store.pad2(ss)
                        store.syncSecondsFromFields()
                    }
                }

                Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }

                TextField {
                    id: playbackMinuteField
                    Layout.fillWidth: true
                    enabled: !store.playbackRunning && !store.playbackPending
                    inputMask: "00;_"
                    placeholderText: "분"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: playbackMinuteField.activeFocus ? theme.accent : theme.border
                        border.width: 1
                        radius: 6
                    }
                    onEditingFinished: {
                        var hh = parseInt(playbackHourField.text, 10)
                        var mm = parseInt(playbackMinuteField.text, 10)
                        var ss = parseInt(playbackSecondField.text, 10)
                        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
                            playbackMinuteField.text = store.pad2(Math.floor(((store.playbackCurrentSeconds >= 0 ? store.playbackCurrentSeconds : 0) % 3600) / 60))
                            return
                        }
                        store.playbackTimeText = store.pad2(hh) + ":" + store.pad2(mm) + ":" + store.pad2(ss)
                        store.syncSecondsFromFields()
                    }
                }

                Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }

                TextField {
                    id: playbackSecondField
                    Layout.fillWidth: true
                    enabled: !store.playbackRunning && !store.playbackPending
                    inputMask: "00;_"
                    placeholderText: "초"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle {
                        color: theme ? theme.bgSecondary : "#09090b"
                        border.color: playbackSecondField.activeFocus ? theme.accent : theme.border
                        border.width: 1
                        radius: 6
                    }
                    onEditingFinished: {
                        var hh = parseInt(playbackHourField.text, 10)
                        var mm = parseInt(playbackMinuteField.text, 10)
                        var ss = parseInt(playbackSecondField.text, 10)
                        if (isNaN(hh) || isNaN(mm) || isNaN(ss) || hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59) {
                            playbackSecondField.text = store.pad2((store.playbackCurrentSeconds >= 0 ? store.playbackCurrentSeconds : 0) % 60)
                            return
                        }
                        store.playbackTimeText = store.pad2(hh) + ":" + store.pad2(mm) + ":" + store.pad2(ss)
                        store.syncSecondsFromFields()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                C.SidebarControlButton {
                    text: "Play"
                    Layout.fillWidth: true
                    accentStyle: true
                    theme: root.theme
                    enabled: !store.playbackRunning && !store.playbackPending && store.playbackTimeInRange
                    onClicked: {
                        if (!store.syncDateFromField() || !store.syncSecondsFromFields())
                            return
                        if (backend.playbackWsPlay()) {
                            store.playbackRunning = true
                            store.playbackPending = false
                            store.requestPlaybackResume()
                        } else {
                            var d = store.formatPlaybackDate()
                            var t = store.formatPlaybackTime()
                            store.playbackPending = true
                            store.requestPlayback(store.playbackChannelIndex, d, t)
                        }
                    }
                }

                C.SidebarControlButton {
                    text: "Pause"
                    Layout.fillWidth: true
                    theme: root.theme
                    enabled: store.playbackRunning && !store.playbackPending
                    onClicked: {
                        store.playbackRunning = false
                        store.playbackPending = false
                        store.requestPlaybackPause()
                        backend.playbackWsPause()
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: store.playbackTimelineInfoText
                color: store.playbackTimeInRange ? (theme ? theme.textSecondary : "#a1a1aa") : "#f59e0b"
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 11
            }

            Label {
                Layout.fillWidth: true
                text: store.formatPlaybackDate() + " " + store.formatPlaybackTime()
                color: theme ? theme.textSecondary : "#a1a1aa"
                horizontalAlignment: Text.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: theme ? theme.border : "#27272a"
                opacity: 0.9
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label { text: "시작"; color: theme ? theme.textSecondary : "#a1a1aa"; font.pixelSize: 11; Layout.preferredWidth: 30 }

                TextField {
                    id: playbackExportStartHourField
                    Layout.fillWidth: true
                    enabled: !store.playbackPending
                    inputMask: "00;_"
                    text: "00"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: playbackExportStartHourField.activeFocus ? theme.accent : theme.border; border.width: 1; radius: 6 }
                    onEditingFinished: {
                        if (!store.syncExportStartFromFields(parseInt(playbackExportStartHourField.text, 10), parseInt(playbackExportStartMinuteField.text, 10), parseInt(playbackExportStartSecondField.text, 10))) {
                            var p = store.playbackExportStartText.split(":")
                            if (p.length === 3) text = p[0]
                        }
                    }
                }
                Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                TextField {
                    id: playbackExportStartMinuteField
                    Layout.fillWidth: true
                    enabled: !store.playbackPending
                    inputMask: "00;_"
                    text: "00"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: playbackExportStartMinuteField.activeFocus ? theme.accent : theme.border; border.width: 1; radius: 6 }
                    onEditingFinished: {
                        if (!store.syncExportStartFromFields(parseInt(playbackExportStartHourField.text, 10), parseInt(playbackExportStartMinuteField.text, 10), parseInt(playbackExportStartSecondField.text, 10))) {
                            var p = store.playbackExportStartText.split(":")
                            if (p.length === 3) text = p[1]
                        }
                    }
                }
                Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                TextField {
                    id: playbackExportStartSecondField
                    Layout.fillWidth: true
                    enabled: !store.playbackPending
                    inputMask: "00;_"
                    text: "00"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: playbackExportStartSecondField.activeFocus ? theme.accent : theme.border; border.width: 1; radius: 6 }
                    onEditingFinished: {
                        if (!store.syncExportStartFromFields(parseInt(playbackExportStartHourField.text, 10), parseInt(playbackExportStartMinuteField.text, 10), parseInt(playbackExportStartSecondField.text, 10))) {
                            var p = store.playbackExportStartText.split(":")
                            if (p.length === 3) text = p[2]
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Label { text: "종료"; color: theme ? theme.textSecondary : "#a1a1aa"; font.pixelSize: 11; Layout.preferredWidth: 30 }

                TextField {
                    id: playbackExportEndHourField
                    Layout.fillWidth: true
                    enabled: !store.playbackPending
                    inputMask: "00;_"
                    text: "00"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: playbackExportEndHourField.activeFocus ? theme.accent : theme.border; border.width: 1; radius: 6 }
                    onEditingFinished: {
                        if (!store.syncExportEndFromFields(parseInt(playbackExportEndHourField.text, 10), parseInt(playbackExportEndMinuteField.text, 10), parseInt(playbackExportEndSecondField.text, 10))) {
                            var p = store.playbackExportEndText.split(":")
                            if (p.length === 3) text = p[0]
                        }
                    }
                }
                Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                TextField {
                    id: playbackExportEndMinuteField
                    Layout.fillWidth: true
                    enabled: !store.playbackPending
                    inputMask: "00;_"
                    text: "00"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: playbackExportEndMinuteField.activeFocus ? theme.accent : theme.border; border.width: 1; radius: 6 }
                    onEditingFinished: {
                        if (!store.syncExportEndFromFields(parseInt(playbackExportEndHourField.text, 10), parseInt(playbackExportEndMinuteField.text, 10), parseInt(playbackExportEndSecondField.text, 10))) {
                            var p = store.playbackExportEndText.split(":")
                            if (p.length === 3) text = p[1]
                        }
                    }
                }
                Label { text: ":"; color: theme ? theme.textPrimary : "white"; font.bold: true }
                TextField {
                    id: playbackExportEndSecondField
                    Layout.fillWidth: true
                    enabled: !store.playbackPending
                    inputMask: "00;_"
                    text: "00"
                    color: theme ? theme.textPrimary : "white"
                    horizontalAlignment: Text.AlignHCenter
                    background: Rectangle { color: theme ? theme.bgSecondary : "#09090b"; border.color: playbackExportEndSecondField.activeFocus ? theme.accent : theme.border; border.width: 1; radius: 6 }
                    onEditingFinished: {
                        if (!store.syncExportEndFromFields(parseInt(playbackExportEndHourField.text, 10), parseInt(playbackExportEndMinuteField.text, 10), parseInt(playbackExportEndSecondField.text, 10))) {
                            var p = store.playbackExportEndText.split(":")
                            if (p.length === 3) text = p[2]
                        }
                    }
                }
            }

            C.SidebarControlButton {
                text: "내보내기"
                Layout.fillWidth: true
                compact: true
                theme: root.theme
                enabled: !store.playbackPending
                         && /^\d{4}-\d{2}-\d{2}$/.test(store.playbackDateText)
                         && store.isValidHmsText(store.playbackExportStartText)
                         && store.isValidHmsText(store.playbackExportEndText)
                         && (store.playbackExportStartText <= store.playbackExportEndText)
                onClicked: {
                    if (!store.syncDateFromField()) return
                    if (!store.isValidHmsText(store.playbackExportStartText) || !store.isValidHmsText(store.playbackExportEndText)) return
                    if (store.playbackExportStartText > store.playbackExportEndText) return
                    store.requestPlaybackExport(store.playbackChannelIndex,
                                                store.formatPlaybackDate(),
                                                store.playbackExportStartText,
                                                store.playbackExportEndText)
                }
            }
        }
    }
}
