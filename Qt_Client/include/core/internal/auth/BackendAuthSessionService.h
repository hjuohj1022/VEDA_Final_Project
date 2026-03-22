#ifndef BACKEND_AUTH_SESSION_SERVICE_H
#define BACKEND_AUTH_SESSION_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendAuthSessionService
{
public:
    static void logout(Backend *backend, BackendPrivate *state);
    static void resetSessionTimer(Backend *backend, BackendPrivate *state);
    static void onSessionTick(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_AUTH_SESSION_SERVICE_H
