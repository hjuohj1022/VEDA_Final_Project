#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportFfmpegService.h"
#include "internal/core/Backend_p.h"

// 재생 FFmpeg 백업 내보내기 시작 함수
bool Backend::startPlaybackExportViaFfmpegBackup(int channelIndex,
                                                 const QString &dateText,
                                                 const QString &startTimeText,
                                                 const QString &endTimeText,
                                                 const QString &outPath,
                                                 const std::function<void()> &onFailedFallback)
{
    return BackendSunapiExportFfmpegService::startPlaybackExportViaFfmpegBackup(this,
                                                                                 d_ptr.get(),
                                                                                 channelIndex,
                                                                                 dateText,
                                                                                 startTimeText,
                                                                                 endTimeText,
                                                                                 outPath,
                                                                                 onFailedFallback);
}

