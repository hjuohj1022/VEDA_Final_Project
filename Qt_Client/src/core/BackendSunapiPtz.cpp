#include "Backend.h"

#include <QDebug>
#include <QNetworkRequest>

bool Backend::sendSunapiPtzFocusCommand(int cameraIndex,
                                        const QString &command,
                                        const QString &actionLabel) {
    if (cameraIndex < 0) {
        emit cameraControlMessage(QString("%1 failed: invalid camera index").arg(actionLabel), true);
        return false;
    }

    if (m_authToken.trimmed().isEmpty()) {
        emit cameraControlMessage(QString("%1 failed: login required").arg(actionLabel), true);
        return false;
    }

    QNetworkRequest request = makeApiJsonRequest("/api/sunapi/ptz/focus", {
        {"channel", QString::number(cameraIndex)},
        {"command", command}
    });
    applyAuthIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << actionLabel << "url=" << request.url().toString();

    QNetworkReply *reply = m_manager->post(request, QByteArray());
    attachIgnoreSslErrors(reply, "SUNAPI_PTZ");
    connect(reply, &QNetworkReply::finished, this, [this, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (reply->error() == QNetworkReply::NoError) {
            QString sunapiErrMsg;
            const bool sunapiBodyError = isSunapiBodyError(body, &sunapiErrMsg);
            if (sunapiBodyError) {
                const QString err = QString("%1 failed: device response error (%2)")
                                        .arg(actionLabel, sunapiErrMsg);
                qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(200);
                emit cameraControlMessage(err, true);
            } else {
                emit cameraControlMessage(QString("%1 success").arg(actionLabel), false);
            }
        } else {
            const QString err = QString("%1 failed (HTTP %2): %3")
                                    .arg(actionLabel)
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit cameraControlMessage(err, true);
        }

        reply->deleteLater();
    });

    return true;
}

// SUNAPI focus 명령을 통한 Zoom/Focus 제어
bool Backend::sunapiZoomIn(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "zoom_in", "Zoom In");
}

bool Backend::sunapiZoomOut(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "zoom_out", "Zoom Out");
}

bool Backend::sunapiZoomStop(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "zoom_stop", "Zoom Stop");
}

bool Backend::sunapiFocusNear(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "focus_near", "Focus Near");
}

bool Backend::sunapiFocusFar(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "focus_far", "Focus Far");
}

bool Backend::sunapiFocusStop(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "focus_stop", "Focus Stop");
}

bool Backend::sunapiSimpleAutoFocus(int cameraIndex) {
    return sendSunapiPtzFocusCommand(cameraIndex, "autofocus", "Auto Focus");
}