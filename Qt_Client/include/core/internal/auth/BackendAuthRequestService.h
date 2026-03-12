#ifndef BACKEND_AUTH_REQUEST_SERVICE_H
#define BACKEND_AUTH_REQUEST_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendAuthRequestService
{
public:
    static void login(Backend *backend, BackendPrivate *state, QString id, QString pw);
    static void registerUser(Backend *backend, BackendPrivate *state, QString id, QString pw);
};

#endif // BACKEND_AUTH_REQUEST_SERVICE_H
