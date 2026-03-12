#include "Backend.h"
#include "internal/stream/BackendPlaybackWsRuntimeService.h"

QString Backend::ensurePlaybackWsSdpSource()
{
    return BackendPlaybackWsRuntimeService::ensurePlaybackWsSdpSource(this, d_ptr.get());
}

void Backend::parsePlaybackH264ConfigFromRtp(const QByteArray &rtpPacket)
{
    BackendPlaybackWsRuntimeService::parsePlaybackH264ConfigFromRtp(this, d_ptr.get(), rtpPacket);
}

void Backend::forwardPlaybackInterleavedRtp(const QByteArray &bytes)
{
    BackendPlaybackWsRuntimeService::forwardPlaybackInterleavedRtp(this, d_ptr.get(), bytes);
}

