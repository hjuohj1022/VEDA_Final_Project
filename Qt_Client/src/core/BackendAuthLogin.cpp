#include "Backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>

// 로그인 요청 전송 및 응답 상태에 따른 인증/락 처리
void Backend::login(QString id, QString pw) {
    // 잠금 상태 또는 중복 요청 차단
    if (m_loginLocked) {
        emit loginFailed("로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
        return;
    }
    if (m_loginInProgress) {
        emit loginFailed("로그인 요청 처리 중입니다. 잠시만 기다려 주세요.");
        return;
    }

    const QString loginUrl = serverUrl() + "/login";
    qInfo() << "[LOGIN] request URL:" << loginUrl;
    QUrl url(loginUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = id;
    json["password"] = pw;
    QJsonDocument doc(json);

    QNetworkReply *reply = m_manager->post(request, doc.toJson());
    m_loginReply = reply;
    m_loginInProgress = true;
    attachIgnoreSslErrors(reply, "LOGIN");

    QTimer *loginTimeout = new QTimer(reply);
    loginTimeout->setSingleShot(true);
    const int timeoutMs = qMax(8000, m_env.value("LOGIN_TIMEOUT_MS", "15000").toInt());
    loginTimeout->setInterval(timeoutMs);
    connect(loginTimeout, &QTimer::timeout, this, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    loginTimeout->start();

    connect(reply, &QNetworkReply::finished, this, [=](){
        const bool timedOut = reply->property("timedOut").toBool();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        if (loginTimeout) {
            loginTimeout->stop();
        }
        if (netError == QNetworkReply::NoError) {
            qWarning() << "[LOGIN] status=" << statusCode
                       << "netError=" << static_cast<int>(netError)
                       << "timedOut=" << timedOut
                       << "timeoutMs=" << timeoutMs;
        } else {
            qWarning() << "[LOGIN] status=" << statusCode
                       << "netError=" << static_cast<int>(netError)
                       << "timedOut=" << timedOut
                       << "timeoutMs=" << timeoutMs
                       << "errorString=" << reply->errorString();
        }

        // 정상 로그인 시 세션 타이머 초기화 및 잠금 상태 해제
        if (reply->error() == QNetworkReply::NoError) {
            QString token;
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (doc.isObject()) {
                token = doc.object().value("token").toString().trimmed();
            }
            m_authToken = token;
            if (m_authToken.isEmpty()) {
                qWarning() << "[LOGIN] token missing in response";
            }

            m_isLoggedIn = true;
            m_userId = id;
            m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
            m_sessionTimer->start();
            if (m_loginFailedAttempts != 0 || m_loginLocked) {
                m_loginFailedAttempts = 0;
                m_loginLocked = false;
                emit loginLockChanged();
            }
            emit isLoggedInChanged();
            emit userIdChanged();
            emit sessionRemainingSecondsChanged();
            emit loginSuccess();
        } else {
            if (timedOut) {
                emit loginFailed("서버 응답 시간이 초과되었습니다. 서버 상태를 확인해 주세요.");
            } else {
                // 네트워크/인증/SSL 오류를 분리하여 메시지 반환
                const bool isAuthFailure = (statusCode == 401 || statusCode == 403
                                         || netError == QNetworkReply::AuthenticationRequiredError
                                         || netError == QNetworkReply::ContentAccessDenied);

                const bool isServerUnavailable =
                        (netError == QNetworkReply::ConnectionRefusedError
                      || netError == QNetworkReply::RemoteHostClosedError
                      || netError == QNetworkReply::HostNotFoundError
                      || netError == QNetworkReply::TimeoutError
                      || netError == QNetworkReply::TemporaryNetworkFailureError
                      || netError == QNetworkReply::NetworkSessionFailedError
                      || netError == QNetworkReply::ServiceUnavailableError
                      || statusCode >= 500);

                const bool isSslFailure =
                        (netError == QNetworkReply::SslHandshakeFailedError
                      || netError == QNetworkReply::UnknownNetworkError);

                if (netError == QNetworkReply::OperationCanceledError) {
                    emit loginFailed("로그인 요청이 취소되었습니다. 네트워크 상태를 확인 후 다시 시도해 주세요.");
                } else if (isSslFailure) {
                    emit loginFailed("서버 SSL 인증서 검증에 실패했습니다. 인증서(CN/SAN) 또는 API_URL(HTTP/HTTPS)을 확인해 주세요.");
                } else if (isServerUnavailable) {
                    emit loginFailed("서버에 연결할 수 없습니다. 서버 상태를 확인해 주세요.");
                } else if (isAuthFailure) {
                    // 인증 실패 횟수 누적 후 임계치 도달 시 로그인 잠금
                    if (!m_loginLocked) {
                        m_loginFailedAttempts++;
                        if (m_loginFailedAttempts >= m_loginMaxAttempts) {
                            m_loginLocked = true;
                        }
                        emit loginLockChanged();
                    }

                    if (m_loginLocked) {
                        emit loginFailed("비밀번호를 여러 번 잘못 입력하여 로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
                    } else {
                        int remaining = m_loginMaxAttempts - m_loginFailedAttempts;
                        emit loginFailed(QString("비밀번호가 잘못되었습니다. (%1회 남음)").arg(remaining));
                    }
                } else {
                    emit loginFailed("로그인 요청에 실패했습니다. 네트워크 또는 서버 상태를 확인해 주세요.");
                }
            }
        }
        if (m_loginReply == reply) {
            m_loginReply = nullptr;
        }
        m_loginInProgress = false;
        reply->deleteLater();
    });
}
