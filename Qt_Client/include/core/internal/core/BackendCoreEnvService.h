#ifndef BACKEND_CORE_ENV_SERVICE_H
#define BACKEND_CORE_ENV_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendCoreEnvService
{
public:
    static void loadEnv(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_CORE_ENV_SERVICE_H
