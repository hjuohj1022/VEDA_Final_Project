#include "../include/ThermalProxy.h"
#include "../include/MqttManager.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

// main.cpp exposes JWT verification as a process-wide helper.
bool verifyJWT(const std::string& token);

namespace {
constexpr uint16_t kDefaultThermalUdpPort = 5005;
constexpr size_t kThermalHeaderBytes = 10;
constexpr size_t kThermalHeaderWithRangeBytes = 8;
constexpr size_t kThermalHeaderLegacyBytes = 4;
constexpr uint16_t kThermalChunkLimit = 100;
constexpr int kThermalWidth = 160;
constexpr int kThermalHeight = 120;
constexpr int kThermalPixelCount = kThermalWidth * kThermalHeight;
constexpr int kThermalFrameBytes8 = kThermalPixelCount;
constexpr int kThermalFrameBytes16 = kThermalPixelCount * 2;
constexpr uint16_t kThermalValidMinRaw = 1000;
constexpr uint16_t kThermalValidMaxRaw = 30000;
constexpr size_t kMaxUdpPacketBytes = 64 * 1024;
constexpr int kReceiveTimeoutUs = 250000;
constexpr int kDefaultUdpSocketBufferBytes = 2 * 1024 * 1024;
constexpr int kDefaultStatsLogIntervalMs = 5000;
constexpr int kDefaultFrameTrackTimeoutMs = 2000;
constexpr int kDefaultMaxTrackedFrames = 8;
constexpr int kDefaultThermalEventThresholdMaxValue = 12000;
constexpr int kDefaultThermalEventCooldownMs = 5000;
constexpr int kDefaultThermalEventBaselineMargin = 1200;
constexpr int kDefaultThermalEventBaselineWindowMs = 300000;
constexpr int kDefaultThermalEventBaselineMinSamples = 30;
constexpr int kDefaultThermalEventBaselineGuardDelta = 500;
constexpr int kDefaultThermalEventConsecutiveFrames = 3;
constexpr int kDefaultThermalEventSignalPercentile = 99;
constexpr int kDefaultThermalEventHotAreaMinPixels = 12;

struct ThermalPacketHeader {
    uint16_t frameId = 0;
    uint16_t chunkIndex = 0;
    uint16_t totalChunks = 0;
    uint16_t minValue = 0;
    uint16_t maxValue = 0;
};

struct ThermalChunkHeader {
    uint16_t frameId = 0;
    uint16_t chunkIndex = 0;
    uint16_t totalChunks = 0;
    uint16_t minValue = 0;
    uint16_t maxValue = 0;
    size_t headerBytes = 0;
    bool hasFrameId = false;
    bool hasRangeData = false;
};

struct ThermalNormalizedPacket {
    ThermalPacketHeader header;
    bool hasRangeData = false;
    std::string wsPayload;
};

struct ThermalLegacyFrameState {
    uint16_t frameId = 0;
    uint16_t totalChunks = 0;
    long long lastSeenAtMs = 0;
    bool active = false;
    std::set<uint16_t> receivedChunks;
};

struct ThermalNormalizerState {
    std::map<std::string, ThermalLegacyFrameState> legacyFramesBySender;
    uint16_t nextSyntheticFrameId = 1;
};

struct ThermalFrameTracker {
    uint16_t totalChunks = 0;
    long long firstSeenAtMs = 0;
    long long lastSeenAtMs = 0;
    size_t payloadBytes = 0;
    uint16_t headerMinValue = 0;
    uint16_t headerMaxValue = 0;
    uint16_t computedMinValue = std::numeric_limits<uint16_t>::max();
    uint16_t computedMaxValue = 0;
    bool headerHasRangeData = false;
    bool computedRawRangeReady = false;
    std::map<uint16_t, std::string> chunkPayloads;
    std::set<uint16_t> uniqueChunks;
};

struct ThermalCompletedFrame {
    ThermalPacketHeader header;
    size_t payloadBytes = 0;
    bool hasRangeData = false;
    bool ready = false;
    std::string encoding = "unknown";
    std::string framePayload;
};

struct ThermalEventRoi {
    int x = 0;
    int y = 0;
    int width = kThermalWidth;
    int height = kThermalHeight;
};

struct ThermalEventFrameAnalysis {
    bool valid = false;
    int percentile = kDefaultThermalEventSignalPercentile;
    ThermalEventRoi roi;
    int validPixels = 0;
    int roiMinValue = 0;
    int roiMaxValue = 0;
    int signalValue = 0;
};

struct ThermalProxyStats {
    bool udp_bound = false;
    bool receiver_running = false;
    std::string udp_bind_host = "0.0.0.0";
    uint16_t udp_port = kDefaultThermalUdpPort;
    int udp_socket_rcvbuf_bytes = 0;
    int ws_clients = 0;
    uint16_t last_frame_id = 0;
    uint16_t last_chunk_index = 0;
    uint16_t last_total_chunks = 0;
    uint16_t last_min_val = 0;
    uint16_t last_max_val = 0;
    std::string last_sender;
    size_t last_packet_bytes = 0;
    long long last_packet_at_ms = 0;
    unsigned long long packets_received = 0;
    unsigned long long bytes_received = 0;
    unsigned long long invalid_packets = 0;
    unsigned long long duplicate_chunks = 0;
    unsigned long long completed_frames = 0;
    unsigned long long incomplete_frames = 0;
    unsigned long long missing_chunks = 0;
    unsigned long long evicted_frames = 0;
    size_t in_flight_frames = 0;
    size_t max_in_flight_frames = 0;
    size_t last_frame_payload_bytes = 0;
    std::string last_frame_encoding = "unknown";
    std::string last_error;
};

struct ThermalEventStats {
    bool monitor_always_on = true;
    bool event_enabled = true;
    bool actuation_enabled = false;
    bool baseline_enabled = true;
    bool broker_connected = false;
    int hotspot_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int cooldown_ms = kDefaultThermalEventCooldownMs;
    int baseline_margin = kDefaultThermalEventBaselineMargin;
    int baseline_window_ms = kDefaultThermalEventBaselineWindowMs;
    int baseline_min_samples = kDefaultThermalEventBaselineMinSamples;
    int baseline_guard_delta = kDefaultThermalEventBaselineGuardDelta;
    int consecutive_frames_required = kDefaultThermalEventConsecutiveFrames;
    int signal_percentile = kDefaultThermalEventSignalPercentile;
    int hot_area_min_pixels = kDefaultThermalEventHotAreaMinPixels;
    int roi_x = 0;
    int roi_y = 0;
    int roi_width = kThermalWidth;
    int roi_height = kThermalHeight;
    int current_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int current_clear_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int baseline_normal_max_value = 0;
    int baseline_sample_count = 0;
    unsigned long long baseline_updates = 0;
    std::string event_topic = "system/event";
    unsigned long long event_attempts = 0;
    unsigned long long events_published = 0;
    unsigned long long actuation_requests = 0;
    uint16_t last_frame_max_value = 0;
    int last_signal_value = 0;
    int last_hot_area_pixels = 0;
    int last_hot_area_threshold = 0;
    int last_valid_pixels = 0;
    int last_roi_min_value = 0;
    int last_roi_max_value = 0;
    uint16_t last_event_attempt_frame_id = 0;
    long long last_event_attempt_at_ms = 0;
    uint16_t last_event_frame_id = 0;
    uint16_t last_event_max_value = 0;
    int last_event_signal_value = 0;
    int last_event_hot_area_pixels = 0;
    int last_event_threshold_max_value = 0;
    long long last_event_at_ms = 0;
    int consecutive_hits = 0;
    bool baseline_ready = false;
    bool hotspot_active = false;
    bool last_publish_ok = false;
    bool last_actuation_ok = false;
    std::string last_event_message;
};

struct ThermalEventConfig {
    bool monitor_always_on = true;
    bool event_enabled = true;
    bool actuation_enabled = false;
    bool baseline_enabled = true;
    int hotspot_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int cooldown_ms = kDefaultThermalEventCooldownMs;
    int baseline_margin = kDefaultThermalEventBaselineMargin;
    int baseline_window_ms = kDefaultThermalEventBaselineWindowMs;
    int baseline_min_samples = kDefaultThermalEventBaselineMinSamples;
    int baseline_guard_delta = kDefaultThermalEventBaselineGuardDelta;
    int consecutive_frames_required = kDefaultThermalEventConsecutiveFrames;
    int signal_percentile = kDefaultThermalEventSignalPercentile;
    int hot_area_min_pixels = kDefaultThermalEventHotAreaMinPixels;
    int roi_x = 0;
    int roi_y = 0;
    int roi_width = kThermalWidth;
    int roi_height = kThermalHeight;
    std::string event_topic = "system/event";
    std::string source = "thermal";
    std::string severity = "warning";
    std::string title = "Thermal hotspot detected";
    std::string mqtt_host = "mqtt-service";
    int mqtt_port = 1883;
    std::string mqtt_client_id = "crow_thermal_event_api";
    std::string motor_control_topic = "motor/control";
    std::string laser_control_topic = "laser/control";
    int motor1_angle = 90;
    int motor2_angle = 90;
    int motor3_angle = 90;
    bool laser_enabled = false;
};

struct ThermalBaselineSample {
    long long observed_at_ms = 0;
    int signal_value = 0;
};

struct ThermalProxyState {
    std::mutex state_mutex;
    std::mutex receiver_mutex;
    std::set<crow::websocket::connection*> clients;
    std::thread receiver_thread;
    std::atomic<bool> stop_requested{false};
    std::atomic<bool> receiver_running{false};
    std::atomic<bool> receiver_thread_started{false};
    ThermalProxyStats stats;
    ThermalEventStats event_stats;
    std::map<uint16_t, ThermalFrameTracker> in_flight_frames;
    std::deque<ThermalBaselineSample> baseline_normal_samples;
    std::unique_ptr<MqttManager> event_mqtt;
    long long last_stats_log_at_ms = 0;
};

ThermalProxyState g_thermal;

long long currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string envOrDefault(const char* key, const std::string& fallback)
{
    const char* value = std::getenv(key);
    return value ? std::string(value) : fallback;
}

uint16_t envPortOrDefault(const char* key, uint16_t fallback)
{
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }

    try {
        const int parsed = std::stoi(value);
        if (parsed <= 0 || parsed > 65535) {
            return fallback;
        }
        return static_cast<uint16_t>(parsed);
    } catch (const std::exception&) {
        return fallback;
    }
}

int envIntOrDefault(const char* key, int fallback)
{
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }

    try {
        return std::stoi(value);
    } catch (const std::exception&) {
        return fallback;
    }
}

int clampServoAngle(int angle)
{
    return std::max(0, std::min(180, angle));
}

int clampPercentile(int value)
{
    return std::max(50, std::min(100, value));
}

int clampCoordinate(int value, int upperBoundInclusive)
{
    if (upperBoundInclusive <= 0) {
        return 0;
    }
    return std::max(0, std::min(upperBoundInclusive, value));
}

bool envBoolOrDefault(const char* key, bool fallback)
{
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }

    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });

    if (normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

ThermalEventConfig loadThermalEventConfig()
{
    ThermalEventConfig config;
    config.monitor_always_on = envBoolOrDefault("THERMAL_MONITOR_ALWAYS_ON", true);
    config.event_enabled = envBoolOrDefault("THERMAL_EVENT_ENABLED", true);
    config.actuation_enabled = envBoolOrDefault("THERMAL_EVENT_ACTUATE", false);
    config.baseline_enabled = envBoolOrDefault("THERMAL_EVENT_BASELINE_ENABLED", true);
    config.hotspot_threshold_max_value = std::max(
        0, envIntOrDefault("THERMAL_EVENT_THRESHOLD_MAX_VALUE", kDefaultThermalEventThresholdMaxValue));
    config.cooldown_ms = std::max(0, envIntOrDefault("THERMAL_EVENT_COOLDOWN_MS", kDefaultThermalEventCooldownMs));
    config.baseline_margin = std::max(
        0, envIntOrDefault("THERMAL_EVENT_BASELINE_MARGIN", kDefaultThermalEventBaselineMargin));
    config.baseline_window_ms = std::max(
        0, envIntOrDefault("THERMAL_EVENT_BASELINE_WINDOW_MS", kDefaultThermalEventBaselineWindowMs));
    config.baseline_min_samples = std::max(
        1, envIntOrDefault("THERMAL_EVENT_BASELINE_MIN_SAMPLES", kDefaultThermalEventBaselineMinSamples));
    config.baseline_guard_delta = std::max(
        0, envIntOrDefault("THERMAL_EVENT_BASELINE_GUARD_DELTA", kDefaultThermalEventBaselineGuardDelta));
    config.consecutive_frames_required = std::max(
        1, envIntOrDefault("THERMAL_EVENT_CONSECUTIVE_FRAMES", kDefaultThermalEventConsecutiveFrames));
    config.signal_percentile = clampPercentile(
        envIntOrDefault("THERMAL_EVENT_SIGNAL_PERCENTILE", kDefaultThermalEventSignalPercentile));
    config.hot_area_min_pixels = std::max(
        0, envIntOrDefault("THERMAL_EVENT_HOT_AREA_MIN_PIXELS", kDefaultThermalEventHotAreaMinPixels));
    config.roi_x = envIntOrDefault("THERMAL_EVENT_ROI_X", 0);
    config.roi_y = envIntOrDefault("THERMAL_EVENT_ROI_Y", 0);
    config.roi_width = envIntOrDefault("THERMAL_EVENT_ROI_WIDTH", kThermalWidth);
    config.roi_height = envIntOrDefault("THERMAL_EVENT_ROI_HEIGHT", kThermalHeight);
    config.event_topic = envOrDefault("THERMAL_EVENT_TOPIC", "system/event");
    if (config.event_topic.empty()) {
        config.event_topic = "system/event";
    }
    config.source = envOrDefault("THERMAL_EVENT_SOURCE", "thermal");
    config.severity = envOrDefault("THERMAL_EVENT_SEVERITY", "warning");
    config.title = envOrDefault("THERMAL_EVENT_TITLE", "Thermal hotspot detected");
    config.mqtt_host = envOrDefault("MQTT_HOST", "mqtt-service");
    config.mqtt_port = envIntOrDefault("MQTT_PORT", 1883);
    if (config.mqtt_port <= 0) {
        config.mqtt_port = 1883;
    }
    config.mqtt_client_id = envOrDefault("THERMAL_EVENT_MQTT_CLIENT_ID", "crow_thermal_event_api");
    config.motor_control_topic = envOrDefault("MOTOR_CONTROL_TOPIC", "motor/control");
    config.laser_control_topic = envOrDefault("LASER_CONTROL_TOPIC", "laser/control");
    config.motor1_angle = clampServoAngle(envIntOrDefault("THERMAL_EVENT_MOTOR1_ANGLE", 90));
    config.motor2_angle = clampServoAngle(envIntOrDefault("THERMAL_EVENT_MOTOR2_ANGLE", 90));
    config.motor3_angle = clampServoAngle(envIntOrDefault("THERMAL_EVENT_MOTOR3_ANGLE", 90));
    config.laser_enabled = envBoolOrDefault("THERMAL_EVENT_LASER_ENABLED", false);
    return config;
}

uint16_t readBe16(const unsigned char* p)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

void writeBe16(char* p, uint16_t value)
{
    p[0] = static_cast<char>((value >> 8) & 0xFF);
    p[1] = static_cast<char>(value & 0xFF);
}

bool isReasonableThermalChunkHeader(const ThermalChunkHeader& header)
{
    return header.totalChunks > 0
        && header.totalChunks <= kThermalChunkLimit
        && header.chunkIndex < header.totalChunks;
}

bool parseThermalChunkHeader(const std::string& payload, ThermalChunkHeader& out)
{
    if (payload.empty()) {
        return false;
    }

    const unsigned char* p = reinterpret_cast<const unsigned char*>(payload.data());

    if (payload.size() >= kThermalHeaderBytes) {
        ThermalChunkHeader parsed;
        parsed.hasFrameId = true;
        parsed.hasRangeData = true;
        parsed.headerBytes = kThermalHeaderBytes;
        parsed.frameId = readBe16(p + 0);
        parsed.chunkIndex = readBe16(p + 2);
        parsed.totalChunks = readBe16(p + 4);
        parsed.minValue = readBe16(p + 6);
        parsed.maxValue = readBe16(p + 8);
        if (isReasonableThermalChunkHeader(parsed)) {
            out = parsed;
            return true;
        }
    }

    if (payload.size() >= kThermalHeaderWithRangeBytes) {
        ThermalChunkHeader parsed;
        parsed.hasRangeData = true;
        parsed.headerBytes = kThermalHeaderWithRangeBytes;
        parsed.chunkIndex = readBe16(p + 0);
        parsed.totalChunks = readBe16(p + 2);
        parsed.minValue = readBe16(p + 4);
        parsed.maxValue = readBe16(p + 6);
        if (isReasonableThermalChunkHeader(parsed)) {
            out = parsed;
            return true;
        }
    }

    if (payload.size() >= kThermalHeaderLegacyBytes) {
        ThermalChunkHeader parsed;
        parsed.headerBytes = kThermalHeaderLegacyBytes;
        parsed.chunkIndex = readBe16(p + 0);
        parsed.totalChunks = readBe16(p + 2);
        if (isReasonableThermalChunkHeader(parsed)) {
            out = parsed;
            return true;
        }
    }

    return false;
}

uint16_t nextSyntheticThermalFrameId(ThermalNormalizerState& state)
{
    if (state.nextSyntheticFrameId == 0) {
        state.nextSyntheticFrameId = 1;
    }

    const uint16_t frameId = state.nextSyntheticFrameId;
    if (state.nextSyntheticFrameId == std::numeric_limits<uint16_t>::max()) {
        state.nextSyntheticFrameId = 1;
    } else {
        state.nextSyntheticFrameId = static_cast<uint16_t>(state.nextSyntheticFrameId + 1);
    }
    return frameId;
}

void pruneThermalLegacyFrames(ThermalNormalizerState& state, long long nowMs, int timeoutMs)
{
    for (auto it = state.legacyFramesBySender.begin(); it != state.legacyFramesBySender.end();) {
        if (!it->second.active) {
            it = state.legacyFramesBySender.erase(it);
            continue;
        }

        if (timeoutMs > 0 && it->second.lastSeenAtMs > 0 && (nowMs - it->second.lastSeenAtMs) > timeoutMs) {
            it = state.legacyFramesBySender.erase(it);
            continue;
        }

        ++it;
    }
}

uint16_t resolveThermalFrameId(ThermalNormalizerState& state,
                               const std::string& senderKey,
                               const ThermalChunkHeader& header,
                               long long nowMs,
                               int timeoutMs)
{
    if (header.hasFrameId) {
        return header.frameId;
    }

    pruneThermalLegacyFrames(state, nowMs, timeoutMs);

    ThermalLegacyFrameState& legacy = state.legacyFramesBySender[senderKey];
    const bool expired =
        timeoutMs > 0 && legacy.lastSeenAtMs > 0 && (nowMs - legacy.lastSeenAtMs) > timeoutMs;
    const bool completed =
        legacy.totalChunks > 0 && legacy.receivedChunks.size() >= legacy.totalChunks;
    const bool startNewFrame =
        !legacy.active
        || expired
        || completed
        || legacy.totalChunks == 0
        || header.chunkIndex == 0
        || legacy.totalChunks != header.totalChunks;

    if (startNewFrame) {
        legacy.active = true;
        legacy.frameId = nextSyntheticThermalFrameId(state);
        legacy.totalChunks = header.totalChunks;
        legacy.receivedChunks.clear();
    }

    legacy.lastSeenAtMs = nowMs;
    if (header.totalChunks > legacy.totalChunks) {
        legacy.totalChunks = header.totalChunks;
    }
    legacy.receivedChunks.insert(header.chunkIndex);

    const uint16_t frameId = legacy.frameId;
    if (legacy.totalChunks > 0 && legacy.receivedChunks.size() >= legacy.totalChunks) {
        legacy.active = false;
        legacy.totalChunks = 0;
        legacy.receivedChunks.clear();
    }

    return frameId;
}

std::string buildCanonicalThermalPayload(const ThermalPacketHeader& header, const std::string& thermalPayload)
{
    std::string payload;
    payload.resize(kThermalHeaderBytes + thermalPayload.size());

    char* p = payload.data();
    writeBe16(p + 0, header.frameId);
    writeBe16(p + 2, header.chunkIndex);
    writeBe16(p + 4, header.totalChunks);
    writeBe16(p + 6, header.minValue);
    writeBe16(p + 8, header.maxValue);

    if (!thermalPayload.empty()) {
        std::memcpy(p + kThermalHeaderBytes, thermalPayload.data(), thermalPayload.size());
    }

    return payload;
}

void updateThermalTrackerRawRange(ThermalFrameTracker& tracker, const std::string& chunkPayload)
{
    const size_t sampleBytes = chunkPayload.size() - (chunkPayload.size() % 2);
    if (sampleBytes == 0) {
        return;
    }

    const unsigned char* data = reinterpret_cast<const unsigned char*>(chunkPayload.data());
    for (size_t i = 0; i < sampleBytes; i += 2) {
        const uint16_t value = readBe16(data + i);
        if (value <= kThermalValidMinRaw || value >= kThermalValidMaxRaw) {
            continue;
        }

        tracker.computedMinValue = std::min(tracker.computedMinValue, value);
        tracker.computedMaxValue = std::max(tracker.computedMaxValue, value);
        tracker.computedRawRangeReady = true;
    }
}

std::string thermalEncodingFromPayloadBytes(size_t payloadBytes)
{
    if (payloadBytes == static_cast<size_t>(kThermalFrameBytes16)) {
        return "raw16";
    }
    if (payloadBytes == static_cast<size_t>(kThermalFrameBytes8)) {
        return "scaled8";
    }
    return "unknown";
}

void finalizeCompletedThermalFrame(const ThermalPacketHeader& latestHeader,
                                   const ThermalFrameTracker& tracker,
                                   ThermalCompletedFrame& out)
{
    out.ready = false;
    out.header = latestHeader;
    out.payloadBytes = tracker.payloadBytes;
    out.encoding = thermalEncodingFromPayloadBytes(tracker.payloadBytes);
    out.hasRangeData = false;
    out.framePayload.clear();
    out.framePayload.reserve(tracker.payloadBytes);
    for (const auto& entry : tracker.chunkPayloads) {
        out.framePayload.append(entry.second);
    }

    if (out.encoding == "raw16" && tracker.computedRawRangeReady) {
        out.header.minValue = tracker.computedMinValue;
        out.header.maxValue = tracker.computedMaxValue;
        out.hasRangeData = true;
    } else if (out.encoding == "scaled8" && tracker.headerHasRangeData) {
        out.header.minValue = tracker.headerMinValue;
        out.header.maxValue = tracker.headerMaxValue;
        out.hasRangeData = true;
    } else if (tracker.headerHasRangeData) {
        out.header.minValue = tracker.headerMinValue;
        out.header.maxValue = tracker.headerMaxValue;
        out.hasRangeData = true;
    }

    out.ready = true;
}

bool normalizeThermalPayload(const std::string& payload,
                             const std::string& senderKey,
                             long long nowMs,
                             int frameTimeoutMs,
                             ThermalNormalizerState& state,
                             ThermalNormalizedPacket& out)
{
    ThermalChunkHeader chunkHeader{};
    if (!parseThermalChunkHeader(payload, chunkHeader)) {
        return false;
    }

    out.header.frameId = resolveThermalFrameId(state, senderKey, chunkHeader, nowMs, frameTimeoutMs);
    out.header.chunkIndex = chunkHeader.chunkIndex;
    out.header.totalChunks = chunkHeader.totalChunks;
    out.header.minValue = chunkHeader.minValue;
    out.header.maxValue = chunkHeader.maxValue;
    out.hasRangeData = chunkHeader.hasRangeData;

    if (chunkHeader.hasFrameId && chunkHeader.headerBytes == kThermalHeaderBytes) {
        out.wsPayload = payload;
        return true;
    }

    out.wsPayload = buildCanonicalThermalPayload(out.header, payload.substr(chunkHeader.headerBytes));
    return true;
}

bool parseThermalPacketHeader(const std::string& payload, ThermalPacketHeader& out)
{
    ThermalChunkHeader chunkHeader{};
    if (!parseThermalChunkHeader(payload, chunkHeader) || !chunkHeader.hasFrameId) {
        return false;
    }

    out.frameId = chunkHeader.frameId;
    out.chunkIndex = chunkHeader.chunkIndex;
    out.totalChunks = chunkHeader.totalChunks;
    out.minValue = chunkHeader.minValue;
    out.maxValue = chunkHeader.maxValue;
    return true;
}

ThermalEventRoi normalizeThermalEventRoi(const ThermalEventConfig& config)
{
    ThermalEventRoi roi;
    roi.x = clampCoordinate(config.roi_x, kThermalWidth - 1);
    roi.y = clampCoordinate(config.roi_y, kThermalHeight - 1);

    const int remainingWidth = std::max(1, kThermalWidth - roi.x);
    const int remainingHeight = std::max(1, kThermalHeight - roi.y);
    roi.width = config.roi_width <= 0 ? remainingWidth : std::min(config.roi_width, remainingWidth);
    roi.height = config.roi_height <= 0 ? remainingHeight : std::min(config.roi_height, remainingHeight);
    return roi;
}

bool decodeThermalFramePixelRaw(const ThermalCompletedFrame& frame, size_t pixelIndex, uint16_t* out)
{
    if (!out) {
        return false;
    }

    if (frame.encoding == "raw16") {
        const size_t offset = pixelIndex * 2;
        if ((offset + 1) >= frame.framePayload.size()) {
            return false;
        }
        *out = readBe16(reinterpret_cast<const unsigned char*>(frame.framePayload.data() + offset));
        return true;
    }

    if (frame.encoding == "scaled8") {
        if (pixelIndex >= frame.framePayload.size()) {
            return false;
        }

        const uint16_t scaled = static_cast<uint8_t>(frame.framePayload[pixelIndex]);
        const uint32_t range = frame.header.maxValue > frame.header.minValue
            ? static_cast<uint32_t>(frame.header.maxValue - frame.header.minValue)
            : 1U;
        *out = static_cast<uint16_t>(
            frame.header.minValue + static_cast<uint16_t>((scaled * range + 127U) / 255U));
        return true;
    }

    return false;
}

bool analyzeThermalEventFrame(const ThermalCompletedFrame& frame,
                              const ThermalEventConfig& config,
                              ThermalEventFrameAnalysis& out)
{
    out = {};
    out.percentile = config.signal_percentile;
    out.roi = normalizeThermalEventRoi(config);

    if (!frame.ready || !frame.hasRangeData || frame.framePayload.empty()) {
        return false;
    }
    if (frame.encoding != "raw16" && frame.encoding != "scaled8") {
        return false;
    }

    std::vector<uint16_t> roiValues;
    roiValues.reserve(static_cast<size_t>(out.roi.width) * static_cast<size_t>(out.roi.height));

    uint16_t roiMin = std::numeric_limits<uint16_t>::max();
    uint16_t roiMax = 0;
    for (int y = out.roi.y; y < out.roi.y + out.roi.height; ++y) {
        for (int x = out.roi.x; x < out.roi.x + out.roi.width; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y * kThermalWidth + x);
            uint16_t rawValue = 0;
            if (!decodeThermalFramePixelRaw(frame, pixelIndex, &rawValue)) {
                continue;
            }
            if (rawValue <= kThermalValidMinRaw || rawValue >= kThermalValidMaxRaw) {
                continue;
            }

            roiValues.push_back(rawValue);
            roiMin = std::min(roiMin, rawValue);
            roiMax = std::max(roiMax, rawValue);
        }
    }

    if (roiValues.empty()) {
        return false;
    }

    size_t percentileRank = 0;
    if (roiValues.size() > 1) {
        const size_t rankCount =
            (static_cast<size_t>(out.percentile) * roiValues.size() + 99U) / 100U;
        percentileRank = rankCount > 0 ? std::min(roiValues.size() - 1, rankCount - 1) : 0;
    }
    std::nth_element(roiValues.begin(), roiValues.begin() + percentileRank, roiValues.end());

    out.valid = true;
    out.validPixels = static_cast<int>(roiValues.size());
    out.roiMinValue = static_cast<int>(roiMin);
    out.roiMaxValue = static_cast<int>(roiMax);
    out.signalValue = static_cast<int>(roiValues[percentileRank]);
    return true;
}

int countThermalPixelsAtOrAboveThreshold(const ThermalCompletedFrame& frame,
                                         const ThermalEventFrameAnalysis& analysis,
                                         int threshold)
{
    if (!analysis.valid || threshold <= 0) {
        return 0;
    }

    int count = 0;
    for (int y = analysis.roi.y; y < analysis.roi.y + analysis.roi.height; ++y) {
        for (int x = analysis.roi.x; x < analysis.roi.x + analysis.roi.width; ++x) {
            const size_t pixelIndex = static_cast<size_t>(y * kThermalWidth + x);
            uint16_t rawValue = 0;
            if (!decodeThermalFramePixelRaw(frame, pixelIndex, &rawValue)) {
                continue;
            }
            if (rawValue <= kThermalValidMinRaw || rawValue >= kThermalValidMaxRaw) {
                continue;
            }
            if (static_cast<int>(rawValue) >= threshold) {
                count += 1;
            }
        }
    }
    return count;
}

std::string senderToString(const sockaddr_in& sender)
{
    char host[INET_ADDRSTRLEN] = {0};
    if (::inet_ntop(AF_INET, &sender.sin_addr, host, sizeof(host)) == nullptr) {
        return "unknown";
    }

    return std::string(host) + ":" + std::to_string(ntohs(sender.sin_port));
}

void configureReceiveBuffer(int fd, int bytes)
{
    if (bytes <= 0) {
        return;
    }

    if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &bytes, sizeof(bytes)) != 0) {
        std::cerr << "[THERMAL] setsockopt(SO_RCVBUF) failed: " << std::strerror(errno) << std::endl;
    }
}

int querySocketBufferBytes(int fd, int optName)
{
    int value = 0;
    socklen_t value_len = sizeof(value);
    if (::getsockopt(fd, SOL_SOCKET, optName, &value, &value_len) != 0) {
        return -1;
    }
    return value;
}

std::string extractBearerToken(const crow::request& req)
{
    const std::string auth_header = req.get_header_value("Authorization");
    if (auth_header.size() <= 7 || auth_header.rfind("Bearer ", 0) != 0) {
        return {};
    }
    return auth_header.substr(7);
}

bool isAuthorized(const crow::request& req)
{
    const std::string token = extractBearerToken(req);
    return !token.empty() && verifyJWT(token);
}

void refreshThermalEventStatusLocked(const ThermalEventConfig& config)
{
    g_thermal.event_stats.monitor_always_on = config.monitor_always_on;
    g_thermal.event_stats.event_enabled = config.event_enabled;
    g_thermal.event_stats.actuation_enabled = config.actuation_enabled;
    g_thermal.event_stats.baseline_enabled = config.baseline_enabled;
    g_thermal.event_stats.hotspot_threshold_max_value = config.hotspot_threshold_max_value;
    g_thermal.event_stats.cooldown_ms = config.cooldown_ms;
    g_thermal.event_stats.baseline_margin = config.baseline_margin;
    g_thermal.event_stats.baseline_window_ms = config.baseline_window_ms;
    g_thermal.event_stats.baseline_min_samples = config.baseline_min_samples;
    g_thermal.event_stats.baseline_guard_delta = config.baseline_guard_delta;
    g_thermal.event_stats.consecutive_frames_required = config.consecutive_frames_required;
    g_thermal.event_stats.signal_percentile = config.signal_percentile;
    g_thermal.event_stats.hot_area_min_pixels = config.hot_area_min_pixels;
    const ThermalEventRoi roi = normalizeThermalEventRoi(config);
    g_thermal.event_stats.roi_x = roi.x;
    g_thermal.event_stats.roi_y = roi.y;
    g_thermal.event_stats.roi_width = roi.width;
    g_thermal.event_stats.roi_height = roi.height;
    g_thermal.event_stats.event_topic = config.event_topic;
    g_thermal.event_stats.broker_connected = g_thermal.event_mqtt ? g_thermal.event_mqtt->isConnected() : false;
}

void pruneThermalBaselineSamplesLocked(long long now_ms, int window_ms)
{
    if (window_ms <= 0) {
        g_thermal.baseline_normal_samples.clear();
        return;
    }

    while (!g_thermal.baseline_normal_samples.empty()) {
        const long long age_ms = now_ms - g_thermal.baseline_normal_samples.front().observed_at_ms;
        if (age_ms <= window_ms) {
            break;
        }
        g_thermal.baseline_normal_samples.pop_front();
    }
}

int computeThermalBaselineNormalMaxLocked()
{
    int baseline_max = 0;
    for (const auto& sample : g_thermal.baseline_normal_samples) {
        baseline_max = std::max(baseline_max, sample.signal_value);
    }
    return baseline_max;
}

int computeThermalAdaptiveThresholdLocked(const ThermalEventConfig& config, int baseline_normal_max)
{
    if (!config.baseline_enabled) {
        return config.hotspot_threshold_max_value;
    }
    if (static_cast<int>(g_thermal.baseline_normal_samples.size()) < config.baseline_min_samples) {
        return config.hotspot_threshold_max_value;
    }
    return std::max(config.hotspot_threshold_max_value, baseline_normal_max + config.baseline_margin);
}

int computeThermalClearThreshold(const ThermalEventConfig& config, int active_threshold)
{
    return std::max(config.hotspot_threshold_max_value, active_threshold - config.baseline_guard_delta);
}

void updateThermalBaselineStatsLocked(const ThermalEventConfig& config, int baseline_normal_max, int active_threshold)
{
    g_thermal.event_stats.baseline_sample_count = static_cast<int>(g_thermal.baseline_normal_samples.size());
    g_thermal.event_stats.baseline_normal_max_value = baseline_normal_max;
    g_thermal.event_stats.baseline_ready =
        config.baseline_enabled
        && g_thermal.event_stats.baseline_sample_count >= config.baseline_min_samples;
    g_thermal.event_stats.current_threshold_max_value = active_threshold;
    g_thermal.event_stats.current_clear_threshold_max_value = computeThermalClearThreshold(config, active_threshold);
}

MqttManager* ensureThermalEventMqtt(const ThermalEventConfig& config)
{
    if (!config.event_enabled && !config.actuation_enabled) {
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    if (!g_thermal.event_mqtt) {
        g_thermal.event_mqtt = std::make_unique<MqttManager>(
            config.mqtt_client_id.c_str(), config.mqtt_host.c_str(), config.mqtt_port);
    }
    g_thermal.event_stats.broker_connected = g_thermal.event_mqtt->isConnected();
    return g_thermal.event_mqtt.get();
}

std::string makeThermalEventPayload(const ThermalEventConfig& config,
                                    const ThermalPacketHeader& header,
                                    int active_threshold,
                                    int baseline_normal_max,
                                    int consecutive_hits,
                                    const ThermalEventFrameAnalysis& analysis,
                                    int hot_area_pixels,
                                    int hot_area_threshold)
{
    crow::json::wvalue payload;
    payload["source"] = config.source;
    payload["severity"] = config.severity;
    payload["title"] = config.title;
    payload["message"] = "Thermal frame " + std::to_string(header.frameId)
        + " reached signal=" + std::to_string(analysis.signalValue)
        + " hotArea=" + std::to_string(hot_area_pixels)
        + " max=" + std::to_string(header.maxValue)
        + " (threshold=" + std::to_string(active_threshold) + ")";
    payload["autoControl"] = false;
    payload["serverAutoControl"] = config.actuation_enabled;
    payload["thermal"]["frameId"] = static_cast<int>(header.frameId);
    payload["thermal"]["maxValue"] = static_cast<int>(header.maxValue);
    payload["thermal"]["minValue"] = static_cast<int>(header.minValue);
    payload["thermal"]["totalChunks"] = static_cast<int>(header.totalChunks);
    payload["thermal"]["baselineNormalMax"] = baseline_normal_max;
    payload["thermal"]["activeThreshold"] = active_threshold;
    payload["thermal"]["consecutiveHits"] = consecutive_hits;
    payload["thermal"]["analysisReady"] = analysis.valid;
    payload["thermal"]["signalPercentile"] = analysis.percentile;
    payload["thermal"]["signalValue"] = analysis.signalValue;
    payload["thermal"]["hotAreaPixels"] = hot_area_pixels;
    payload["thermal"]["hotAreaThreshold"] = hot_area_threshold;
    payload["thermal"]["validPixels"] = analysis.validPixels;
    payload["thermal"]["roiMinValue"] = analysis.roiMinValue;
    payload["thermal"]["roiMaxValue"] = analysis.roiMaxValue;
    payload["thermal"]["roi"]["x"] = analysis.roi.x;
    payload["thermal"]["roi"]["y"] = analysis.roi.y;
    payload["thermal"]["roi"]["width"] = analysis.roi.width;
    payload["thermal"]["roi"]["height"] = analysis.roi.height;
    if (config.actuation_enabled) {
        payload["control"]["motor1Angle"] = config.motor1_angle;
        payload["control"]["motor2Angle"] = config.motor2_angle;
        payload["control"]["motor3Angle"] = config.motor3_angle;
        payload["control"]["laserEnabled"] = config.laser_enabled;
    }
    return payload.dump();
}

bool publishThermalActuation(MqttManager* mqtt, const ThermalEventConfig& config)
{
    if (!mqtt || !config.actuation_enabled) {
        return true;
    }

    const bool motor1_ok = mqtt->publishMessage(config.motor_control_topic, "motor1 set " + std::to_string(config.motor1_angle));
    const bool motor2_ok = mqtt->publishMessage(config.motor_control_topic, "motor2 set " + std::to_string(config.motor2_angle));
    const bool motor3_ok = mqtt->publishMessage(config.motor_control_topic, "motor3 set " + std::to_string(config.motor3_angle));
    const bool laser_ok = mqtt->publishMessage(config.laser_control_topic, config.laser_enabled ? "laser on" : "laser off");
    return motor1_ok && motor2_ok && motor3_ok && laser_ok;
}

void maybePublishThermalEvent(const ThermalCompletedFrame& frame)
{
    if (!frame.ready) {
        return;
    }

    const ThermalPacketHeader& header = frame.header;
    const ThermalEventConfig config = loadThermalEventConfig();
    const long long now_ms = currentTimeMs();
    ThermalEventFrameAnalysis analysis{};
    const bool analysis_ready = analyzeThermalEventFrame(frame, config, analysis);
    const int observed_signal_value = analysis_ready ? analysis.signalValue : static_cast<int>(header.maxValue);
    int baseline_normal_max = 0;
    int active_threshold = config.hotspot_threshold_max_value;
    int hot_area_threshold = config.hotspot_threshold_max_value;
    int hot_area_pixels = 0;
    int consecutive_hits = 0;
    bool should_dispatch = false;

    if (!config.event_enabled && !config.actuation_enabled) {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked(config);
        if (frame.hasRangeData) {
            g_thermal.event_stats.last_frame_max_value = header.maxValue;
            g_thermal.event_stats.last_signal_value = observed_signal_value;
            g_thermal.event_stats.last_hot_area_pixels = 0;
            g_thermal.event_stats.last_hot_area_threshold = hot_area_threshold;
            g_thermal.event_stats.last_valid_pixels = analysis_ready ? analysis.validPixels : 0;
            g_thermal.event_stats.last_roi_min_value = analysis_ready ? analysis.roiMinValue : 0;
            g_thermal.event_stats.last_roi_max_value = analysis_ready ? analysis.roiMaxValue : 0;
            g_thermal.event_stats.consecutive_hits = 0;
            g_thermal.event_stats.hotspot_active = false;
            pruneThermalBaselineSamplesLocked(now_ms, config.baseline_window_ms);
            baseline_normal_max = computeThermalBaselineNormalMaxLocked();
            updateThermalBaselineStatsLocked(config, baseline_normal_max, config.hotspot_threshold_max_value);
        }
        return;
    }

    if (!frame.hasRangeData) {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked(config);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked(config);

        g_thermal.event_stats.last_frame_max_value = header.maxValue;
        g_thermal.event_stats.last_signal_value = observed_signal_value;
        g_thermal.event_stats.last_valid_pixels = analysis_ready ? analysis.validPixels : 0;
        g_thermal.event_stats.last_roi_min_value = analysis_ready ? analysis.roiMinValue : 0;
        g_thermal.event_stats.last_roi_max_value = analysis_ready ? analysis.roiMaxValue : 0;

        pruneThermalBaselineSamplesLocked(now_ms, config.baseline_window_ms);
        baseline_normal_max = computeThermalBaselineNormalMaxLocked();
        active_threshold = computeThermalAdaptiveThresholdLocked(config, baseline_normal_max);
        hot_area_threshold = active_threshold;
        hot_area_pixels = analysis_ready
            ? countThermalPixelsAtOrAboveThreshold(frame, analysis, hot_area_threshold)
            : 0;
        const int clear_threshold = computeThermalClearThreshold(config, active_threshold);
        const bool area_below_threshold =
            !analysis_ready || config.hot_area_min_pixels <= 0 || hot_area_pixels < config.hot_area_min_pixels;
        const bool can_learn_baseline =
            config.baseline_enabled
            && !g_thermal.event_stats.hotspot_active
            && observed_signal_value < clear_threshold
            && area_below_threshold;

        if (can_learn_baseline) {
            g_thermal.baseline_normal_samples.push_back({now_ms, observed_signal_value});
            pruneThermalBaselineSamplesLocked(now_ms, config.baseline_window_ms);
            baseline_normal_max = computeThermalBaselineNormalMaxLocked();
            active_threshold = computeThermalAdaptiveThresholdLocked(config, baseline_normal_max);
            hot_area_threshold = active_threshold;
            hot_area_pixels = analysis_ready
                ? countThermalPixelsAtOrAboveThreshold(frame, analysis, hot_area_threshold)
                : 0;
        }

        updateThermalBaselineStatsLocked(config, baseline_normal_max, active_threshold);
        g_thermal.event_stats.last_hot_area_pixels = hot_area_pixels;
        g_thermal.event_stats.last_hot_area_threshold = hot_area_threshold;
        if (can_learn_baseline) {
            g_thermal.event_stats.baseline_updates += 1;
        }

        const int updated_clear_threshold = g_thermal.event_stats.current_clear_threshold_max_value;
        const bool signal_hit = observed_signal_value >= active_threshold;
        const bool area_hit =
            !analysis_ready || config.hot_area_min_pixels <= 0 || hot_area_pixels >= config.hot_area_min_pixels;
        const bool hit = signal_hit && area_hit;
        if (hit) {
            g_thermal.event_stats.consecutive_hits += 1;
        } else {
            g_thermal.event_stats.consecutive_hits = 0;
            if (observed_signal_value < updated_clear_threshold) {
                g_thermal.event_stats.hotspot_active = false;
            }
        }
        consecutive_hits = g_thermal.event_stats.consecutive_hits;

        if (!g_thermal.event_stats.hotspot_active
            && hit
            && consecutive_hits >= config.consecutive_frames_required
            && g_thermal.event_stats.last_event_attempt_frame_id != header.frameId
            && (config.cooldown_ms <= 0
                || g_thermal.event_stats.last_event_attempt_at_ms == 0
                || (now_ms - g_thermal.event_stats.last_event_attempt_at_ms) >= config.cooldown_ms)) {
            should_dispatch = true;
            g_thermal.event_stats.event_attempts += 1;
            g_thermal.event_stats.last_event_attempt_frame_id = header.frameId;
            g_thermal.event_stats.last_event_attempt_at_ms = now_ms;
        }
    }

    if (!should_dispatch) {
        return;
    }

    MqttManager* mqtt = ensureThermalEventMqtt(config);
    ThermalEventFrameAnalysis payload_analysis = analysis;
    if (!analysis_ready) {
        payload_analysis.percentile = config.signal_percentile;
        payload_analysis.roi = normalizeThermalEventRoi(config);
        payload_analysis.signalValue = observed_signal_value;
    }
    const std::string payload = makeThermalEventPayload(config,
                                                        header,
                                                        active_threshold,
                                                        baseline_normal_max,
                                                        consecutive_hits,
                                                        payload_analysis,
                                                        hot_area_pixels,
                                                        hot_area_threshold);
    const bool publish_ok = config.event_enabled && mqtt && mqtt->publishMessage(config.event_topic, payload);
    const bool actuation_ok = config.actuation_enabled && mqtt && publishThermalActuation(mqtt, config);
    const bool mark_hotspot_active =
        (config.event_enabled && publish_ok) || (config.actuation_enabled && actuation_ok);

    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        g_thermal.event_stats.broker_connected = mqtt ? mqtt->isConnected() : false;
        g_thermal.event_stats.last_publish_ok = config.event_enabled ? publish_ok : false;
        g_thermal.event_stats.last_actuation_ok = config.actuation_enabled ? actuation_ok : false;
        g_thermal.event_stats.last_event_message = payload;
        if (publish_ok) {
            g_thermal.event_stats.events_published += 1;
            g_thermal.event_stats.last_event_frame_id = header.frameId;
            g_thermal.event_stats.last_event_max_value = header.maxValue;
            g_thermal.event_stats.last_event_signal_value = observed_signal_value;
            g_thermal.event_stats.last_event_hot_area_pixels = hot_area_pixels;
            g_thermal.event_stats.last_event_threshold_max_value = active_threshold;
            g_thermal.event_stats.last_event_at_ms = now_ms;
        }
        if (config.actuation_enabled) {
            g_thermal.event_stats.actuation_requests += 1;
        }
        if (mark_hotspot_active) {
            g_thermal.event_stats.hotspot_active = true;
        }
    }

    if (config.event_enabled && !publish_ok) {
        std::cerr << "[THERMAL][EVENT] failed to publish MQTT event for frame=" << header.frameId
                  << " max=" << header.maxValue
                  << " signal=" << observed_signal_value
                  << " hot_area=" << hot_area_pixels
                  << std::endl;
    }

    if (publish_ok) {
        std::cout << "[THERMAL][EVENT] published frame=" << header.frameId
                  << " max=" << header.maxValue
                  << " signal=" << observed_signal_value
                  << " baseline=" << baseline_normal_max
                  << " threshold=" << active_threshold
                  << " hot_area=" << hot_area_pixels
                  << " hits=" << consecutive_hits
                  << " topic=" << config.event_topic
                  << std::endl;
    }

    if (config.actuation_enabled && !actuation_ok) {
        std::cerr << "[THERMAL][EVENT] server-side actuation publish failed for frame=" << header.frameId << std::endl;
    }
}

std::vector<crow::websocket::connection*> snapshotClients()
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    return std::vector<crow::websocket::connection*>(g_thermal.clients.begin(), g_thermal.clients.end());
}

void setReceiverState(bool running, bool bound, const std::string& bind_host, uint16_t port, const std::string& error = {})
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    g_thermal.stats.receiver_running = running;
    g_thermal.stats.udp_bound = bound;
    g_thermal.stats.udp_bind_host = bind_host;
    g_thermal.stats.udp_port = port;
    g_thermal.stats.last_error = error;
    g_thermal.receiver_running.store(running);
}

void noteIncompleteFrameLocked(uint16_t frameId, const ThermalFrameTracker& tracker, const char* reason)
{
    g_thermal.stats.incomplete_frames += 1;
    if (tracker.totalChunks > tracker.uniqueChunks.size()) {
        g_thermal.stats.missing_chunks += static_cast<unsigned long long>(tracker.totalChunks - tracker.uniqueChunks.size());
    }
    if (std::strcmp(reason, "drop incomplete frame") == 0) {
        g_thermal.stats.evicted_frames += 1;
    }

    std::cout << "[THERMAL] " << reason
              << " id= " << frameId
              << " chunks= " << tracker.uniqueChunks.size() << " / " << tracker.totalChunks
              << std::endl;
}

void pruneExpiredFramesLocked(long long nowMs, int timeoutMs)
{
    if (timeoutMs <= 0) {
        return;
    }

    for (auto it = g_thermal.in_flight_frames.begin(); it != g_thermal.in_flight_frames.end();) {
        if ((nowMs - it->second.lastSeenAtMs) <= timeoutMs) {
            ++it;
            continue;
        }

        noteIncompleteFrameLocked(it->first, it->second, "frame timeout");
        it = g_thermal.in_flight_frames.erase(it);
    }
}

void trimTrackedFramesLocked(int maxTrackedFrames)
{
    if (maxTrackedFrames <= 0) {
        return;
    }

    while (static_cast<int>(g_thermal.in_flight_frames.size()) > maxTrackedFrames) {
        const auto oldest = std::min_element(g_thermal.in_flight_frames.begin(),
                                             g_thermal.in_flight_frames.end(),
                                             [](const auto& lhs, const auto& rhs) {
                                                 return lhs.second.lastSeenAtMs < rhs.second.lastSeenAtMs;
                                             });
        if (oldest == g_thermal.in_flight_frames.end()) {
            return;
        }

        noteIncompleteFrameLocked(oldest->first, oldest->second, "drop incomplete frame");
        g_thermal.in_flight_frames.erase(oldest);
    }
}

void maybeLogThermalStatsLocked(long long nowMs, int statsLogIntervalMs)
{
    if (statsLogIntervalMs <= 0 || g_thermal.stats.packets_received == 0) {
        return;
    }
    if (g_thermal.last_stats_log_at_ms != 0 && (nowMs - g_thermal.last_stats_log_at_ms) < statsLogIntervalMs) {
        return;
    }

    g_thermal.last_stats_log_at_ms = nowMs;
    std::cout << "[THERMAL][STATS] packets=" << g_thermal.stats.packets_received
              << " bytes=" << g_thermal.stats.bytes_received
              << " completed=" << g_thermal.stats.completed_frames
              << " incomplete=" << g_thermal.stats.incomplete_frames
              << " missing=" << g_thermal.stats.missing_chunks
              << " duplicate=" << g_thermal.stats.duplicate_chunks
              << " invalid=" << g_thermal.stats.invalid_packets
              << " inflight=" << g_thermal.stats.in_flight_frames
              << " last_frame=" << g_thermal.stats.last_frame_id
              << " last_chunk=" << g_thermal.stats.last_chunk_index << "/" << g_thermal.stats.last_total_chunks
              << " last_sender=" << g_thermal.stats.last_sender
              << std::endl;
}

void noteInvalidPacketStats(size_t packetBytes,
                            const std::string& senderText,
                            bool enableFrameStats,
                            int statsLogIntervalMs)
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    const long long nowMs = currentTimeMs();

    g_thermal.stats.packets_received += 1;
    g_thermal.stats.bytes_received += static_cast<unsigned long long>(packetBytes);
    g_thermal.stats.last_packet_bytes = packetBytes;
    g_thermal.stats.last_packet_at_ms = nowMs;
    g_thermal.stats.invalid_packets += 1;
    if (enableFrameStats) {
        g_thermal.stats.last_sender = senderText;
        maybeLogThermalStatsLocked(nowMs, statsLogIntervalMs);
    }
}

void updatePacketStats(const ThermalNormalizedPacket& packet,
                       size_t packetBytes,
                       const std::string& senderText,
                       bool enableFrameStats,
                       int frameTimeoutMs,
                       int maxTrackedFrames,
                       int statsLogIntervalMs,
                       ThermalCompletedFrame& completedFrame)
{
    const ThermalPacketHeader& header = packet.header;
    completedFrame.ready = false;

    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    const long long nowMs = currentTimeMs();

    g_thermal.stats.packets_received += 1;
    g_thermal.stats.bytes_received += static_cast<unsigned long long>(packetBytes);
    g_thermal.stats.last_packet_bytes = packetBytes;
    g_thermal.stats.last_packet_at_ms = nowMs;

    g_thermal.stats.last_frame_id = header.frameId;
    g_thermal.stats.last_chunk_index = header.chunkIndex;
    g_thermal.stats.last_total_chunks = header.totalChunks;
    g_thermal.stats.last_min_val = header.minValue;
    g_thermal.stats.last_max_val = header.maxValue;
    if (enableFrameStats) {
        g_thermal.stats.last_sender = senderText;
    }

    pruneExpiredFramesLocked(nowMs, frameTimeoutMs);
    g_thermal.stats.in_flight_frames = g_thermal.in_flight_frames.size();
    g_thermal.stats.max_in_flight_frames = std::max(g_thermal.stats.max_in_flight_frames, g_thermal.stats.in_flight_frames);

    ThermalFrameTracker& tracker = g_thermal.in_flight_frames[header.frameId];
    if (tracker.firstSeenAtMs == 0) {
        tracker.firstSeenAtMs = nowMs;
        tracker.totalChunks = header.totalChunks;
    }
    tracker.lastSeenAtMs = nowMs;
    if (header.totalChunks > tracker.totalChunks) {
        tracker.totalChunks = header.totalChunks;
    }
    if (packet.hasRangeData) {
        tracker.headerMinValue = header.minValue;
        tracker.headerMaxValue = header.maxValue;
        tracker.headerHasRangeData = true;
    }

    const uint16_t minimumChunks = static_cast<uint16_t>(header.chunkIndex + 1);
    if (minimumChunks > tracker.totalChunks) {
        tracker.totalChunks = minimumChunks;
    }

    const bool inserted = tracker.uniqueChunks.insert(header.chunkIndex).second;
    if (!inserted) {
        g_thermal.stats.duplicate_chunks += 1;
    } else {
        const std::string chunkPayload = packet.wsPayload.substr(kThermalHeaderBytes);
        tracker.payloadBytes += chunkPayload.size();
        tracker.chunkPayloads[header.chunkIndex] = chunkPayload;
        updateThermalTrackerRawRange(tracker, chunkPayload);
    }

    if (tracker.totalChunks > 0 && tracker.uniqueChunks.size() >= tracker.totalChunks) {
        g_thermal.stats.completed_frames += 1;
        finalizeCompletedThermalFrame(header, tracker, completedFrame);
        g_thermal.stats.last_min_val = completedFrame.header.minValue;
        g_thermal.stats.last_max_val = completedFrame.header.maxValue;
        g_thermal.stats.last_frame_payload_bytes = completedFrame.payloadBytes;
        g_thermal.stats.last_frame_encoding = completedFrame.encoding;
        g_thermal.in_flight_frames.erase(header.frameId);
    }

    trimTrackedFramesLocked(maxTrackedFrames);
    g_thermal.stats.in_flight_frames = g_thermal.in_flight_frames.size();
    g_thermal.stats.max_in_flight_frames = std::max(g_thermal.stats.max_in_flight_frames, g_thermal.stats.in_flight_frames);

    if (enableFrameStats) {
        maybeLogThermalStatsLocked(nowMs, statsLogIntervalMs);
    }
}

void clearThermalFrameTrackers()
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
    const ThermalEventConfig config = loadThermalEventConfig();
    g_thermal.in_flight_frames.clear();
    g_thermal.baseline_normal_samples.clear();
    g_thermal.last_stats_log_at_ms = 0;
    g_thermal.stats.in_flight_frames = 0;

    g_thermal.stats.last_frame_id = 0;
    g_thermal.stats.last_chunk_index = 0;
    g_thermal.stats.last_total_chunks = 0;
    g_thermal.stats.last_min_val = 0;
    g_thermal.stats.last_max_val = 0;
    g_thermal.stats.last_sender.clear();
    g_thermal.stats.last_packet_bytes = 0;
    g_thermal.stats.last_packet_at_ms = 0;
    g_thermal.stats.packets_received = 0;
    g_thermal.stats.bytes_received = 0;
    g_thermal.stats.invalid_packets = 0;
    g_thermal.stats.duplicate_chunks = 0;
    g_thermal.stats.completed_frames = 0;
    g_thermal.stats.incomplete_frames = 0;
    g_thermal.stats.missing_chunks = 0;
    g_thermal.stats.evicted_frames = 0;
    g_thermal.stats.max_in_flight_frames = 0;
    g_thermal.stats.last_frame_payload_bytes = 0;
    g_thermal.stats.last_frame_encoding = "unknown";
    g_thermal.stats.udp_socket_rcvbuf_bytes = 0;
    refreshThermalEventStatusLocked(config);
    g_thermal.event_stats.current_threshold_max_value = config.hotspot_threshold_max_value;
    g_thermal.event_stats.current_clear_threshold_max_value = config.hotspot_threshold_max_value;
    g_thermal.event_stats.baseline_normal_max_value = 0;
    g_thermal.event_stats.baseline_sample_count = 0;
    g_thermal.event_stats.baseline_updates = 0;
    g_thermal.event_stats.last_frame_max_value = 0;
    g_thermal.event_stats.last_signal_value = 0;
    g_thermal.event_stats.last_hot_area_pixels = 0;
    g_thermal.event_stats.last_hot_area_threshold = 0;
    g_thermal.event_stats.last_valid_pixels = 0;
    g_thermal.event_stats.last_roi_min_value = 0;
    g_thermal.event_stats.last_roi_max_value = 0;
    g_thermal.event_stats.event_attempts = 0;
    g_thermal.event_stats.events_published = 0;
    g_thermal.event_stats.actuation_requests = 0;
    g_thermal.event_stats.last_event_attempt_frame_id = 0;
    g_thermal.event_stats.last_event_attempt_at_ms = 0;
    g_thermal.event_stats.last_event_frame_id = 0;
    g_thermal.event_stats.last_event_max_value = 0;
    g_thermal.event_stats.last_event_signal_value = 0;
    g_thermal.event_stats.last_event_hot_area_pixels = 0;
    g_thermal.event_stats.last_event_threshold_max_value = 0;
    g_thermal.event_stats.last_event_at_ms = 0;
    g_thermal.event_stats.consecutive_hits = 0;
    g_thermal.event_stats.baseline_ready = false;
    g_thermal.event_stats.hotspot_active = false;
    g_thermal.event_stats.last_publish_ok = false;
    g_thermal.event_stats.last_actuation_ok = false;
    g_thermal.event_stats.last_event_message.clear();
}

void broadcastThermalChunk(const std::string& payload)
{
    const auto clients = snapshotClients();
    for (auto* client : clients) {
        if (!client) {
            continue;
        }
        client->send_binary(payload);
    }
}

crow::json::wvalue makeThermalStatusJson()
{
    std::lock_guard<std::mutex> lock(g_thermal.state_mutex);

    crow::json::wvalue response;
    response["status"] = "ok";
    response["stream"] = "thermal16";
    response["udp_bound"] = g_thermal.stats.udp_bound;
    response["receiver_running"] = g_thermal.stats.receiver_running;
    response["udp_bind_host"] = g_thermal.stats.udp_bind_host;
    response["udp_port"] = g_thermal.stats.udp_port;
    response["udp_socket_rcvbuf_bytes"] = g_thermal.stats.udp_socket_rcvbuf_bytes;
    response["ws_clients"] = g_thermal.stats.ws_clients;
    response["last_frame_id"] = static_cast<int>(g_thermal.stats.last_frame_id);
    response["last_chunk_index"] = static_cast<int>(g_thermal.stats.last_chunk_index);
    response["last_total_chunks"] = static_cast<int>(g_thermal.stats.last_total_chunks);
    response["last_min_val"] = static_cast<int>(g_thermal.stats.last_min_val);
    response["last_max_val"] = static_cast<int>(g_thermal.stats.last_max_val);
    response["last_sender"] = g_thermal.stats.last_sender;
    response["last_packet_bytes"] = static_cast<int>(g_thermal.stats.last_packet_bytes);
    response["last_packet_at_ms"] = g_thermal.stats.last_packet_at_ms;
    response["packets_received"] = static_cast<uint64_t>(g_thermal.stats.packets_received);
    response["bytes_received"] = static_cast<uint64_t>(g_thermal.stats.bytes_received);
    response["invalid_packets"] = static_cast<uint64_t>(g_thermal.stats.invalid_packets);
    response["duplicate_chunks"] = static_cast<uint64_t>(g_thermal.stats.duplicate_chunks);
    response["completed_frames"] = static_cast<uint64_t>(g_thermal.stats.completed_frames);
    response["incomplete_frames"] = static_cast<uint64_t>(g_thermal.stats.incomplete_frames);
    response["missing_chunks"] = static_cast<uint64_t>(g_thermal.stats.missing_chunks);
    response["evicted_frames"] = static_cast<uint64_t>(g_thermal.stats.evicted_frames);
    response["in_flight_frames"] = static_cast<int>(g_thermal.stats.in_flight_frames);
    response["max_in_flight_frames"] = static_cast<int>(g_thermal.stats.max_in_flight_frames);
    response["last_frame_payload_bytes"] = static_cast<int>(g_thermal.stats.last_frame_payload_bytes);
    response["last_frame_encoding"] = g_thermal.stats.last_frame_encoding;
    response["last_error"] = g_thermal.stats.last_error;
    response["monitor_always_on"] = g_thermal.event_stats.monitor_always_on;
    response["event"]["enabled"] = g_thermal.event_stats.event_enabled;
    response["event"]["actuation_enabled"] = g_thermal.event_stats.actuation_enabled;
    response["event"]["baseline_enabled"] = g_thermal.event_stats.baseline_enabled;
    response["event"]["broker_connected"] = g_thermal.event_stats.broker_connected;
    response["event"]["topic"] = g_thermal.event_stats.event_topic;
    response["event"]["hotspot_threshold_max_value"] = g_thermal.event_stats.hotspot_threshold_max_value;
    response["event"]["cooldown_ms"] = g_thermal.event_stats.cooldown_ms;
    response["event"]["baseline_margin"] = g_thermal.event_stats.baseline_margin;
    response["event"]["baseline_window_ms"] = g_thermal.event_stats.baseline_window_ms;
    response["event"]["baseline_min_samples"] = g_thermal.event_stats.baseline_min_samples;
    response["event"]["baseline_guard_delta"] = g_thermal.event_stats.baseline_guard_delta;
    response["event"]["consecutive_frames_required"] = g_thermal.event_stats.consecutive_frames_required;
    response["event"]["signal_percentile"] = g_thermal.event_stats.signal_percentile;
    response["event"]["hot_area_min_pixels"] = g_thermal.event_stats.hot_area_min_pixels;
    response["event"]["roi"]["x"] = g_thermal.event_stats.roi_x;
    response["event"]["roi"]["y"] = g_thermal.event_stats.roi_y;
    response["event"]["roi"]["width"] = g_thermal.event_stats.roi_width;
    response["event"]["roi"]["height"] = g_thermal.event_stats.roi_height;
    response["event"]["current_threshold_max_value"] = g_thermal.event_stats.current_threshold_max_value;
    response["event"]["current_clear_threshold_max_value"] = g_thermal.event_stats.current_clear_threshold_max_value;
    response["event"]["baseline_normal_max_value"] = g_thermal.event_stats.baseline_normal_max_value;
    response["event"]["baseline_sample_count"] = g_thermal.event_stats.baseline_sample_count;
    response["event"]["baseline_updates"] = static_cast<uint64_t>(g_thermal.event_stats.baseline_updates);
    response["event"]["attempts"] = static_cast<uint64_t>(g_thermal.event_stats.event_attempts);
    response["event"]["published"] = static_cast<uint64_t>(g_thermal.event_stats.events_published);
    response["event"]["actuation_requests"] = static_cast<uint64_t>(g_thermal.event_stats.actuation_requests);
    response["event"]["last_frame_max_value"] = static_cast<int>(g_thermal.event_stats.last_frame_max_value);
    response["event"]["last_signal_value"] = g_thermal.event_stats.last_signal_value;
    response["event"]["last_hot_area_pixels"] = g_thermal.event_stats.last_hot_area_pixels;
    response["event"]["last_hot_area_threshold"] = g_thermal.event_stats.last_hot_area_threshold;
    response["event"]["last_valid_pixels"] = g_thermal.event_stats.last_valid_pixels;
    response["event"]["last_roi_min_value"] = g_thermal.event_stats.last_roi_min_value;
    response["event"]["last_roi_max_value"] = g_thermal.event_stats.last_roi_max_value;
    response["event"]["last_event_attempt_frame_id"] = static_cast<int>(g_thermal.event_stats.last_event_attempt_frame_id);
    response["event"]["last_event_attempt_at_ms"] = g_thermal.event_stats.last_event_attempt_at_ms;
    response["event"]["last_event_frame_id"] = static_cast<int>(g_thermal.event_stats.last_event_frame_id);
    response["event"]["last_event_max_value"] = static_cast<int>(g_thermal.event_stats.last_event_max_value);
    response["event"]["last_event_signal_value"] = g_thermal.event_stats.last_event_signal_value;
    response["event"]["last_event_hot_area_pixels"] = g_thermal.event_stats.last_event_hot_area_pixels;
    response["event"]["last_event_threshold_max_value"] = g_thermal.event_stats.last_event_threshold_max_value;
    response["event"]["last_event_at_ms"] = g_thermal.event_stats.last_event_at_ms;
    response["event"]["consecutive_hits"] = g_thermal.event_stats.consecutive_hits;
    response["event"]["baseline_ready"] = g_thermal.event_stats.baseline_ready;
    response["event"]["hotspot_active"] = g_thermal.event_stats.hotspot_active;
    response["event"]["last_publish_ok"] = g_thermal.event_stats.last_publish_ok;
    response["event"]["last_actuation_ok"] = g_thermal.event_stats.last_actuation_ok;
    response["event"]["last_message"] = g_thermal.event_stats.last_event_message;
    response["format"]["width"] = kThermalWidth;
    response["format"]["height"] = kThermalHeight;
    response["format"]["pixel"] = "u16be|u8";
    response["format"]["header_bytes"] = static_cast<int>(kThermalHeaderBytes);
    response["format"]["frame_bytes"] = kThermalFrameBytes16;
    response["format"]["frame_bytes_16"] = kThermalFrameBytes16;
    response["format"]["frame_bytes_8"] = kThermalFrameBytes8;
    return response;
}

void thermalReceiverLoop()
{
    const std::string bind_host = envOrDefault("THERMAL_UDP_BIND_HOST", "0.0.0.0");
    const uint16_t bind_port = envPortOrDefault("THERMAL_UDP_PORT", kDefaultThermalUdpPort);
    const bool enable_frame_stats = envBoolOrDefault("THERMAL_ENABLE_FRAME_STATS", false);
    const int receive_buffer_bytes = envIntOrDefault("THERMAL_UDP_RCVBUF_BYTES", kDefaultUdpSocketBufferBytes);
    const int frame_timeout_ms = envIntOrDefault("THERMAL_FRAME_TRACK_TIMEOUT_MS", kDefaultFrameTrackTimeoutMs);
    const int max_tracked_frames = envIntOrDefault("THERMAL_MAX_TRACKED_FRAMES", kDefaultMaxTrackedFrames);
    const int stats_log_interval_ms = envIntOrDefault("THERMAL_STATS_LOG_INTERVAL_MS", kDefaultStatsLogIntervalMs);

    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        const std::string error = std::string("socket() failed: ") + std::strerror(errno);
        std::cerr << "[THERMAL] " << error << std::endl;
        setReceiverState(false, false, bind_host, bind_port, error);
        g_thermal.receiver_thread_started.store(false);
        return;
    }

    int reuse = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        std::cerr << "[THERMAL] setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno) << std::endl;
    }
    configureReceiveBuffer(fd, receive_buffer_bytes);

    timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = kReceiveTimeoutUs;
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0) {
        std::cerr << "[THERMAL] setsockopt(SO_RCVTIMEO) failed: " << std::strerror(errno) << std::endl;
    }

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(bind_port);

    if (bind_host == "*" || bind_host == "0.0.0.0" || bind_host.empty()) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
        const std::string error = "invalid THERMAL_UDP_BIND_HOST: " + bind_host;
        std::cerr << "[THERMAL] " << error << std::endl;
        ::close(fd);
        setReceiverState(false, false, bind_host, bind_port, error);
        g_thermal.receiver_thread_started.store(false);
        return;
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        const std::string error = std::string("bind() failed: ") + std::strerror(errno);
        std::cerr << "[THERMAL] " << error << " host=" << bind_host << " port=" << bind_port << std::endl;
        ::close(fd);
        setReceiverState(false, false, bind_host, bind_port, error);
        g_thermal.receiver_thread_started.store(false);
        return;
    }

    clearThermalFrameTrackers();
    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        g_thermal.stats.udp_socket_rcvbuf_bytes = querySocketBufferBytes(fd, SO_RCVBUF);
    }
    setReceiverState(true, true, bind_host, bind_port);
    if (enable_frame_stats) {
        std::cout << "[THERMAL] UDP receiver bound to " << bind_host << ":" << bind_port
                  << " recvbuf=" << querySocketBufferBytes(fd, SO_RCVBUF)
                  << std::endl;
    } else {
        std::cout << "[THERMAL] UDP receiver bound to " << bind_host << ":" << bind_port << std::endl;
    }

    ThermalNormalizerState normalizer;
    std::vector<char> buffer(kMaxUdpPacketBytes);
    while (true) {
        {
            std::lock_guard<std::mutex> lock(g_thermal.receiver_mutex);
            if (g_thermal.stop_requested.load()) {
                break;
            }
        }

        sockaddr_in sender {};
        socklen_t sender_len = sizeof(sender);
        const ssize_t received = ::recvfrom(fd,
                                            buffer.data(),
                                            buffer.size(),
                                            0,
                                            reinterpret_cast<sockaddr*>(&sender),
                                            &sender_len);
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }

            const std::string error = std::string("recvfrom() failed: ") + std::strerror(errno);
            std::cerr << "[THERMAL] " << error << std::endl;
            setReceiverState(true, true, bind_host, bind_port, error);
            continue;
        }

        const std::string sender_key = senderToString(sender);
        const std::string sender_text = enable_frame_stats ? sender_key : std::string();
        const std::string payload(buffer.data(), static_cast<size_t>(received));
        const long long now_ms = currentTimeMs();

        ThermalNormalizedPacket normalized{};
        if (!normalizeThermalPayload(payload, sender_key, now_ms, frame_timeout_ms, normalizer, normalized)) {
            noteInvalidPacketStats(payload.size(), sender_text, enable_frame_stats, stats_log_interval_ms);
            continue;
        }

        ThermalCompletedFrame completedFrame{};
        updatePacketStats(normalized,
                          payload.size(),
                          sender_text,
                          enable_frame_stats,
                          frame_timeout_ms,
                          max_tracked_frames,
                          stats_log_interval_ms,
                          completedFrame);
        maybePublishThermalEvent(completedFrame);
        broadcastThermalChunk(normalized.wsPayload);
    }

    ::close(fd);
    setReceiverState(false, false, bind_host, bind_port);
    g_thermal.receiver_thread_started.store(false);
    std::cout << "[THERMAL] UDP receiver stopped" << std::endl;
}

void ensureThermalReceiverRunning()
{
    std::lock_guard<std::mutex> lock(g_thermal.receiver_mutex);
    if (g_thermal.receiver_thread_started.load()) {
        return;
    }
    if (g_thermal.receiver_thread.joinable()) {
        g_thermal.receiver_thread.join();
    }

    g_thermal.stop_requested.store(false);
    g_thermal.receiver_thread_started.store(true);
    g_thermal.receiver_thread = std::thread(thermalReceiverLoop);
}

void requestThermalReceiverStopIfIdle()
{
    std::lock_guard<std::mutex> receiver_lock(g_thermal.receiver_mutex);
    std::lock_guard<std::mutex> state_lock(g_thermal.state_mutex);
    if (g_thermal.event_stats.monitor_always_on) {
        return;
    }
    if (!g_thermal.clients.empty()) {
        return;
    }
    g_thermal.stop_requested.store(true);
}
} // namespace

void registerThermalProxyRoutes(crow::SimpleApp& app)
{
    const ThermalEventConfig config = loadThermalEventConfig();
    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked(config);
    }
    if (config.event_enabled || config.actuation_enabled) {
        (void)ensureThermalEventMqtt(config);
    }
    if (config.monitor_always_on) {
        ensureThermalReceiverRunning();
    }

    CROW_ROUTE(app, "/thermal/control/start").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        ensureThermalReceiverRunning();

        crow::json::wvalue response;
        response["status"] = "accepted";
        response["stream"] = "thermal16";
        response["transport"] = "websocket";
        response["ws_path"] = "/thermal/stream";
        response["monitor_always_on"] = loadThermalEventConfig().monitor_always_on;
        response["udp_bind_host"] = envOrDefault("THERMAL_UDP_BIND_HOST", "0.0.0.0");
        response["udp_port"] = envPortOrDefault("THERMAL_UDP_PORT", kDefaultThermalUdpPort);
        response["format"]["width"] = kThermalWidth;
        response["format"]["height"] = kThermalHeight;
        response["format"]["pixel"] = "u16be|u8";
        response["format"]["header_bytes"] = static_cast<int>(kThermalHeaderBytes);
        response["format"]["frame_bytes"] = kThermalFrameBytes16;
        response["format"]["frame_bytes_16"] = kThermalFrameBytes16;
        response["format"]["frame_bytes_8"] = kThermalFrameBytes8;
        return crow::response(202, response);
    });

    CROW_ROUTE(app, "/thermal/control/stop").methods(crow::HTTPMethod::POST)
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        requestThermalReceiverStopIfIdle();

        crow::json::wvalue response;
        response["status"] = "accepted";
        response["monitor_always_on"] = loadThermalEventConfig().monitor_always_on;
        response["note"] = loadThermalEventConfig().monitor_always_on
            ? "receiver remains active for server-side monitoring/events"
            : "receiver stops when no WebSocket clients remain";
        return crow::response(202, response);
    });

    CROW_ROUTE(app, "/thermal/status")
    ([](const crow::request& req) {
        if (!isAuthorized(req)) {
            return crow::response(401, "Unauthorized");
        }

        return crow::response(makeThermalStatusJson());
    });

    CROW_WEBSOCKET_ROUTE(app, "/thermal/stream")
        .onaccept([](const crow::request& req, void**) {
            return isAuthorized(req);
        })
        .onopen([](crow::websocket::connection& conn) {
            ensureThermalReceiverRunning();

            std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
            g_thermal.clients.insert(&conn);
            g_thermal.stats.ws_clients = static_cast<int>(g_thermal.clients.size());
        })
        .onclose([](crow::websocket::connection& conn, const std::string&) {
            {
                std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
                g_thermal.clients.erase(&conn);
                g_thermal.stats.ws_clients = static_cast<int>(g_thermal.clients.size());
            }
            requestThermalReceiverStopIfIdle();
        })
        .onmessage([](crow::websocket::connection&, const std::string&, bool) {
            // The thermal stream is server -> client only.
        });
}

void shutdownThermalProxy()
{
    {
        std::lock_guard<std::mutex> lock(g_thermal.receiver_mutex);
        g_thermal.stop_requested.store(true);
    }

    if (g_thermal.receiver_thread.joinable()) {
        g_thermal.receiver_thread.join();
    }
    g_thermal.receiver_thread_started.store(false);
    g_thermal.event_mqtt.reset();
}
