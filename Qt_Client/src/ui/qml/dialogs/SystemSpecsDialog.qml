import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    property var theme
    property var specs: ({})

    modal: true
    width: 640
    height: 500
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function showWithData(data) {
        specs = data || ({})
        open()
    }

    function valueOf(key, fallbackValue) {
        if (!specs || specs[key] === undefined || specs[key] === null)
            return fallbackValue
        var txt = String(specs[key])
        return txt.length > 0 ? txt : fallbackValue
    }

    component SpecRow: RowLayout {
        property string rowLabel: ""
        property string rowValue: "-"
        Layout.fillWidth: true
        spacing: 12

        Text {
            Layout.preferredWidth: 180
            text: rowLabel
            color: root.theme ? root.theme.textSecondary : "#a1a1aa"
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
        }

        Text {
            Layout.fillWidth: true
            text: rowValue
            color: root.theme ? root.theme.textPrimary : "white"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
        }
    }

    header: Rectangle {
        implicitHeight: 46
        color: root.theme ? root.theme.bgSecondary : "#09090b"
        border.color: root.theme ? root.theme.border : "#27272a"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10

            Text {
                text: "Client System Specs"
                color: root.theme ? root.theme.textPrimary : "white"
                font.bold: true
                font.pixelSize: 14
            }

            Item { Layout.fillWidth: true }

            Text {
                text: root.valueOf("generatedAt", "-")
                color: root.theme ? root.theme.textSecondary : "#a1a1aa"
                font.pixelSize: 11
            }
        }
    }

    contentItem: Rectangle {
        color: "transparent"

        ScrollView {
            anchors.fill: parent
            anchors.margins: 12
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: root.theme ? root.theme.bgSecondary : "#0f172a"
                    border.color: root.theme ? root.theme.border : "#27272a"
                    border.width: 1
                    implicitHeight: systemCol.implicitHeight + 16

                    ColumnLayout {
                        id: systemCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Text {
                            text: "System"
                            color: root.theme ? root.theme.accent : "#f97316"
                            font.bold: true
                            font.pixelSize: 12
                        }

                        SpecRow { rowLabel: "OS"; rowValue: root.valueOf("osName", "-") }
                        SpecRow { rowLabel: "Host"; rowValue: root.valueOf("hostName", "-") }
                        SpecRow { rowLabel: "Kernel"; rowValue: root.valueOf("kernelType", "-") + " " + root.valueOf("kernelVersion", "-") }
                        SpecRow { rowLabel: "CPU Arch"; rowValue: root.valueOf("cpuArch", "-") }
                        SpecRow { rowLabel: "Build ABI"; rowValue: root.valueOf("buildAbi", "-") }
                        SpecRow { rowLabel: "Qt Version"; rowValue: root.valueOf("qtVersion", "-") }
                        SpecRow { rowLabel: "DirectX"; rowValue: root.valueOf("directxVersion", "-") }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: root.theme ? root.theme.bgSecondary : "#0f172a"
                    border.color: root.theme ? root.theme.border : "#27272a"
                    border.width: 1
                    implicitHeight: hwCol.implicitHeight + 16

                    ColumnLayout {
                        id: hwCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Text {
                            text: "Hardware"
                            color: root.theme ? root.theme.accent : "#f97316"
                            font.bold: true
                            font.pixelSize: 12
                        }

                        SpecRow { rowLabel: "CPU Model"; rowValue: root.valueOf("cpuModel", "-") }
                        SpecRow { rowLabel: "GPU Model"; rowValue: root.valueOf("gpuModel", "-") }
                        SpecRow { rowLabel: "Logical Cores"; rowValue: root.valueOf("logicalCores", "-") }
                        SpecRow { rowLabel: "RAM Total"; rowValue: root.valueOf("ramTotal", "-") }
                        SpecRow { rowLabel: "RAM Available"; rowValue: root.valueOf("ramAvailable", "-") }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 8
                    color: root.theme ? root.theme.bgSecondary : "#0f172a"
                    border.color: root.theme ? root.theme.border : "#27272a"
                    border.width: 1
                    implicitHeight: diskCol.implicitHeight + 16

                    ColumnLayout {
                        id: diskCol
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Text {
                            text: "Storage"
                            color: root.theme ? root.theme.accent : "#f97316"
                            font.bold: true
                            font.pixelSize: 12
                        }

                        SpecRow { rowLabel: "System Drive"; rowValue: root.valueOf("systemDrivePath", "-") }
                        SpecRow { rowLabel: "Drive Total"; rowValue: root.valueOf("systemDriveTotal", "-") }
                        SpecRow { rowLabel: "Drive Free"; rowValue: root.valueOf("systemDriveFree", "-") }
                    }
                }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: 58
        color: "transparent"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Item { Layout.fillWidth: true }

            Button {
                text: "Close"
                Layout.preferredWidth: 96
                Layout.preferredHeight: 34
                onClicked: root.close()
                background: Rectangle {
                    color: parent.down ? "#ea580c" : (root.theme ? root.theme.accent : "#f97316")
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
        }
    }

    background: Rectangle {
        color: root.theme ? root.theme.bgComponent : "#18181b"
        border.color: root.theme ? root.theme.border : "#27272a"
        radius: 10
    }
}
