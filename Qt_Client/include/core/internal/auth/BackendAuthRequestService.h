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
    static void registerUser(Backend *backend, BackendPrivate *state, QString id, QString pw);
};

#endif // BACKEND_AUTH_REQUEST_SERVICE_H
