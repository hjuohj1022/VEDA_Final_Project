#include "Backend.h"

#include <QTcpSocket>
#include <QTimer>

void Backend::probeRtspEndpoint(QString ip, QString port, int timeoutMs) {
    const QString ipTrimmed = ip.trimmed();
    if (ipTrimmed.isEmpty()) {
        emit rtspProbeFinished(false, "IP가 비어 있습니다.");
        return;
    }

    bool ok = false;
    int portNum = port.trimmed().toInt(&ok);
    if (!ok || portNum < 1 || portNum > 65535) {
        portNum = 8555;
    }

    const int safeTimeoutMs = qBound(300, timeoutMs, 5000);

    QTcpSocket *socket = new QTcpSocket(this);
    QTimer *timer = new QTimer(socket);
    timer->setSingleShot(true);
    timer->setInterval(safeTimeoutMs);

    auto done = [this, socket, timer](bool success, const QString &errorMsg) {
        // connected/error/timeout 중복 콜백이 들어와도 1회만 완료 처리
        if (socket->property("probe_done").toBool()) {
            return;
        }
        socket->setProperty("probe_done", true);
        timer->stop();
        socket->abort();
        emit rtspProbeFinished(success, errorMsg);
        socket->deleteLater();
    };

    connect(socket, &QTcpSocket::connected, this, [done]() {
        done(true, QString());
    });

    connect(socket, &QTcpSocket::errorOccurred, this,
            [done, socket](QAbstractSocket::SocketError) {
                done(false, QString("RTSP 서버 연결 실패: %1").arg(socket->errorString()));
            });

    connect(timer, &QTimer::timeout, this, [done]() {
        done(false, QString("RTSP 연결 확인 시간 초과"));
    });

    timer->start();
    socket->connectToHost(ipTrimmed, static_cast<quint16>(portNum));
}
