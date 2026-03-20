#include "internal/auth/BackendAuthSessionService.h"

#include "Backend.h"
#include "internal/core/Backend_p.h"

void BackendAuthSessionService::logout(Backend *backend, BackendPrivate *state)
{
    if (!state->m_isLoggedIn) {
        return;
    }

    if (state->m_twoFactorStatusReply && state->m_twoFactorStatusReply->isRunning()) {
        state->m_twoFactorStatusReply->abort();
    }
    state->m_twoFactorStatusReply = nullptr;
    state->m_twoFactorStatusInProgress = false;
    if (state->m_twoFactorSetupReply && state->m_twoFactorSetupReply->isRunning()) {
        state->m_twoFactorSetupReply->abort();
    }
    state->m_twoFactorSetupReply = nullptr;
    state->m_twoFactorSetupInProgress = false;
    if (state->m_twoFactorConfirmReply && state->m_twoFactorConfirmReply->isRunning()) {
        state->m_twoFactorConfirmReply->abort();
    }
    state->m_twoFactorConfirmReply = nullptr;
    state->m_twoFactorConfirmInProgress = false;
    if (state->m_twoFactorDisableReply && state->m_twoFactorDisableReply->isRunning()) {
        state->m_twoFactorDisableReply->abort();
    }
    state->m_twoFactorDisableReply = nullptr;
    state->m_twoFactorDisableInProgress = false;
    if (state->m_accountDeleteReply && state->m_accountDeleteReply->isRunning()) {
        state->m_accountDeleteReply->abort();
    }
    state->m_accountDeleteReply = nullptr;
    state->m_accountDeleteInProgress = false;
    if (state->m_passwordChangeReply && state->m_passwordChangeReply->isRunning()) {
        state->m_passwordChangeReply->abort();
    }
    state->m_passwordChangeReply = nullptr;
    state->m_passwordChangeInProgress = false;
    if (state->m_thermalStartReply && state->m_thermalStartReply->isRunning()) {
        state->m_thermalStartReply->abort();
    }
    state->m_thermalStartReply = nullptr;
    if (state->m_thermalStopReply && state->m_thermalStopReply->isRunning()) {
        state->m_thermalStopReply->abort();
    }
    state->m_thermalStopReply = nullptr;
    if (state->m_thermalWs) {
        state->m_thermalStopExpected = true;
        state->m_thermalWs->close();
    }
    if (state->m_thermalStreaming) {
        state->m_thermalStreaming = false;
        emit backend->thermalStreamingChanged();
    }

    state->m_isLoggedIn = false;
    if (state->m_twoFactorRequired || !state->m_preAuthToken.isEmpty()) {
        state->m_twoFactorRequired = false;
        state->m_preAuthToken.clear();
        state->m_pendingLoginId.clear();
        emit backend->twoFactorRequiredChanged();
    }
    if (state->m_twoFactorEnabled) {
        state->m_twoFactorEnabled = false;
        emit backend->twoFactorEnabledChanged();
    }
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

