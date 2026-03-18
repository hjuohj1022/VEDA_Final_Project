#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportWsPrepService.h"
#include "internal/core/Backend_p.h"

QString Backend::resolvePlaybackExportFfmpegBinary() const
{
    return BackendSunapiExportWsPrepService::resolvePlaybackExportFfmpegBinary(d_ptr.get());
}

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

