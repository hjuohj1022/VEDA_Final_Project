#include "Backend.h"

#include <QAuthenticator>
#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QNetworkCookieJar>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <memory>

Backend::Backend(QObject *parent) : QObject(parent)
{
    // 네트워크 매니저 및 쿠키 저장소 초기화
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this));
    setActiveCameras(0);
    loadEnv();
    setupSslConfiguration();

    // Qt에서는 카메라 Digest 계정을 직접 주입하지 않는다.
    // 인증은 Crow 고정 API + Bearer 흐름으로 처리한다.
    connect(m_manager, &QNetworkAccessManager::authenticationRequired,
            this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const QString host = reply ? reply->url().host() : QString();
                qInfo() << "[SUNAPI][AUTH] challenge host=" << host
                        << "realm=" << authenticator->realm();
                Q_UNUSED(authenticator);
    });
    setupMqtt();

    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8555").trimmed();

    // 초기 RTSP 설정값 반영
    m_useCustomRtspConfig = false;
    m_rtspIp = envIp;
    m_rtspPort = envPort;

    if (m_rtspIp.isEmpty()) {
        m_rtspIp = envIp;
    }
    if (m_rtspPort.isEmpty()) {
        m_rtspPort = envPort;
    }

    m_sessionTimer = new QTimer(this);
    m_sessionTimer->setInterval(1000);
    connect(m_sessionTimer, &QTimer::timeout, this, &Backend::onSessionTick);

    // 저장소 갱신 타이머 시작
    m_storageTimer = new QTimer(this);
    connect(m_storageTimer, &QTimer::timeout, this, &Backend::checkStorage);
    m_storageTimer->start(60000);
    checkStorage();

    // Playback WS keepalive 타이머 초기화
    m_playbackWsKeepaliveTimer = new QTimer(this);
    m_playbackWsKeepaliveTimer->setInterval(15000);
    connect(m_playbackWsKeepaliveTimer, &QTimer::timeout, this, [this]() {
        if (!m_playbackWsActive || !m_streamingWs || m_streamingWs->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        if (m_playbackWsUri.isEmpty() || m_playbackWsSession.isEmpty()) {
            return;
        }

        QByteArray req;
        req += "GET_PARAMETER " + m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
        req += "CSeq: " + QByteArray::number(m_playbackWsNextCseq++) + "\r\n";
        if (!m_playbackWsAuthHeader.isEmpty()) {
            req += m_playbackWsAuthHeader.toUtf8();
            req += "\r\n";
        }
        req += "User-Agent: UWC[undefined]\r\n";
        req += "Session: " + m_playbackWsSession.toUtf8() + "\r\n";
        req += "Content-Length: 0\r\n";
        req += "\r\n";
        streamingWsSendHex(QString::fromLatin1(req.toHex().toUpper()));
    });

    connect(this, &Backend::streamingWsStateChanged, this, [this](const QString &state) {
        if (state == "disconnected") {
            // WS 종료 시 Playback 세션 상태 초기화
            m_playbackWsKeepaliveTimer->stop();
            m_playbackWsPlaySent = false;
            m_playbackWsPaused = false;
            m_playbackWsSession.clear();
            m_playbackInterleavedBuffer.clear();
            m_playbackValidRtpCount = 0;
        }
    });

    m_playbackRtpOutSocket = new QUdpSocket(this);

    // WS 수신 프레임 처리 연결
    connect(this, &Backend::streamingWsFrame, this, [this](const QString &direction, const QString &hexPayload) {
        if (!m_playbackWsActive || direction != "recv-bin") {
            return;
        }

        QString normalized = hexPayload;
        normalized.remove(QRegularExpression("\\s+"));
        const QByteArray bytes = QByteArray::fromHex(normalized.toLatin1());
        forwardPlaybackInterleavedRtp(bytes);
        if (!bytes.startsWith("RTSP/1.0")) {
            return;
        }

        const QString text = QString::fromLatin1(bytes);
        const QRegularExpression statusRe("^RTSP/1\\.0\\s+(\\d{3})");
        const QRegularExpression cseqRe("CSeq:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression sessionRe("Session:\\s*([^;\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression transportRe("Transport:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression interleavedRe("interleaved\\s*=\\s*(\\d+)\\s*-\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);

        const int status = statusRe.match(text).captured(1).toInt();
        const int cseq = cseqRe.match(text).captured(1).toInt();
        const QString session = sessionRe.match(text).captured(1).trimmed();
        const QString transport = transportRe.match(text).captured(1).trimmed();
        if (!session.isEmpty()) {
            m_playbackWsSession = session;
        }

        if (status < 400 && cseq > 0 && cseq == m_playbackWsH264SetupCseq && !transport.isEmpty()) {
            // H264 interleaved 채널 매핑 반영
            const auto m = interleavedRe.match(transport);
            if (m.hasMatch()) {
                m_playbackRtpVideoChannel = m.captured(1).toInt();
                m_playbackRtpVideoAltChannel = m.captured(2).toInt();
                qInfo() << "[PLAYBACK][WS] H264 interleaved mapped:"
                        << m_playbackRtpVideoChannel << "-" << m_playbackRtpVideoAltChannel;
            }
        }

        if (status >= 400) {
            return;
        }

        if (!m_playbackWsPlaySent
            && !m_playbackWsPaused
            && m_playbackWsFinalSetupCseq > 0
            && cseq == m_playbackWsFinalSetupCseq
            && !m_playbackWsSession.isEmpty()
            && m_streamingWs
            && m_streamingWs->state() == QAbstractSocket::ConnectedState) {
            // 마지막 SETUP 성공 이후 PLAY 자동 전송
            playbackWsPlay();
        }
    });

    // RTSP 포트 지연 측정 타이머 시작
    QTimer *simTimer = new QTimer(this);
    simTimer->setInterval(5000);
    connect(simTimer, &QTimer::timeout, this, [this]() {
        // RTSPS 사용 시 중복 지연 프로브 생략
        const QString rtspScheme = m_env.value("RTSP_SCHEME", "rtsp").trimmed().toLower();
        if (rtspScheme == "rtsps") {
            return;
        }

        static bool probeInFlight = false;
        if (probeInFlight) {
            return;
        }
        probeInFlight = true;

        QTcpSocket *socket = new QTcpSocket(this);
        QElapsedTimer *timer = new QElapsedTimer();
        timer->start();

        QString ip = rtspIp();
        int port = rtspPort().toInt();
        if (port == 0) port = 8555;

        auto finished = std::make_shared<bool>(false);
        auto finishProbe = [socket, timer, finished]() {
            if (*finished) {
                return;
            }
            *finished = true;
            socket->deleteLater();
            delete timer;
            probeInFlight = false;
        };

        connect(socket, &QTcpSocket::connected, this, [this, socket, timer, finishProbe]() {
            const int elapsed = timer->elapsed();
            setLatency(elapsed);
            socket->disconnectFromHost();
            finishProbe();
        });

        connect(socket, &QTcpSocket::errorOccurred, this, [this, finishProbe](QAbstractSocket::SocketError socketError) {
            Q_UNUSED(socketError);
            finishProbe();
        });

        QPointer<QTcpSocket> socketGuard(socket);
        // 연결 지연 프로브 타임아웃 처리
        QTimer::singleShot(1200, this, [socketGuard, finishProbe]() {
            if (socketGuard && socketGuard->state() == QAbstractSocket::ConnectingState) {
                socketGuard->abort();
                finishProbe();
            }
        });

        socket->connectToHost(ip, port);
    });
    simTimer->start();
}

Backend::~Backend() {}

bool Backend::isLoggedIn() const { return m_isLoggedIn; }
QString Backend::serverUrl() const { return m_env.value("API_URL", "https://localhost:8080"); }
QString Backend::rtspIp() const { return m_rtspIp; }
QString Backend::rtspPort() const { return m_rtspPort; }
