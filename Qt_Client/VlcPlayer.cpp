#include "VlcPlayer.h"
#include <QDebug>
#include <QTimer>
#include <QQuickWindow>
#include <QThread>

VlcPlayer::VlcPlayer(QQuickItem *parent) : QQuickItem(parent)
{
    // Native Window를 사용하므로 QML 자체 렌더링은 없음
    setFlag(ItemHasContents, false);
    
    // 이벤트 루프에서 윈도우 위치 동기화를 위해
    connect(this, &QQuickItem::xChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::yChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::widthChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::heightChanged, this, &VlcPlayer::syncWindowPosition);
    connect(this, &QQuickItem::visibleChanged, this, &VlcPlayer::syncWindowPosition);

    qDebug() << "Initializing VLC (Native Window Mode)...";
    const char * const vlc_args[] = {
        "--no-xlib",
        "--rtsp-tcp",            // TCP를 통한 RTSP
        "--network-caching=3000", // 높은 네트워크 지연으로 인해 3000ms로 증가
        "--no-audio",            // 오디오 비활성화
        "--verbose=0"            // 로그 최소화
    };
    int vlc_argc = sizeof(vlc_args) / sizeof(vlc_args[0]);
    m_vlcInstance = libvlc_new(vlc_argc, vlc_args);
    
    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, &VlcPlayer::updateStats);
    m_statsTimer->setInterval(1000); 
}

VlcPlayer::~VlcPlayer()
{
    stop(); // 먼저 정지
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
    }
    if (m_renderWindow) {
        delete m_renderWindow;
    }
}

QString VlcPlayer::url() const { return m_url; }

void VlcPlayer::setUrl(const QString &url)
{
    if (m_url != url) {
        m_url = url;
        emit urlChanged();
        if (m_isPlaying) {
            stop();
            play(); 
        }
    }
}

void VlcPlayer::attachWindow()
{
    if (!m_renderWindow) {
        m_renderWindow = new QWindow();
        m_renderWindow->setFlags(Qt::FramelessWindowHint);
        // m_renderWindow->setColor(Qt::black); // QWindow에는 setColor가 없음, VLC 또는 Expose 이벤트에서 처리
    }

    // QQuickWindow(최상위 레벨) 찾기
    QQuickWindow *rootWindow = window();
    if (rootWindow) {
        // 앱과 함께 이동하도록 부모 설정
        m_renderWindow->setParent(rootWindow);
        m_renderWindow->setVisible(isVisible() && rootWindow->isVisible());
        syncWindowPosition();
    }
}

void VlcPlayer::syncWindowPosition()
{
    if (!m_renderWindow || !window()) return;

    // QML 좌표를 전역 화면 좌표로 매핑한 다음 윈도우 좌표로 매핑
    // 사실 m_renderWindow는 window()의 자식이므로, 윈도우에 상대적인 좌표가 필요함.
    
    QPointF pos = mapToItem(nullptr, QPointF(0, 0)); // 윈도우 콘텐츠 아이템에 상대적인 위치
    
    // 고해상도(High DPI) 조정? QWindow는 보통 장치 독립 픽셀을 예상하지만 확인 필요.
    // Qt6에서 지오메트리는 보통 논리적임.
    
    QRectF rect(pos.x(), pos.y(), width(), height());
    
    m_renderWindow->setGeometry(rect.toRect());
    m_renderWindow->setVisible(isVisible() && width() > 0 && height() > 0);
    
    // 필요한 경우 강제 다시 그리기? 아니요, 표준 지오메트리 업데이트면 충분함.
}

void VlcPlayer::play()
{
    qDebug() << "VlcPlayer::play called. URL:" << m_url << "Instance:" << m_vlcInstance;
    if (m_url.isEmpty() || !m_vlcInstance) {
        qWarning() << "Cannot play: URL empty or Instance null";
        return;
    }

    stop();

    // 윈도우 존재 확인
    attachWindow();
    
    if (!m_renderWindow) {
        qWarning() << "No Render Window available!";
        return;
    }

    m_mediaPlayer = libvlc_media_player_new(m_vlcInstance);
    
    libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, m_url.toUtf8().constData());
    
    libvlc_media_add_option(media, ":rtsp-tcp");
    
    libvlc_media_player_set_media(m_mediaPlayer, media);
    libvlc_media_release(media);

    // *중요*: 하드웨어 렌더링을 위해 HWND 설정
    WId wid = m_renderWindow->winId();
    libvlc_media_player_set_hwnd(m_mediaPlayer, (void*)wid);

    libvlc_media_player_play(m_mediaPlayer);
    
    m_isPlaying = true;
    emit isPlayingChanged();
    emit stateChanged(3); 

    m_statsTimer->start();
    
    // 윈도우가 보이는지 확인
    m_renderWindow->show();
}

void VlcPlayer::stop()
{
    if (m_mediaPlayer) {
        libvlc_media_player_stop(m_mediaPlayer);
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    
    if (m_renderWindow) {
        // 검은색으로 채우거나 숨길까? 
        // 즉시 다시 재생할 경우 숨기면 깜빡임이 발생할 수 있음.
        // 원한다면 보이게 하되 검은색으로 유지하거나 그냥 숨김.
        // 그리드 뷰의 경우 검은색으로 유지하는 것이 더 나음.
        // m_renderWindow->hide(); 
    }

    m_isPlaying = false;
    emit isPlayingChanged();
    emit stateChanged(0); // 정지됨
    
    if (m_statsTimer) m_statsTimer->stop();
}

void VlcPlayer::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    syncWindowPosition();
}

void VlcPlayer::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);
    if (change == ItemSceneChange && value.window) {
        // 윈도우에 부착될 때
        attachWindow();
    }
}

void VlcPlayer::updateStats()
{
    if (!m_mediaPlayer) return;
    
    libvlc_media_t *media = libvlc_media_player_get_media(m_mediaPlayer);
    if (!media) return;
    
    libvlc_media_stats_t stats;
    bool hasStats = libvlc_media_get_stats(media, &stats);
    
    // 통계 디버깅
    // qDebug() << "VLC Stats:" << hasStats 
    //         << "DemuxBitrate:" << stats.f_demux_bitrate 
    //         << "DemuxBytes:" << stats.i_demux_read_bytes
    //         << "InputBytes:" << stats.i_read_bytes;

    if (hasStats) {
        // 방법 1: 가능한 경우 VLC 내장 비트레이트 사용
        if (stats.f_demux_bitrate > 0.1) {
             m_bitrate = stats.f_demux_bitrate * 8000.0; // VLC가 보통 스케일을 반환하나? 아니요, 값을 반환함. 
             // ...
             m_bitrate = stats.f_demux_bitrate * 1000.0; 
        }
        
        // Demux 바이트를 사용한 수동 계산 (TCP를 통한 RTSP에서 더 신뢰성 있음)
        long long currentBytes = stats.i_demux_read_bytes;
        // Demux가 0인 경우 i_read_bytes로 대체 (HW 가속 시 가끔 발생?)
        if (currentBytes == 0) currentBytes = stats.i_read_bytes;
        
        long long diff = 0;
        
        long long maxInt = 2147483647;
        
        if (currentBytes < m_lastReadBytes) {
            diff = currentBytes + (maxInt - m_lastReadBytes); 
        } else {
            diff = currentBytes - m_lastReadBytes;
        }

        if (m_lastReadBytes != 0) {
             // diff는 초당 바이트 수
             double kbps = (diff * 8.0) / 1000.0;
             
             // 노이즈 필터링
             if (kbps > 0) {
                 m_bitrate = kbps;
             }
        }
        // 유효한 읽기 값이 있는 경우(차이가 0이더라도) 항상 마지막 바이트 업데이트
        if (currentBytes > 0) m_lastReadBytes = currentBytes;
        
        emit bitrateChanged();
        
    } else {
        // 통계 실패 - 이 VLC 빌드에서 RTSP를 지원하지 않을 수 있음?
        // 대체: 재생 중인 경우 최소한의 정보를 표시하거나 0으로 유지
        // qDebug() << "No Stats Available";
    }
    
    libvlc_media_release(media);
}
