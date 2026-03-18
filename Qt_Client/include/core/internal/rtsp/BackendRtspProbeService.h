#ifndef BACKEND_RTSP_PROBE_SERVICE_H
#define BACKEND_RTSP_PROBE_SERVICE_H

class Backend;
class QString;

class BackendRtspProbeService
{
public:
    static void probeRtspEndpoint(Backend *backend, QString ip, QString port, int timeoutMs);
};

#endif // BACKEND_RTSP_PROBE_SERVICE_H
