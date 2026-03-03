#include "Backend.h"

#include <QByteArray>
#include <QDebug>
#include <QRegularExpression>
#include <QTimer>

namespace {
QString normalizeHex(const QString &input) {
    QString s = input;
    s.remove(QRegularExpression("\\s+"));
    if (s.startsWith("0x", Qt::CaseInsensitive)) {
        s = s.mid(2);
    }
    return s.toUpper();
}
}

// 스트리밍 서버(WebSocket) 연결 시작 처리
void Backend::streamingWsConnect() {
    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit streamingWsError("SUNAPI_IP is empty");
        return;
    }

    const QUrl wsUrl(QString("ws://%1/StreamingServer").arg(host));
    if (!m_streamingWs) {
        m_streamingWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

        connect(m_streamingWs, &QWebSocket::connected, this, [this]() {
            emit streamingWsStateChanged("connected");
        });
        connect(m_streamingWs, &QWebSocket::disconnected, this, [this]() {
            emit streamingWsStateChanged("disconnected");
        });
        connect(m_streamingWs, &QWebSocket::textMessageReceived, this, [this](const QString &msg) {
            emit streamingWsFrame("recv-text", msg);
        });
        connect(m_streamingWs, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &payload) {
            emit streamingWsFrame("recv-bin", QString::fromLatin1(payload.toHex().toUpper()));
        });
        connect(m_streamingWs, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            emit streamingWsError(m_streamingWs ? m_streamingWs->errorString() : QString("unknown websocket error"));
        });
    } else if (m_streamingWs->state() == QAbstractSocket::ConnectedState
               || m_streamingWs->state() == QAbstractSocket::ConnectingState) {
        m_streamingWs->abort();
    }

    emit streamingWsStateChanged("connecting");
    m_streamingWs->open(wsUrl);
}

// 스트리밍 서버(WebSocket) 연결 종료 처리
void Backend::streamingWsDisconnect() {
    if (!m_streamingWs) {
        return;
    }
    if (m_streamingWs->state() == QAbstractSocket::ConnectedState
        || m_streamingWs->state() == QAbstractSocket::ConnectingState) {
        m_streamingWs->close();
    }
}

// 16진 문자열 바이너리 프레임 변환 전송
bool Backend::streamingWsSendHex(QString hexPayload) {
    if (!m_streamingWs || m_streamingWs->state() != QAbstractSocket::ConnectedState) {
        emit streamingWsError("websocket is not connected");
        return false;
    }

    const QString normalized = normalizeHex(hexPayload);
    if (normalized.isEmpty() || (normalized.length() % 2) != 0) {
        emit streamingWsError("invalid hex payload length");
        return false;
    }

    const QByteArray bytes = QByteArray::fromHex(normalized.toLatin1());
    if (bytes.isEmpty() && !normalized.isEmpty()) {
        emit streamingWsError("hex decode failed");
        return false;
    }

    m_streamingWs->sendBinaryMessage(bytes);
    emit streamingWsFrame("send-bin", normalized);
    return true;
}

bool Backend::playbackWsPause() {
    if (!m_playbackWsActive || m_playbackWsUri.isEmpty() || m_playbackWsSession.isEmpty()) {
        emit streamingWsError("playback pause unavailable: missing RTSP session");
        return false;
    }

    QByteArray req;
    // 일부 Hanwha 모델 PAUSE 시 재생 리소스 잠금 유지 이슈
    // 타 클라이언트(웹) 즉시 재생 허용용 TEARDOWN 기반 RTSP 세션 즉시 해제
    req += "TEARDOWN " + m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
    req += "CSeq: " + QByteArray::number(m_playbackWsNextCseq++) + "\r\n";
    if (!m_playbackWsAuthHeader.isEmpty()) {
        req += m_playbackWsAuthHeader.toUtf8();
        req += "\r\n";
    }
    req += "User-Agent: UWC[undefined]\r\n";
    req += "Session: " + m_playbackWsSession.toUtf8() + "\r\n";
    req += "\r\n";

    const bool ok = streamingWsSendHex(QString::fromLatin1(req.toHex().toUpper()));
    if (m_playbackWsKeepaliveTimer) {
        m_playbackWsKeepaliveTimer->stop();
    }
    m_playbackWsPaused = true;
    m_playbackWsPlaySent = false;
    // 서버 측 세션 해제 보장용 TEARDOWN 직후 웹소켓 종료
    QTimer::singleShot(60, this, [this]() {
        streamingWsDisconnect();
    });
    return ok;
}

bool Backend::playbackWsPlay() {
    if (!m_playbackWsActive || m_playbackWsUri.isEmpty() || m_playbackWsSession.isEmpty()) {
        emit streamingWsError("playback play unavailable: missing RTSP session");
        return false;
    }

    QByteArray req;
    req += "PLAY " + m_playbackWsUri.toUtf8() + " RTSP/1.0\r\n";
    req += "CSeq: " + QByteArray::number(m_playbackWsNextCseq++) + "\r\n";
    if (!m_playbackWsAuthHeader.isEmpty()) {
        req += m_playbackWsAuthHeader.toUtf8();
        req += "\r\n";
    }
    req += "User-Agent: UWC[undefined]\r\n";
    req += "Session: " + m_playbackWsSession.toUtf8() + "\r\n";
    req += "Range: npt=0.000-\r\n";
    req += "\r\n";

    const bool ok = streamingWsSendHex(QString::fromLatin1(req.toHex().toUpper()));
    if (ok) {
        m_playbackWsPaused = false;
        m_playbackWsPlaySent = true;
        if (m_playbackWsKeepaliveTimer) {
            m_playbackWsKeepaliveTimer->start();
        }
    }
    return ok;
}
