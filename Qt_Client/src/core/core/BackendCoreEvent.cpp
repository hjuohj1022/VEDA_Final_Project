#include "Backend.h"
#include "internal/core/BackendCoreEventLogService.h"
#include "internal/core/BackendCoreEventService.h"
#include "internal/core/Backend_p.h"

void Backend::markEventAlertRead()
{
    BackendCoreEventService::markEventAlertRead(this, d_ptr.get());
}

void Backend::clearEventAlert()
{
    BackendCoreEventService::clearEventAlert(this, d_ptr.get());
}

bool Backend::deleteEventAlertHistory()
{
    return BackendCoreEventLogService::deleteEventHistory(this, d_ptr.get());
}

bool Backend::deleteEventAlertItem(qulonglong eventLogId)
{
    return BackendCoreEventLogService::deleteEventHistoryItem(this, d_ptr.get(), eventLogId);
}

bool Backend::refreshEventAlertHistory()
{
    if (!d_ptr || d_ptr->m_authToken.trimmed().isEmpty()) {
        return false;
    }

    BackendCoreEventLogService::loadEventHistory(this, d_ptr.get());
    return true;
}

void Backend::updateEventAlertPreset(int motor1Angle, int motor2Angle, int motor3Angle, bool laserEnabled)
{
    BackendCoreEventService::updateEventAlertPreset(this, d_ptr.get(), motor1Angle, motor2Angle, motor3Angle, laserEnabled);
}

bool Backend::applyEventAlertControl()
{
    return BackendCoreEventService::applyEventAlertControl(this, d_ptr.get());
}

bool Backend::stopEventAlertControl()
{
    return BackendCoreEventService::stopEventAlertControl(this, d_ptr.get());
}

void Backend::handleEventAlertMessage(const QByteArray &message)
{
    BackendCoreEventService::handleEventAlertMessage(this, d_ptr.get(), message);
}
