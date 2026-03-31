#include "Backend.h"
#include "internal/stream/BackendStreamingWsService.h"

// 스트리밍 WebSocket 연결 함수
void Backend::streamingWsConnect()
{
    BackendStreamingWsService::streamingWsConnect(this, d_ptr.get());
}

// 스트리밍 WebSocket 연결 해제 함수
void Backend::streamingWsDisconnect()
{
    BackendStreamingWsService::streamingWsDisconnect(this, d_ptr.get());
}

// 스트리밍 WebSocket Hex 전송 함수
bool Backend::streamingWsSendHex(QString hexPayload)
{
    return BackendStreamingWsService::streamingWsSendHex(this, d_ptr.get(), hexPayload);
}

// 재생 WebSocket 일시정지 함수
bool Backend::playbackWsPause()
{
    return BackendStreamingWsService::playbackWsPause(this, d_ptr.get());
}

// 재생 WebSocket 재생 함수
bool Backend::playbackWsPlay()
{
    return BackendStreamingWsService::playbackWsPlay(this, d_ptr.get());
}

