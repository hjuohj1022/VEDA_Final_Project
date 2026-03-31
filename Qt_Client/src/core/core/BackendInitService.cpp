#include "internal/core/BackendInitService.h"

#include "Backend.h"
#include "internal/core/BackendCoreCertConfigService.h"
#include "internal/core/BackendCoreEventLogService.h"
#include "internal/core/Backend_p.h"

#include <QAbstractSocket>
#include <QAuthenticator>
#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include <memory>

// initialize 처리 함수
void BackendInitService::initialize(Backend *backend, BackendPrivate *state)
{
    state->m_manager = new QNetworkAccessManager(backend);
    state->m_manager->setCookieJar(new QNetworkCookieJar(backend));
    backend->setActiveCameras(0);
    backend->loadEnv();
    BackendCoreCertConfigService::loadCertDirectoryOverride(backend, state);
    backend->setupSslConfiguration();

    QObject::connect(state->m_manager, &QNetworkAccessManager::authenticationRequired,
                     backend,
                     [backend](QNetworkReply *reply, QAuthenticator *authenticator) {
                         const QString host = reply ? reply->url().host() : QString();
                         qInfo() << "[SUNAPI][AUTH] challenge host=" << host
                                 << "realm=" << authenticator->realm();
                         Q_UNUSED(authenticator);
                     });

    QObject::connect(backend, &Backend::loginSuccess, backend, [backend, state]() {
        BackendCoreEventLogService::loadEventHistory(backend, state);
    });

    backend->setupMqtt();

    const QString envIp = state->m_env.value("RTSP_IP", "127.0.0.1").trimmed();
    const QString envPort = state->m_env.value("RTSP_PORT", "8555").trimmed();

    state->m_useCustomRtspConfig = false;
    state->m_rtspIp = envIp;
    state->m_rtspPort = envPort;

    if (state->m_rtspIp.isEmpty()) {
        state->m_rtspIp = envIp;
    }
    if (state->m_rtspPort.isEmpty()) {
        state->m_rtspPort = envPort;
    }

    state->m_sessionTimer = new QTimer(backend);
    state->m_sessionTimer->setInterval(1000);
    QObject::connect(state->m_sessionTimer, &QTimer::timeout, backend, &Backend::onSessionTick);

    state->m_storageTimer = new QTimer(backend);
    QObject::connect(state->m_storageTimer, &QTimer::timeout, backend, &Backend::checkStorage);
    state->m_storageTimer->start(60000);
    backend->checkStorage();

    state->m_playbackWsKeepaliveTimer = new QTimer(backend);
    state->m_playbackWsKeepaliveTimer->setInterval(15000);
    QObject::connect(state->m_playbackWsKeepaliveTimer, &QTimer::timeout, backend, [backend, state]() {
        if (!state->m_playbackWsActive || !state->m_streamingWs
            // 상태 m 스트리밍 WebSocket 상태 처리 함수
            || state->m_streamingWs->state() != QAbstractSocket::ConnectedState) {
            return;
        }
        if (state->m_playbackWsUri.isEmpty() || state->m_playbackWsSession.isEmpty()) {
            return;
        }

        QByteArray req;
        req += "GET_PARAMETER " + state->m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
        req += "CSeq: " + QByteArray::number(state->m_playbackWsNextCseq++) + "\r\n";
        if (!state->m_playbackWsAuthHeader.isEmpty()) {
            req += state->m_playbackWsAuthHeader.toUtf8();
            req += "\r\n";
        }
        req += "User-Agent: UWC[undefined]\r\n";
        req += "Session: " + state->m_playbackWsSession.toUtf8() + "\r\n";
        req += "Content-Length: 0\r\n";
        req += "\r\n";
        backend->streamingWsSendHex(QString::fromLatin1(req.toHex().toUpper()));
    });

    QObject::connect(backend, &Backend::streamingWsStateChanged, backend, [state](const QString &wsState) {
        if (wsState == "disconnected") {
            state->m_playbackWsKeepaliveTimer->stop();
            state->m_playbackWsPlaySent = false;
            state->m_playbackWsPaused = false;
            state->m_playbackWsSession.clear();
            state->m_playbackInterleavedBuffer.clear();
            state->m_playbackValidRtpCount = 0;
        }
    });

    state->m_playbackRtpOutSocket = new QUdpSocket(backend);

    QObject::connect(backend, &Backend::streamingWsFrame, backend,
                     [backend, state](const QString &direction, const QString &hexPayload) {
                         if (!state->m_playbackWsActive || direction != "recv-bin") {
                             return;
                         }

                         QString normalized = hexPayload;
                         normalized.remove(QRegularExpression("\\s+"));
                         const QByteArray bytes = QByteArray::fromHex(normalized.toLatin1());
                         backend->forwardPlaybackInterleavedRtp(bytes);
                         if (!bytes.startsWith("RTSP/1.0")) {
                             return;
                         }

                         const QString text = QString::fromLatin1(bytes);
                         const QRegularExpression statusRe("^RTSP/1\\.0\\s+(\\d{3})");
                         const QRegularExpression cseqRe("CSeq:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
                         const QRegularExpression sessionRe("Session:\\s*([^;\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
                         const QRegularExpression transportRe("Transport:\\s*([^\\r\\n]+)", QRegularExpression::CaseInsensitiveOption);
                         const QRegularExpression interleavedRe("interleaved\\s*=\\s*(\\d+)\\s*-\\s*(\\d+)",
                                                                QRegularExpression::CaseInsensitiveOption);

                         const int status = statusRe.match(text).captured(1).toInt();
                         const int cseq = cseqRe.match(text).captured(1).toInt();
                         const QString session = sessionRe.match(text).captured(1).trimmed();
                         const QString transport = transportRe.match(text).captured(1).trimmed();
                         if (!session.isEmpty()) {
                             state->m_playbackWsSession = session;
                         }

                         if (status < 400 && cseq > 0 && cseq == state->m_playbackWsH264SetupCseq && !transport.isEmpty()) {
                             const QRegularExpressionMatch match = interleavedRe.match(transport);
                             if (match.hasMatch()) {
                                 state->m_playbackRtpVideoChannel = match.captured(1).toInt();
                                 state->m_playbackRtpVideoAltChannel = match.captured(2).toInt();
                                 qInfo() << "[PLAYBACK][WS] H264 interleaved mapped:"
                                         << state->m_playbackRtpVideoChannel << "-" << state->m_playbackRtpVideoAltChannel;
                             }
                         }

                         if (status >= 400) {
                             return;
                         }

                         if (!state->m_playbackWsPlaySent
                             && !state->m_playbackWsPaused
                             && state->m_playbackWsFinalSetupCseq > 0
                             && cseq == state->m_playbackWsFinalSetupCseq
                             && !state->m_playbackWsSession.isEmpty()
                             && state->m_streamingWs
                             // 상태 m 스트리밍 WebSocket 상태 처리 함수
                             && state->m_streamingWs->state() == QAbstractSocket::ConnectedState) {
                             backend->playbackWsPlay();
                         }
                     });

    QTimer *simTimer = new QTimer(backend);
    simTimer->setInterval(5000);
    QObject::connect(simTimer, &QTimer::timeout, backend, [backend, state]() {
        static bool probeInFlight = false;
        if (probeInFlight) {
            return;
        }
        probeInFlight = true;

        QTcpSocket *socket = new QTcpSocket(backend);
        QElapsedTimer *timer = new QElapsedTimer();
        timer->start();

        QString ip = backend->rtspIp();
        int port = backend->rtspPort().toInt();
        if (port == 0) {
            port = 8555;
        }

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

        QObject::connect(socket, &QTcpSocket::connected, backend, [backend, socket, timer, finishProbe]() {
            const int elapsed = timer->elapsed();
            backend->setLatency(elapsed);
            socket->disconnectFromHost();
            finishProbe();
        });

        QObject::connect(socket, &QTcpSocket::errorOccurred, backend,
                         [finishProbe](QAbstractSocket::SocketError socketError) {
                             Q_UNUSED(socketError);
                             finishProbe();
                         });

        QPointer<QTcpSocket> socketGuard(socket);
        QTimer::singleShot(1200, backend, [socketGuard, finishProbe]() {
            if (socketGuard && socketGuard->state() == QAbstractSocket::ConnectingState) {
                socketGuard->abort();
                finishProbe();
            }
        });

        socket->connectToHost(ip, port);
    });
    simTimer->start();

    QObject::connect(backend, &Backend::loginSuccess, backend, [backend, state]() {
        BackendCoreEventLogService::loadEventHistory(backend, state);
    });
}

