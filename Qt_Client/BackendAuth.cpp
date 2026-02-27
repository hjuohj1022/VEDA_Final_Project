#include "Backend.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QSettings>
#include <QUrl>

// 로그인 요청을 전송하고 결과를 처리한다.
void Backend::login(QString id, QString pw) {
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
        if (loginTimeout) {
            loginTimeout->stop();
        }
        qWarning() << "[LOGIN] status=" << statusCode
                   << "netError=" << static_cast<int>(netError)
                   << "timedOut=" << timedOut
                   << "timeoutMs=" << timeoutMs
                   << "errorString=" << reply->errorString();

        if (reply->error() == QNetworkReply::NoError) {
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

// 로그인 없이 임시로 앱을 사용 상태로 전환한다.
void Backend::skipLoginTemporarily() {
    if (m_isLoggedIn) {
        return;
    }

    m_isLoggedIn = true;
    m_userId = "Skip";
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
}

// 사용자 세션을 종료한다.
void Backend::logout() {
    if (!m_isLoggedIn) return;

    m_isLoggedIn = false;
    m_userId.clear();
    m_sessionTimer->stop();
    m_sessionRemainingSeconds = 0;

    emit isLoggedInChanged();
    emit userIdChanged();
    emit sessionRemainingSecondsChanged();
}

// 사용자 활동 시 세션 타이머를 초기화한다.
void Backend::resetSessionTimer() {
    if (!m_isLoggedIn) return;

    if (m_sessionRemainingSeconds != m_sessionTimeoutSeconds) {
        m_sessionRemainingSeconds = m_sessionTimeoutSeconds;
        emit sessionRemainingSecondsChanged();
    }

    if (!m_sessionTimer->isActive()) {
        m_sessionTimer->start();
    }
}

// 관리자 코드로 잠금 상태를 해제한다.
bool Backend::adminUnlock(QString adminCode) {
    QString expected = m_env.value("ADMIN_UNLOCK_KEY").trimmed();
    if (expected.isEmpty()) {
        emit loginFailed("관리자 해제 키가 설정되어 있지 않습니다.");
        return false;
    }

    if (adminCode.trimmed() != expected) {
        emit loginFailed("관리자 해제 키가 올바르지 않습니다.");
        return false;
    }

    m_loginLocked = false;
    m_loginFailedAttempts = 0;
    emit loginLockChanged();
    return true;
}

// 1초마다 세션 만료를 체크한다.
void Backend::onSessionTick() {
    if (!m_isLoggedIn) {
        m_sessionTimer->stop();
        return;
    }

    if (m_sessionRemainingSeconds > 0) {
        m_sessionRemainingSeconds--;
        emit sessionRemainingSecondsChanged();
    }

    if (m_sessionRemainingSeconds <= 0) {
        logout();
        emit sessionExpired();
    }
}
