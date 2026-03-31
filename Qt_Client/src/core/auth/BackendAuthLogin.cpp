#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"
#include "internal/core/Backend_p.h"

// 로그인 함수
void Backend::login(QString id, QString pw)
{
    BackendAuthRequestService::login(this, d_ptr.get(), id, pw);
}

// 이중 인증 OTP 검증 함수
void Backend::verifyTwoFactorOtp(QString otp)
{
    BackendAuthRequestService::verifyTwoFactorOtp(this, d_ptr.get(), otp);
}

// 이중 인증 상태 갱신 함수
void Backend::refreshTwoFactorStatus()
{
    BackendAuthRequestService::refreshTwoFactorStatus(this, d_ptr.get());
}

// 이중 인증 설정 시작 함수
void Backend::startTwoFactorSetup()
{
    BackendAuthRequestService::startTwoFactorSetup(this, d_ptr.get());
}

// 이중 인증 설정 확정 함수
void Backend::confirmTwoFactorSetup(QString otp)
{
    BackendAuthRequestService::confirmTwoFactorSetup(this, d_ptr.get(), otp);
}

// 이중 인증 비활성화 함수
void Backend::disableTwoFactor(QString otp)
{
    BackendAuthRequestService::disableTwoFactor(this, d_ptr.get(), otp);
}

// 계정 삭제 함수
void Backend::deleteAccount(QString password, QString otp)
{
    BackendAuthRequestService::deleteAccount(this, d_ptr.get(), password, otp);
}

// 비밀번호 변경 함수
void Backend::changePassword(QString currentPassword, QString newPassword)
{
    // 로그인 사용자 비밀번호 변경 요청 위임
    BackendAuthRequestService::changePassword(this, d_ptr.get(), currentPassword, newPassword);
}

// 이중 인증 로그인 취소 함수
void Backend::cancelTwoFactorLogin()
{
    if (d_ptr->m_twoFactorVerifyReply && d_ptr->m_twoFactorVerifyReply->isRunning()) {
        d_ptr->m_twoFactorVerifyReply->setProperty("userCanceled", true);
        d_ptr->m_twoFactorVerifyReply->abort();
    }

    const bool hadTwoFactorState = d_ptr->m_twoFactorRequired || !d_ptr->m_preAuthToken.isEmpty();
    d_ptr->m_twoFactorRequired = false;
    d_ptr->m_preAuthToken.clear();
    d_ptr->m_pendingLoginId.clear();

    if (hadTwoFactorState) {
        emit twoFactorRequiredChanged();
    }
}
