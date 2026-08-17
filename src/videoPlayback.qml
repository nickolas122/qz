import QtMultimedia 5.15
import QtQuick.Controls.Material 2.0
import QtQuick.Layouts 1.15
import QtQuick 2.15

Rectangle {
    anchors.fill: parent

    Timer {
        id: pauseTimer
        interval: 1000; running: true; repeat: true
        onTriggered: { (rootItem.currentSpeed > 0 ?
                            videoPlayback.play() :
                            videoPlayback.pause()) }
    }

    // Qt 6: errorOccurred() replaces error(), and the player names its videoOutput
    // rather than the VideoOutput naming its source. See the same block in Home.qml.
    MediaPlayer {
           id: videoPlayback
           source: rootItem.videoPath
           playbackRate: rootItem.videoRate
           videoOutput: videoPlayer

           onError: function(error, errorString) {
               if (MediaPlayer.NoError !== error) {
                   console.log("[qmlvideo] MediaPlayer error " + error + " errorString " + errorString)
               }
           }

       }

    VideoOutput {
             id:videoPlayer
             anchors.fill: parent

             Component.onCompleted: {
                 console.log("mediaPlayer onCompleted: " + rootItem.videoPath)
                 videoPlayback.seek(rootItem.videoPosition)
                 videoPlayback.play()
             }
         }
}
