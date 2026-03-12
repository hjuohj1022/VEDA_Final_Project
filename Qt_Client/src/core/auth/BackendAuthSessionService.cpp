#include "internal/auth/BackendAuthSessionService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

void BackendAuthSessionService::skipLoginTemporarily(Backend *backend, BackendPrivate *state)
{
    if (state->m_isLoggedIn) {
        return;
    }

    state->m_isLoggedIn = true;
    state->m_userId = "Skip";
    state->m_authToken.clear();
    state->m_sessionRemainingSeconds = state->m_sessionTimeoutSeconds;
    state->m_sessionTimer->start();

    if (state->m_loginFailedAttempts != 0 || state->m_loginLocked) {
        state->m_loginFailedAttempts = 0;
        state->m_loginLocked = false;
        emit backend->loginLockChanged();
    }

    emit backend->isLoggedInChanged();
    emit backend->userIdChanged();
    emit backend->sessionRemainingSecondsChanged();
    emit backend->loginSuccess();
}

void BackendAuthSessionService::logout(Backend *backend, BackendPrivate *state)
{
    if (!state->m_isLoggedIn) {
        return;
    }

    state->m_isLoggedIn = false;
    state->m_userId.clear();
    state->m_authToken.clear();
    state->m_sessionTimer->stop();
    state->m_sessionRemainingSeconds = 0;

    emit backend->isLoggedInChanged();
    emit backend->userIdChanged();
    emit backend->sessionRemainingSecondsChanged();
}

void BackendAuthSessionService::resetSessionTimer(Backend *backend, BackendPrivate *state)
{
    Q_UNUSED(backend);

    if (!state->m_isLoggedIn) {
        return;
    }

    if (state->m_sessionRemainingSeconds != state->m_sessionTimeoutSeconds) {
        state->m_sessionRemainingSeconds = state->m_sessionTimeoutSeconds;
        emit backend->sessionRemainingSecondsChanged();
    }

    if (!state->m_sessionTimer->isActive()) {
        state->m_sessionTimer->start();
    }
}

bool BackendAuthSessionService::adminUnlock(Backend *backend, BackendPrivate *state, const QString &adminCode)
{
    const QString expected = state->m_env.value("ADMIN_UNLOCK_KEY").trimmed();
    if (expected.isEmpty()) {
        emit backend->loginFailed("관리자 해제 키가 설정되어 있지 않습니다.");
        return false;
    }

    if (adminCode.trimmed() != expected) {
        emit backend->loginFailed("관리자 해제 키가 올바르지 않습니다.");
        return false;
    }

    state->m_loginLocked = false;
    state->m_loginFailedAttempts = 0;
    emit backend->loginLockChanged();
    return true;
}

void BackendAuthSessionService::onSessionTick(Backend *backend, BackendPrivate *state)
{
    if (!state->m_isLoggedIn) {
        state->m_sessionTimer->stop();
        return;
    }

    if (state->m_sessionRemainingSeconds > 0) {
        state->m_sessionRemainingSeconds--;
        emit backend->sessionRemainingSecondsChanged();
    }

    if (state->m_sessionRemainingSeconds <= 0) {
        BackendAuthSessionService::logout(backend, state);
        emit backend->sessionExpired();
    }
}

