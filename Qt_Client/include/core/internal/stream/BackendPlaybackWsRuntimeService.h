#ifndef BACKEND_PLAYBACK_WS_RUNTIME_SERVICE_H
#define BACKEND_PLAYBACK_WS_RUNTIME_SERVICE_H

class Backend;
struct BackendPrivate;
class QByteArray;
class QString;

class BackendPlaybackWsRuntimeService
{
public:
    static QString ensurePlaybackWsSdpSource(Backend *backend, BackendPrivate *state);
    static void parsePlaybackH264ConfigFromRtp(Backend *backend,
                                               BackendPrivate *state,
                                               const QByteArray &rtpPacket);
    static void forwardPlaybackInterleavedRtp(Backend *backend,
                                              BackendPrivate *state,
                                              const QByteArray &bytes);
};

#endif // BACKEND_PLAYBACK_WS_RUNTIME_SERVICE_H
