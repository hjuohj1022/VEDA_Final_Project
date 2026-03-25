#ifndef BACKEND_CORE_EVENT_LOG_SERVICE_H
#define BACKEND_CORE_EVENT_LOG_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendCoreEventLogService
{
public:
    static void loadEventHistory(Backend *backend, BackendPrivate *state, int limit = 50);
    static void clearCachedEventHistory(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_CORE_EVENT_LOG_SERVICE_H
