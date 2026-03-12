import QtQuick

Item {
    id: root
    property var theme
    property string playbackSource: ""
    property string playbackTitle: "Playback Stream"
    property int playbackCurrentSeconds: 0
    property var playbackSegments: []
    property string playbackTimelineInfoText: "녹화 구간 없음"
    signal seekRequested(int seconds)

    function pausePlayback() {
        content.pausePlayback()
    }

    function resumePlayback() {
        content.resumePlayback()
    }

    PlaybackScreen {
        id: content
        anchors.fill: parent
        theme: root.theme
        playbackSource: root.playbackSource
        playbackTitle: root.playbackTitle
        playbackCurrentSeconds: root.playbackCurrentSeconds
        playbackSegments: root.playbackSegments
        playbackTimelineInfoText: root.playbackTimelineInfoText
        onSeekRequested: function(seconds) {
            root.seekRequested(seconds)
        }
    }
}