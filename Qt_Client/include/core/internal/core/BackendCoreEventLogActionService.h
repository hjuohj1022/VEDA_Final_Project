#ifndef BACKEND_CORE_EVENT_LOG_ACTION_SERVICE_H
#define BACKEND_CORE_EVENT_LOG_ACTION_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendCoreEventLogActionService
{
public:
    // 현재 이벤트 row의 비상 동작 상태를 서버 DB에 반영한다.
    static bool requestCurrentEventActionUpdate(Backend *backend,
                                                BackendPrivate *state,
                                                const QString &actionType,
                                                const QString &actionResult,
                                                const QString &actionMessage,
                                                bool allowRetry = true);
};

#endif // BACKEND_CORE_EVENT_LOG_ACTION_SERVICE_H
