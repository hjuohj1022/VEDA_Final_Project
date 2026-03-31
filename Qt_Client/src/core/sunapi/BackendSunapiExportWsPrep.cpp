#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportWsPrepService.h"
#include "internal/core/Backend_p.h"

// 재생 내보내기 FFmpeg 실행 파일 경로 확인 함수
QString Backend::resolvePlaybackExportFfmpegBinary() const
{
    return BackendSunapiExportWsPrepService::resolvePlaybackExportFfmpegBinary(d_ptr.get());
}

// 재생 내보내기 웹소켓 출력 경로 생성 함수
bool Backend::buildPlaybackExportWsOutputPath(const QString &savePath,
                                              bool *wantsAvi,
                                              QString *outPath,
                                              QString *finalOutPath,
                                              QString *error)
{
    return BackendSunapiExportWsPrepService::buildPlaybackExportWsOutputPath(d_ptr.get(),
                                                                              savePath,
                                                                              wantsAvi,
                                                                              outPath,
                                                                              finalOutPath,
                                                                              error);
}

