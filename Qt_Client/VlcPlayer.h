#ifndef VLCPLAYER_H
#define VLCPLAYER_H

#include <QQuickItem>
#include <QWindow>
#include <QMutex>
#include <vlc/vlc.h>

class VlcPlayer : public QQuickItem
{
    Q_OBJECT
    // QVideoSink 제거 (Native Window 사용)
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(double bitrate READ bitrate NOTIFY bitrateChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)

public:
    explicit VlcPlayer(QQuickItem *parent = nullptr);
    ~VlcPlayer();

    QString url() const;
    void setUrl(const QString &url);

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    
    double bitrate() const { return m_bitrate; }
    bool isPlaying() const { return m_isPlaying; }

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

signals:
    void urlChanged();
    void bitrateChanged();
    void isPlayingChanged();
    void errorOccurred(QString message);
    void stateChanged(int state); // 3=Playing, etc.

private:
    void updateStats();
    void attachWindow();
    void syncWindowPosition();

    // LibVLC Setup
    void initVlc();
    void cleanupVlc();

    QString m_url;
    double m_bitrate = 0.0;
    bool m_isPlaying = false;
    long long m_lastReadBytes = 0;
    QTimer *m_statsTimer = nullptr;
    
    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_mediaPlayer = nullptr;

    // Native Window Embedding
    QWindow *m_renderWindow = nullptr;
    QWidget *m_containerWidget = nullptr; // Optional wrapper if needed, but QWindow is usually enough
};

#endif // VLCPLAYER_H
