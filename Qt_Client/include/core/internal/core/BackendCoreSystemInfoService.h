#ifndef BACKEND_CORE_SYSTEM_INFO_SERVICE_H
#define BACKEND_CORE_SYSTEM_INFO_SERVICE_H

#include <QVariantMap>

class Backend;
struct BackendPrivate;

class BackendCoreSystemInfoService
{
public:
    static QVariantMap getClientSystemInfo(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_CORE_SYSTEM_INFO_SERVICE_H
