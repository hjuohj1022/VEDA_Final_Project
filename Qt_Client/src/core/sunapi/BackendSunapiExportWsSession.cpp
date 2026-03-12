#include "Backend.h"
#include "internal/sunapi/BackendSunapiExportWsSessionService.h"
#include "internal/core/Backend_p.h"

void Backend::requestPlaybackExportViaWs(int channelIndex,
                                         const QString &dateText,
                                         const QString &startTimeText,
                                         const QString &endTimeText,
                                         const QString &savePath)
{
    BackendSunapiExportWsSessionService::requestPlaybackExportViaWs(this,
                                                                    d_ptr.get(),
                                                                    channelIndex,
                                                                    dateText,
                                                                    startTimeText,
                                                                    endTimeText,
                                                                    savePath);
}

