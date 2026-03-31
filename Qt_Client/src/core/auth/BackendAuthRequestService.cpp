#include "internal/auth/BackendAuthRequestService.h"

#include "Backend.h"
#include "internal/auth/BackendAuthSessionService.h"
#include "internal/core/BackendCoreApiService.h"
#include "internal/core/BackendCoreSslService.h"
#include "internal/core/Backend_p.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

namespace {

// 응답 텍스트 반환 함수
QString responseText(const QByteArray &responseBody)
{
    return QString::fromUtf8(responseBody).trimmed();
}

// 로그인 잠금 상태 초기화 함수
void resetLoginLockState(Backend *backend, BackendPrivate *state)
{
    if (state->m_loginFailedAttempts != 0 || state->m_loginLocked) {
        state->m_loginFailedAttempts = 0;
        state->m_loginLocked = false;
        emit backend->loginLockChanged();
    }
}

// 대기 중인 이중 인증 상태 정리 함수
void clearPendingTwoFactorState(Backend *backend, BackendPrivate *state, bool notify)
{
    const bool hadTwoFactorState = state->m_twoFactorRequired || !state->m_preAuthToken.isEmpty();
    state->m_twoFactorRequired = false;
    state->m_preAuthToken.clear();
    state->m_pendingLoginId.clear();

    if (notify && hadTwoFactorState) {
        emit backend->twoFactorRequiredChanged();
    }
}

// 이중 인증 활성화 상태 설정 함수
void setTwoFactorEnabledState(Backend *backend, BackendPrivate *state, bool enabled)
{
    if (state->m_twoFactorEnabled == enabled) {
        return;
    }

    state->m_twoFactorEnabled = enabled;
    emit backend->twoFactorEnabledChanged();
}

void finalizeAuthenticatedLogin(Backend *backend,
                                BackendPrivate *state,
                                const QString &userId,
                                const QString &token,
                                bool twoFactorEnabled)
{
    const QString resolvedUserId = !userId.trimmed().isEmpty()
            ? userId.trimmed()
            : (!state->m_pendingLoginId.trimmed().isEmpty()
               ? state->m_pendingLoginId.trimmed()
               : state->m_userId.trimmed());
    const bool hadTwoFactorState = state->m_twoFactorRequired || !state->m_preAuthToken.isEmpty();
    state->m_twoFactorRequired = false;
    state->m_preAuthToken.clear();
    state->m_pendingLoginId.clear();
    setTwoFactorEnabledState(backend, state, twoFactorEnabled);
    state->m_authToken = token;
    state->m_isLoggedIn = true;
    state->m_userId = resolvedUserId;
    state->m_sessionRemainingSeconds = state->m_sessionTimeoutSeconds;
    state->m_sessionTimer->start();

    resetLoginLockState(backend, state);
    backend->checkStorage();

    if (hadTwoFactorState) {
        emit backend->twoFactorRequiredChanged();
    }
    emit backend->isLoggedInChanged();
    emit backend->userIdChanged();
    emit backend->sessionRemainingSecondsChanged();
    emit backend->loginSuccess();
}

// 서버 사용 불가 상태 확인 함수
bool isServerUnavailable(int statusCode, QNetworkReply::NetworkError netError)
{
    return (netError == QNetworkReply::ConnectionRefusedError
            || netError == QNetworkReply::RemoteHostClosedError
            || netError == QNetworkReply::HostNotFoundError
            || netError == QNetworkReply::TimeoutError
            || netError == QNetworkReply::TemporaryNetworkFailureError
            || netError == QNetworkReply::NetworkSessionFailedError
            || netError == QNetworkReply::ServiceUnavailableError
            || statusCode >= 500);
}

// SSL 실패 상태 확인 함수
bool isSslFailure(QNetworkReply::NetworkError netError)
{
    return (netError == QNetworkReply::SslHandshakeFailedError
            || netError == QNetworkReply::UnknownNetworkError);
}

// 만료된 이중 인증 챌린지 확인 함수
bool isExpiredTwoFactorChallenge(int statusCode, const QString &bodyText)
{
    return statusCode == 404
           || bodyText.contains("pre-auth", Qt::CaseInsensitive)
           || bodyText.contains("not enabled", Qt::CaseInsensitive)
           || bodyText.contains("user not found", Qt::CaseInsensitive);
}

// 클라이언트 비밀번호 복잡도 검증 함수
QString validatePasswordComplexityForClient(const QString &password)
{
    // 비밀번호 변경/회원가입 공통 복잡도 규칙 검증
    if (password.length() < 8) {
        return "비밀번호는 8자 이상이어야 합니다.";
    }
    if (password.length() > 16) {
        return "비밀번호는 16자 이하여야 합니다.";
    }
    if (password.contains(QRegularExpression("\\s"))) {
        return "비밀번호에는 공백을 사용할 수 없습니다.";
    }
    if (!password.contains(QRegularExpression("\\d"))) {
        return "비밀번호에는 숫자가 1개 이상 포함되어야 합니다.";
    }
    if (!password.contains(QRegularExpression("[^A-Za-z0-9\\s]"))) {
        return "비밀번호에는 특수문자가 1개 이상 포함되어야 합니다.";
    }

    return QString();
}

// 클라이언트 이메일 형식 검증 함수
QString validateEmailForClient(const QString &email)
{
    // 회원가입 이메일 형식 사전 검증
    const QString trimmed = email.trimmed();
    if (trimmed.isEmpty()) {
        return "이메일을 입력해 주세요.";
    }
    if (trimmed.contains(QRegularExpression("\\s"))) {
        return "이메일에는 공백을 사용할 수 없습니다.";
    }
    static const QRegularExpression emailPattern(
        "^[A-Za-z0-9.!#$%&'*+/=?^_`{|}~-]+@[A-Za-z0-9-]+(\\.[A-Za-z0-9-]+)+$");
    if (!emailPattern.match(trimmed).hasMatch()) {
        return "이메일 형식이 올바르지 않습니다.";
    }
    return QString();
}

} // namespace

// 관리자 잠금 해제 처리 함수
bool BackendAuthRequestService::adminUnlock(Backend *backend, BackendPrivate *state, QString adminCode)
{
    const QString trimmedCode = adminCode.trimmed();
    if (trimmedCode.isEmpty()) {
        emit backend->loginFailed("관리자 해제 키를 입력해 주세요.");
        return false;
    }
    if (state->m_adminUnlockInProgress) {
        emit backend->loginFailed("관리자 잠금 해제 요청 처리 중입니다. 잠시만 기다려 주세요.");
        return false;
    }

    const QString unlockUrl = backend->serverUrl() + "/auth/admin/unlock";
    QNetworkRequest request{QUrl(unlockUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["code"] = trimmedCode;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_adminUnlockReply = reply;
    state->m_adminUnlockInProgress = true;
    backend->attachIgnoreSslErrors(reply, "ADMIN_UNLOCK");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            resetLoginLockState(backend, state);
        } else if (isSslFailure(netError)) {
            emit backend->loginFailed("서버 SSL 검증에 실패했습니다.");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->loginFailed("서버에 연결할 수 없습니다.");
        } else {
            emit backend->loginFailed(bodyText.isEmpty()
                                          ? "관리자 잠금 해제에 실패했습니다."
                                          : bodyText);
        }

        if (state->m_adminUnlockReply == reply) {
            state->m_adminUnlockReply = nullptr;
        }
        state->m_adminUnlockInProgress = false;
        reply->deleteLater();
    });

    return true;
}

// 로그인 함수
void BackendAuthRequestService::login(Backend *backend, BackendPrivate *state, QString id, QString pw)
{
    const QString trimmedId = id.trimmed();
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
    QString sslError;
    // 잘못된 인증서 경로일 때는 HTTPS 로그인 요청 자체를 막는다.
    if (!BackendCoreSslService::isHttpsRequestReady(state, url, &sslError)) {
        emit backend->loginFailed(sslError);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = trimmedId;
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
            QString preAuthToken;
            bool requiresTwoFactor = false;

            const QJsonDocument tokenDoc = QJsonDocument::fromJson(responseBody);
            if (tokenDoc.isObject()) {
                const QJsonObject obj = tokenDoc.object();
                token = obj.value("token").toString().trimmed();
                preAuthToken = obj.value("pre_auth_token").toString().trimmed();
                requiresTwoFactor = obj.value("requires_2fa").toBool();
            }

            if (!token.isEmpty()) {
                finalizeAuthenticatedLogin(backend, state, trimmedId, token, false);
            } else if (requiresTwoFactor && !preAuthToken.isEmpty()) {
                state->m_isLoggedIn = false;
                state->m_authToken.clear();
                state->m_twoFactorRequired = true;
                state->m_preAuthToken = preAuthToken;
                state->m_pendingLoginId = trimmedId;
                state->m_userId = trimmedId;
                setTwoFactorEnabledState(backend, state, true);
                state->m_sessionTimer->stop();
                if (state->m_sessionRemainingSeconds != 0) {
                    state->m_sessionRemainingSeconds = 0;
                    emit backend->sessionRemainingSecondsChanged();
                }

                resetLoginLockState(backend, state);
                emit backend->twoFactorRequiredChanged();
            } else {
                emit backend->loginFailed("로그인 응답이 올바르지 않습니다. 다시 시도해 주세요.");
            }
        } else {
            if (timedOut) {
                emit backend->loginFailed("서버 응답 시간이 초과되었습니다. 서버 상태를 확인해 주세요.");
            } else {
                const bool isAuthFailure = (statusCode == 401 || statusCode == 403
                                            || netError == QNetworkReply::AuthenticationRequiredError
                                            || netError == QNetworkReply::ContentAccessDenied);

                if (netError == QNetworkReply::OperationCanceledError) {
                    emit backend->loginFailed("로그인 요청이 취소되었습니다. 네트워크 상태를 확인 후 다시 시도해 주세요.");
                } else if (isSslFailure(netError)) {
                    emit backend->loginFailed("서버 SSL 인증서 검증에 실패했습니다. 인증서(CN/SAN)와 API_URL(HTTP/HTTPS)을 확인해 주세요.");
                } else if (isServerUnavailable(statusCode, netError)) {
                    emit backend->loginFailed("서버에 연결할 수 없습니다. 서버 상태를 확인해 주세요.");
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

// 이중 인증 OTP 검증 함수
void BackendAuthRequestService::verifyTwoFactorOtp(Backend *backend, BackendPrivate *state, QString otp)
{
    const QString trimmedOtp = otp.trimmed();
    static const QRegularExpression sixDigits("^\\d{6}$");

    if (!state->m_twoFactorRequired || state->m_preAuthToken.isEmpty()) {
        emit backend->loginFailed("OTP 인증 세션이 없습니다. 다시 로그인해 주세요.");
        return;
    }
    if (state->m_twoFactorVerifyInProgress) {
        emit backend->loginFailed("OTP 인증 요청 처리 중입니다. 잠시만 기다려 주세요.");
        return;
    }
    if (!sixDigits.match(trimmedOtp).hasMatch()) {
        emit backend->loginFailed("OTP 6자리를 입력해 주세요.");
        return;
    }

    const QString verifyUrl = backend->serverUrl() + "/2fa/verify";
    qInfo() << "[2FA_VERIFY] request URL:" << verifyUrl;
    QUrl url(verifyUrl);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["pre_auth_token"] = state->m_preAuthToken;
    json["otp"] = trimmedOtp;
    QJsonDocument doc(json);

    QNetworkReply *reply = state->m_manager->post(request, doc.toJson());
    state->m_twoFactorVerifyReply = reply;
    state->m_twoFactorVerifyInProgress = true;
    backend->attachIgnoreSslErrors(reply, "2FA_VERIFY");

    QTimer *verifyTimeout = new QTimer(reply);
    verifyTimeout->setSingleShot(true);
    const int timeoutMs = qMax(8000, state->m_env.value("OTP_VERIFY_TIMEOUT_MS", "15000").toInt());
    verifyTimeout->setInterval(timeoutMs);
    QObject::connect(verifyTimeout, &QTimer::timeout, backend, [reply]() {
        if (reply->isRunning()) {
            reply->setProperty("timedOut", true);
            reply->abort();
        }
    });
    verifyTimeout->start();

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const bool timedOut = reply->property("timedOut").toBool();
        const bool userCanceled = reply->property("userCanceled").toBool();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);
        if (verifyTimeout) {
            verifyTimeout->stop();
        }

        if (netError == QNetworkReply::NoError) {
            qWarning() << "[2FA_VERIFY] status=" << statusCode
                       << "netError=" << static_cast<int>(netError)
                       << "timedOut=" << timedOut
                       << "timeoutMs=" << timeoutMs;
        } else {
            qWarning() << "[2FA_VERIFY] status=" << statusCode
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

            if (token.isEmpty()) {
                emit backend->loginFailed("OTP 인증 응답에 토큰이 없습니다. 다시 시도해 주세요.");
            } else {
                finalizeAuthenticatedLogin(backend, state, state->m_pendingLoginId, token, true);
            }
        } else if (!userCanceled) {
            if (timedOut) {
                emit backend->loginFailed("OTP 인증 시간이 초과되었습니다. 다시 시도해 주세요.");
            } else if (netError == QNetworkReply::OperationCanceledError) {
                emit backend->loginFailed("OTP 인증 요청이 취소되었습니다. 다시 시도해 주세요.");
            } else if (isSslFailure(netError)) {
                emit backend->loginFailed("서버 SSL 인증서 검증에 실패했습니다. 인증서(CN/SAN)와 API_URL(HTTP/HTTPS)을 확인해 주세요.");
            } else if (isServerUnavailable(statusCode, netError)) {
                emit backend->loginFailed("서버에 연결할 수 없습니다. 서버 상태를 확인해 주세요.");
            } else if (statusCode == 401 && isExpiredTwoFactorChallenge(statusCode, bodyText)) {
                clearPendingTwoFactorState(backend, state, true);
                emit backend->loginFailed("OTP 세션이 만료되었거나 유효하지 않습니다. 다시 로그인해 주세요.");
            } else if (statusCode == 400 && isExpiredTwoFactorChallenge(statusCode, bodyText)) {
                clearPendingTwoFactorState(backend, state, true);
                emit backend->loginFailed("OTP 세션 상태가 유효하지 않습니다. 다시 로그인해 주세요.");
            } else if (statusCode == 404) {
                clearPendingTwoFactorState(backend, state, true);
                emit backend->loginFailed("사용자 정보를 찾을 수 없습니다. 다시 로그인해 주세요.");
            } else if (statusCode == 401) {
                emit backend->loginFailed(bodyText.isEmpty() ? "OTP가 올바르지 않습니다." : bodyText);
            } else {
                emit backend->loginFailed(bodyText.isEmpty()
                                              ? "OTP 인증 요청에 실패했습니다. 네트워크 또는 서버 상태를 확인해 주세요."
                                              : bodyText);
            }
        }

        if (state->m_twoFactorVerifyReply == reply) {
            state->m_twoFactorVerifyReply = nullptr;
        }
        state->m_twoFactorVerifyInProgress = false;
        reply->deleteLater();
    });
}

// 이중 인증 상태 갱신 함수
void BackendAuthRequestService::refreshTwoFactorStatus(Backend *backend, BackendPrivate *state)
{
    if (!state->m_isLoggedIn || state->m_authToken.trimmed().isEmpty()) {
        return;
    }
    if (state->m_twoFactorStatusInProgress) {
        return;
    }

    QNetworkRequest request = BackendCoreApiService::makeApiJsonRequest(
        backend, state, "/2fa/status", QMap<QString, QString>{});
    BackendCoreApiService::applyAuthIfNeeded(backend, state, request);

    QNetworkReply *reply = state->m_manager->get(request);
    state->m_twoFactorStatusReply = reply;
    state->m_twoFactorStatusInProgress = true;
    backend->attachIgnoreSslErrors(reply, "2FA_STATUS");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (doc.isObject()) {
                const bool enabled = doc.object().value("two_factor_enabled").toBool();
                if (state->m_isLoggedIn) {
                    setTwoFactorEnabledState(backend, state, enabled);
                }
            }
        } else {
            qWarning() << "[2FA_STATUS] status=" << statusCode
                       << "netError=" << static_cast<int>(netError)
                       << "errorString=" << reply->errorString()
                       << "body=" << bodyText;
        }

        if (state->m_twoFactorStatusReply == reply) {
            state->m_twoFactorStatusReply = nullptr;
        }
        state->m_twoFactorStatusInProgress = false;
        reply->deleteLater();
    });
}

// 이중 인증 설정 시작 함수
void BackendAuthRequestService::startTwoFactorSetup(Backend *backend, BackendPrivate *state)
{
    if (!state->m_isLoggedIn || state->m_authToken.trimmed().isEmpty()) {
        emit backend->twoFactorSetupFailed("Login required");
        return;
    }
    if (state->m_twoFactorSetupInProgress || state->m_twoFactorConfirmInProgress) {
        emit backend->twoFactorSetupFailed("2FA setup is already in progress");
        return;
    }

    QNetworkRequest request = BackendCoreApiService::makeApiJsonRequest(
        backend, state, "/2fa/setup/init", QMap<QString, QString>{});
    BackendCoreApiService::applyAuthIfNeeded(backend, state, request);

    QNetworkReply *reply = state->m_manager->post(request, QByteArray());
    state->m_twoFactorSetupReply = reply;
    state->m_twoFactorSetupInProgress = true;
    backend->attachIgnoreSslErrors(reply, "2FA_SETUP_INIT");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            const QJsonObject obj = doc.object();
            const QString manualKey = obj.value("manual_key").toString().trimmed();
            const QString otpAuthUrl = obj.value("otpauth_url").toString().trimmed();

            if (manualKey.isEmpty()) {
                emit backend->twoFactorSetupFailed("manual_key was not returned");
            } else {
                emit backend->twoFactorSetupReady(manualKey, otpAuthUrl);
            }
        } else if (statusCode == 409) {
            setTwoFactorEnabledState(backend, state, true);
            BackendAuthRequestService::refreshTwoFactorStatus(backend, state);
            emit backend->twoFactorSetupFailed(bodyText.isEmpty() ? "2FA is already enabled" : bodyText);
        } else if (netError != QNetworkReply::OperationCanceledError || state->m_isLoggedIn) {
            emit backend->twoFactorSetupFailed(bodyText.isEmpty()
                                                   ? "Failed to start 2FA setup"
                                                   : bodyText);
        }

        if (state->m_twoFactorSetupReply == reply) {
            state->m_twoFactorSetupReply = nullptr;
        }
        state->m_twoFactorSetupInProgress = false;
        reply->deleteLater();
    });
}

// 이중 인증 설정 확정 함수
void BackendAuthRequestService::confirmTwoFactorSetup(Backend *backend, BackendPrivate *state, QString otp)
{
    static const QRegularExpression sixDigits("^\\d{6}$");
    const QString trimmedOtp = otp.trimmed();

    if (!state->m_isLoggedIn || state->m_authToken.trimmed().isEmpty()) {
        emit backend->twoFactorSetupFailed("Login required");
        return;
    }
    if (state->m_twoFactorConfirmInProgress) {
        emit backend->twoFactorSetupFailed("2FA confirmation is already in progress");
        return;
    }
    if (!sixDigits.match(trimmedOtp).hasMatch()) {
        emit backend->twoFactorSetupFailed("OTP must be 6 digits");
        return;
    }

    QNetworkRequest request = BackendCoreApiService::makeApiJsonRequest(
        backend, state, "/2fa/setup/confirm", QMap<QString, QString>{});
    BackendCoreApiService::applyAuthIfNeeded(backend, state, request);

    QJsonObject json;
    json["otp"] = trimmedOtp;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_twoFactorConfirmReply = reply;
    state->m_twoFactorConfirmInProgress = true;
    backend->attachIgnoreSslErrors(reply, "2FA_SETUP_CONFIRM");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            setTwoFactorEnabledState(backend, state, true);
            BackendAuthRequestService::refreshTwoFactorStatus(backend, state);
            emit backend->twoFactorSetupCompleted();
        } else if (netError != QNetworkReply::OperationCanceledError || state->m_isLoggedIn) {
            emit backend->twoFactorSetupFailed(bodyText.isEmpty()
                                                   ? "Failed to confirm 2FA setup"
                                                   : bodyText);
        }

        if (state->m_twoFactorConfirmReply == reply) {
            state->m_twoFactorConfirmReply = nullptr;
        }
        state->m_twoFactorConfirmInProgress = false;
        reply->deleteLater();
    });
}

// 이중 인증 비활성화 함수
void BackendAuthRequestService::disableTwoFactor(Backend *backend, BackendPrivate *state, QString otp)
{
    static const QRegularExpression sixDigits("^\\d{6}$");
    const QString trimmedOtp = otp.trimmed();

    if (!state->m_isLoggedIn || state->m_authToken.trimmed().isEmpty()) {
        emit backend->twoFactorDisableFailed("Login required");
        return;
    }
    if (state->m_twoFactorDisableInProgress) {
        emit backend->twoFactorDisableFailed("2FA disable is already in progress");
        return;
    }
    if (!sixDigits.match(trimmedOtp).hasMatch()) {
        emit backend->twoFactorDisableFailed("OTP must be 6 digits");
        return;
    }

    QNetworkRequest request = BackendCoreApiService::makeApiJsonRequest(
        backend, state, "/2fa/disable", QMap<QString, QString>{});
    BackendCoreApiService::applyAuthIfNeeded(backend, state, request);

    QJsonObject json;
    json["otp"] = trimmedOtp;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_twoFactorDisableReply = reply;
    state->m_twoFactorDisableInProgress = true;
    backend->attachIgnoreSslErrors(reply, "2FA_DISABLE");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            setTwoFactorEnabledState(backend, state, false);
            BackendAuthRequestService::refreshTwoFactorStatus(backend, state);
            emit backend->twoFactorDisableCompleted();
        } else if (statusCode == 400 && bodyText.contains("already disabled", Qt::CaseInsensitive)) {
            setTwoFactorEnabledState(backend, state, false);
            BackendAuthRequestService::refreshTwoFactorStatus(backend, state);
            emit backend->twoFactorDisableCompleted();
        } else if (netError != QNetworkReply::OperationCanceledError || state->m_isLoggedIn) {
            emit backend->twoFactorDisableFailed(bodyText.isEmpty()
                                                     ? "Failed to disable 2FA"
                                                     : bodyText);
        }

        if (state->m_twoFactorDisableReply == reply) {
            state->m_twoFactorDisableReply = nullptr;
        }
        state->m_twoFactorDisableInProgress = false;
        reply->deleteLater();
    });
}

// 계정 삭제 함수
void BackendAuthRequestService::deleteAccount(Backend *backend, BackendPrivate *state, QString password, QString otp)
{
    static const QRegularExpression sixDigits("^\\d{6}$");
    const QString trimmedOtp = otp.trimmed();

    if (!state->m_isLoggedIn || state->m_authToken.trimmed().isEmpty()) {
        emit backend->accountDeleteFailed("Login required");
        return;
    }
    if (state->m_accountDeleteInProgress) {
        emit backend->accountDeleteFailed("Account deletion is already in progress");
        return;
    }
    if (password.isEmpty()) {
        emit backend->accountDeleteFailed("Password is required");
        return;
    }
    if (state->m_twoFactorEnabled && !sixDigits.match(trimmedOtp).hasMatch()) {
        emit backend->accountDeleteFailed("OTP must be 6 digits");
        return;
    }

    QNetworkRequest request = BackendCoreApiService::makeApiJsonRequest(
        backend, state, "/account/delete", QMap<QString, QString>{});
    BackendCoreApiService::applyAuthIfNeeded(backend, state, request);

    QJsonObject json;
    json["password"] = password;
    if (state->m_twoFactorEnabled) {
        json["otp"] = trimmedOtp;
    }

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_accountDeleteReply = reply;
    state->m_accountDeleteInProgress = true;
    backend->attachIgnoreSslErrors(reply, "ACCOUNT_DELETE");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            BackendAuthSessionService::logout(backend, state);
            emit backend->accountDeleteCompleted();
        } else if (netError == QNetworkReply::OperationCanceledError && !state->m_isLoggedIn) {
            // 로그아웃으로 중단된 경우 추가 에러 미표시
        } else if (isSslFailure(netError)) {
            emit backend->accountDeleteFailed("Server SSL validation failed");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->accountDeleteFailed("Unable to reach the server");
        } else {
            emit backend->accountDeleteFailed(bodyText.isEmpty()
                                                  ? "Failed to delete account"
                                                  : bodyText);
        }

        if (state->m_accountDeleteReply == reply) {
            state->m_accountDeleteReply = nullptr;
        }
        state->m_accountDeleteInProgress = false;
        reply->deleteLater();
    });
}

// 비밀번호 변경 함수
void BackendAuthRequestService::changePassword(Backend *backend, BackendPrivate *state, QString currentPassword, QString newPassword)
{
    // 로그인 상태 비밀번호 변경 요청 처리
    if (!state->m_isLoggedIn || state->m_authToken.trimmed().isEmpty()) {
        emit backend->passwordChangeFailed("로그인 후 이용해 주세요.");
        return;
    }
    if (state->m_passwordChangeInProgress) {
        emit backend->passwordChangeFailed("비밀번호 변경 요청이 이미 처리 중입니다.");
        return;
    }
    if (currentPassword.isEmpty()) {
        emit backend->passwordChangeFailed("현재 비밀번호를 입력해 주세요.");
        return;
    }
    if (newPassword.isEmpty()) {
        emit backend->passwordChangeFailed("새 비밀번호를 입력해 주세요.");
        return;
    }
    if (currentPassword == newPassword) {
        emit backend->passwordChangeFailed("새 비밀번호는 현재 비밀번호와 달라야 합니다.");
        return;
    }

    // 클라이언트 측 비밀번호 복잡도 사전 검증
    const QString complexityError = validatePasswordComplexityForClient(newPassword);
    if (!complexityError.isEmpty()) {
        emit backend->passwordChangeFailed(complexityError);
        return;
    }

    QNetworkRequest request = BackendCoreApiService::makeApiJsonRequest(
        backend, state, "/account/password/change", QMap<QString, QString>{});
    BackendCoreApiService::applyAuthIfNeeded(backend, state, request);

    QJsonObject json;
    json["current_password"] = currentPassword;
    json["new_password"] = newPassword;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_passwordChangeReply = reply;
    state->m_passwordChangeInProgress = true;
    backend->attachIgnoreSslErrors(reply, "ACCOUNT_PASSWORD_CHANGE");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            emit backend->passwordChangeCompleted();
        } else if (netError == QNetworkReply::OperationCanceledError && !state->m_isLoggedIn) {
            // 로그아웃으로 중단된 경우 추가 에러 미표시
        } else if (isSslFailure(netError)) {
            emit backend->passwordChangeFailed("서버 SSL 검증에 실패했습니다.");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->passwordChangeFailed("서버에 연결할 수 없습니다.");
        } else {
            emit backend->passwordChangeFailed(bodyText.isEmpty()
                                                   ? "비밀번호 변경에 실패했습니다."
                                                   : bodyText);
        }

        if (state->m_passwordChangeReply == reply) {
            state->m_passwordChangeReply = nullptr;
        }
        state->m_passwordChangeInProgress = false;
        reply->deleteLater();
    });
}

// 비밀번호 재설정 요청 함수
void BackendAuthRequestService::requestPasswordReset(Backend *backend, BackendPrivate *state, QString id, QString email)
{
    // 비밀번호 재설정 코드 발급 요청 처리
    const QString trimmedId = id.trimmed();
    const QString trimmedEmail = email.trimmed();
    if (trimmedId.isEmpty() || trimmedEmail.isEmpty()) {
        emit backend->passwordResetRequestFailed("ID와 이메일을 모두 입력해 주세요.");
        return;
    }
    const QString emailError = validateEmailForClient(trimmedEmail);
    if (!emailError.isEmpty()) {
        emit backend->passwordResetRequestFailed(emailError);
        return;
    }
    if (state->m_passwordResetRequestInProgress) {
        emit backend->passwordResetRequestFailed("비밀번호 재설정 요청이 이미 처리 중입니다.");
        return;
    }

    const QString requestUrl = backend->serverUrl() + "/auth/password/forgot";
    QNetworkRequest request{QUrl(requestUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = trimmedId;
    json["email"] = trimmedEmail;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_passwordResetRequestReply = reply;
    state->m_passwordResetRequestInProgress = true;
    backend->attachIgnoreSslErrors(reply, "PASSWORD_RESET_REQUEST");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            QString message;
            QString debugCode;
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                message = obj.value("message").toString().trimmed();
                debugCode = obj.value("debug_code").toString().trimmed();
                if (debugCode.isEmpty())
                    debugCode = obj.value("debug_token").toString().trimmed();
            }
            if (message.isEmpty()) {
                message = "입력한 정보가 일치하면 비밀번호 재설정 코드가 메일로 전송됩니다.";
            }
            emit backend->passwordResetRequested(message, debugCode);
        } else if (isSslFailure(netError)) {
            emit backend->passwordResetRequestFailed("서버 SSL 검증에 실패했습니다.");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->passwordResetRequestFailed("서버에 연결할 수 없습니다.");
        } else {
            emit backend->passwordResetRequestFailed(bodyText.isEmpty()
                                                         ? "비밀번호 재설정 요청에 실패했습니다."
                                                         : bodyText);
        }

        if (state->m_passwordResetRequestReply == reply) {
            state->m_passwordResetRequestReply = nullptr;
        }
        state->m_passwordResetRequestInProgress = false;
        reply->deleteLater();
    });
}

void BackendAuthRequestService::resetPasswordWithCode(Backend *backend,
                                                       BackendPrivate *state,
                                                       QString code,
                                                       QString newPassword)
{
    // 재설정 코드 기반 새 비밀번호 적용 요청 처리
    const QString trimmedCode = code.trimmed();
    if (trimmedCode.isEmpty()) {
        emit backend->passwordResetFailed("재설정 코드를 입력해 주세요.");
        return;
    }
    if (newPassword.isEmpty()) {
        emit backend->passwordResetFailed("새 비밀번호를 입력해 주세요.");
        return;
    }
    const QString complexityError = validatePasswordComplexityForClient(newPassword);
    if (!complexityError.isEmpty()) {
        emit backend->passwordResetFailed(complexityError);
        return;
    }
    if (state->m_passwordResetInProgress) {
        emit backend->passwordResetFailed("비밀번호 재설정 요청이 이미 처리 중입니다.");
        return;
    }

    const QString resetUrl = backend->serverUrl() + "/auth/password/reset";
    QNetworkRequest request{QUrl(resetUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["code"] = trimmedCode;
    json["token"] = trimmedCode;
    json["new_password"] = newPassword;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_passwordResetReply = reply;
    state->m_passwordResetInProgress = true;
    backend->attachIgnoreSslErrors(reply, "PASSWORD_RESET");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            QString message;
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (doc.isObject()) {
                message = doc.object().value("message").toString().trimmed();
            }
            if (message.isEmpty()) {
                message = "비밀번호가 성공적으로 재설정되었습니다.";
            }
            emit backend->passwordResetCompleted(message);
        } else if (isSslFailure(netError)) {
            emit backend->passwordResetFailed("서버 SSL 검증에 실패했습니다.");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->passwordResetFailed("서버에 연결할 수 없습니다.");
        } else {
            emit backend->passwordResetFailed(bodyText.isEmpty()
                                                  ? "비밀번호 재설정에 실패했습니다."
                                                  : bodyText);
        }

        if (state->m_passwordResetReply == reply) {
            state->m_passwordResetReply = nullptr;
        }
        state->m_passwordResetInProgress = false;
        reply->deleteLater();
    });
}

// 이메일 인증 요청 함수
void BackendAuthRequestService::requestEmailVerification(Backend *backend, BackendPrivate *state, QString id, QString email)
{
    // 회원가입 이메일 인증 코드 발급 요청 처리
    const QString trimmedId = id.trimmed();
    const QString trimmedEmail = email.trimmed();
    if (trimmedId.isEmpty() || trimmedEmail.isEmpty()) {
        emit backend->emailVerificationRequestFailed("ID와 이메일을 모두 입력해 주세요.");
        return;
    }
    const QString emailError = validateEmailForClient(trimmedEmail);
    if (!emailError.isEmpty()) {
        emit backend->emailVerificationRequestFailed(emailError);
        return;
    }
    if (state->m_emailVerifyRequestInProgress) {
        emit backend->emailVerificationRequestFailed("이메일 인증 요청이 이미 처리 중입니다.");
        return;
    }

    const QString requestUrl = backend->serverUrl() + "/auth/email/verify/request";
    QNetworkRequest request{QUrl(requestUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = trimmedId;
    json["email"] = trimmedEmail;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_emailVerifyRequestReply = reply;
    state->m_emailVerifyRequestInProgress = true;
    backend->attachIgnoreSslErrors(reply, "EMAIL_VERIFY_REQUEST");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            QString message;
            QString debugToken;
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (doc.isObject()) {
                const QJsonObject obj = doc.object();
                message = obj.value("message").toString().trimmed();
                debugToken = obj.value("debug_token").toString().trimmed();
            }
            if (message.isEmpty()) {
                message = "이메일 인증 코드를 전송했습니다.";
            }
            emit backend->emailVerificationRequested(message, debugToken);
        } else if (isSslFailure(netError)) {
            emit backend->emailVerificationRequestFailed("서버 SSL 검증에 실패했습니다.");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->emailVerificationRequestFailed("서버에 연결할 수 없습니다.");
        } else {
            emit backend->emailVerificationRequestFailed(bodyText.isEmpty()
                                                             ? "이메일 인증 코드 전송에 실패했습니다."
                                                             : bodyText);
        }

        if (state->m_emailVerifyRequestReply == reply) {
            state->m_emailVerifyRequestReply = nullptr;
        }
        state->m_emailVerifyRequestInProgress = false;
        reply->deleteLater();
    });
}

void BackendAuthRequestService::confirmEmailVerification(Backend *backend,
                                                         BackendPrivate *state,
                                                         QString id,
                                                         QString email,
                                                         QString code)
{
    // 회원가입 이메일 인증 코드 확인 요청 처리
    const QString trimmedId = id.trimmed();
    const QString trimmedEmail = email.trimmed();
    const QString trimmedCode = code.trimmed();
    if (trimmedId.isEmpty() || trimmedEmail.isEmpty()) {
        emit backend->emailVerificationConfirmFailed("ID와 이메일을 먼저 입력해 주세요.");
        return;
    }
    const QString emailError = validateEmailForClient(trimmedEmail);
    if (!emailError.isEmpty()) {
        emit backend->emailVerificationConfirmFailed(emailError);
        return;
    }
    if (trimmedCode.isEmpty()) {
        emit backend->emailVerificationConfirmFailed("인증 코드를 입력해 주세요.");
        return;
    }
    if (state->m_emailVerifyConfirmInProgress) {
        emit backend->emailVerificationConfirmFailed("인증 코드 확인 요청이 이미 처리 중입니다.");
        return;
    }

    const QString confirmUrl = backend->serverUrl() + "/auth/email/verify/confirm";
    QNetworkRequest request{QUrl(confirmUrl)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    backend->applySslIfNeeded(request);

    QJsonObject json;
    json["id"] = trimmedId;
    json["email"] = trimmedEmail;
    json["code"] = trimmedCode;

    QNetworkReply *reply = state->m_manager->post(request, QJsonDocument(json).toJson());
    state->m_emailVerifyConfirmReply = reply;
    state->m_emailVerifyConfirmInProgress = true;
    backend->attachIgnoreSslErrors(reply, "EMAIL_VERIFY_CONFIRM");

    QObject::connect(reply, &QNetworkReply::finished, backend, [=]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError netError = reply->error();
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);

        if (netError == QNetworkReply::NoError) {
            QString message;
            const QJsonDocument doc = QJsonDocument::fromJson(responseBody);
            if (doc.isObject()) {
                message = doc.object().value("message").toString().trimmed();
            }
            if (message.isEmpty()) {
                message = "이메일 인증이 완료되었습니다.";
            }
            emit backend->emailVerificationConfirmed(message);
        } else if (isSslFailure(netError)) {
            emit backend->emailVerificationConfirmFailed("서버 SSL 검증에 실패했습니다.");
        } else if (isServerUnavailable(statusCode, netError)) {
            emit backend->emailVerificationConfirmFailed("서버에 연결할 수 없습니다.");
        } else {
            emit backend->emailVerificationConfirmFailed(bodyText.isEmpty()
                                                             ? "이메일 인증 코드 확인에 실패했습니다."
                                                             : bodyText);
        }

        if (state->m_emailVerifyConfirmReply == reply) {
            state->m_emailVerifyConfirmReply = nullptr;
        }
        state->m_emailVerifyConfirmInProgress = false;
        reply->deleteLater();
    });
}

// 사용자 회원가입 함수
void BackendAuthRequestService::registerUser(Backend *backend, BackendPrivate *state, QString id, QString pw, QString email)
{
    const QString trimmedId = id.trimmed();
    const QString trimmedEmail = email.trimmed();
    if (trimmedId.isEmpty() || pw.isEmpty() || trimmedEmail.isEmpty()) {
        emit backend->registerFailed("ID, 비밀번호, 이메일을 모두 입력해 주세요.");
        return;
    }
    const QString emailError = validateEmailForClient(trimmedEmail);
    if (!emailError.isEmpty()) {
        emit backend->registerFailed(emailError);
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
    json["email"] = trimmedEmail;
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
        const QByteArray responseBody = reply->readAll();
        const QString bodyText = responseText(responseBody);
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
            const QJsonDocument jsonDoc = QJsonDocument::fromJson(responseBody);
            QString successMessage;
            if (jsonDoc.isObject()) {
                successMessage = jsonDoc.object().value("message").toString().trimmed();
            }
            if (successMessage.isEmpty()) {
                successMessage = "회원가입이 완료되었습니다. 이메일 인증 후 로그인해 주세요.";
            }
            emit backend->registerSuccess(successMessage);
        } else {
            if (timedOut) {
                emit backend->registerFailed("회원가입 요청 시간이 초과되었습니다.");
            } else if (netError == QNetworkReply::OperationCanceledError) {
                emit backend->registerFailed("회원가입 요청이 취소되었습니다.");
            } else if (statusCode == 409) {
                emit backend->registerFailed(bodyText.isEmpty() ? "이미 사용 중인 ID 또는 이메일입니다." : bodyText);
            } else if (statusCode == 400) {
                emit backend->registerFailed(bodyText.isEmpty() ? "회원가입 입력값이 올바르지 않습니다." : bodyText);
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
