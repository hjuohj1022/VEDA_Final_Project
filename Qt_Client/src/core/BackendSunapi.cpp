#include "Backend.h"

#include <QDebug>
#include <QNetworkRequest>
#include <QUrl>

bool Backend::sendSunapiCommand(const QString &cgiName,
                                const QMap<QString, QString> &params,
                                int cameraIndex,
                                const QString &actionLabel,
                                bool includeChannelParam) {
    // 잘못된 인덱스/필수 설정 누락은 요청 전에 즉시 차단
    if (cameraIndex < 0) {
        emit cameraControlMessage(QString("%1 failed: invalid camera index").arg(actionLabel), true);
        return false;
    }

    const QString host = m_env.value("SUNAPI_IP").trimmed();
    if (host.isEmpty()) {
        emit cameraControlMessage(QString("%1 failed: SUNAPI_IP is empty").arg(actionLabel), true);
        return false;
    }

    const QUrl url = buildSunapiUrl(cgiName, params, cameraIndex, includeChannelParam);

    QNetworkRequest request(url);
    applySslIfNeeded(request);
    qInfo() << "[SUNAPI] request:" << actionLabel << "url=" << url.toString();

    QNetworkReply *reply = m_manager->get(request);
    attachIgnoreSslErrors(reply, "SUNAPI");
    connect(reply, &QNetworkReply::finished, this, [this, reply, actionLabel]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString body = QString::fromUtf8(reply->readAll()).trimmed();

        if (reply->error() == QNetworkReply::NoError) {
            QString sunapiErrMsg;
            const bool sunapiBodyError = isSunapiBodyError(body, &sunapiErrMsg);

            if (sunapiBodyError) {
                // HTTP 200이어도 본문 오류 키워드가 있으면 실패로 처리
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
