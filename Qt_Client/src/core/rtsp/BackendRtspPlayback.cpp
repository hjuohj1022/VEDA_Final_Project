#include "Backend.h"
#include "internal/rtsp/BackendRtspPlaybackService.h"

// 재생 RTSP 준비 함수
void Backend::preparePlaybackRtsp(int channelIndex, const QString &dateText, const QString &timeText)
{
    BackendRtspPlaybackService::preparePlaybackRtsp(this, d_ptr.get(), channelIndex, dateText, timeText);
}

