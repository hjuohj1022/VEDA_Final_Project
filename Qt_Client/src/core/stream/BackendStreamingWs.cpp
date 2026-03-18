#include "Backend.h"
#include "internal/stream/BackendStreamingWsService.h"

void Backend::streamingWsConnect()
{
    BackendStreamingWsService::streamingWsConnect(this, d_ptr.get());
}

void Backend::streamingWsDisconnect()
{
    BackendStreamingWsService::streamingWsDisconnect(this, d_ptr.get());
}

bool Backend::streamingWsSendHex(QString hexPayload)
{
    return BackendStreamingWsService::streamingWsSendHex(this, d_ptr.get(), hexPayload);
}

bool Backend::playbackWsPause()
{
    return BackendStreamingWsService::playbackWsPause(this, d_ptr.get());
}

bool Backend::playbackWsPlay()
{
    return BackendStreamingWsService::playbackWsPlay(this, d_ptr.get());
}

