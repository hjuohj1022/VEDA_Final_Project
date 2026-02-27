#include "Backend.h"

#include <QDebug>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

// SUNAPI GET 요청을 만들고 공통 응답 처리를 수행한다.
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

// 줌 인 명령을 전송한다.
bool Backend::sunapiZoomIn(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "In"}},
        cameraIndex,
        "Zoom In");
}

// 줌 아웃 명령을 전송한다.
bool Backend::sunapiZoomOut(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Out"}},
        cameraIndex,
        "Zoom Out");
}

// 줌 동작을 정지한다.
bool Backend::sunapiZoomStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"ZoomContinuous", "Stop"}},
        cameraIndex,
        "Zoom Stop");
}

// 포커스를 Near 방향으로 이동한다.
bool Backend::sunapiFocusNear(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Near"}},
        cameraIndex,
        "Focus Near");
}

// 포커스를 Far 방향으로 이동한다.
bool Backend::sunapiFocusFar(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Far"}},
        cameraIndex,
        "Focus Far");
}

// 포커스 동작을 정지한다.
bool Backend::sunapiFocusStop(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"FocusContinuous", "Stop"}},
        cameraIndex,
        "Focus Stop");
}

// 오토포커스 명령을 전송한다.
bool Backend::sunapiSimpleAutoFocus(int cameraIndex) {
    return sendSunapiCommand(
        "image.cgi",
        {{"msubmenu", "focus"}, {"action", "control"}, {"Mode", "SimpleFocus"}},
        cameraIndex,
        "Auto Focus");
}

// 카메라별 지원 PTZ 액션을 조회한다.
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
