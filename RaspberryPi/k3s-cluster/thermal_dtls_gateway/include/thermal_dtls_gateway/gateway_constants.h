#pragma once

#include <cstddef>

namespace thermal_dtls_gateway {

// DTLS Gateway가 기본으로 수신하는 UDP 포트입니다.
inline constexpr int kDefaultDtlsPort = 5005;
// Crow Server로 thermal payload를 전달할 때 사용하는 기본 포트입니다.
inline constexpr int kDefaultForwardPort = 5005;
// 한 번에 처리할 thermal packet의 최대 크기입니다.
inline constexpr std::size_t kMaxThermalPacketBytes = 4096;
// thermal chunk 헤더의 고정 바이트 수입니다.
inline constexpr std::size_t kThermalHeaderBytes = 10;
// DTLS record 헤더를 판별할 때 필요한 최소 바이트 수입니다.
inline constexpr std::size_t kDtlsRecordHeaderBytes = 13;
// 보안상 허용하는 최소 PSK 길이입니다.
inline constexpr int kMinPskBytes = 16;
// DTLS handshake 단계의 소켓 타임아웃입니다.
inline constexpr int kHandshakeTimeoutSeconds = 15;
// 세션이 유휴 상태일 때 worker가 종료되기까지의 타임아웃입니다.
inline constexpr int kSessionIdleTimeoutSeconds = 30;
// UDP 소켓에 적용할 기본 송수신 버퍼 크기입니다.
inline constexpr int kDefaultUdpSocketBufferBytes = 2 * 1024 * 1024;
// 통계 로그를 출력하는 기본 주기입니다.
inline constexpr int kDefaultStatsLogIntervalMs = 5000;
// 프레임 추적 정보가 만료되기까지의 기본 시간입니다.
inline constexpr int kDefaultFrameTrackTimeoutMs = 2000;
// 동시에 추적할 최대 프레임 수입니다.
inline constexpr int kDefaultMaxTrackedFrames = 8;

} // namespace thermal_dtls_gateway
