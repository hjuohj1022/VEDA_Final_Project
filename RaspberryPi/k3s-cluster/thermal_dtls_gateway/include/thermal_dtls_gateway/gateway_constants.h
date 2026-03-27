#pragma once

#include <cstddef>

namespace thermal_dtls_gateway {

inline constexpr int kDefaultDtlsPort = 5005;
inline constexpr int kDefaultForwardPort = 5005;
inline constexpr std::size_t kMaxThermalPacketBytes = 4096;
inline constexpr std::size_t kThermalHeaderBytes = 10;
inline constexpr std::size_t kDtlsRecordHeaderBytes = 13;
inline constexpr int kMinPskBytes = 16;
inline constexpr int kHandshakeTimeoutSeconds = 15;
inline constexpr int kSessionIdleTimeoutSeconds = 30;
inline constexpr int kDefaultUdpSocketBufferBytes = 2 * 1024 * 1024;
inline constexpr int kDefaultStatsLogIntervalMs = 5000;
inline constexpr int kDefaultFrameTrackTimeoutMs = 2000;
inline constexpr int kDefaultMaxTrackedFrames = 8;

} // namespace thermal_dtls_gateway
