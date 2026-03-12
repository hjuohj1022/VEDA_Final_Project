#include "Backend.h"
#include "internal/rtsp/BackendRtspProbeService.h"

void Backend::probeRtspEndpoint(QString ip, QString port, int timeoutMs)
{
    BackendRtspProbeService::probeRtspEndpoint(this, ip, port, timeoutMs);
}

