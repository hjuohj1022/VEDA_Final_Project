import QtQuick
import QtQuick.Controls

Button {
    id: controlBtn
    property var theme
    property bool accentStyle: false
    property bool compact: false

    implicitHeight: compact ? 30 : 40
    hoverEnabled: enabled
    scale: controlBtn.down ? 0.97 : 1.0

    Behavior on scale {
        NumberAnimation { duration: 90; easing.type: Easing.OutCubic }
    }

    background: Rectangle {
        radius: 6
        border.width: 1
        border.color: controlBtn.accentStyle
                      ? (controlBtn.enabled ? (controlBtn.hovered ? "#fb923c" : (theme ? theme.accent : "#f97316")) : (theme ? theme.border : "#27272a"))
                      : (controlBtn.hovered ? "#3f3f46" : (theme ? theme.border : "#27272a"))
        color: !controlBtn.enabled
               ? (theme ? theme.bgComponent : "#18181b")
               : controlBtn.down
                 ? (controlBtn.accentStyle ? "#ea580c" : (theme ? theme.bgSecondary : "#09090b"))
                 : controlBtn.accentStyle
                   ? (theme ? theme.accent : "#f97316")
                   : (theme ? theme.bgSecondary : "#09090b")
    }

    contentItem: Text {
        text: controlBtn.text
        color: !controlBtn.enabled
               ? (theme ? theme.textSecondary : "#71717a")
               : (controlBtn.accentStyle ? "white" : (theme ? theme.textPrimary : "white"))
        font.pixelSize: compact ? 12 : 13
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
