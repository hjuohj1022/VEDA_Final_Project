#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"
#include "internal/core/Backend_p.h"

void Backend::login(QString id, QString pw)
{
    BackendAuthRequestService::login(this, d_ptr.get(), id, pw);
}

void Backend::verifyTwoFactorOtp(QString otp)
{
    BackendAuthRequestService::verifyTwoFactorOtp(this, d_ptr.get(), otp);
}

void Backend::refreshTwoFactorStatus()
{
    BackendAuthRequestService::refreshTwoFactorStatus(this, d_ptr.get());
}

void Backend::startTwoFactorSetup()
{
    BackendAuthRequestService::startTwoFactorSetup(this, d_ptr.get());
}

void Backend::confirmTwoFactorSetup(QString otp)
{
    BackendAuthRequestService::confirmTwoFactorSetup(this, d_ptr.get(), otp);
}

void Backend::disableTwoFactor(QString otp)
{
    BackendAuthRequestService::disableTwoFactor(this, d_ptr.get(), otp);
}

void Backend::deleteAccount(QString password, QString otp)
{
    BackendAuthRequestService::deleteAccount(this, d_ptr.get(), password, otp);
}

void Backend::changePassword(QString currentPassword, QString newPassword)
{
    // 로그인 사용자 비밀번호 변경 요청 위임
    BackendAuthRequestService::changePassword(this, d_ptr.get(), currentPassword, newPassword);
}

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
