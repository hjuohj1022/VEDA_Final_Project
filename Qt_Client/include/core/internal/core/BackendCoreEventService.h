#ifndef BACKEND_CORE_EVENT_SERVICE_H
#define BACKEND_CORE_EVENT_SERVICE_H

class Backend;
struct BackendPrivate;
class QByteArray;

class BackendCoreEventService
{
public:
    static void markEventAlertRead(Backend *backend, BackendPrivate *state);
    static void clearEventAlert(Backend *backend, BackendPrivate *state);
    static void updateEventAlertPreset(Backend *backend,
                                       BackendPrivate *state,
                                       int motor1Angle,
                                       int motor2Angle,
                                       int motor3Angle,
                                       bool laserEnabled);
    static bool applyEventAlertControl(Backend *backend, BackendPrivate *state);
    static bool stopEventAlertControl(Backend *backend, BackendPrivate *state);
    static void handleEventAlertMessage(Backend *backend, BackendPrivate *state, const QByteArray &message);
};

#endif // BACKEND_CORE_EVENT_SERVICE_H
