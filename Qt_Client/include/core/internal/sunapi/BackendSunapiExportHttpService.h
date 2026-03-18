#ifndef BACKEND_SUNAPI_EXPORT_HTTP_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_HTTP_SERVICE_H

class Backend;
struct BackendPrivate;
class QString;

class BackendSunapiExportHttpService
{
public:
    static void requestPlaybackExport(Backend *backend,
                                      BackendPrivate *state,
                                      int channelIndex,
                                      const QString &dateText,
                                      const QString &startTimeText,
                                      const QString &endTimeText,
                                      const QString &savePath);
};

#endif // BACKEND_SUNAPI_EXPORT_HTTP_SERVICE_H
