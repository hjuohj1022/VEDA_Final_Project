#include "thermal_dtls_gateway/thermal_protocol.h"

#include "thermal_dtls_gateway/gateway_common.h"
#include "thermal_dtls_gateway/gateway_constants.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace thermal_dtls_gateway {
namespace {

// 추적 중인 frame이 끝까지 완성되지 못했을 때 누락 통계를 반영합니다.
void noteIncompleteFrame(ThermalPathStats& stats, const ThermalFrameTracker& tracker)
{
    stats.incompleteFrames += 1;
    if (tracker.totalChunks > tracker.uniqueChunks.size()) {
        stats.missingChunks += static_cast<unsigned long long>(tracker.totalChunks - tracker.uniqueChunks.size());
    }
}

// 일정 시간 이상 갱신되지 않은 frame 추적 정보를 정리합니다.
void pruneExpiredFrames(ThermalPathStats& stats, long long nowMs, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return;
    }

    for (auto it = stats.inFlightFrames.begin(); it != stats.inFlightFrames.end();) {
        if ((nowMs - it->second.lastSeenAtMs) <= timeoutMs) {
            ++it;
            continue;
        }

        noteIncompleteFrame(stats, it->second);
        it = stats.inFlightFrames.erase(it);
    }
}

// 동시에 추적하는 frame 수가 너무 많아지지 않도록 오래된 항목을 제거합니다.
void trimTrackedFrames(ThermalPathStats& stats, int maxTrackedFrames)
{
    if (maxTrackedFrames <= 0) {
        return;
    }

    while (static_cast<int>(stats.inFlightFrames.size()) > maxTrackedFrames) {
        const auto oldest = std::min_element(stats.inFlightFrames.begin(),
                                             stats.inFlightFrames.end(),
                                             [](const auto& lhs, const auto& rhs) {
                                                 return lhs.second.lastSeenAtMs < rhs.second.lastSeenAtMs;
                                             });
        if (oldest == stats.inFlightFrames.end()) {
            return;
        }

        noteIncompleteFrame(stats, oldest->second);
        stats.evictedFrames += 1;
        stats.inFlightFrames.erase(oldest);
    }
}

} // namespace

// thermal chunk 헤더를 raw payload에서 추출합니다.
bool parseThermalPacketHeader(const unsigned char* data, std::size_t len, ThermalPacketHeader& out)
{
    if (data == nullptr || len < kThermalHeaderBytes) {
        return false;
    }

    out.frameId = readBe16(data + 0);
    out.chunkIndex = readBe16(data + 2);
    out.totalChunks = readBe16(data + 4);
    out.minValue = readBe16(data + 6);
    out.maxValue = readBe16(data + 8);
    return true;
}

// 헤더 값이 정상적인 thermal chunk 범위인지 확인합니다.
bool isReasonableThermalPacketHeader(const ThermalPacketHeader& header)
{
    return header.totalChunks > 0
           && header.totalChunks <= 100
           && header.chunkIndex < header.totalChunks;
}

// DTLS record 헤더 패턴인지 빠르게 식별합니다.
bool looksLikeDtlsRecord(const unsigned char* data, std::size_t len)
{
    if (data == nullptr || len < kDtlsRecordHeaderBytes) {
        return false;
    }

    const unsigned char contentType = data[0];
    const bool validContentType =
        contentType == 20
        || contentType == 21
        || contentType == 22
        || contentType == 23
        || contentType == 24;
    if (!validContentType) {
        return false;
    }

    const bool validVersion =
        (data[1] == 0xFE)
        && (data[2] == 0xFD || data[2] == 0xFF);
    if (!validVersion) {
        return false;
    }

    const std::size_t recordLen = (static_cast<std::size_t>(data[11]) << 8U)
                                  | static_cast<std::size_t>(data[12]);
    return recordLen > 0;
}

// thermal chunk 헤더가 파싱되고 값도 타당한지 확인합니다.
bool looksLikeThermalChunk(const unsigned char* data,
                           std::size_t len,
                           ThermalPacketHeader* headerOut)
{
    ThermalPacketHeader header{};
    if (!parseThermalPacketHeader(data, len, header) || !isReasonableThermalPacketHeader(header)) {
        return false;
    }

    if (headerOut != nullptr) {
        *headerOut = header;
    }
    return true;
}

// 들어온 datagram을 DTLS record, thermal chunk, unknown 중 하나로 분류합니다.
IncomingDatagramKind classifyIncomingDatagram(const unsigned char* data,
                                              std::size_t len,
                                              ThermalPacketHeader* thermalHeaderOut)
{
    if (looksLikeDtlsRecord(data, len)) {
        return IncomingDatagramKind::DtlsRecord;
    }
    if (looksLikeThermalChunk(data, len, thermalHeaderOut)) {
        return IncomingDatagramKind::ThermalChunk;
    }
    return IncomingDatagramKind::Unknown;
}

// 디버그 로그에 쓰기 위한 hex preview 문자열을 만듭니다.
std::string hexPreview(const unsigned char* data, std::size_t len, std::size_t maxBytes)
{
    if (data == nullptr || len == 0) {
        return "(empty)";
    }

    std::ostringstream oss;
    const std::size_t previewLen = std::min(len, maxBytes);
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < previewLen; ++i) {
        if (i != 0) {
            oss << ' ';
        }
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    if (len > previewLen) {
        oss << " ...";
    }
    return oss.str();
}

// thermal packet 하나를 반영해 frame 조립 상태와 누적 통계를 갱신합니다.
void updateThermalPathStats(ThermalPathStats& stats,
                            const unsigned char* data,
                            std::size_t len,
                            long long nowMs,
                            int frameTimeoutMs,
                            int maxTrackedFrames)
{
    stats.packets += 1;
    stats.bytes += static_cast<unsigned long long>(len);
    stats.lastPacketBytes = len;
    stats.lastPacketAtMs = nowMs;

    ThermalPacketHeader header{};
    if (!parseThermalPacketHeader(data, len, header)) {
        stats.invalidPackets += 1;
        return;
    }

    stats.lastFrameId = header.frameId;
    stats.lastChunkIndex = header.chunkIndex;
    stats.lastTotalChunks = header.totalChunks;

    // 오래 방치된 frame은 먼저 정리한 뒤 현재 packet을 반영합니다.
    pruneExpiredFrames(stats, nowMs, frameTimeoutMs);

    ThermalFrameTracker& tracker = stats.inFlightFrames[header.frameId];
    if (tracker.firstSeenAtMs == 0) {
        tracker.firstSeenAtMs = nowMs;
        tracker.totalChunks = header.totalChunks;
    }
    tracker.lastSeenAtMs = nowMs;
    if (header.totalChunks > tracker.totalChunks) {
        tracker.totalChunks = header.totalChunks;
    }

    const std::uint16_t minimumChunks = static_cast<std::uint16_t>(header.chunkIndex + 1);
    if (minimumChunks > tracker.totalChunks) {
        tracker.totalChunks = minimumChunks;
    }

    if (!tracker.uniqueChunks.insert(header.chunkIndex).second) {
        stats.duplicateChunks += 1;
    }

    // 마지막 chunk까지 모두 모였으면 frame 완성으로 기록하고 추적 대상에서 제거합니다.
    if (tracker.totalChunks > 0 && tracker.uniqueChunks.size() >= tracker.totalChunks) {
        stats.completedFrames += 1;
        stats.inFlightFrames.erase(header.frameId);
    }

    // 추적 대상이 너무 많아지지 않도록 상한을 유지합니다.
    trimTrackedFrames(stats, maxTrackedFrames);
    stats.maxInFlightFrames = std::max(stats.maxInFlightFrames, stats.inFlightFrames.size());
}

} // namespace thermal_dtls_gateway
