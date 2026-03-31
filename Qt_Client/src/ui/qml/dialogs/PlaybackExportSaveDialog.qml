import QtQuick
import QtQuick.Dialogs

FileDialog {
    id: root
    property var hostWindow
    signal saveConfirmed(string savePath)

    title: "Playback 내보내기 저장 경로 선택"
    fileMode: FileDialog.SaveFile
    nameFilters: ["Video files (*.avi *.zip)", "All files (*)"]
    // 입력 확정 처리 함수
    onAccepted: {
        var savePath = hostWindow ? hostWindow.urlToLocalPath(selectedFile) : ""
        if (!savePath || savePath.length === 0)
            return
        root.saveConfirmed(savePath)
    }
}
