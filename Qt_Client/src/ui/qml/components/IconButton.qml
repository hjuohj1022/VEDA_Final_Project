import QtQuick
import QtQuick.Controls

Rectangle {
    id: iconBtn

    property var theme
    property string label: ""
    property string iconSource: ""
    property int iconSize: 16
    property color iconBackdropColor: "transparent"
    property color iconBackdropBorderColor: "transparent"
    property color fg: theme ? theme.textSecondary : "#a1a1aa"
    property bool enabledButton: true
    property string tooltipText: ""

    signal clicked()

    width: 32
    height: 32
    radius: 9
    color: !enabledButton
           ? "transparent"
           : mouse.pressed
           ? (theme ? theme.border : "#27272a")
           : (mouse.containsMouse ? (theme ? theme.bgComponent : "#18181b") : "transparent")
    border.color: (enabledButton && mouse.containsMouse) ? (theme ? theme.border : "#27272a") : "transparent"
    border.width: 1
    opacity: enabledButton ? 1.0 : 0.35
    scale: (enabledButton && mouse.pressed) ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: 80; easing.type: Easing.OutQuad } }

    Rectangle {
        anchors.centerIn: parent
        width: iconBtn.iconSize + 6
        height: iconBtn.iconSize + 6
        radius: 4
        color: iconBtn.iconBackdropColor
        visible: iconBtn.iconSource.length > 0 && iconBtn.iconBackdropColor.a > 0
        border.color: iconBtn.iconBackdropBorderColor
        border.width: iconBtn.iconBackdropBorderColor.a > 0 ? 1 : 0
    }

    Image {
        anchors.centerIn: parent
        visible: iconBtn.iconSource.length > 0
        source: iconBtn.iconSource
        width: iconBtn.iconSize
        height: iconBtn.iconSize
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
    }

    Text {
        anchors.centerIn: parent
        visible: iconBtn.iconSource.length === 0
        text: iconBtn.label
        color: iconBtn.fg
        font.family: "Segoe MDL2 Assets"
        font.pixelSize: 13
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: iconBtn.enabledButton
        cursorShape: iconBtn.enabledButton ? Qt.PointingHandCursor : Qt.ArrowCursor
        // 클릭 이벤트 처리 함수
        onClicked: {
            backend.resetSessionTimer()
            iconBtn.clicked()
        }
    }

    ToolTip.visible: tooltipText.length > 0 && mouse.containsMouse
    ToolTip.delay: 350
    ToolTip.timeout: 2000
    ToolTip.text: tooltipText
}
