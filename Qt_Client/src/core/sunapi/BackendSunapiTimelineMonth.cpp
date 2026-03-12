#include "Backend.h"
#include "internal/sunapi/BackendSunapiTimelineService.h"

void Backend::loadPlaybackMonthRecordedDays(int channelIndex, int year, int month)
{
    BackendSunapiTimelineService::loadPlaybackMonthRecordedDays(this, d_ptr.get(), channelIndex, year, month);
}

