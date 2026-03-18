#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportHttpService.h"
#include "internal/core/Backend_p.h"

void Backend::requestPlaybackExport(int channelIndex,
                                    const QString &dateText,
                                    const QString &startTimeText,
                                    const QString &endTimeText,
                                    const QString &savePath)
{
    BackendSunapiExportHttpService::requestPlaybackExport(this,
                                                          d_ptr.get(),
                                                          channelIndex,
                                                          dateText,
                                                          startTimeText,
                                                          endTimeText,
                                                          savePath);
}

