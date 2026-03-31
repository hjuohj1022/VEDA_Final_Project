#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace thermal_dtls_gateway {

// ESP32/Teensy 측 thermal chunk 헤더를 해석한 결과입니다.
struct ThermalPacketHeader {
    std::uint16_t frameId = 0;
    std::uint16_t chunkIndex = 0;
    std::uint16_t totalChunks = 0;
    std::uint16_t minValue = 0;
    std::uint16_t maxValue = 0;
};

// 하나의 thermal frame이 조립되는 동안 중간 상태를 추적합니다.
struct ThermalFrameTracker {
    std::uint16_t totalChunks = 0;
    long long firstSeenAtMs = 0;
    long long lastSeenAtMs = 0;
    std::set<std::uint16_t> uniqueChunks;
};

// 한 경로에서 관찰된 thermal packet/frame 통계를 기록합니다.
struct ThermalPathStats {
    unsigned long long packets = 0;
    unsigned long long bytes = 0;
    unsigned long long invalidPackets = 0;
    unsigned long long duplicateChunks = 0;
    unsigned long long completedFrames = 0;
    unsigned long long incompleteFrames = 0;
    unsigned long long missingChunks = 0;
    unsigned long long evictedFrames = 0;
    std::uint16_t lastFrameId = 0;
    std::uint16_t lastChunkIndex = 0;
    std::uint16_t lastTotalChunks = 0;
    std::size_t lastPacketBytes = 0;
    long long lastPacketAtMs = 0;
    std::size_t maxInFlightFrames = 0;
    std::map<std::uint16_t, ThermalFrameTracker> inFlightFrames;
};

// 복호화 입력과 전달 출력의 전체 통계를 합쳐 관리합니다.
struct GatewayStats {
    ThermalPathStats decrypted;
    ThermalPathStats forwarded;
    unsigned long long forwardFailures = 0;
    unsigned long long forwardFailedBytes = 0;
    long long lastLogAtMs = 0;
};

// 들어온 datagram이 DTLS record인지 thermal chunk인지 분류한 결과입니다.
enum class IncomingDatagramKind {
    DtlsRecord,
    ThermalChunk,
    Unknown,
};

// raw bytes에서 thermal chunk 헤더를 파싱합니다.
bool parseThermalPacketHeader(const unsigned char* data, std::size_t len, ThermalPacketHeader& out);
// 파싱된 헤더가 thermal chunk로 볼 만한 범위인지 확인합니다.
bool isReasonableThermalPacketHeader(const ThermalPacketHeader& header);
// 입력이 DTLS record처럼 보이는지 빠르게 판별합니다.
bool looksLikeDtlsRecord(const unsigned char* data, std::size_t len);
// 입력이 thermal chunk처럼 보이는지 빠르게 판별합니다.
bool looksLikeThermalChunk(const unsigned char* data,
                           std::size_t len,
                           ThermalPacketHeader* headerOut = nullptr);
// 들어온 datagram의 종류를 DTLS/thermal/unknown 중 하나로 분류합니다.
IncomingDatagramKind classifyIncomingDatagram(const unsigned char* data,
                                              std::size_t len,
                                              ThermalPacketHeader* thermalHeaderOut = nullptr);
// 로그에 남길 수 있도록 payload 앞부분을 hex 문자열로 요약합니다.
std::string hexPreview(const unsigned char* data, std::size_t len, std::size_t maxBytes = 16);
// thermal packet 하나를 반영해 프레임 조립 상태와 통계를 갱신합니다.
void updateThermalPathStats(ThermalPathStats& stats,
                            const unsigned char* data,
                            std::size_t len,
                            long long nowMs,
                            int frameTimeoutMs,
                            int maxTrackedFrames);

} // namespace thermal_dtls_gateway
