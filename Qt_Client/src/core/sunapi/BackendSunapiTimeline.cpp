#include "Backend.h"
#include "internal/sunapi/BackendSunapiTimelineService.h"

// 재생 타임라인 로드 함수
void Backend::loadPlaybackTimeline(int channelIndex, const QString &dateText)
{
    BackendSunapiTimelineService::loadPlaybackTimeline(this, d_ptr.get(), channelIndex, dateText);
}

