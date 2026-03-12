#ifndef BACKEND_STREAMING_WS_SERVICE_H
#define BACKEND_STREAMING_WS_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendStreamingWsService
{
public:
    static void streamingWsConnect(Backend *backend, BackendPrivate *state);
    static void streamingWsDisconnect(Backend *backend, BackendPrivate *state);
    static bool streamingWsSendHex(Backend *backend, BackendPrivate *state, QString hexPayload);
    static bool playbackWsPause(Backend *backend, BackendPrivate *state);
    static bool playbackWsPlay(Backend *backend, BackendPrivate *state);
};

#endif // BACKEND_STREAMING_WS_SERVICE_H
