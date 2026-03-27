#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace thermal_dtls_gateway {

struct ThermalPacketHeader {
    std::uint16_t frameId = 0;
    std::uint16_t chunkIndex = 0;
    std::uint16_t totalChunks = 0;
    std::uint16_t minValue = 0;
    std::uint16_t maxValue = 0;
};

struct ThermalFrameTracker {
    std::uint16_t totalChunks = 0;
    long long firstSeenAtMs = 0;
    long long lastSeenAtMs = 0;
    std::set<std::uint16_t> uniqueChunks;
};

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

struct GatewayStats {
    ThermalPathStats decrypted;
    ThermalPathStats forwarded;
    unsigned long long forwardFailures = 0;
    unsigned long long forwardFailedBytes = 0;
    long long lastLogAtMs = 0;
};

enum class IncomingDatagramKind {
    DtlsRecord,
    ThermalChunk,
    Unknown,
};

bool parseThermalPacketHeader(const unsigned char* data, std::size_t len, ThermalPacketHeader& out);
bool isReasonableThermalPacketHeader(const ThermalPacketHeader& header);
bool looksLikeDtlsRecord(const unsigned char* data, std::size_t len);
bool looksLikeThermalChunk(const unsigned char* data,
                           std::size_t len,
                           ThermalPacketHeader* headerOut = nullptr);
IncomingDatagramKind classifyIncomingDatagram(const unsigned char* data,
                                              std::size_t len,
                                              ThermalPacketHeader* thermalHeaderOut = nullptr);
std::string hexPreview(const unsigned char* data, std::size_t len, std::size_t maxBytes = 16);
void updateThermalPathStats(ThermalPathStats& stats,
                            const unsigned char* data,
                            std::size_t len,
                            long long nowMs,
                            int frameTimeoutMs,
                            int maxTrackedFrames);

} // namespace thermal_dtls_gateway
