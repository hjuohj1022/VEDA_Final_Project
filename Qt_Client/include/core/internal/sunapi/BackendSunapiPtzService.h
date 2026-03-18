#ifndef BACKEND_SUNAPI_PTZ_SERVICE_H
#define BACKEND_SUNAPI_PTZ_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendSunapiPtzService
{
public:
    static bool sendSunapiPtzFocusCommand(Backend *backend,
                                          BackendPrivate *state,
                                          int cameraIndex,
                                          const QString &command,
                                          const QString &actionLabel);
    static bool sunapiZoomIn(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiZoomOut(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiZoomStop(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiFocusNear(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiFocusFar(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiFocusStop(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiSimpleAutoFocus(Backend *backend, BackendPrivate *state, int cameraIndex);
};

#endif // BACKEND_SUNAPI_PTZ_SERVICE_H
