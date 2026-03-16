#ifndef BACKEND_MOTOR_CONTROL_SERVICE_H
#define BACKEND_MOTOR_CONTROL_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendMotorControlService
{
public:
    static bool motorPress(Backend *backend, BackendPrivate *state, int motor, const QString &direction);
    static bool motorRelease(Backend *backend, BackendPrivate *state, int motor);
    static bool motorStop(Backend *backend, BackendPrivate *state, int motor);
    static bool motorSetAngle(Backend *backend, BackendPrivate *state, int motor, int angle);
    static bool motorCenter(Backend *backend, BackendPrivate *state, int angle);
    static bool motorStopAll(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_MOTOR_CONTROL_SERVICE_H
