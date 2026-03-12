#include "Backend.h"
#include "internal/sunapi/BackendSunapiTimelineService.h"

void Backend::loadPlaybackTimeline(int channelIndex, const QString &dateText)
{
    BackendSunapiTimelineService::loadPlaybackTimeline(this, d_ptr.get(), channelIndex, dateText);
}

