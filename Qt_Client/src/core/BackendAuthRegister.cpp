#include "Backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

// 회원가입 요청 전송 및 응답 상태 처리
void Backend::registerUser(QString id, QString pw) {
    const QString trimmedId = id.trimmed();
    if (trimmedId.isEmpty() || pw.isEmpty()) {
        emit registerFailed("ID와 비밀번호를 입력해 주세요.");
        return;
    }
    if (m_registerInProgress) {
        emit registerFailed("회원가입 요청 처리 중입니다. 잠시만 기다려 주세요.");
        return;
    }

    const QString registerUrl = serverUrl() + "/register";
    qInfo() << "[REGISTER] request URL:" << registerUrl;
    QUrl url(registerUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = trimmedId;
    json["password"] = pw;
    QJsonDocument doc(json);

    QNetworkReply *reply = m_manager->post(request, doc.toJson());
    m_registerReply = reply;
    m_registerInProgress = true;
    attachIgnoreSslErrors(reply, "REGISTER");

    QTimer *registerTimeout = new QTimer(reply);
    registerTimeout->setSingleShot(true);
    const int timeoutMs = qMax(8000, m_env.value("REGISTER_TIMEOUT_MS", "15000").toInt());
    registerTimeout->setInterval(timeoutMs);
    connect(registerTimeout, &QTimer::timeout, this, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    registerTimeout->start();

    connect(reply, &QNetworkReply::finished, this, [=]() {
        const bool timedOut = reply->property("timedOut").toBool();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        if (registerTimeout) {
            registerTimeout->stop();
        }

        if (netError == QNetworkReply::NoError) {
            qWarning() << "[REGISTER] status=" << statusCode
                       << "netError=" << static_cast<int>(netError)
                       << "timedOut=" << timedOut
                       << "timeoutMs=" << timeoutMs;
        } else {
            qWarning() << "[REGISTER] status=" << statusCode
                       << "netError=" << static_cast<int>(netError)
                       << "timedOut=" << timedOut
                       << "timeoutMs=" << timeoutMs
                       << "errorString=" << reply->errorString();
        }

        if (reply->error() == QNetworkReply::NoError) {
            emit registerSuccess(QString("회원가입 완료: %1").arg(trimmedId));
        } else {
            if (timedOut) {
                emit registerFailed("회원가입 요청 시간이 초과되었습니다.");
            } else if (netError == QNetworkReply::OperationCanceledError) {
                emit registerFailed("회원가입 요청이 취소되었습니다.");
            } else if (statusCode == 409) {
                emit registerFailed("이미 사용 중인 ID입니다.");
            } else if (statusCode == 400) {
                emit registerFailed("회원가입 입력값이 올바르지 않습니다.");
            } else if (statusCode >= 500) {
                emit registerFailed("서버 오류로 회원가입에 실패했습니다.");
            } else {
                emit registerFailed("회원가입 요청에 실패했습니다. 네트워크/서버 상태를 확인해 주세요.");
            }
        }

        if (m_registerReply == reply) {
            m_registerReply = nullptr;
        }
        m_registerInProgress = false;
        reply->deleteLater();
    });
}
