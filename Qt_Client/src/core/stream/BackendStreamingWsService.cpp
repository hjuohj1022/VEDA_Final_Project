#include "internal/stream/BackendStreamingWsService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QAbstractSocket>
#include <QByteArray>
#include <QDebug>
#include <QRegularExpression>
#include <QSslError>
#include <QTimer>
#include <QUrl>

namespace {
// Hex 정리 함수
QString normalizeHex(const QString &input)
{
    QString s = input;
    s.remove(QRegularExpression("\\s+"));
    if (s.startsWith("0x", Qt::CaseInsensitive)) {
        s = s.mid(2);
    }
    return s.toUpper();
}
} // namespace

// 스트리밍 WebSocket 연결 함수
void BackendStreamingWsService::streamingWsConnect(Backend *backend, BackendPrivate *state)
{
    const QUrl apiBase(backend->serverUrl());
    const QString host = apiBase.host().trimmed();
    if (!apiBase.isValid() || host.isEmpty()) {
        emit backend->streamingWsError("API_URL is invalid");
        return;
    }

    const QString apiScheme = apiBase.scheme().trimmed().toLower();
    const QString wsScheme = (apiScheme == "https") ? QStringLiteral("wss") : QStringLiteral("ws");
    const int defaultPort = (apiScheme == "https") ? 443 : 80;
    const int httpPort = apiBase.port(defaultPort);

    QUrl wsUrl;
    wsUrl.setScheme(wsScheme);
    wsUrl.setHost(host);
    if (httpPort > 0) {
        wsUrl.setPort(httpPort);
    }

    QString wsPath = state->m_env.value("SUNAPI_STREAMING_WS_PATH", "/StreamingServer").trimmed();
    if (wsPath.isEmpty()) {
        wsPath = "/StreamingServer";
    } else if (!wsPath.startsWith('/')) {
        wsPath.prepend('/');
    }
    wsUrl.setPath(wsPath);

    if (!state->m_streamingWs) {
        state->m_streamingWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, backend);

        QObject::connect(state->m_streamingWs, &QWebSocket::connected, backend, [backend]() {
            emit backend->streamingWsStateChanged("connected");
        });
        QObject::connect(state->m_streamingWs, &QWebSocket::disconnected, backend, [backend]() {
            emit backend->streamingWsStateChanged("disconnected");
        });
        QObject::connect(state->m_streamingWs, &QWebSocket::textMessageReceived, backend, [backend](const QString &msg) {
            emit backend->streamingWsFrame("recv-text", msg);
        });
        QObject::connect(state->m_streamingWs, &QWebSocket::binaryMessageReceived, backend, [backend](const QByteArray &payload) {
            emit backend->streamingWsFrame("recv-bin", QString::fromLatin1(payload.toHex().toUpper()));
        });
        QObject::connect(state->m_streamingWs, &QWebSocket::errorOccurred, backend, [backend, state](QAbstractSocket::SocketError) {
            emit backend->streamingWsError(state->m_streamingWs ? state->m_streamingWs->errorString() : QString("unknown websocket error"));
        });
        QObject::connect(state->m_streamingWs, &QWebSocket::sslErrors, backend, [state](const QList<QSslError> &errors) {
            for (const auto &err : errors) {
                qWarning() << "[STREAMING_WS][SSL]" << err.errorString();
            }
            if (state->m_sslIgnoreErrors && state->m_streamingWs) {
                state->m_streamingWs->ignoreSslErrors();
            }
        });
    } else if (state->m_streamingWs->state() == QAbstractSocket::ConnectedState
               // 상태 m 스트리밍 WebSocket 상태 처리 함수
               || state->m_streamingWs->state() == QAbstractSocket::ConnectingState) {
        state->m_streamingWs->abort();
    }

    if (wsScheme == "wss" && state->m_sslConfigReady) {
        state->m_streamingWs->setSslConfiguration(state->m_sslConfig);
    }

    emit backend->streamingWsStateChanged("connecting");
    state->m_streamingWs->open(wsUrl);
}

// 스트리밍 WebSocket 연결 해제 함수
void BackendStreamingWsService::streamingWsDisconnect(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);
    if (!state->m_streamingWs) {
        return;
    }
    if (state->m_streamingWs->state() == QAbstractSocket::ConnectedState
        // 상태 m 스트리밍 WebSocket 상태 처리 함수
        || state->m_streamingWs->state() == QAbstractSocket::ConnectingState) {
        state->m_streamingWs->close();
    }
}

// 스트리밍 WebSocket Hex 전송 함수
bool BackendStreamingWsService::streamingWsSendHex(Backend *backend, BackendPrivate *state, QString hexPayload)
{
    if (!state->m_streamingWs || state->m_streamingWs->state() != QAbstractSocket::ConnectedState) {
        emit backend->streamingWsError("websocket is not connected");
        return false;
    }

    const QString normalized = normalizeHex(hexPayload);
    if (normalized.isEmpty() || (normalized.length() % 2) != 0) {
        emit backend->streamingWsError("invalid hex payload length");
        return false;
    }

    const QByteArray bytes = QByteArray::fromHex(normalized.toLatin1());
    if (bytes.isEmpty() && !normalized.isEmpty()) {
        emit backend->streamingWsError("hex decode failed");
        return false;
    }

    state->m_streamingWs->sendBinaryMessage(bytes);
    emit backend->streamingWsFrame("send-bin", normalized);
    return true;
}

// 재생 WebSocket 일시정지 함수
bool BackendStreamingWsService::playbackWsPause(Backend *backend, BackendPrivate *state)
{
    if (!state->m_playbackWsActive || state->m_playbackWsUri.isEmpty() || state->m_playbackWsSession.isEmpty()) {
        emit backend->streamingWsError("playback pause unavailable: missing RTSP session");
        return false;
    }

    QByteArray req;
    req += "TEARDOWN " + state->m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
    req += "CSeq: " + QByteArray::number(state->m_playbackWsNextCseq++) + "\r\n";
    if (!state->m_playbackWsAuthHeader.isEmpty()) {
        req += state->m_playbackWsAuthHeader.toUtf8();
        req += "\r\n";
    }
    req += "User-Agent: UWC[undefined]\r\n";
    req += "Session: " + state->m_playbackWsSession.toUtf8() + "\r\n";
    req += "\r\n";

    const bool ok = BackendStreamingWsService::streamingWsSendHex(backend, state, QString::fromLatin1(req.toHex().toUpper()));
    if (state->m_playbackWsKeepaliveTimer) {
        state->m_playbackWsKeepaliveTimer->stop();
    }
    state->m_playbackWsPaused = true;
    state->m_playbackWsPlaySent = false;
    QTimer::singleShot(60, backend, [backend, state]() {
        BackendStreamingWsService::streamingWsDisconnect(backend, state);
    });
    return ok;
}

// 재생 WebSocket 재생 함수
bool BackendStreamingWsService::playbackWsPlay(Backend *backend, BackendPrivate *state)
{
    if (!state->m_playbackWsActive || state->m_playbackWsUri.isEmpty() || state->m_playbackWsSession.isEmpty()) {
        emit backend->streamingWsError("playback play unavailable: missing RTSP session");
        return false;
    }

    QByteArray req;
    req += "PLAY " + state->m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
    req += "CSeq: " + QByteArray::number(state->m_playbackWsNextCseq++) + "\r\n";
    if (!state->m_playbackWsAuthHeader.isEmpty()) {
        req += state->m_playbackWsAuthHeader.toUtf8();
        req += "\r\n";
    }
    req += "User-Agent: UWC[undefined]\r\n";
    req += "Session: " + state->m_playbackWsSession.toUtf8() + "\r\n";
    req += "Range: npt=0.000-\r\n";
    req += "\r\n";

    const bool ok = BackendStreamingWsService::streamingWsSendHex(backend, state, QString::fromLatin1(req.toHex().toUpper()));
    if (ok) {
        state->m_playbackWsPaused = false;
        state->m_playbackWsPlaySent = true;
        if (state->m_playbackWsKeepaliveTimer) {
            state->m_playbackWsKeepaliveTimer->start();
        }
    }
    return ok;
}

