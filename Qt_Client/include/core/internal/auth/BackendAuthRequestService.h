#ifndef BACKEND_AUTH_REQUEST_SERVICE_H
#define BACKEND_AUTH_REQUEST_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendAuthRequestService
{
public:
    static void login(Backend *backend, BackendPrivate *state, QString id, QString pw);
    static void verifyTwoFactorOtp(Backend *backend, BackendPrivate *state, QString otp);
    static void refreshTwoFactorStatus(Backend *backend, BackendPrivate *state);
    static void startTwoFactorSetup(Backend *backend, BackendPrivate *state);
    static void confirmTwoFactorSetup(Backend *backend, BackendPrivate *state, QString otp);
    static void disableTwoFactor(Backend *backend, BackendPrivate *state, QString otp);
    static void deleteAccount(Backend *backend, BackendPrivate *state, QString password, QString otp);
    // 로그인 계정 비밀번호 변경 요청 처리
    static void changePassword(Backend *backend, BackendPrivate *state, QString currentPassword, QString newPassword);
    // 회원가입 이메일 인증 코드 발급 요청 처리
    static void requestEmailVerification(Backend *backend, BackendPrivate *state, QString id, QString email);
    // 회원가입 이메일 인증 코드 확인 요청 처리
    static void confirmEmailVerification(Backend *backend, BackendPrivate *state, QString id, QString email, QString code);
    static void registerUser(Backend *backend, BackendPrivate *state, QString id, QString pw, QString email);
};

#endif // BACKEND_AUTH_REQUEST_SERVICE_H
