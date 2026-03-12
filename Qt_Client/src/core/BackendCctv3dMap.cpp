#include "Backend.h"

#include <QAbstractSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslError>
#include <QUrl>

namespace {
constexpr int kCctv3dMapMoveStatusPollIntervalMs = 400;
constexpr int kCctv3dMapMoveStatusMaxAttempts = 20;
constexpr int kCctv3dMapAutofocusSettleMs = 5000;
constexpr int kCctv3dMapStartRetryDelayMs = 600;
constexpr int kCctv3dMapStartRetryMaxAttempts = 10;

bool parseZoomMovingState(const QString &body, int channel, bool *known, bool *moving) {
    if (!known || !moving) {
        return false;
    }
    *known = false;
    *moving = false;

    const QRegularExpression byChannelText(
        QString("Channel\\.%1\\.Zoom\\s*=\\s*(Idle|Moving)").arg(channel),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression anyChannelText(
        QStringLiteral("Zoom\\s*=\\s*(Idle|Moving)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression byChannelJson(
        QStringLiteral("\"Channel\"\\s*:\\s*%1[^\\}]*\"Zoom\"\\s*:\\s*\"(Idle|Moving)\"")
            .arg(channel),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression anyChannelJson(
        QStringLiteral("\"Zoom\"\\s*:\\s*\"(Idle|Moving)\""),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatch m = byChannelText.match(body);
    if (!m.hasMatch()) {
        m = byChannelJson.match(body);
    }
    if (!m.hasMatch()) {
        m = anyChannelText.match(body);
    }
    if (!m.hasMatch()) {
        m = anyChannelJson.match(body);
    }
    if (!m.hasMatch()) {
        return false;
    }

    const QString st = m.captured(1).trimmed();
    *known = true;
    *moving = (st.compare("Moving", Qt::CaseInsensitive) == 0);
    return true;
}
}

bool Backend::startCctv3dMapSequence(int cameraIndex) {
    if (cameraIndex < 0 || cameraIndex > 3) {
        emit cameraControlMessage("3D Map start failed: invalid camera index", true);
        return false;
    }

    // Cancel any previous sequence callbacks and close stale WS first.
    m_cctv3dMapSequenceToken += 1;
    m_cctv3dMapPendingStep = 0;
    if (m_cctv3dMapStepTimer) {
        m_cctv3dMapStepTimer->stop();
    }
    disconnectCctvStreamWs(true);

    m_cctv3dMapCameraIndex = cameraIndex;
    m_cctv3dMapMoveStatusPollCount = 0;
    m_cctv3dMapStartRetryCount = 0;
    m_cctv3dMapFrameCount = 0;
    m_cctv3dMapTotalBytes = 0;

    if (!m_cctv3dMapStepTimer) {
        m_cctv3dMapStepTimer = new QTimer(this);
        m_cctv3dMapStepTimer->setSingleShot(true);
        connect(m_cctv3dMapStepTimer, &QTimer::timeout, this, [this]() {
            runCctv3dMapSequenceStep(m_cctv3dMapSequenceToken, m_cctv3dMapPendingStep);
        });
    }

    emit cameraControlMessage("3D Map: zoom out", false);
    if (!sunapiZoomOut(cameraIndex)) {
        emit cameraControlMessage("3D Map start aborted: zoom out failed", true);
        return false;
    }

    m_cctv3dMapPendingStep = 1;
    m_cctv3dMapStepTimer->start(kCctv3dMapMoveStatusPollIntervalMs);
    emit cameraControlMessage("3D Map: waiting zoom settle via movestatus", false);
    return true;
}

void Backend::stopCctv3dMapSequence() {
    m_cctv3dMapSequenceToken += 1;
    m_cctv3dMapPendingStep = 0;
    m_cctv3dMapMoveStatusPollCount = 0;
    m_cctv3dMapStartRetryCount = 0;
    m_cctv3dMapCameraIndex = -1;
    if (m_cctv3dMapStepTimer) {
        m_cctv3dMapStepTimer->stop();
    }
    disconnectCctvStreamWs(true);

    QNetworkRequest request = makeApiJsonRequest("/cctv/control/stop");
    applyAuthIfNeeded(request);
    QNetworkReply *reply = m_manager->post(request, QByteArray());
    attachIgnoreSslErrors(reply, "CCTV_3DMAP_STOP");
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        if (reply->error() == QNetworkReply::NoError && (statusCode < 400 || statusCode == 0)) {
            qInfo() << "[CCTV_3DMAP] stop requested. status=" << statusCode;
        } else {
            qWarning() << "[CCTV_3DMAP] stop failed. status=" << statusCode
                       << "err=" << reply->errorString()
                       << "body=" << body.left(180);
        }
        reply->deleteLater();
    });

    emit cameraControlMessage("3D Map stopped", false);
}

void Backend::runCctv3dMapSequenceStep(int sequenceToken, int step) {
    if (sequenceToken != m_cctv3dMapSequenceToken || m_cctv3dMapCameraIndex < 0) {
        return;
    }

    if (step == 1) {
        pollCctv3dMapMoveStatus(sequenceToken);
        return;
    }

    if (step == 2) {
        // Some cameras keep zoom moving until stop; try stop before autofocus.
        if (!sunapiZoomStop(m_cctv3dMapCameraIndex)) {
            qWarning() << "[CCTV_3DMAP] zoom stop failed before autofocus";
        }

        emit cameraControlMessage("3D Map: autofocus", false);
        if (!sunapiSimpleAutoFocus(m_cctv3dMapCameraIndex)) {
            emit cameraControlMessage("3D Map start aborted: autofocus failed", true);
            return;
        }

        m_cctv3dMapPendingStep = 3;
        if (m_cctv3dMapStepTimer) {
            m_cctv3dMapStepTimer->start(kCctv3dMapAutofocusSettleMs);
        }
        emit cameraControlMessage("3D Map: wait 5s, then start API", false);
        return;
    }

    if (step == 3) {
        postCctvControlStart(sequenceToken);
    }
}

void Backend::pollCctv3dMapMoveStatus(int sequenceToken) {
    if (sequenceToken != m_cctv3dMapSequenceToken || m_cctv3dMapCameraIndex < 0) {
        return;
    }

    m_cctv3dMapMoveStatusPollCount += 1;
    const int attempt = m_cctv3dMapMoveStatusPollCount;

    QNetworkRequest request(buildApiUrl("/sunapi/stw-cgi/ptzcontrol.cgi", {
        {"msubmenu", "movestatus"},
        {"action", "view"},
        {"Channel", QString::number(m_cctv3dMapCameraIndex)},
    }));
    applySslIfNeeded(request);
    applyAuthIfNeeded(request);

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "CCTV_3DMAP_MOVE_STATUS");
    connect(reply, &QNetworkReply::finished, this, [this, reply, sequenceToken, attempt]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        reply->deleteLater();

        if (sequenceToken != m_cctv3dMapSequenceToken) {
            return;
        }

        bool zoomKnown = false;
        bool zoomMoving = false;
        bool parseOk = false;

        if (reply->error() == QNetworkReply::NoError && (statusCode < 400 || statusCode == 0)) {
            parseOk = parseZoomMovingState(body, m_cctv3dMapCameraIndex, &zoomKnown, &zoomMoving);
        } else {
            qWarning() << "[CCTV_3DMAP] movestatus request failed. status=" << statusCode
                       << "err=" << reply->errorString()
                       << "body=" << body.left(180);
        }

        if (parseOk && zoomKnown && !zoomMoving) {
            emit cameraControlMessage("3D Map: zoom settled (Idle), starting autofocus", false);
            m_cctv3dMapPendingStep = 2;
            if (m_cctv3dMapStepTimer) {
                m_cctv3dMapStepTimer->start(30);
            }
            return;
        }

        if (attempt >= kCctv3dMapMoveStatusMaxAttempts) {
            emit cameraControlMessage("3D Map: zoom status timeout, continue with autofocus", true);
            m_cctv3dMapPendingStep = 2;
            if (m_cctv3dMapStepTimer) {
                m_cctv3dMapStepTimer->start(30);
            }
            return;
        }

        if ((attempt % 4) == 0) {
            emit cameraControlMessage(
                QString("3D Map: waiting zoom settle... (%1/%2)")
                    .arg(attempt)
                    .arg(kCctv3dMapMoveStatusMaxAttempts),
                false);
        }

        m_cctv3dMapPendingStep = 1;
        if (m_cctv3dMapStepTimer) {
            m_cctv3dMapStepTimer->start(kCctv3dMapMoveStatusPollIntervalMs);
        }
    });
}

bool Backend::postCctvControlStart(int sequenceToken) {
    if (sequenceToken != m_cctv3dMapSequenceToken || m_cctv3dMapCameraIndex < 0) {
        return false;
    }

    QNetworkRequest request = makeApiJsonRequest("/cctv/control/start");
    applyAuthIfNeeded(request);
    const QJsonObject payload {
        {"channel", m_cctv3dMapCameraIndex},
        {"mode", "headless"},
    };

    QNetworkReply *reply = m_manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    attachIgnoreSslErrors(reply, "CCTV_3DMAP_START");
    connect(reply, &QNetworkReply::finished, this, [this, reply, sequenceToken]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (sequenceToken != m_cctv3dMapSequenceToken) {
            reply->deleteLater();
            return;
        }

        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        if (!ok) {
            const bool isBusy = (statusCode == 409)
                                && body.contains("\"status\":\"BUSY\"", Qt::CaseInsensitive);
            if (isBusy && m_cctv3dMapStartRetryCount < kCctv3dMapStartRetryMaxAttempts) {
                m_cctv3dMapStartRetryCount += 1;
                const int retry = m_cctv3dMapStartRetryCount;
                emit cameraControlMessage(
                    QString("3D Map start busy (retry %1/%2)...")
                        .arg(retry)
                        .arg(kCctv3dMapStartRetryMaxAttempts),
                    false);
                reply->deleteLater();
                QTimer::singleShot(kCctv3dMapStartRetryDelayMs, this, [this, sequenceToken]() {
                    postCctvControlStart(sequenceToken);
                });
                return;
            }

            const QString err = QString("3D Map start API failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        m_cctv3dMapStartRetryCount = 0;
        qInfo() << "[CCTV_3DMAP] start accepted. status=" << statusCode << "body=" << body.left(180);
        emit cameraControlMessage("3D Map API start accepted", false);
        reply->deleteLater();
        postCctvControlStream(sequenceToken);
    });

    return true;
}

bool Backend::postCctvControlStream(int sequenceToken) {
    if (sequenceToken != m_cctv3dMapSequenceToken) {
        qWarning() << "[CCTV_3DMAP] skip stream request: token mismatch."
                   << "requested=" << sequenceToken
                   << "current=" << m_cctv3dMapSequenceToken;
        return false;
    }

    QNetworkRequest request = makeApiJsonRequest("/cctv/control/stream");
    applyAuthIfNeeded(request);
    const QJsonObject payload {
        {"stream", "pc_stream"},
    };
    const QByteArray bodyData = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    qInfo() << "[CCTV_3DMAP] stream request send."
            << "token=" << sequenceToken
            << "url=" << request.url().toString()
            << "body=" << QString::fromUtf8(bodyData);

    QNetworkReply *reply = m_manager->post(request, bodyData);
    attachIgnoreSslErrors(reply, "CCTV_3DMAP_STREAM");
    connect(reply, &QNetworkReply::finished, this, [this, reply, sequenceToken]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        qInfo() << "[CCTV_3DMAP] stream request finished."
                << "token=" << sequenceToken
                << "status=" << statusCode
                << "netErr=" << static_cast<int>(reply->error())
                << "errStr=" << reply->errorString();

        if (sequenceToken != m_cctv3dMapSequenceToken) {
            qWarning() << "[CCTV_3DMAP] stream reply ignored: token mismatch."
                       << "replyToken=" << sequenceToken
                       << "current=" << m_cctv3dMapSequenceToken;
            reply->deleteLater();
            return;
        }

        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        if (!ok) {
            const QString err = QString("3D Map stream request failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit cameraControlMessage(err + " (try WS connect)", true);
        } else {
            qInfo() << "[CCTV_3DMAP] stream request OK. status=" << statusCode << "body=" << body.left(180);
            emit cameraControlMessage("3D Map stream mode requested (pc_stream)", false);
        }

        reply->deleteLater();
        connectCctvStreamWs(sequenceToken);
    });

    return true;
}

void Backend::connectCctvStreamWs(int sequenceToken) {
    if (sequenceToken != m_cctv3dMapSequenceToken) {
        return;
    }

    const QUrl apiBase(serverUrl());
    const QString host = apiBase.host().trimmed();
    if (!apiBase.isValid() || host.isEmpty()) {
        emit cameraControlMessage("3D Map WS connect failed: invalid API_URL", true);
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
    wsUrl.setPath("/cctv/stream");

    if (!m_cctvStreamWs) {
        m_cctvStreamWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

        connect(m_cctvStreamWs, &QWebSocket::connected, this, [this]() {
            if (m_cctv3dMapWsActiveToken != m_cctv3dMapSequenceToken) {
                disconnectCctvStreamWs(true);
                return;
            }
            m_cctv3dMapFrameCount = 0;
            m_cctv3dMapTotalBytes = 0;
            emit cameraControlMessage("3D Map WS connected", false);
            qInfo() << "[CCTV_3DMAP][WS] connected";
        });

        connect(m_cctvStreamWs, &QWebSocket::disconnected, this, [this]() {
            if (m_cctv3dMapStoppingExpected) {
                m_cctv3dMapStoppingExpected = false;
                return;
            }
            emit cameraControlMessage("3D Map WS disconnected", true);
            qWarning() << "[CCTV_3DMAP][WS] disconnected unexpectedly";
        });

        connect(m_cctvStreamWs, &QWebSocket::binaryMessageReceived, this, [this](const QByteArray &payload) {
            if (m_cctv3dMapWsActiveToken != m_cctv3dMapSequenceToken) {
                return;
            }
            m_cctv3dMapFrameCount += 1;
            m_cctv3dMapTotalBytes += payload.size();

            if (m_cctv3dMapFrameCount == 1) {
                emit cameraControlMessage(
                    QString("3D Map WS first frame received (%1 bytes)").arg(payload.size()),
                    false);
            } else if ((m_cctv3dMapFrameCount % 30) == 0) {
                qInfo() << "[CCTV_3DMAP][WS] frames=" << m_cctv3dMapFrameCount
                        << "bytes=" << m_cctv3dMapTotalBytes;
            }
        });

        connect(m_cctvStreamWs, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
            if (m_cctv3dMapWsActiveToken != m_cctv3dMapSequenceToken) {
                return;
            }
            qInfo() << "[CCTV_3DMAP][WS][TEXT]" << message.left(180);
        });

        connect(m_cctvStreamWs, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            const QString err = m_cctvStreamWs ? m_cctvStreamWs->errorString() : QString("unknown websocket error");
            emit cameraControlMessage("3D Map WS error: " + err, true);
            qWarning() << "[CCTV_3DMAP][WS] error:" << err;
        });

        connect(m_cctvStreamWs, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
            for (const auto &err : errors) {
                qWarning() << "[CCTV_3DMAP][WS][SSL]" << err.errorString();
            }
            if (m_sslIgnoreErrors && m_cctvStreamWs) {
                m_cctvStreamWs->ignoreSslErrors();
            }
        });
    } else if (m_cctvStreamWs->state() == QAbstractSocket::ConnectedState
               || m_cctvStreamWs->state() == QAbstractSocket::ConnectingState) {
        m_cctv3dMapStoppingExpected = true;
        m_cctvStreamWs->abort();
    }

    m_cctv3dMapWsActiveToken = sequenceToken;
    m_cctv3dMapStoppingExpected = false;
    if (wsScheme == "wss" && m_sslConfigReady) {
        m_cctvStreamWs->setSslConfiguration(m_sslConfig);
    }

    emit cameraControlMessage("3D Map WS connecting...", false);
    m_cctvStreamWs->open(wsUrl);
}

void Backend::disconnectCctvStreamWs(bool expectedStop) {
    m_cctv3dMapStoppingExpected = expectedStop;
    if (!m_cctvStreamWs) {
        return;
    }

    if (m_cctvStreamWs->state() == QAbstractSocket::ConnectedState
        || m_cctvStreamWs->state() == QAbstractSocket::ConnectingState) {
        m_cctvStreamWs->close();
    }
}
