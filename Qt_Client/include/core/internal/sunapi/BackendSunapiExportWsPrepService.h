#ifndef BACKEND_SUNAPI_EXPORT_WS_PREP_SERVICE_H
#define BACKEND_SUNAPI_EXPORT_WS_PREP_SERVICE_H

class BackendPrivate;
class QString;

class BackendSunapiExportWsPrepService
{
public:
    static QString resolvePlaybackExportFfmpegBinary(const BackendPrivate *state);
    static bool buildPlaybackExportWsOutputPath(BackendPrivate *state,
                                                const QString &savePath,
                                                bool *wantsAvi,
                                                QString *outPath,
                                                QString *finalOutPath,
                                                QString *error);
};

#endif // BACKEND_SUNAPI_EXPORT_WS_PREP_SERVICE_H
