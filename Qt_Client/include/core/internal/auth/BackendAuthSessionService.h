#ifndef BACKEND_AUTH_SESSION_SERVICE_H
#define BACKEND_AUTH_SESSION_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendAuthSessionService
{
public:
    static void skipLoginTemporarily(Backend *backend, BackendPrivate *state);
    static void logout(Backend *backend, BackendPrivate *state);
    static void resetSessionTimer(Backend *backend, BackendPrivate *state);
    static bool adminUnlock(Backend *backend, BackendPrivate *state, const QString &adminCode);
    static void onSessionTick(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_AUTH_SESSION_SERVICE_H
