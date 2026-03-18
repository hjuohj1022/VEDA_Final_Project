#include "Backend.h"
#include "internal/sunapi/BackendSunapiPtzService.h"

bool Backend::sendSunapiPtzFocusCommand(int cameraIndex, const QString &command, const QString &actionLabel)
{
    return BackendSunapiPtzService::sendSunapiPtzFocusCommand(this, d_ptr.get(), cameraIndex, command, actionLabel);
}

bool Backend::sunapiZoomIn(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiZoomIn(this, d_ptr.get(), cameraIndex);
}

bool Backend::sunapiZoomOut(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiZoomOut(this, d_ptr.get(), cameraIndex);
}

bool Backend::sunapiZoomStop(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiZoomStop(this, d_ptr.get(), cameraIndex);
}

bool Backend::sunapiFocusNear(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiFocusNear(this, d_ptr.get(), cameraIndex);
}

bool Backend::sunapiFocusFar(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiFocusFar(this, d_ptr.get(), cameraIndex);
}

bool Backend::sunapiFocusStop(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiFocusStop(this, d_ptr.get(), cameraIndex);
}

bool Backend::sunapiSimpleAutoFocus(int cameraIndex)
{
    return BackendSunapiPtzService::sunapiSimpleAutoFocus(this, d_ptr.get(), cameraIndex);
}

