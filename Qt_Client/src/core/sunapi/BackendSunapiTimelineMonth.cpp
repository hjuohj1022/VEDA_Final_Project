#include "Backend.h"
#include "internal/sunapi/BackendSunapiTimelineService.h"

// 재생 월간 녹화 일자 로드 함수
void Backend::loadPlaybackMonthRecordedDays(int channelIndex, int year, int month)
{
    BackendSunapiTimelineService::loadPlaybackMonthRecordedDays(this, d_ptr.get(), channelIndex, year, month);
}

