#include "Backend.h"

// 서버 인증 없이 UI 테스트용 임시 로그인 상태 전환
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

// 세션 종료 및 로그인 상태 초기화
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

// 사용자 활동 시 세션 만료 카운트 재설정
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

// 관리자 코드 검증 후 로그인 잠금 해제
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

// 세션 만료 타이머 1초 주기 갱신 처리
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
