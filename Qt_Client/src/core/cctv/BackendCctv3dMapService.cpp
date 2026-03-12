#include "internal/cctv/BackendCctv3dMapService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QAbstractSocket>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSslError>
#include <QTimer>
#include <QUrl>

namespace {
constexpr int kCctv3dMapMoveStatusPollIntervalMs = 400;
constexpr int kCctv3dMapMoveStatusMaxAttempts = 20;
constexpr int kCctv3dMapAutofocusSettleMs = 5000;
constexpr int kCctv3dMapStartRetryDelayMs = 600;
constexpr int kCctv3dMapStartRetryMaxAttempts = 10;

bool parseZoomMovingState(const QString &body, int channel, bool *known, bool *moving)
{
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
        QStringLiteral("\\\"Channel\\\"\\s*:\\s*%1[^\\}]*\\\"Zoom\\\"\\s*:\\s*\\\"(Idle|Moving)\\\"")
            .arg(channel),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression anyChannelJson(
        QStringLiteral("\\\"Zoom\\\"\\s*:\\s*\\\"(Idle|Moving)\\\""),
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
} // namespace

bool BackendCctv3dMapService::startCctv3dMapSequence(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    if (cameraIndex < 0 || cameraIndex > 3) {
        emit backend->cameraControlMessage("3D Map start failed: invalid camera index", true);
        return false;
    }

    state->m_cctv3dMapSequenceToken += 1;
    state->m_cctv3dMapPendingStep = 0;
    if (state->m_cctv3dMapStepTimer) {
        state->m_cctv3dMapStepTimer->stop();
    }
    BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);

    state->m_cctv3dMapCameraIndex = cameraIndex;
    state->m_cctv3dMapMoveStatusPollCount = 0;
    state->m_cctv3dMapStartRetryCount = 0;
    state->m_cctv3dMapFrameCount = 0;
    state->m_cctv3dMapTotalBytes = 0;

    if (!state->m_cctv3dMapStepTimer) {
        state->m_cctv3dMapStepTimer = new QTimer(backend);
        state->m_cctv3dMapStepTimer->setSingleShot(true);
        QObject::connect(state->m_cctv3dMapStepTimer, &QTimer::timeout, backend, [backend, state]() {
            BackendCctv3dMapService::runCctv3dMapSequenceStep(
                backend,
                state,
                state->m_cctv3dMapSequenceToken,
                state->m_cctv3dMapPendingStep);
        });
    }

    emit backend->cameraControlMessage("3D Map: zoom out", false);
    if (!backend->sunapiZoomOut(cameraIndex)) {
        emit backend->cameraControlMessage("3D Map start aborted: zoom out failed", true);
        return false;
    }

    state->m_cctv3dMapPendingStep = 1;
    state->m_cctv3dMapStepTimer->start(kCctv3dMapMoveStatusPollIntervalMs);
    emit backend->cameraControlMessage("3D Map: waiting zoom settle via movestatus", false);
    return true;
}

void BackendCctv3dMapService::stopCctv3dMapSequence(Backend *backend, BackendPrivate *state)
{
    state->m_cctv3dMapSequenceToken += 1;
    state->m_cctv3dMapPendingStep = 0;
    state->m_cctv3dMapMoveStatusPollCount = 0;
    state->m_cctv3dMapStartRetryCount = 0;
    state->m_cctv3dMapCameraIndex = -1;
    if (state->m_cctv3dMapStepTimer) {
        state->m_cctv3dMapStepTimer->stop();
    }
    BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);

    QNetworkRequest request = backend->makeApiJsonRequest("/cctv/control/stop");
    backend->applyAuthIfNeeded(request);
    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    backend->attachIgnoreSslErrors(reply, "CCTV_3DMAP_STOP");
    QObject::connect(reply, &QNetworkReply::finished, backend, [reply]() {
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

    emit backend->cameraControlMessage("3D Map stopped", false);
}

void BackendCctv3dMapService::runCctv3dMapSequenceStep(Backend *backend, BackendPrivate *state, int sequenceToken, int step)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken || state->m_cctv3dMapCameraIndex < 0) {
        return;
    }

    if (step == 1) {
        BackendCctv3dMapService::pollCctv3dMapMoveStatus(backend, state, sequenceToken);
        return;
    }

    if (step == 2) {
        if (!backend->sunapiZoomStop(state->m_cctv3dMapCameraIndex)) {
            qWarning() << "[CCTV_3DMAP] zoom stop failed before autofocus";
        }

        emit backend->cameraControlMessage("3D Map: autofocus", false);
        if (!backend->sunapiSimpleAutoFocus(state->m_cctv3dMapCameraIndex)) {
            emit backend->cameraControlMessage("3D Map start aborted: autofocus failed", true);
            return;
        }

        state->m_cctv3dMapPendingStep = 3;
        if (state->m_cctv3dMapStepTimer) {
            state->m_cctv3dMapStepTimer->start(kCctv3dMapAutofocusSettleMs);
        }
        emit backend->cameraControlMessage("3D Map: wait 5s, then start API", false);
        return;
    }

    if (step == 3) {
        BackendCctv3dMapService::postCctvControlStart(backend, state, sequenceToken);
    }
}

void BackendCctv3dMapService::pollCctv3dMapMoveStatus(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken || state->m_cctv3dMapCameraIndex < 0) {
        return;
    }

    state->m_cctv3dMapMoveStatusPollCount += 1;
    const int attempt = state->m_cctv3dMapMoveStatusPollCount;

    QNetworkRequest request(backend->buildApiUrl("/sunapi/stw-cgi/ptzcontrol.cgi", {
        {"msubmenu", "movestatus"},
        {"action", "view"},
        {"Channel", QString::number(state->m_cctv3dMapCameraIndex)},
    }));
    backend->applySslIfNeeded(request);
    backend->applyAuthIfNeeded(request);

    QNetworkReply *reply = state->m_manager->get(request);
    backend->attachIgnoreSslErrors(reply, "CCTV_3DMAP_MOVE_STATUS");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken, attempt]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        reply->deleteLater();

        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            return;
        }

        bool zoomKnown = false;
        bool zoomMoving = false;
        bool parseOk = false;

        if (reply->error() == QNetworkReply::NoError && (statusCode < 400 || statusCode == 0)) {
            parseOk = parseZoomMovingState(body, state->m_cctv3dMapCameraIndex, &zoomKnown, &zoomMoving);
        } else {
            qWarning() << "[CCTV_3DMAP] movestatus request failed. status=" << statusCode
                       << "err=" << reply->errorString()
                       << "body=" << body.left(180);
        }

        if (parseOk && zoomKnown && !zoomMoving) {
            emit backend->cameraControlMessage("3D Map: zoom settled (Idle), starting autofocus", false);
            state->m_cctv3dMapPendingStep = 2;
            if (state->m_cctv3dMapStepTimer) {
                state->m_cctv3dMapStepTimer->start(30);
            }
            return;
        }

        if (attempt >= kCctv3dMapMoveStatusMaxAttempts) {
            emit backend->cameraControlMessage("3D Map: zoom status timeout, continue with autofocus", true);
            state->m_cctv3dMapPendingStep = 2;
            if (state->m_cctv3dMapStepTimer) {
                state->m_cctv3dMapStepTimer->start(30);
            }
            return;
        }

        if ((attempt % 4) == 0) {
            emit backend->cameraControlMessage(
                QString("3D Map: waiting zoom settle... (%1/%2)")
                    .arg(attempt)
                    .arg(kCctv3dMapMoveStatusMaxAttempts),
                false);
        }

        state->m_cctv3dMapPendingStep = 1;
        if (state->m_cctv3dMapStepTimer) {
            state->m_cctv3dMapStepTimer->start(kCctv3dMapMoveStatusPollIntervalMs);
        }
    });
}

bool BackendCctv3dMapService::postCctvControlStart(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken || state->m_cctv3dMapCameraIndex < 0) {
        return false;
    }

    QNetworkRequest request = backend->makeApiJsonRequest("/cctv/control/start");
    backend->applyAuthIfNeeded(request);
    const QJsonObject payload {
        {"channel", state->m_cctv3dMapCameraIndex},
        {"mode", "headless"},
    };

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    backend->attachIgnoreSslErrors(reply, "CCTV_3DMAP_START");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            reply->deleteLater();
            return;
        }

        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        if (!ok) {
            const bool isBusy = (statusCode == 409)
                                && body.contains("\"status\":\"BUSY\"", Qt::CaseInsensitive);
            if (isBusy && state->m_cctv3dMapStartRetryCount < kCctv3dMapStartRetryMaxAttempts) {
                state->m_cctv3dMapStartRetryCount += 1;
                const int retry = state->m_cctv3dMapStartRetryCount;
                emit backend->cameraControlMessage(
                    QString("3D Map start busy (retry %1/%2)...")
                        .arg(retry)
                        .arg(kCctv3dMapStartRetryMaxAttempts),
                    false);
                reply->deleteLater();
                QTimer::singleShot(kCctv3dMapStartRetryDelayMs, backend, [backend, state, sequenceToken]() {
                    BackendCctv3dMapService::postCctvControlStart(backend, state, sequenceToken);
                });
                return;
            }

            const QString err = QString("3D Map start API failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err, true);
            reply->deleteLater();
            return;
        }

        state->m_cctv3dMapStartRetryCount = 0;
        qInfo() << "[CCTV_3DMAP] start accepted. status=" << statusCode << "body=" << body.left(180);
        emit backend->cameraControlMessage("3D Map API start accepted", false);
        reply->deleteLater();
        BackendCctv3dMapService::postCctvControlStream(backend, state, sequenceToken);
    });

    return true;
}

bool BackendCctv3dMapService::postCctvControlStream(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken) {
        qWarning() << "[CCTV_3DMAP] skip stream request: token mismatch."
                   << "requested=" << sequenceToken
                   << "current=" << state->m_cctv3dMapSequenceToken;
        return false;
    }

    QNetworkRequest request = backend->makeApiJsonRequest("/cctv/control/stream");
    backend->applyAuthIfNeeded(request);
    const QJsonObject payload {
        {"stream", "pc_stream"},
    };
    const QByteArray bodyData = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    qInfo() << "[CCTV_3DMAP] stream request send."
            << "token=" << sequenceToken
            << "url=" << request.url().toString()
            << "body=" << QString::fromUtf8(bodyData);

    QNetworkReply *reply = state->m_manager->post(request, bodyData);
    backend->attachIgnoreSslErrors(reply, "CCTV_3DMAP_STREAM");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, state, reply, sequenceToken]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        qInfo() << "[CCTV_3DMAP] stream request finished."
                << "token=" << sequenceToken
                << "status=" << statusCode
                << "netErr=" << static_cast<int>(reply->error())
                << "errStr=" << reply->errorString();

        if (sequenceToken != state->m_cctv3dMapSequenceToken) {
            qWarning() << "[CCTV_3DMAP] stream reply ignored: token mismatch."
                       << "replyToken=" << sequenceToken
                       << "current=" << state->m_cctv3dMapSequenceToken;
            reply->deleteLater();
            return;
        }

        const bool ok = (reply->error() == QNetworkReply::NoError) && (statusCode < 400 || statusCode == 0);
        if (!ok) {
            const QString err = QString("3D Map stream request failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[CCTV_3DMAP]" << err << "body=" << body.left(180);
            emit backend->cameraControlMessage(err + " (try WS connect)", true);
        } else {
            qInfo() << "[CCTV_3DMAP] stream request OK. status=" << statusCode << "body=" << body.left(180);
            emit backend->cameraControlMessage("3D Map stream mode requested (pc_stream)", false);
        }

        reply->deleteLater();
        BackendCctv3dMapService::connectCctvStreamWs(backend, state, sequenceToken);
    });

    return true;
}

void BackendCctv3dMapService::connectCctvStreamWs(Backend *backend, BackendPrivate *state, int sequenceToken)
{
    if (sequenceToken != state->m_cctv3dMapSequenceToken) {
        return;
    }

    const QUrl apiBase(backend->serverUrl());
    const QString host = apiBase.host().trimmed();
    if (!apiBase.isValid() || host.isEmpty()) {
        emit backend->cameraControlMessage("3D Map WS connect failed: invalid API_URL", true);
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

    if (!state->m_cctvStreamWs) {
        state->m_cctvStreamWs = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, backend);

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::connected, backend, [backend, state]() {
            if (state->m_cctv3dMapWsActiveToken != state->m_cctv3dMapSequenceToken) {
                BackendCctv3dMapService::disconnectCctvStreamWs(backend, state, true);
                return;
            }
            state->m_cctv3dMapFrameCount = 0;
            state->m_cctv3dMapTotalBytes = 0;
            emit backend->cameraControlMessage("3D Map WS connected", false);
            qInfo() << "[CCTV_3DMAP][WS] connected";
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::disconnected, backend, [backend, state]() {
            if (state->m_cctv3dMapStoppingExpected) {
                state->m_cctv3dMapStoppingExpected = false;
                return;
            }
            emit backend->cameraControlMessage("3D Map WS disconnected", true);
            qWarning() << "[CCTV_3DMAP][WS] disconnected unexpectedly";
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::binaryMessageReceived, backend, [backend, state](const QByteArray &payload) {
            if (state->m_cctv3dMapWsActiveToken != state->m_cctv3dMapSequenceToken) {
                return;
            }
            state->m_cctv3dMapFrameCount += 1;
            state->m_cctv3dMapTotalBytes += payload.size();

            if (state->m_cctv3dMapFrameCount == 1) {
                emit backend->cameraControlMessage(
                    QString("3D Map WS first frame received (%1 bytes)").arg(payload.size()),
                    false);
            } else if ((state->m_cctv3dMapFrameCount % 30) == 0) {
                qInfo() << "[CCTV_3DMAP][WS] frames=" << state->m_cctv3dMapFrameCount
                        << "bytes=" << state->m_cctv3dMapTotalBytes;
            }
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::textMessageReceived, backend, [state](const QString &message) {
            if (state->m_cctv3dMapWsActiveToken != state->m_cctv3dMapSequenceToken) {
                return;
            }
            qInfo() << "[CCTV_3DMAP][WS][TEXT]" << message.left(180);
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::errorOccurred, backend, [backend, state](QAbstractSocket::SocketError) {
            const QString err = state->m_cctvStreamWs
                                    ? state->m_cctvStreamWs->errorString()
                                    : QString("unknown websocket error");
            emit backend->cameraControlMessage("3D Map WS error: " + err, true);
            qWarning() << "[CCTV_3DMAP][WS] error:" << err;
        });

        QObject::connect(state->m_cctvStreamWs, &QWebSocket::sslErrors, backend, [state](const QList<QSslError> &errors) {
            for (const auto &err : errors) {
                qWarning() << "[CCTV_3DMAP][WS][SSL]" << err.errorString();
            }
            if (state->m_sslIgnoreErrors && state->m_cctvStreamWs) {
                state->m_cctvStreamWs->ignoreSslErrors();
            }
        });
    } else if (state->m_cctvStreamWs->state() == QAbstractSocket::ConnectedState
               || state->m_cctvStreamWs->state() == QAbstractSocket::ConnectingState) {
        state->m_cctv3dMapStoppingExpected = true;
        state->m_cctvStreamWs->abort();
    }

    state->m_cctv3dMapWsActiveToken = sequenceToken;
    state->m_cctv3dMapStoppingExpected = false;
    if (wsScheme == "wss" && state->m_sslConfigReady) {
        state->m_cctvStreamWs->setSslConfiguration(state->m_sslConfig);
    }

    emit backend->cameraControlMessage("3D Map WS connecting...", false);
    state->m_cctvStreamWs->open(wsUrl);
}

void BackendCctv3dMapService::disconnectCctvStreamWs(Backend *backend, BackendPrivate *state, bool expectedStop)
{
    Q_UNUSED(backend);
    state->m_cctv3dMapStoppingExpected = expectedStop;
    if (!state->m_cctvStreamWs) {
        return;
    }

    if (state->m_cctvStreamWs->state() == QAbstractSocket::ConnectedState
        || state->m_cctvStreamWs->state() == QAbstractSocket::ConnectingState) {
        state->m_cctvStreamWs->close();
    }
}

