#ifndef BACKEND_SUNAPI_DISPLAY_SERVICE_H
#define BACKEND_SUNAPI_DISPLAY_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendSunapiDisplayService
{
public:
    static void sunapiLoadDisplaySettings(Backend *backend, BackendPrivate *state, int cameraIndex);
    static bool sunapiSetDisplaySettings(Backend *backend,
                                         BackendPrivate *state,
                                         int cameraIndex,
                                         int contrast,
                                         int brightness,
                                         int sharpnessLevel,
                                         int colorLevel,
                                         bool sharpnessEnabled);
    static bool sunapiResetDisplaySettings(Backend *backend, BackendPrivate *state, int cameraIndex);
};

#endif // BACKEND_SUNAPI_DISPLAY_SERVICE_H
