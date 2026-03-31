#include "Backend.h"
#include "internal/stream/BackendPlaybackWsRuntimeService.h"

// 재생 WebSocket Sdp 소스 확인 함수
QString Backend::ensurePlaybackWsSdpSource()
{
    return BackendPlaybackWsRuntimeService::ensurePlaybackWsSdpSource(this, d_ptr.get());
}

// 재생 H264 설정 From RTP 파싱 함수
void Backend::parsePlaybackH264ConfigFromRtp(const QByteArray &rtpPacket)
{
    BackendPlaybackWsRuntimeService::parsePlaybackH264ConfigFromRtp(this, d_ptr.get(), rtpPacket);
}

// forward 재생 Interleaved RTP 처리 함수
void Backend::forwardPlaybackInterleavedRtp(const QByteArray &bytes)
{
    BackendPlaybackWsRuntimeService::forwardPlaybackInterleavedRtp(this, d_ptr.get(), bytes);
}

