#include "internal/sunapi/BackendSunapiPtzService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

// Sunapi PTZ 포커스 명령 전송 함수
bool BackendSunapiPtzService::sendSunapiPtzFocusCommand(Backend *backend,
                                                        BackendPrivate *state,
                                                        int cameraIndex,
                                                        const QString &command,
                                                        const QString &actionLabel)
{
    if (cameraIndex < 0) {
        emit backend->cameraControlMessage(QString("%1 failed: invalid camera index").arg(actionLabel), true);
        return false;
    }

    if (state->m_authToken.trimmed().isEmpty()) {
        emit backend->cameraControlMessage(QString("%1 failed: login required").arg(actionLabel), true);
        return false;
    }

    // API JSON 요청 생성 함수
    QNetworkRequest request = backend->makeApiJsonRequest("/api/sunapi/ptz/focus", {
        { "channel", QString::number(cameraIndex) },
        { "command", command }
    });
    backend->applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << actionLabel << "url=" << request.url().toString();

    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    backend->attachIgnoreSslErrors(reply, "SUNAPI_PTZ");
    QObject::connect(reply, &QNetworkReply::finished, backend, [backend, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (reply->error() == QNetworkReply::NoError) {
            QString sunapiErrMsg;
            const bool sunapiBodyError = backend->isSunapiBodyError(body, &sunapiErrMsg);
            if (sunapiBodyError) {
                const QString err = QString("%1 failed: device response error (%2)").arg(actionLabel, sunapiErrMsg);
                qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
                emit backend->cameraControlMessage(err, true);
            } else {
                emit backend->cameraControlMessage(QString("%1 success").arg(actionLabel), false);
            }
        } else {
            const QString err = QString("%1 failed (HTTP %2): %3").arg(actionLabel).arg(statusCode).arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit backend->cameraControlMessage(err, true);
        }

        reply->deleteLater();
    });

    return true;
}

// Sunapi 줌 인 함수
bool BackendSunapiPtzService::sunapiZoomIn(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "zoom_in", "Zoom In");
}

// Sunapi 줌 아웃 함수
bool BackendSunapiPtzService::sunapiZoomOut(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "zoom_out", "Zoom Out");
}

// Sunapi 줌 중지 함수
bool BackendSunapiPtzService::sunapiZoomStop(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "zoom_stop", "Zoom Stop");
}

// Sunapi 포커스 근거리 이동 함수
bool BackendSunapiPtzService::sunapiFocusNear(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "focus_near", "Focus Near");
}

// Sunapi 포커스 원거리 이동 함수
bool BackendSunapiPtzService::sunapiFocusFar(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "focus_far", "Focus Far");
}

// Sunapi 포커스 중지 함수
bool BackendSunapiPtzService::sunapiFocusStop(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "focus_stop", "Focus Stop");
}

// Sunapi 단순 자동 포커스 함수
bool BackendSunapiPtzService::sunapiSimpleAutoFocus(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    // Sunapi PTZ 포커스 명령 전송 함수
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "autofocus", "Auto Focus");
}

