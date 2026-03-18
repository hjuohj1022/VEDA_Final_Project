#ifndef BACKEND_CORE_STATE_SERVICE_H
#define BACKEND_CORE_STATE_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendCoreStateService
{
public:
    static bool twoFactorRequired(const BackendPrivate *state);
    static bool twoFactorEnabled(const BackendPrivate *state);
    static QString userId(const BackendPrivate *state);
    static int sessionRemainingSeconds(const BackendPrivate *state);
    static bool loginLocked(const BackendPrivate *state);
    static int loginFailedAttempts(const BackendPrivate *state);
    static int loginMaxAttempts(const BackendPrivate *state);
    static int activeCameras(const BackendPrivate *state);
    static int currentFps(const BackendPrivate *state);
    static int latency(const BackendPrivate *state);
    static QString storageUsed(const BackendPrivate *state);
    static QString storageTotal(const BackendPrivate *state);
    static int storagePercent(const BackendPrivate *state);
    static int detectedObjects(const BackendPrivate *state);
    static QString networkStatus(const BackendPrivate *state);

    static QString thermalFrameDataUrl(const BackendPrivate *state);
    static QString cctv3dMapFrameDataUrl(const BackendPrivate *state);
    static QString thermalInfoText(const BackendPrivate *state);
    static bool thermalStreaming(const BackendPrivate *state);
    static QString thermalPalette(const BackendPrivate *state);
    static bool thermalAutoRange(const BackendPrivate *state);
    static int thermalAutoRangeWindowPercent(const BackendPrivate *state);
    static int thermalManualMin(const BackendPrivate *state);
    static int thermalManualMax(const BackendPrivate *state);
    static int displayContrast(const BackendPrivate *state);
    static int displayBrightness(const BackendPrivate *state);
    static int displaySharpnessLevel(const BackendPrivate *state);
    static bool displaySharpnessEnabled(const BackendPrivate *state);
    static int displayColorLevel(const BackendPrivate *state);

    static void setActiveCameras(Backend *backend, BackendPrivate *state, int count);
    static void setCurrentFps(Backend *backend, BackendPrivate *state, int fps);
    static void setLatency(Backend *backend, BackendPrivate *state, int ms);
};

#endif // BACKEND_CORE_STATE_SERVICE_H
