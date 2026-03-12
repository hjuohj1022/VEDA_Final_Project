#ifndef BACKEND_RTSP_CONFIG_SERVICE_H
#define BACKEND_RTSP_CONFIG_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendRtspConfigService
{
public:
    static void setRtspIp(Backend *backend, BackendPrivate *state, const QString &ip);
    static void setRtspPort(Backend *backend, BackendPrivate *state, const QString &port);
    static QString buildRtspUrl(const Backend *backend, const BackendPrivate *state, int cameraIndex, bool useSubStream);
    static bool updateRtspIp(Backend *backend, BackendPrivate *state, const QString &ip);
    static bool updateRtspConfig(Backend *backend, BackendPrivate *state, const QString &ip, const QString &port);
    static bool resetRtspConfigToEnv(Backend *backend, BackendPrivate *state);
    static bool updateRtspCredentials(Backend *backend, BackendPrivate *state, const QString &username, const QString &password);
    static void useEnvRtspCredentials(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_RTSP_CONFIG_SERVICE_H
