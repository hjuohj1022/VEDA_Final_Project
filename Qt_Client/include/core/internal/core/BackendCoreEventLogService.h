#ifndef BACKEND_CORE_EVENT_LOG_SERVICE_H
#define BACKEND_CORE_EVENT_LOG_SERVICE_H

#include <QtGlobal>

class Backend;
struct BackendPrivate;

class BackendCoreEventLogService
{
public:
    static void loadEventHistory(Backend *backend, BackendPrivate *state, int limit = 50);
    static void syncCurrentEventLogIdFromHistory(Backend *backend, BackendPrivate *state);
    // 서버에 저장된 이벤트 목록 전체를 삭제한다.
    static bool deleteEventHistory(Backend *backend, BackendPrivate *state);
    // 선택한 이벤트 1건만 서버 목록에서 삭제한다.
    static bool deleteEventHistoryItem(Backend *backend, BackendPrivate *state, qulonglong eventLogId);
    static void clearCachedEventHistory(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_CORE_EVENT_LOG_SERVICE_H
