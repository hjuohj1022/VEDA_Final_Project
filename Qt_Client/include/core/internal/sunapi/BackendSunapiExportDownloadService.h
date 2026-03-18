#ifndef BACKEND_SUNAPI_EXPORT_DOWNLOAD_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_DOWNLOAD_SERVICE_H

class Backend;
struct BackendPrivate;
class QUrl;
class QString;

class BackendSunapiExportDownloadService
{
public:
    static void playbackExportStartDownload(Backend *backend,
                                            BackendPrivate *state,
                                            const QUrl &downloadUrl,
                                            const QString &outPath);
};

#endif // BACKEND_SUNAPI_EXPORT_DOWNLOAD_SERVICE_H
