#include "Backend.h"
#include "internal/cctv/BackendMotorControlService.h"

// 모터 지정 방향 press 명령 전달
bool Backend::motorPress(int motor, const QString &direction)
{
    return BackendMotorControlService::motorPress(this, d_ptr.get(), motor, direction);
}

// 모터 press 상태 release 해제
bool Backend::motorRelease(int motor)
{
    return BackendMotorControlService::motorRelease(this, d_ptr.get(), motor);
}

// 특정 모터 즉시 정지
bool Backend::motorStop(int motor)
{
    return BackendMotorControlService::motorStop(this, d_ptr.get(), motor);
}

// 특정 모터 목표 각도 이동
bool Backend::motorSetAngle(int motor, int angle)
{
    return BackendMotorControlService::motorSetAngle(this, d_ptr.get(), motor, angle);
}

// 전체 모터 동일 중심 각도 정렬
bool Backend::motorSetSpeed(int motor, int speed)
{
    return BackendMotorControlService::motorSetSpeed(this, d_ptr.get(), motor, speed);
}

bool Backend::motorCenter(int angle)
{
    return BackendMotorControlService::motorCenter(this, d_ptr.get(), angle);
}

// 전체 모터 동작 일괄 정지
bool Backend::motorStopAll()
{
    return BackendMotorControlService::motorStopAll(this, d_ptr.get());
}

bool Backend::motorEmergency()
{
    return BackendMotorControlService::motorEmergency(this, d_ptr.get());
}

bool Backend::laserOn()
{
    return BackendMotorControlService::laserOn(this, d_ptr.get());
}

bool Backend::laserOff()
{
    return BackendMotorControlService::laserOff(this, d_ptr.get());
}

bool Backend::laserStatus()
{
    return BackendMotorControlService::laserStatus(this, d_ptr.get());
}

bool Backend::laserSetEnabled(bool enabled)
{
    return BackendMotorControlService::laserSetEnabled(this, d_ptr.get(), enabled);
}
