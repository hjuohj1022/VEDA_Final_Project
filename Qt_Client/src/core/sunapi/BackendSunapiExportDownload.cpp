#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportDownloadService.h"
#include "internal/core/Backend_p.h"

// 재생 내보내기 다운로드 시작 함수
void Backend::playbackExportStartDownload(const QUrl &downloadUrl, const QString &outPath)
{
    BackendSunapiExportDownloadService::playbackExportStartDownload(this, d_ptr.get(), downloadUrl, outPath);
}

