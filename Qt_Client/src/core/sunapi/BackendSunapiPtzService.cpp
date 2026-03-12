#include "internal/sunapi/BackendSunapiPtzService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QDebug>
#include <QNetworkReply>
#include <QNetworkRequest>

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

bool BackendSunapiPtzService::sunapiZoomIn(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "zoom_in", "Zoom In");
}

bool BackendSunapiPtzService::sunapiZoomOut(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "zoom_out", "Zoom Out");
}

bool BackendSunapiPtzService::sunapiZoomStop(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "zoom_stop", "Zoom Stop");
}

bool BackendSunapiPtzService::sunapiFocusNear(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "focus_near", "Focus Near");
}

bool BackendSunapiPtzService::sunapiFocusFar(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "focus_far", "Focus Far");
}

bool BackendSunapiPtzService::sunapiFocusStop(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "focus_stop", "Focus Stop");
}

bool BackendSunapiPtzService::sunapiSimpleAutoFocus(Backend *backend, BackendPrivate *state, int cameraIndex)
{
    return sendSunapiPtzFocusCommand(backend, state, cameraIndex, "autofocus", "Auto Focus");
}

