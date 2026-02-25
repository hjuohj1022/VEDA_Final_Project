#include "Backend.h"

#include <QDebug>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

bool Backend::sendSunapiCommand(const QString &cgiName,
                                const QMap<QString, QString> &params,
                                int cameraIndex,
                                const QString &actionLabel,
                                bool includeChannelParam) {
    if (cameraIndex < 0) {
        emit cameraControlMessage(QString("%1 failed: invalid camera index").arg(actionLabel), true);
        return false;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage(QString("%1 failed: SUNAPI_IP is empty").arg(actionLabel), true);
        return false;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath(QString("/stw-cgi/%1").arg(cgiName));

    QUrlQuery query;
    // Keep canonical order for compatibility: msubmenu -> action -> Channel -> others.
    if (params.contains("msubmenu")) {
        query.addQueryItem("msubmenu", params.value("msubmenu"));
    }
    if (params.contains("action")) {
        query.addQueryItem("action", params.value("action"));
    }

    if (params.contains("Channel")) {
        query.addQueryItem("Channel", params.value("Channel"));
    } else if (params.contains("channel")) {
        query.addQueryItem("channel", params.value("channel"));
    } else if (includeChannelParam) {
        query.addQueryItem("Channel", QString::number(cameraIndex));
    }

    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (it.key() == "msubmenu" || it.key() == "action" || it.key() == "Channel" || it.key() == "channel") {
            continue;
        }
        query.addQueryItem(it.key(), it.value());
    }
    url.setQuery(query);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << actionLabel << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI");
    connect(reply, &QNetworkReply::finished, this, [this, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString bodyLower = body.toLower();

        if (reply->error() == QNetworkReply::NoError) {
            bool sunapiBodyError = false;
            QString sunapiErrMsg;

            if (!body.isEmpty()) {
                const QRegularExpression errPattern("^\\s*error\\s*=\\s*([^\\r\\n]+)",
                                                    QRegularExpression::CaseInsensitiveOption
                                                    | QRegularExpression::MultilineOption);
                const QRegularExpressionMatch errMatch = errPattern.match(body);
                if (errMatch.hasMatch()) {
                    const QString errValue = errMatch.captured(1).trimmed();
                    const QString errValueLower = errValue.toLower();
                    if (errValueLower != "0"
                        && errValueLower != "ok"
                        && errValueLower != "none"
                        && errValueLower != "success") {
                        sunapiBodyError = true;
                        sunapiErrMsg = QString("Error=%1").arg(errValue);
                    }
                }

                if (!sunapiBodyError
                    && (bodyLower.contains("fail")
                        || bodyLower.contains("unsupported")
                        || bodyLower.contains("not support")
                        || bodyLower.contains("invalid")
                        || bodyLower.startsWith("ng"))) {
                    sunapiBodyError = true;
                    sunapiErrMsg = body.left(160);
                }
            }

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

bool Backend::sunapiZoomIn(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "In"}},
        cameraIndex,
        "Zoom In");
}

bool Backend::sunapiZoomOut(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Out"}},
        cameraIndex,
        "Zoom Out");
}

bool Backend::sunapiZoomStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Stop"}},
        cameraIndex,
        "Zoom Stop");
}

bool Backend::sunapiFocusNear(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Near"}},
        cameraIndex,
        "Focus Near");
}

bool Backend::sunapiFocusFar(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Far"}},
        cameraIndex,
        "Focus Far");
}

bool Backend::sunapiFocusStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Stop"}},
        cameraIndex,
        "Focus Stop");
}

bool Backend::sunapiSimpleAutoFocus(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"Mode", "SimpleFocus"}},
        cameraIndex,
        "Auto Focus");
}

bool Backend::sunapiSetWhiteBalanceMode(int cameraIndex, QString mode) {
    QString normalized = mode.trimmed();
    if (normalized.isEmpty()) {
        emit cameraControlMessage("White balance mode is empty", true);
        return false;
    }

    // Common SUNAPI values: ATW / Indoor / Outdoor / Manual / AWC
    if (normalized.compare("auto", Qt::CaseInsensitive) == 0) {
        normalized = "ATW";
    }

    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "whitebalance"}, {"action", "set"}, {"WhiteBalanceMode", normalized}},
        cameraIndex,
        QString("WhiteBalance %1").arg(normalized));
}

bool Backend::sunapiSetFlipAndRotate(int cameraIndex, bool horizontalFlip, bool verticalFlip, int rotate) {
    int normalizedRotate = rotate;
    if (normalizedRotate < 0) normalizedRotate = 0;
    if (normalizedRotate > 270) normalizedRotate = 270;
    // Force 90-degree steps expected by flip submenu.
    normalizedRotate = (normalizedRotate / 90) * 90;

    return sendSunapiCommand(
        "image.cgi",
        {
            {"msubmenu", "flip"},
            {"action", "set"},
            {"HorizontalFlipEnable", horizontalFlip ? "True" : "False"},
            {"VerticalFlipEnable", verticalFlip ? "True" : "False"},
            {"Rotate", QString::number(normalizedRotate)}
        },
        cameraIndex,
        QString("Flip/Rotate H:%1 V:%2 R:%3")
            .arg(horizontalFlip ? "On" : "Off")
            .arg(verticalFlip ? "On" : "Off")
            .arg(normalizedRotate));
}

void Backend::sunapiLoadSupportedPtzActions(int cameraIndex) {
    if (cameraIndex < 0) {
        emit cameraControlMessage("PTZ capability query failed: invalid camera index", true);
        return;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage("PTZ capability query failed: SUNAPI_IP is empty", true);
        return;
    }

    const QString schemeRaw = m_env.value("SUNAPI_SCHEME", "http").trimmed().toLower();
    const QString scheme = (schemeRaw == "https") ? QString("https") : QString("http");
    const int defaultPort = (scheme == "https") ? 443 : 80;
    const int port = m_env.value("SUNAPI_PORT", QString::number(defaultPort)).toInt();

    QUrl url;
    url.setScheme(scheme);
    url.setHost(host);
    if (port > 0) {
        url.setPort(port);
    }
    url.setPath("/stw-cgi/ptzcontrol.cgi");

    QUrlQuery query;
    query.addQueryItem("msubmenu", "supportedptzactions");
    query.addQueryItem("action", "view");
    query.addQueryItem("Channel", QString::number(cameraIndex));
    url.setQuery(query);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << "Supported PTZ actions" << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI_CAPS");
    connect(reply, &QNetworkReply::finished, this, [this, reply, cameraIndex]() {
        QVariantMap actions;
        actions.insert("zoom", true);
        actions.insert("focus", true);

        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();
        const QString lower = body.toLower();

        if (reply->error() == QNetworkReply::NoError) {
            const bool zoomUnsupported =
                (lower.contains("zoom=\"false\"")
                 || lower.contains("zoom=false")
                 || lower.contains("zoom: false")
                 || lower.contains("zoom unsupported"));
            const bool focusUnsupported =
                (lower.contains("focus=\"false\"")
                 || lower.contains("focus=false")
                 || lower.contains("focus: false")
                 || lower.contains("focus unsupported"));

            if (zoomUnsupported) actions.insert("zoom", false);
            if (focusUnsupported) actions.insert("focus", false);

            emit sunapiSupportedPtzActionsLoaded(cameraIndex, actions);
            emit cameraControlMessage("PTZ capability query complete", false);
        } else {
            const QString err = QString("PTZ capability query failed (HTTP %1): %2")
                                    .arg(statusCode)
                                    .arg(reply->errorString());
            qWarning() << "[SUNAPI]" << err << "url=" << reply->request().url() << "body=" << body.left(160);
            emit cameraControlMessage(err, true);
            emit sunapiSupportedPtzActionsLoaded(cameraIndex, actions);
        }

        reply->deleteLater();
    });
}
