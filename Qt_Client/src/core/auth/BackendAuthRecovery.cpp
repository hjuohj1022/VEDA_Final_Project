#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"

// 비밀번호 재설정 요청 함수
void Backend::requestPasswordReset(QString id, QString email)
{
    // 비밀번호 재설정 코드 요청 전달
    BackendAuthRequestService::requestPasswordReset(this, d_ptr.get(), id, email);
}

// 비밀번호 코드 기반 초기화 함수
void Backend::resetPasswordWithCode(QString code, QString newPassword)
{
    // 재설정 코드 기반 비밀번호 변경 요청 전달
    BackendAuthRequestService::resetPasswordWithCode(this, d_ptr.get(), code, newPassword);
}
