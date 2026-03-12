#ifndef BACKEND_INIT_SERVICE_H
#define BACKEND_INIT_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendInitService
{
public:
    static void initialize(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_INIT_SERVICE_H
