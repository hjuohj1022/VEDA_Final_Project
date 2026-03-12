#ifndef BACKEND_CCTV3D_MAP_SERVICE_H
#define BACKEND_CCTV3D_MAP_SERVICE_H

class Backend;
struct BackendPrivate;

class BackendCctv3dMapService
{
public:
    static bool startCctv3dMapSequence(Backend *backend, BackendPrivate *state, int cameraIndex);
    static void stopCctv3dMapSequence(Backend *backend, BackendPrivate *state);
    static void runCctv3dMapSequenceStep(Backend *backend, BackendPrivate *state, int sequenceToken, int step);
    static void pollCctv3dMapMoveStatus(Backend *backend, BackendPrivate *state, int sequenceToken);
    static bool postCctvControlStart(Backend *backend, BackendPrivate *state, int sequenceToken);
    static bool postCctvControlStream(Backend *backend, BackendPrivate *state, int sequenceToken);
    static void connectCctvStreamWs(Backend *backend, BackendPrivate *state, int sequenceToken);
    static void disconnectCctvStreamWs(Backend *backend, BackendPrivate *state, bool expectedStop = false);
};

#endif // BACKEND_CCTV3D_MAP_SERVICE_H
