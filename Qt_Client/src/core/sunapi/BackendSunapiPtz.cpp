#include "Backend.h"
#include "internal/sunapi/BackendSunapiPtzService.h"

// Sunapi PTZ 포커스 명령 전송 함수
bool Backend::sendSunapiPtzFocusCommand(int cameraIndex, const QString &command, const QString &actionLabel)
{
    return BackendSunapiPtzService::sendSunapiPtzFocusCommand(this, d_ptr.get(), cameraIndex, command, actionLabel);
}

// Sunapi 줌 인 함수
bool Backend::sunapiZoomIn(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiZoomIn(this, d_ptr.get(), cameraIndex);
}

// Sunapi 줌 아웃 함수
bool Backend::sunapiZoomOut(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiZoomOut(this, d_ptr.get(), cameraIndex);
}

// Sunapi 줌 중지 함수
bool Backend::sunapiZoomStop(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiZoomStop(this, d_ptr.get(), cameraIndex);
}

// Sunapi 포커스 근거리 이동 함수
bool Backend::sunapiFocusNear(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiFocusNear(this, d_ptr.get(), cameraIndex);
}

// Sunapi 포커스 원거리 이동 함수
bool Backend::sunapiFocusFar(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiFocusFar(this, d_ptr.get(), cameraIndex);
}

// Sunapi 포커스 중지 함수
bool Backend::sunapiFocusStop(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiFocusStop(this, d_ptr.get(), cameraIndex);
}

// Sunapi 단순 자동 포커스 함수
bool Backend::sunapiSimpleAutoFocus(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiSimpleAutoFocus(this, d_ptr.get(), cameraIndex);
}

