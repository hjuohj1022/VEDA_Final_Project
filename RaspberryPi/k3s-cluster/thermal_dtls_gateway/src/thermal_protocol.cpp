#include "thermal_dtls_gateway/thermal_protocol.h"

#include "thermal_dtls_gateway/gateway_common.h"
#include "thermal_dtls_gateway/gateway_constants.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace thermal_dtls_gateway {
namespace {

void noteIncompleteFrame(ThermalPathStats& stats, const ThermalFrameTracker& tracker)
{
    stats.incompleteFrames += 1;
    if (tracker.totalChunks > tracker.uniqueChunks.size()) {
        stats.missingChunks += static_cast<unsigned long long>(tracker.totalChunks - tracker.uniqueChunks.size());
    }
}

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

bool isReasonableThermalPacketHeader(const ThermalPacketHeader& header)
{
    return header.totalChunks > 0
           && header.totalChunks <= 100
           && header.chunkIndex < header.totalChunks;
}

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

    if (tracker.totalChunks > 0 && tracker.uniqueChunks.size() >= tracker.totalChunks) {
        stats.completedFrames += 1;
        stats.inFlightFrames.erase(header.frameId);
    }

    trimTrackedFrames(stats, maxTrackedFrames);
    stats.maxInFlightFrames = std::max(stats.maxInFlightFrames, stats.inFlightFrames.size());
}

} // namespace thermal_dtls_gateway
