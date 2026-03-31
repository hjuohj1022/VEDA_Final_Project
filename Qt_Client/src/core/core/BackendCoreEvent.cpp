#include "Backend.h"
#include "internal/core/BackendCoreEventLogService.h"
#include "internal/core/BackendCoreEventService.h"
#include "internal/core/Backend_p.h"

// mark 이벤트 알림 Read 처리 함수
void Backend::markEventAlertRead()
{
    BackendCoreEventService::markEventAlertRead(this, d_ptr.get());
}

// 이벤트 알림 정리 함수
void Backend::clearEventAlert()
{
    BackendCoreEventService::clearEventAlert(this, d_ptr.get());
}

// 이벤트 알림 이력 삭제 함수
bool Backend::deleteEventAlertHistory()
{
    return BackendCoreEventLogService::deleteEventHistory(this, d_ptr.get());
}

// 이벤트 알림 항목 삭제 함수
bool Backend::deleteEventAlertItem(qulonglong eventLogId)
{
    return BackendCoreEventLogService::deleteEventHistoryItem(this, d_ptr.get(), eventLogId);
}

// 이벤트 알림 이력 갱신 함수
bool Backend::refreshEventAlertHistory()
{
    if (!d_ptr || d_ptr->m_authToken.trimmed().isEmpty()) {
        return false;
    }

    BackendCoreEventLogService::loadEventHistory(this, d_ptr.get());
    return true;
}

// 이벤트 알림 Preset 갱신 함수
void Backend::updateEventAlertPreset(int motor1Angle, int motor2Angle, int motor3Angle, bool laserEnabled)
{
    BackendCoreEventService::updateEventAlertPreset(this, d_ptr.get(), motor1Angle, motor2Angle, motor3Angle, laserEnabled);
}

// 이벤트 알림 제어 적용 함수
bool Backend::applyEventAlertControl()
{
    return BackendCoreEventService::applyEventAlertControl(this, d_ptr.get());
}

// 이벤트 알림 제어 중지 함수
bool Backend::stopEventAlertControl()
{
    return BackendCoreEventService::stopEventAlertControl(this, d_ptr.get());
}

// 이벤트 알림 메시지 처리 함수
void Backend::handleEventAlertMessage(const QByteArray &message)
{
    BackendCoreEventService::handleEventAlertMessage(this, d_ptr.get(), message);
}
