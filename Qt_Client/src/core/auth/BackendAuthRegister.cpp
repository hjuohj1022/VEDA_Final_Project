#include "Backend.h"
#include "internal/auth/BackendAuthRequestService.h"

// 이메일 인증 요청 함수
void Backend::requestEmailVerification(QString id, QString email)
{
    // 회원가입 이메일 인증 코드 발급 요청 전달
    BackendAuthRequestService::requestEmailVerification(this, d_ptr.get(), id, email);
}

// 이메일 인증 확정 함수
void Backend::confirmEmailVerification(QString id, QString email, QString code)
{
    // 회원가입 이메일 인증 코드 확인 요청 전달
    BackendAuthRequestService::confirmEmailVerification(this, d_ptr.get(), id, email, code);
}

// 사용자 회원가입 함수
void Backend::registerUser(QString id, QString pw, QString email)
{
    // 회원가입 요청 전달
    BackendAuthRequestService::registerUser(this, d_ptr.get(), id, pw, email);
}
