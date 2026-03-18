import QtQuick
import QtQuick.Controls

Slider {
    id: displaySlider
    property var theme
    signal stepped()

    implicitHeight: 22

    background: Rectangle {
        x: parent.leftPadding
        y: parent.topPadding + parent.availableHeight / 2 - height / 2
        width: parent.availableWidth
        height: 6
        radius: 3
        color: theme ? theme.border : "#3f3f46"
    }

    handle: Rectangle {
        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
        y: parent.topPadding + parent.availableHeight / 2 - height / 2
        implicitWidth: 8
        implicitHeight: 8
        width: 8
        height: 8
        radius: 4
        color: parent.pressed ? (theme ? theme.accent : "#f97316") : (theme ? theme.textSecondary : "#a1a1aa")
        border.width: 1
        border.color: theme ? theme.border : "#27272a"
    }
}
