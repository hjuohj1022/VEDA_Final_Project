#include "Backend.h"
#include "internal/rtsp/BackendRtspProbeService.h"

// probe RTSP Endpoint 처리 함수
void Backend::probeRtspEndpoint(QString ip, QString port, int timeoutMs)
{
    BackendRtspProbeService::probeRtspEndpoint(this, ip, port, timeoutMs);
}

