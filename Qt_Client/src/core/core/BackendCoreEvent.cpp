#include "Backend.h"
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

void Backend::updateEventAlertPreset(int motor1Angle, int motor2Angle, int motor3Angle, bool laserEnabled)
{
    BackendCoreEventService::updateEventAlertPreset(this, d_ptr.get(), motor1Angle, motor2Angle, motor3Angle, laserEnabled);
}

bool Backend::applyEventAlertControl()
{
    return BackendCoreEventService::applyEventAlertControl(this, d_ptr.get());
}

void Backend::handleEventAlertMessage(const QByteArray &message)
{
    BackendCoreEventService::handleEventAlertMessage(this, d_ptr.get(), message);
}
