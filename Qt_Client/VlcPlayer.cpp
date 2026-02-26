#include "VlcPlayer.h"

#include <QDebug>
#include <QDateTime>
#include <QQuickWindow>
#include <QTimer>
#include <QMetaObject>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QLibrary>
#include <QLineF>
#include <QByteArray>
#include <cmath>
#include <algorithm>

namespace {
libvlc_instance_t *g_sharedVlcInstance = nullptr;
int g_sharedVlcUsers = 0;

void vlcSilentLogCallback(void *, int, const libvlc_log_t *, const char *, va_list)
{
}

void stopMediaPlayerCompat(libvlc_media_player_t *player)
{
    if (!player) {
        return;
    }

    using StopFn = void (*)(libvlc_media_player_t *);
    static QLibrary libvlcLib("libvlc");
    static bool symbolsResolved = false;
    static StopFn stopAsyncFn = nullptr;
    static StopFn stopFn = nullptr;

    if (!symbolsResolved) {
        libvlcLib.load();
        stopAsyncFn = reinterpret_cast<StopFn>(libvlcLib.resolve("libvlc_media_player_stop_async"));
        stopFn = reinterpret_cast<StopFn>(libvlcLib.resolve("libvlc_media_player_stop"));
        symbolsResolved = true;
    }

    if (stopAsyncFn) {
        stopAsyncFn(player);
        return;
    }

    if (stopFn) {
        stopFn(player);
        return;
    }

    libvlc_media_player_set_pause(player, 1);
}

void setCropGeometryCompat(libvlc_media_player_t *player, const char *geometry)
{
    if (!player) {
        return;
    }

    using CropFn = void (*)(libvlc_media_player_t *, const char *);
    static QLibrary libvlcLib("libvlc");
    static bool symbolsResolved = false;
    static CropFn cropFn = nullptr;

    if (!symbolsResolved) {
        libvlcLib.load();
        cropFn = reinterpret_cast<CropFn>(libvlcLib.resolve("libvlc_video_set_crop_geometry"));
        symbolsResolved = true;
    }

    if (cropFn) {
        cropFn(player, geometry);
    }
}
}

VlcPlayer::VlcPlayer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents, false);

    connect(this, &QQuickItem::xChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::yChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::widthChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::heightChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::visibleChanged, this, &VlcPlayer::syncWindowPosition);

    const char *const vlcArgs[] = {
        "--no-xlib",
        "--rtsp-tcp",
        "--rtsp-timeout=2",
        "--no-audio",
        "--aout=dummy",
        "--clock-jitter=0",
        "--clock-synchro=0",
        "--quiet",
        "--verbose=-1",
        "--network-caching=80",
        "--live-caching=80",
        "--avcodec-threads=2",
        "--drop-late-frames",
        "--skip-frames"
    };
    if (!g_sharedVlcInstance) {
        const int vlcArgc = static_cast<int>(sizeof(vlcArgs) / sizeof(vlcArgs[0]));
        g_sharedVlcInstance = libvlc_new(vlcArgc, vlcArgs);
        if (!g_sharedVlcInstance) {
            qWarning() << "Failed to initialize shared libVLC instance";
        } else {
            // Drop libVLC internal logs entirely in app runtime.
            libvlc_log_set(g_sharedVlcInstance, vlcSilentLogCallback, nullptr);
        }
    }
    m_vlcInstance = g_sharedVlcInstance;
    if (m_vlcInstance) {
        g_sharedVlcUsers++;
    }

    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(1000);
    connect(m_statsTimer, &QTimer::timeout, this, &VlcPlayer::updateStats);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (m_url.isEmpty()) {
            return;
        }
        play();
    });

    m_offlineDelayTimer = new QTimer(this);
    m_offlineDelayTimer->setSingleShot(true);
    connect(m_offlineDelayTimer, &QTimer::timeout, this, [this]() {
        setPlayingState(false);
    });
}

VlcPlayer::~VlcPlayer()
{
    stopInternal(true);
    if (m_vlcInstance) {
        g_sharedVlcUsers = std::max(0, g_sharedVlcUsers - 1);
        if (g_sharedVlcUsers == 0 && g_sharedVlcInstance) {
            libvlc_release(g_sharedVlcInstance);
            g_sharedVlcInstance = nullptr;
        }
        m_vlcInstance = nullptr;
    }

    delete m_renderWindow;
    m_renderWindow = nullptr;
}

// 현재 재생 URL을 반환한다.
QString VlcPlayer::url() const
{
    return m_url;
}

// 재생 URL을 변경하고 필요 시 재생을 갱신한다.
void VlcPlayer::setUrl(const QString &url)
{
    if (m_url == url) {
        return;
    }

    m_url = url;
    emit urlChanged();

    if (m_isPlaying) {
        // Force OFFLINE->ONLINE transition on source switch so grid ACTIVE count is refreshed.
        setPlayingState(false);
        emit stateChanged(0);
        stopInternal(false);
        play();
    }
}

// VLC 렌더링용 윈도우를 생성/연결한다.
void VlcPlayer::attachWindow()
{
    if (!m_renderWindow) {
        m_renderWindow = new QWindow();
        m_renderWindow->setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
        m_renderWindow->installEventFilter(this);
    }

    QQuickWindow *rootWindow = window();
    if (!rootWindow) {
        return;
    }

    m_renderWindow->setParent(rootWindow);
    // Keep hidden until VLC actually has a vout surface.
    m_renderWindow->setVisible(false);
    syncWindowPosition();
}

// QML 아이템 위치와 렌더 윈도우 위치를 동기화한다.
void VlcPlayer::syncWindowPosition()
{
    if (!m_renderWindow || !window()) {
        return;
    }

    const QPointF pos = mapToItem(nullptr, QPointF(0, 0));
    const QRectF rect(pos.x(), pos.y(), width(), height());

    m_renderWindow->setGeometry(rect.toRect());
    const bool hasVout = m_mediaPlayer && (libvlc_media_player_has_vout(m_mediaPlayer) > 0);
    m_renderWindow->setVisible(isVisible() && width() > 0 && height() > 0 && m_isPlaying && hasVout);
}

// VLC 플레이어를 초기화하고 재생을 시작한다.
void VlcPlayer::play()
{
    const bool isReconnectAttempt = (m_reconnectAttempt > 0);
    if (!isReconnectAttempt) {
        cancelReconnect();
    }
    m_manualStopRequested = false;

    if (m_url.isEmpty() || !m_vlcInstance) {
        qWarning() << "Cannot play: URL empty or Instance null";
        return;
    }

    stopInternal(false);
    attachWindow();

    if (!m_renderWindow) {
        qWarning() << "No Render Window available";
        return;
    }

    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    m_activeMediaUrlUtf8 = m_url.toUtf8();
    libvlc_media_t *media = libvlc_media_new_location(m_activeMediaUrlUtf8.constData());
    if (!media) {
        qWarning() << "libVLC failed to create media for URL:" << m_url;
        if (m_mediaPlayer) {
            libvlc_media_player_release(m_mediaPlayer);
            m_mediaPlayer = nullptr;
        }
        scheduleReconnect();
        return;
    }
    libvlc_media_add_option(media, ":rtsp-tcp");
    libvlc_media_add_option(media, ":rtsp-timeout=2");
    libvlc_media_add_option(media, ":network-caching=80");
    libvlc_media_add_option(media, ":live-caching=80");
    libvlc_media_add_option(media, ":clock-jitter=0");
    libvlc_media_add_option(media, ":clock-synchro=0");
    libvlc_media_add_option(media, ":avcodec-threads=2");
    libvlc_media_add_option(media, ":drop-late-frames");
    libvlc_media_add_option(media, ":skip-frames");
    libvlc_media_add_option(media, ":no-audio");
    libvlc_media_add_option(media, ":gnutls-verify-trust=0");
    libvlc_media_player_set_media(m_mediaPlayer, media);
    libvlc_media_release(media);

    const WId wid = m_renderWindow->winId();
    libvlc_media_player_set_hwnd(m_mediaPlayer, reinterpret_cast<void *>(wid));
    libvlc_video_set_mouse_input(m_mediaPlayer, false);
    libvlc_video_set_key_input(m_mediaPlayer, false);
    applyDigitalZoom();

    libvlc_event_manager_t *eventManager = libvlc_media_player_event_manager(m_mediaPlayer);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerPlaying, &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerPaused, &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerStopped, &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerStopping, &VlcPlayer::handleVlcEvent, this);
    libvlc_event_attach(eventManager, libvlc_MediaPlayerEncounteredError, &VlcPlayer::handleVlcEvent, this);
    m_vlcEventsAttached = true;

    const int playRc = libvlc_media_player_play(m_mediaPlayer);
    if (playRc != 0) {
        qWarning() << "libVLC failed to start playback for URL:" << m_url;
        scheduleReconnect();
    }

    m_statsTimer->start();
}

// 디지털 줌 배율을 설정한다.
void VlcPlayer::setVideoScale(double scale)
{
    if (scale < 1.0) {
        scale = 1.0;
    }
    if (scale > 4.0) {
        scale = 4.0;
    }

    if (qFuzzyCompare(m_videoScale, scale)) {
        return;
    }

    m_videoScale = scale;

    if (m_mediaPlayer) {
        applyDigitalZoom();
    }
}

// 배율과 포커스 좌표를 함께 반영한다.
void VlcPlayer::setDigitalZoom(double scale, double focusX, double focusY)
{
    if (scale < 1.0) {
        scale = 1.0;
    }
    if (scale > 4.0) {
        scale = 4.0;
    }

    focusX = std::max(0.0, std::min(1.0, focusX));
    focusY = std::max(0.0, std::min(1.0, focusY));

    const bool scaleChanged = !qFuzzyCompare(m_videoScale, scale);
    const bool focusChanged = std::fabs(m_zoomFocusX - focusX) > 0.0001
                           || std::fabs(m_zoomFocusY - focusY) > 0.0001;

    if (!scaleChanged && !focusChanged) {
        return;
    }

    m_videoScale = scale;
    m_zoomFocusX = focusX;
    m_zoomFocusY = focusY;

    if (m_mediaPlayer) {
        applyDigitalZoom();
    }
}

// 현재 배율/좌표 기준으로 VLC crop을 적용한다.
void VlcPlayer::applyDigitalZoom()
{
    if (!m_mediaPlayer) {
        return;
    }

    libvlc_video_set_scale(m_mediaPlayer, 0.0f);
    updateVideoSize();

    if (m_videoScale <= 1.01) {
        setCropGeometryCompat(m_mediaPlayer, nullptr);
        return;
    }

    unsigned int videoW = 0;
    unsigned int videoH = 0;
    const int sizeRc = libvlc_video_get_size(m_mediaPlayer, 0, &videoW, &videoH);
    if (sizeRc != 0 || videoW == 0 || videoH == 0) {
        return;
    }

    const int srcW = static_cast<int>(videoW);
    const int srcH = static_cast<int>(videoH);
    const int cropW = std::max(1, static_cast<int>(std::round(static_cast<double>(srcW) / m_videoScale)));
    const int cropH = std::max(1, static_cast<int>(std::round(static_cast<double>(srcH) / m_videoScale)));

    const int focusPxX = static_cast<int>(std::round(m_zoomFocusX * srcW));
    const int focusPxY = static_cast<int>(std::round(m_zoomFocusY * srcH));

    const int maxX = std::max(0, srcW - cropW);
    const int maxY = std::max(0, srcH - cropH);
    // Keep the pixel under the cursor at the same on-screen normalized position.
    const int targetXInCrop = static_cast<int>(std::round(m_zoomFocusX * cropW));
    const int targetYInCrop = static_cast<int>(std::round(m_zoomFocusY * cropH));
    const int cropX = std::max(0, std::min(maxX, focusPxX - targetXInCrop));
    const int cropY = std::max(0, std::min(maxY, focusPxY - targetYInCrop));

    const int cropRight = std::min(srcW, cropX + cropW);
    const int cropBottom = std::min(srcH, cropY + cropH);

    const QByteArray geometry = QString("%1x%2+%3+%4")
                                    .arg(cropRight)
                                    .arg(cropBottom)
                                    .arg(cropX)
                                    .arg(cropY)
                                    .toUtf8();
    setCropGeometryCompat(m_mediaPlayer, geometry.constData());
}

// 현재 비디오 해상도를 읽어 프로퍼티를 갱신한다.
void VlcPlayer::updateVideoSize()
{
    if (!m_mediaPlayer) {
        if (m_videoWidth != 0 || m_videoHeight != 0) {
            m_videoWidth = 0;
            m_videoHeight = 0;
            emit videoSizeChanged();
        }
        return;
    }

    unsigned int w = 0;
    unsigned int h = 0;
    const int rc = libvlc_video_get_size(m_mediaPlayer, 0, &w, &h);
    const int newW = (rc == 0) ? static_cast<int>(w) : 0;
    const int newH = (rc == 0) ? static_cast<int>(h) : 0;
    if (newW != m_videoWidth || newH != m_videoHeight) {
        m_videoWidth = newW;
        m_videoHeight = newH;
        emit videoSizeChanged();
    }
}

// 재생을 사용자 요청으로 중지한다.
void VlcPlayer::stop()
{
    stopInternal(true);
}

// 플레이어/타이머/상태를 내부적으로 정리한다.
void VlcPlayer::stopInternal(bool manualStop)
{
    if (manualStop) {
        m_manualStopRequested = true;
        cancelReconnect();
    }

    if (m_mediaPlayer) {
        if (m_vlcEventsAttached) {
            libvlc_event_manager_t *eventManager = libvlc_media_player_event_manager(m_mediaPlayer);
            libvlc_event_detach(eventManager, libvlc_MediaPlayerPlaying, &VlcPlayer::handleVlcEvent, this);
            libvlc_event_detach(eventManager, libvlc_MediaPlayerPaused, &VlcPlayer::handleVlcEvent, this);
            libvlc_event_detach(eventManager, libvlc_MediaPlayerStopped, &VlcPlayer::handleVlcEvent, this);
            libvlc_event_detach(eventManager, libvlc_MediaPlayerStopping, &VlcPlayer::handleVlcEvent, this);
            libvlc_event_detach(eventManager, libvlc_MediaPlayerEncounteredError, &VlcPlayer::handleVlcEvent, this);
            m_vlcEventsAttached = false;
        }

        stopMediaPlayerCompat(m_mediaPlayer);
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    m_activeMediaUrlUtf8.clear();

    if (m_renderWindow) {
        m_renderWindow->hide();
    }

    if (m_bitrate != 0.0) {
        m_bitrate = 0.0;
        emit bitrateChanged();
    }

    if (m_fps != 0.0) {
        m_fps = 0.0;
        emit fpsChanged();
    }

    m_lastReadBytes = 0;
    m_lastDisplayedPictures = 0;
    m_lastStatsTimestampMs = 0;
    m_hasDisplayedBaseline = false;

    if (manualStop) {
        setPlayingState(false);
        emit stateChanged(0);
    }

    if (m_statsTimer) {
        m_statsTimer->stop();
    }
}

// 지수 백오프로 재연결을 예약한다.
void VlcPlayer::scheduleReconnect()
{
    if (m_manualStopRequested || m_url.isEmpty() || !m_reconnectTimer) {
        return;
    }
    if (m_reconnectTimer->isActive()) {
        return;
    }

    m_reconnectAttempt = std::min(m_reconnectAttempt + 1, 10);
    const int expStep = std::min(m_reconnectAttempt - 1, 4);
    const int expDelayMs = 500 * (1 << expStep); // 0.5s,1s,2s,4s,8s
    const uint hash = qHash(m_url);
    const int jitterMs = static_cast<int>((hash + static_cast<uint>(m_reconnectAttempt * 97)) % 350u);
    const int delayMs = std::min(10000, expDelayMs + jitterMs);
    m_reconnectTimer->start(delayMs);

    // Keep LIVE for transient drops. Mark OFFLINE only after reconnect retries.
    if (m_offlineDelayTimer && !m_offlineDelayTimer->isActive() && m_reconnectAttempt >= 3) {
        m_offlineDelayTimer->start(7000);
    }
}

// 예약된 재연결과 오프라인 지연 처리를 취소한다.
void VlcPlayer::cancelReconnect()
{
    m_reconnectAttempt = 0;
    if (m_reconnectTimer) {
        m_reconnectTimer->stop();
    }
    if (m_offlineDelayTimer) {
        m_offlineDelayTimer->stop();
    }
}

// 재생 상태 플래그를 갱신한다.
void VlcPlayer::setPlayingState(bool playing)
{
    if (m_isPlaying == playing) {
        return;
    }
    m_isPlaying = playing;
    emit isPlayingChanged();
}

// VLC 이벤트를 Qt 시그널/상태로 변환한다.
void VlcPlayer::handleVlcEvent(const libvlc_event_t *event, void *userData)
{
    auto *self = static_cast<VlcPlayer *>(userData);
    if (!self || !event) {
        return;
    }

    switch (event->type) {
    case libvlc_MediaPlayerPlaying:
        // UI 스레드에서 상태를 안전하게 반영한다.
        QMetaObject::invokeMethod(self, [self]() {
            self->cancelReconnect();
            self->m_manualStopRequested = false;
            self->setPlayingState(true);
            // Refresh immediately so first frame appears without exposing an empty surface.
            self->updateStats();
            emit self->stateChanged(3);
        }, Qt::QueuedConnection);
        break;
    case libvlc_MediaPlayerStopped:
    case libvlc_MediaPlayerStopping:
    case libvlc_MediaPlayerEncounteredError:
        // UI 스레드에서 상태를 안전하게 반영한다.
        QMetaObject::invokeMethod(self, [self]() {
            if (self->m_manualStopRequested || self->m_url.isEmpty()) {
                if (self->m_renderWindow) {
                    self->m_renderWindow->hide();
                }
                self->setPlayingState(false);
                emit self->stateChanged(0);
                return;
            }
            // transient drop: try reconnect first
            self->scheduleReconnect();
        }, Qt::QueuedConnection);
        break;
    case libvlc_MediaPlayerPaused:
        // Paused can happen transiently during clock/PCR adjustments; don't force reconnect.
        break;
    default:
        break;
    }
}

// 아이템 지오메트리 변경 시 렌더 윈도우를 갱신한다.
void VlcPlayer::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    // 기본 동작 호출
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    syncWindowPosition();
}

// 씬/윈도우 변경 시 렌더 윈도우를 재연결한다.
void VlcPlayer::itemChange(ItemChange change, const ItemChangeData &value)
{
    // 기본 동작 호출
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange && value.window) {
        attachWindow();
    }
}

// 마우스/휠 이벤트를 받아 QML 시그널로 전달한다.
bool VlcPlayer::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_renderWindow || !event) {
        // 기본 이벤트 필터 동작 사용
        return QQuickItem::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_mousePressed = true;
            m_lastMousePos = me->position();
        }
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton) {
            m_mousePressed = false;
            const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
            const bool withinTime = (nowMs - m_lastClickTimestampMs) <= 320;
            const bool withinDistance = (QLineF(me->position(), m_lastClickPos).length() <= 8.0);
            if (withinTime && withinDistance) {
                emit videoDoubleClicked(me->position().x(), me->position().y());
                m_lastClickTimestampMs = 0;
            } else {
                m_lastClickTimestampMs = nowMs;
                m_lastClickPos = me->position();
            }
        }
        break;
    }
    case QEvent::MouseMove: {
        auto *me = static_cast<QMouseEvent *>(event);
        const QPointF pos = me->position();
        if (m_mousePressed) {
            const QPointF delta = pos - m_lastMousePos;
            if (!qFuzzyIsNull(delta.x()) || !qFuzzyIsNull(delta.y())) {
                emit videoDrag(delta.x(), delta.y());
            }
        }
        m_lastMousePos = pos;
        break;
    }
    case QEvent::MouseButtonDblClick: {
        auto *me = static_cast<QMouseEvent *>(event);
        emit videoDoubleClicked(me->position().x(), me->position().y());
        break;
    }
    case QEvent::Wheel: {
        auto *we = static_cast<QWheelEvent *>(event);
        int deltaY = we->angleDelta().y();
        if (deltaY == 0) {
            const QPoint pixel = we->pixelDelta();
            if (!pixel.isNull()) {
                deltaY = pixel.y();
            }
        }
        if (deltaY != 0) {
            emit videoWheel(deltaY, we->position().x(), we->position().y());
        }
        break;
    }
    default:
        break;
    }

    // 기본 이벤트 필터 동작 사용
    return QQuickItem::eventFilter(watched, event);
}

// VLC 통계를 읽어 FPS/비트레이트를 갱신한다.
void VlcPlayer::updateStats()
{
    if (!m_mediaPlayer) {
        return;
    }

    if (m_renderWindow) {
        const bool hasVout = (libvlc_media_player_has_vout(m_mediaPlayer) > 0);
        const bool shouldShow = hasVout && isVisible() && width() > 0 && height() > 0;
        m_renderWindow->setVisible(shouldShow);
    }

    updateVideoSize();

    libvlc_media_t *media = libvlc_media_player_get_media(m_mediaPlayer);
    if (!media) {
        return;
    }

    libvlc_media_stats_t stats;
    const bool hasStats = libvlc_media_get_stats(media, &stats);

    if (hasStats) {
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        const long long displayedPictures = static_cast<long long>(stats.i_displayed_pictures);

        if (m_hasDisplayedBaseline) {
            const qint64 deltaMs = nowMs - m_lastStatsTimestampMs;
            const long long deltaFrames = displayedPictures - m_lastDisplayedPictures;
            if (deltaMs > 0 && deltaFrames >= 0) {
                const double fpsValue = (static_cast<double>(deltaFrames) * 1000.0) / static_cast<double>(deltaMs);
                if (std::fabs(m_fps - fpsValue) > 0.1) {
                    m_fps = fpsValue;
                    emit fpsChanged();
                }
            }
        }

        m_lastDisplayedPictures = displayedPictures;
        m_lastStatsTimestampMs = nowMs;
        m_hasDisplayedBaseline = true;

        double nextBitrate = m_bitrate;
        if (stats.f_demux_bitrate > 0.1f) {
            nextBitrate = stats.f_demux_bitrate * 1000.0;
        }

        long long currentBytes = stats.i_demux_read_bytes;
        if (currentBytes == 0) {
            currentBytes = stats.i_read_bytes;
        }

        long long diff = 0;
        const long long maxInt = 2147483647;

        if (currentBytes < m_lastReadBytes) {
            diff = currentBytes + (maxInt - m_lastReadBytes);
        } else {
            diff = currentBytes - m_lastReadBytes;
        }

        if (m_lastReadBytes != 0) {
            const double kbps = (diff * 8.0) / 1000.0;
            if (kbps > 0.0) {
                nextBitrate = kbps;
            }
        }

        if (currentBytes > 0) {
            m_lastReadBytes = currentBytes;
        }

        if (std::fabs(m_bitrate - nextBitrate) > 1.0) {
            m_bitrate = nextBitrate;
            emit bitrateChanged();
        }
    }

    libvlc_media_release(media);
}

