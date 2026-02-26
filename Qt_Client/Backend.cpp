#include "Backend.h"

#include <QAuthenticator>
#include <QDebug>
#include <QElapsedTimer>
#include <QNetworkCookieJar>
#include <QPointer>
#include <QTcpSocket>
#include <memory>

Backend::Backend(QObject *parent) : QObject(parent)
{
    m_manager = new QNetworkAccessManager(this);
    m_manager->setCookieJar(new QNetworkCookieJar(this));
    setActiveCameras(0);
    loadEnv();
    setupSslConfiguration();
    connect(m_manager, &QNetworkAccessManager::authenticationRequired,
            this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                const QString user = m_env.value("SUNAPI_USER").trimmed();
                const QString pass = m_env.value("SUNAPI_PASSWORD").trimmed();
                const QString sunapiHost = m_env.value("SUNAPI_IP").trimmed();
                const QString host = reply ? reply->url().host() : QString();
                qInfo() << "[SUNAPI][AUTH] challenge host=" << host
                        << "realm=" << authenticator->realm()
                        << "userConfigured=" << !user.isEmpty();
                if (!user.isEmpty() && !sunapiHost.isEmpty() && host.compare(sunapiHost, Qt::CaseInsensitive) == 0) {
                    authenticator->setUser(user);
                    authenticator->setPassword(pass);
                }
            });
    setupMqtt();

    const QString envIp = m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = m_env.value("RTSP_PORT", "8555").trimmed();

    // Always prefer .env values on app startup.
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

    m_storageTimer = new QTimer(this);
    connect(m_storageTimer, &QTimer::timeout, this, &Backend::checkStorage);
    m_storageTimer->start(5000);
    checkStorage();

    QTimer *simTimer = new QTimer(this);
    simTimer->setInterval(5000);
    connect(simTimer, &QTimer::timeout, this, [this]() {
        // Avoid plain TCP probing against RTSPS port, which creates noisy EOF/TLS warnings on server.
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
            setLatency(999);
            finishProbe();
        });

        QPointer<QTcpSocket> socketGuard(socket);
        // 연결 지연이 길어지면 프로브를 중단한다.
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

// 로그인 상태를 반환한다.
bool Backend::isLoggedIn() const { return m_isLoggedIn; }
// API 서버 주소를 반환한다.
QString Backend::serverUrl() const { return m_env.value("API_URL", "http://localhost:8080"); }
// 현재 RTSP IP를 반환한다.
QString Backend::rtspIp() const { return m_rtspIp; }
// 현재 RTSP 포트를 반환한다.
QString Backend::rtspPort() const { return m_rtspPort; }
