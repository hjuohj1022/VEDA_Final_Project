#include "internal/auth/BackendAuthRequestService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

void BackendAuthRequestService::login(Backend *backend, BackendPrivate *state, QString id, QString pw)
{
    if (state->m_loginLocked) {
        emit backend->loginFailed("로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
        return;
    }
    if (state->m_loginInProgress) {
        emit backend->loginFailed("로그인 요청 처리 중입니다. 잠시만 기다려 주세요.");
        return;
    }

    const QString loginUrl = backend->serverUrl() + "/login";
    qInfo() << "[LOGIN] request URL:" << loginUrl;
    QUrl url(loginUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = id;
    json["password"] = pw;
    QJsonDocument doc(json);

    QNetworkReply *reply = state->m_manager->post(request, doc.toJson());
    state->m_loginReply = reply;
    state->m_loginInProgress = true;
    backend->attachIgnoreSslErrors(reply, "LOGIN");

    QTimer *loginTimeout = new QTimer(reply);
    loginTimeout->setSingleShot(true);
    const int timeoutMs = qMax(8000, state->m_env.value("LOGIN_TIMEOUT_MS", "15000").toInt());
    loginTimeout->setInterval(timeoutMs);
    QObject::connect(loginTimeout, &QTimer::timeout, backend, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    loginTimeout->start();

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
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

        if (reply->error() == QNetworkReply::NoError) {
            QString token;
            const QJsonDocument tokenDoc = QJsonDocument::fromJson(responseBody);
            if (tokenDoc.isObject()) {
                token = tokenDoc.object().value("token").toString().trimmed();
            }
            state->m_authToken = token;
            if (state->m_authToken.isEmpty()) {
                qWarning() << "[LOGIN] token missing in response";
            }

            state->m_isLoggedIn = true;
            state->m_userId = id;
            state->m_sessionRemainingSeconds = state->m_sessionTimeoutSeconds;
            state->m_sessionTimer->start();
            if (state->m_loginFailedAttempts != 0 || state->m_loginLocked) {
                state->m_loginFailedAttempts = 0;
                state->m_loginLocked = false;
                emit backend->loginLockChanged();
            }
            backend->checkStorage();
            emit backend->isLoggedInChanged();
            emit backend->userIdChanged();
            emit backend->sessionRemainingSecondsChanged();
            emit backend->loginSuccess();
        } else {
            if (timedOut) {
                emit backend->loginFailed("서버 응답 시간이 초과되었습니다. 서버 상태를 확인해 주세요.");
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
                    emit backend->loginFailed("로그인 요청이 취소되었습니다. 네트워크 상태를 확인 후 다시 시도해 주세요.");
                } else if (isSslFailure) {
                    emit backend->loginFailed("서버 SSL 인증서 검증에 실패했습니다. 인증서(CN/SAN) 또는 API_URL(HTTP/HTTPS)을 확인해 주세요.");
                } else if (isServerUnavailable) {
                    emit backend->loginFailed("서버와 연결할 수 없습니다. 서버 상태를 확인해 주세요.");
                } else if (isAuthFailure) {
                    if (!state->m_loginLocked) {
                        state->m_loginFailedAttempts++;
                        if (state->m_loginFailedAttempts >= state->m_loginMaxAttempts) {
                            state->m_loginLocked = true;
                        }
                        emit backend->loginLockChanged();
                    }

                    if (state->m_loginLocked) {
                        emit backend->loginFailed("비밀번호를 여러 번 잘못 입력하여 로그인이 잠겼습니다. 관리자 해제가 필요합니다.");
                    } else {
                        const int remaining = state->m_loginMaxAttempts - state->m_loginFailedAttempts;
                        emit backend->loginFailed(QString("비밀번호가 올바르지 않습니다. (%1회 남음)").arg(remaining));
                    }
                } else {
                    emit backend->loginFailed("로그인 요청에 실패했습니다. 네트워크 또는 서버 상태를 확인해 주세요.");
                }
            }
        }
        if (state->m_loginReply == reply) {
            state->m_loginReply = nullptr;
        }
        state->m_loginInProgress = false;
        reply->deleteLater();
    });
}

void BackendAuthRequestService::registerUser(Backend *backend, BackendPrivate *state, QString id, QString pw)
{
    const QString trimmedId = id.trimmed();
    if (trimmedId.isEmpty() || pw.isEmpty()) {
        emit backend->registerFailed("ID와 비밀번호를 입력해 주세요.");
        return;
    }
    if (state->m_registerInProgress) {
        emit backend->registerFailed("회원가입 요청 처리 중입니다. 잠시만 기다려 주세요.");
        return;
    }

    const QString registerUrl = backend->serverUrl() + "/register";
    qInfo() << "[REGISTER] request URL:" << registerUrl;
    QUrl url(registerUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = trimmedId;
    json["password"] = pw;
    QJsonDocument doc(json);

    QNetworkReply *reply = state->m_manager->post(request, doc.toJson());
    state->m_registerReply = reply;
    state->m_registerInProgress = true;
    backend->attachIgnoreSslErrors(reply, "REGISTER");

    QTimer *registerTimeout = new QTimer(reply);
    registerTimeout->setSingleShot(true);
    const int timeoutMs = qMax(8000, state->m_env.value("REGISTER_TIMEOUT_MS", "15000").toInt());
    registerTimeout->setInterval(timeoutMs);
    QObject::connect(registerTimeout, &QTimer::timeout, backend, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    registerTimeout->start();

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
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
            emit backend->registerSuccess(QString("회원가입 완료: %1").arg(trimmedId));
        } else {
            if (timedOut) {
                emit backend->registerFailed("회원가입 요청 시간이 초과되었습니다.");
            } else if (netError == QNetworkReply::OperationCanceledError) {
                emit backend->registerFailed("회원가입 요청이 취소되었습니다.");
            } else if (statusCode == 409) {
                emit backend->registerFailed("이미 사용 중인 ID입니다.");
            } else if (statusCode == 400) {
                emit backend->registerFailed("회원가입 입력값이 올바르지 않습니다.");
            } else if (statusCode >= 500) {
                emit backend->registerFailed("서버 오류로 회원가입에 실패했습니다.");
            } else {
                emit backend->registerFailed("회원가입 요청에 실패했습니다. 네트워크/서버 상태를 확인해 주세요.");
            }
        }

        if (state->m_registerReply == reply) {
            state->m_registerReply = nullptr;
        }
        state->m_registerInProgress = false;
        reply->deleteLater();
    });
}

