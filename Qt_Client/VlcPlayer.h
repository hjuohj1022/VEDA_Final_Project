#ifndef VLCPLAYER_H
#define VLCPLAYER_H

#include <QQuickItem>
#include <QWindow>
#include <QByteArray>
#include <vlc/vlc.h>

class QTimer;
class QWidget;

class VlcPlayer : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(double bitrate READ bitrate NOTIFY bitrateChanged)
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)

public:
    explicit VlcPlayer(QQuickItem *parent = nullptr);
    ~VlcPlayer();

    QString url() const;
    void setUrl(const QString &url);

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void setVideoScale(double scale);
    Q_INVOKABLE void setDigitalZoom(double scale, double focusX, double focusY);

    double bitrate() const { return m_bitrate; }
    double fps() const { return m_fps; }
    bool isPlaying() const { return m_isPlaying; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    void urlChanged();
    void bitrateChanged();
    void fpsChanged();
    void isPlayingChanged();
    void errorOccurred(QString message);
    void stateChanged(int state);
    void videoSizeChanged();
    void videoDoubleClicked(double x, double y);
    void videoWheel(int deltaY, double x, double y);
    void videoDrag(double dx, double dy);

private:
    static void handleVlcEvent(const libvlc_event_t *event, void *userData);
    void stopInternal(bool manualStop);
    void scheduleReconnect();
    void cancelReconnect();
    void setPlayingState(bool playing);
    void updateStats();
    void updateVideoSize();
    void attachWindow();
    void syncWindowPosition();
    void applyDigitalZoom();

    void initVlc();
    void cleanupVlc();

    QString m_url;
    double m_bitrate = 0.0;
    double m_fps = 0.0;
    bool m_isPlaying = false;
    double m_videoScale = 1.0;
    double m_zoomFocusX = 0.5;
    double m_zoomFocusY = 0.5;
    long long m_lastReadBytes = 0;
    long long m_lastDisplayedPictures = 0;
    qint64 m_lastStatsTimestampMs = 0;
    bool m_hasDisplayedBaseline = false;
    bool m_vlcEventsAttached = false;
    QTimer *m_statsTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_offlineDelayTimer = nullptr;
    bool m_manualStopRequested = false;
    int m_reconnectAttempt = 0;
    QByteArray m_activeMediaUrlUtf8;

    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_mediaPlayer = nullptr;

    QWindow *m_renderWindow = nullptr;
    QWidget *m_containerWidget = nullptr;
    bool m_mousePressed = false;
    QPointF m_lastMousePos;
    qint64 m_lastClickTimestampMs = 0;
    QPointF m_lastClickPos;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
};

#endif // VLCPLAYER_H
