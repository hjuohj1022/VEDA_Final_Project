#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"

void Backend::requestPasswordReset(QString id, QString email)
{
    // 비밀번호 재설정 코드 요청 전달
    BackendAuthRequestService::requestPasswordReset(this, d_ptr.get(), id, email);
}

void Backend::resetPasswordWithCode(QString code, QString newPassword)
{
    // 재설정 코드 기반 비밀번호 변경 요청 전달
    BackendAuthRequestService::resetPasswordWithCode(this, d_ptr.get(), code, newPassword);
}
