#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportDownloadService.h"
#include "internal/core/Backend_p.h"

void Backend::playbackExportStartDownload(const QUrl &downloadUrl, const QString &outPath)
{
    BackendSunapiExportDownloadService::playbackExportStartDownload(this, d_ptr.get(), downloadUrl, outPath);
}

