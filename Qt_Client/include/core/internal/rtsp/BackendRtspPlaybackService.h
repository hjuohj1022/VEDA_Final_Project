#ifndef BACKEND_RTSP_PLAYBACK_SERVICE_H
#define BACKEND_RTSP_PLAYBACK_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendRtspPlaybackService
{
public:
    static void preparePlaybackRtsp(Backend *backend,
                                    BackendPrivate *state,
                                    int channelIndex,
                                    const QString &dateText,
                                    const QString &timeText);
};

#endif // BACKEND_RTSP_PLAYBACK_SERVICE_H
