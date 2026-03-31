#include "internal/rtsp/BackendRtspProbeService.h"

#include "Backend.h"

#include <QAbstractSocket>
#include <QTcpSocket>
#include <QTimer>

// probe RTSP Endpoint 처리 함수
void BackendRtspProbeService::probeRtspEndpoint(Backend *backend, QString ip, QString port, int timeoutMs)
{
    const QString ipTrimmed = ip.trimmed();
    if (ipTrimmed.isEmpty()) {
        emit backend->rtspProbeFinished(false, "IP가 비어 있습니다.");
        return;
    }

    bool ok = false;
    int portNum = port.trimmed().toInt(&ok);
    if (!ok || portNum < 1 || portNum > 65535) {
        portNum = 8555;
    }

    const int safeTimeoutMs = qBound(300, timeoutMs, 5000);

    QTcpSocket *socket = new QTcpSocket(backend);
    QTimer *timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->setInterval(safeTimeoutMs);

    auto done = [backend, socket, timer](bool success, const QString &errorMsg) {
        // connected/error/timeout 중복 콜백이 들어와도 1회만 완료 처리
        if (socket->property("probe_done").toBool()) {
            return;
        }
        socket->setProperty("probe_done", true);
        timer->stop();
        socket->abort();
        emit backend->rtspProbeFinished(success, errorMsg);
        socket->deleteLater();
    };

    QObject::connect(socket, &QTcpSocket::connected, backend, [done]() {
        done(true, QString());
    });

    QObject::connect(socket, &QTcpSocket::errorOccurred, backend, [done, socket](QAbstractSocket::SocketError) {
        done(false, QString("RTSP 서버 연결 실패: %1").arg(socket->errorString()));
    });

    QObject::connect(timer, &QTimer::timeout, backend, [done]() {
        done(false, QString("RTSP 연결 확인 시간 초과"));
    });

    timer->start();
    socket->connectToHost(ipTrimmed, static_cast<quint16>(portNum));
}

