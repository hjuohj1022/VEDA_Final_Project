#ifndef BACKEND_SUNAPI_EXPORT_FFMPEG_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_FFMPEG_SERVICE_H

#include <functional>

class Backend;
struct BackendPrivate;
class QString;

class BackendSunapiExportFfmpegService
{
public:
    static bool startPlaybackExportViaFfmpegBackup(Backend *backend,
                                                   BackendPrivate *state,
                                                   int channelIndex,
                                                   const QString &dateText,
                                                   const QString &startTimeText,
                                                   const QString &endTimeText,
                                                   const QString &outPath,
                                                   const std::function<void()> &onFailedFallback);
};

#endif // BACKEND_SUNAPI_EXPORT_FFMPEG_SERVICE_H
