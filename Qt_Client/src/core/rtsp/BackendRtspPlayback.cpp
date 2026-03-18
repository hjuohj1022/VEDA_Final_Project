#include "Backend.h"
#include "internal/rtsp/BackendRtspPlaybackService.h"

void Backend::preparePlaybackRtsp(int channelIndex, const QString &dateText, const QString &timeText)
{
    BackendRtspPlaybackService::preparePlaybackRtsp(this, d_ptr.get(), channelIndex, dateText, timeText);
}

