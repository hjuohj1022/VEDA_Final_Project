#include "features/thermal/ThermalProxy.h"
#include "features/event_log/EventLogStore.h"
#include "infra/mqtt/MqttManager.h"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <chrono>
#include <cmath>
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

// 열화상 UDP 스트림을 수신해 정규화하고,
// 실시간 웹소켓 중계와 고온 지점 이벤트 감지, 자동 제어까지 수행하는 구현 파일이다.
// main.cpp에서 제공하는 공용 JWT 검증 헬퍼를 그대로 재사용한다.
bool verifyJWT(const std::string& token);

namespace {
// 열화상 UDP 스트림의 고정 포맷과 이벤트 감지 기본 튜닝값을 한 곳에 모아둔다.
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
constexpr int kDefaultThermalEventThresholdMaxValue = 8500;
constexpr int kDefaultThermalEventCooldownMs = 5000;
constexpr int kDefaultThermalEventBaselineMargin = 250;
constexpr int kDefaultThermalEventBaselineWindowMs = 300000;
constexpr int kDefaultThermalEventBaselineMinSamples = 30;
constexpr int kDefaultThermalEventBaselineGuardDelta = 250;
constexpr int kDefaultThermalEventConsecutiveFrames = 2;
constexpr int kDefaultThermalEventSignalPercentile = 100;
constexpr int kDefaultThermalEventHotAreaMinPixels = 4;
constexpr int kDefaultThermalEventSeedDelta = 120;
constexpr int kDefaultThermalEventGrowDelta = 60;
constexpr int kDefaultThermalEventComponentAreaMin = 2;
constexpr int kDefaultThermalEventComponentAreaMax = 48;
constexpr int kDefaultThermalEventLocalContrastMin = 150;
constexpr int kDefaultThermalEventNewPixelsMin = 1;
constexpr int kDefaultThermalEventClearFrames = 4;
constexpr int kDefaultThermalEventTrackMatchDistancePx = 18;
constexpr int kDefaultThermalEventRingRadius = 2;
constexpr double kDefaultThermalEventAspectRatioMin = 1.8;
constexpr int kDefaultThermalEventTipX = -1;
constexpr int kDefaultThermalEventTipY = -1;
constexpr int kDefaultThermalEventTipDistanceMaxPx = 24;
constexpr int kDefaultThermalEventScoreMin = 6;

// 서버 내부에서 사용하는 "정규화된" 열화상 패킷 헤더 표현이다.
// 신규 포맷/구형 포맷을 모두 이 구조로 맞춘 뒤 이후 파이프라인을 태운다.
struct ThermalPacketHeader {
    uint16_t frameId = 0;
    uint16_t chunkIndex = 0;
    uint16_t totalChunks = 0;
    uint16_t minValue = 0;
    uint16_t maxValue = 0;
};

// 원본 UDP 청크에서 읽어낸 헤더 정보다. 레거시 포맷 여부도 함께 보관한다.
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

// 웹소켓 브로드캐스트에 사용할 바이너리 본문과 메타데이터를 함께 묶는다.
struct ThermalNormalizedPacket {
    ThermalPacketHeader header;
    bool hasRangeData = false;
    std::string wsPayload;
};

// frameId가 없는 레거시 송신자도 서버 쪽에서 임시 프레임 식별자를 붙여 추적한다.
struct ThermalLegacyFrameState {
    uint16_t frameId = 0;
    uint16_t totalChunks = 0;
    long long lastSeenAtMs = 0;
    bool active = false;
    std::set<uint16_t> receivedChunks;
};

// 송신자별 레거시 프레임 상태를 관리해 구형 장비 패킷도 신규 파이프라인에 태운다.
struct ThermalNormalizerState {
    std::map<std::string, ThermalLegacyFrameState> legacyFramesBySender;
    uint16_t nextSyntheticFrameId = 1;
};

// 여러 UDP 조각을 모아 "하나의 열화상 프레임"으로 다시 조립하는 중간 상태다.
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

// 모든 조각이 모여 이벤트 분석과 웹소켓 송신이 가능한 완성 프레임이다.
struct ThermalCompletedFrame {
    ThermalPacketHeader header;
    size_t payloadBytes = 0;
    bool hasRangeData = false;
    bool ready = false;
    std::string encoding = "unknown";
    std::string framePayload;
};

// 이벤트 판정 시 실제로 분석할 열화상 관심 영역이다.
struct ThermalEventRoi {
    int x = 0;
    int y = 0;
    int width = kThermalWidth;
    int height = kThermalHeight;
};

// 한 프레임에서 계산한 통계값(최대/백분위/중앙값 등)을 담는다.
struct ThermalEventFrameAnalysis {
    bool valid = false;
    int percentile = kDefaultThermalEventSignalPercentile;
    ThermalEventRoi roi;
    int validPixels = 0;
    int roiMinValue = 0;
    int roiMaxValue = 0;
    int medianValue = 0;
    int p95Value = 0;
    int p99Value = 0;
    int madValue = 0;
    int signalValue = 0;
    std::vector<int> roiRawValues;
};

// 임계값을 넘은 픽셀 덩어리를 하나의 고온 지점 후보 덩어리로 표현한다.
struct ThermalEventComponent {
    bool valid = false;
    int area = 0;
    int seedArea = 0;
    int peakValue = 0;
    int p90Value = 0;
    int localRingMedian = 0;
    int localContrast = 0;
    int newPixels = 0;
    double aspectRatio = 1.0;
    int distanceToTipPx = -1;
    int score = 0;
    int centerX = -1;
    int centerY = -1;
    int minX = 0;
    int minY = 0;
    int maxX = 0;
    int maxY = 0;
    std::string rejectionReason;
    std::vector<uint8_t> mask;
};

// 이전 프레임과의 연속성 정보를 저장해 일시적 노이즈를 줄인다.
struct ThermalEventTrackerState {
    int persistFrames = 0;
    int missFrames = 0;
    int lastCenterX = -1;
    int lastCenterY = -1;
    ThermalEventRoi roi;
    std::vector<uint8_t> prevMask;
};

// UDP 수신, 프레임 재조립, 웹소켓 브로드캐스트 상태를 외부에 보여주기 위한 통계다.
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

// 이벤트 감지 파이프라인의 최근 판정 결과와 누적 통계를 저장한다.
struct ThermalEventStats {
    bool broker_connected = false;
    int current_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int current_clear_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int baseline_normal_max_value = 0;
    int baseline_sample_count = 0;
    unsigned long long baseline_updates = 0;
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
    int last_median_value = 0;
    int last_p95_value = 0;
    int last_p99_value = 0;
    int last_mad_value = 0;
    int last_seed_threshold = 0;
    int last_grow_threshold = 0;
    int last_candidate_area = 0;
    int last_candidate_seed_area = 0;
    int last_candidate_peak_value = 0;
    int last_candidate_p90_value = 0;
    int last_candidate_local_contrast = 0;
    int last_candidate_new_pixels = 0;
    double last_candidate_aspect_ratio = 0.0;
    int last_candidate_distance_to_tip_px = -1;
    int last_candidate_score = 0;
    int last_component_count = 0;
    std::string last_candidate_rejection_reason;
    int last_candidate_center_x = -1;
    int last_candidate_center_y = -1;
    int last_candidate_persist_frames = 0;
    int last_candidate_miss_frames = 0;
    int active_clear_miss_frames = 0;
    uint16_t last_event_attempt_frame_id = 0;
    long long last_event_attempt_at_ms = 0;
    uint16_t last_event_frame_id = 0;
    uint16_t last_event_max_value = 0;
    int last_event_signal_value = 0;
    int last_event_hot_area_pixels = 0;
    int last_event_candidate_area = 0;
    int last_event_candidate_local_contrast = 0;
    double last_event_candidate_aspect_ratio = 0.0;
    int last_event_candidate_distance_to_tip_px = -1;
    int last_event_candidate_score = 0;
    int last_event_candidate_persist_frames = 0;
    int last_event_threshold_max_value = 0;
    long long last_event_at_ms = 0;
    int consecutive_hits = 0;
    bool baseline_ready = false;
    bool hotspot_active = false;
    bool last_publish_ok = false;
    bool last_actuation_ok = false;
    std::string last_event_message;
};

// 환경 변수에서 읽어오는 열화상 이벤트 감지/자동 제어 설정값이다.
struct ThermalEventConfig {
    bool monitor_always_on = true;
    bool event_enabled = true;
    bool actuation_enabled = false;
    bool baseline_enabled = true;
    bool debug_log = false;
    int hotspot_threshold_max_value = kDefaultThermalEventThresholdMaxValue;
    int cooldown_ms = kDefaultThermalEventCooldownMs;
    int baseline_margin = kDefaultThermalEventBaselineMargin;
    int baseline_window_ms = kDefaultThermalEventBaselineWindowMs;
    int baseline_min_samples = kDefaultThermalEventBaselineMinSamples;
    int baseline_guard_delta = kDefaultThermalEventBaselineGuardDelta;
    int consecutive_frames_required = kDefaultThermalEventConsecutiveFrames;
    int signal_percentile = kDefaultThermalEventSignalPercentile;
    int hot_area_min_pixels = kDefaultThermalEventHotAreaMinPixels;
    int seed_delta = kDefaultThermalEventSeedDelta;
    int grow_delta = kDefaultThermalEventGrowDelta;
    int component_area_min = kDefaultThermalEventComponentAreaMin;
    int component_area_max = kDefaultThermalEventComponentAreaMax;
    int local_contrast_min = kDefaultThermalEventLocalContrastMin;
    int new_pixels_min = kDefaultThermalEventNewPixelsMin;
    int clear_frames = kDefaultThermalEventClearFrames;
    int track_match_distance_px = kDefaultThermalEventTrackMatchDistancePx;
    double aspect_ratio_min = kDefaultThermalEventAspectRatioMin;
    int tip_x = kDefaultThermalEventTipX;
    int tip_y = kDefaultThermalEventTipY;
    int tip_distance_max_px = kDefaultThermalEventTipDistanceMaxPx;
    int score_min = kDefaultThermalEventScoreMin;
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

// 기준선 적응 임계값 계산을 위해 최근 정상 프레임 샘플을 저장한다.
struct ThermalBaselineSample {
    long long observed_at_ms = 0;
    int signal_value = 0;
};

// 열화상 프록시의 전역 실행 상태다. 수신 스레드와 웹소켓 클라이언트가 함께 공유한다.
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
    ThermalEventTrackerState event_tracker;
    std::map<uint16_t, ThermalFrameTracker> in_flight_frames;
    std::deque<ThermalBaselineSample> baseline_normal_samples;
    std::unique_ptr<MqttManager> event_mqtt;
    long long last_stats_log_at_ms = 0;
};

// 열화상 프록시는 단일 수신 스레드/공유 상태 모델로 동작한다.
ThermalProxyState g_thermal;

// 내부 통계와 타임아웃 계산에 쓰는 현재 시각(ms) 유틸리티다.
long long currentTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// 문자열 환경 변수를 읽고 비어 있으면 안전한 기본값을 사용한다.
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

// 정수 환경 변수를 읽되 파싱 실패나 범위 오류 시 기본값으로 되돌린다.
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

double envDoubleOrDefault(const char* key, double fallback)
{
    const char* value = std::getenv(key);
    if (!value || !*value) {
        return fallback;
    }

    try {
        return std::stod(value);
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

// on/off 성격의 환경 변수를 느슨하게 해석해 bool로 변환한다.
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
    config.debug_log = envBoolOrDefault("THERMAL_EVENT_DEBUG_LOG", false);
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
    config.seed_delta = std::max(
        0, envIntOrDefault("THERMAL_EVENT_SEED_DELTA", kDefaultThermalEventSeedDelta));
    config.grow_delta = std::max(
        0, envIntOrDefault("THERMAL_EVENT_GROW_DELTA", kDefaultThermalEventGrowDelta));
    config.component_area_min = std::max(
        1, envIntOrDefault("THERMAL_EVENT_COMPONENT_AREA_MIN", kDefaultThermalEventComponentAreaMin));
    config.component_area_max = std::max(
        config.component_area_min,
        envIntOrDefault("THERMAL_EVENT_COMPONENT_AREA_MAX", kDefaultThermalEventComponentAreaMax));
    config.local_contrast_min = std::max(
        0, envIntOrDefault("THERMAL_EVENT_LOCAL_CONTRAST_MIN", kDefaultThermalEventLocalContrastMin));
    config.new_pixels_min = std::max(
        0, envIntOrDefault("THERMAL_EVENT_NEW_PIXELS_MIN", kDefaultThermalEventNewPixelsMin));
    config.clear_frames = std::max(
        1, envIntOrDefault("THERMAL_EVENT_CLEAR_FRAMES", kDefaultThermalEventClearFrames));
    config.track_match_distance_px = std::max(
        0, envIntOrDefault("THERMAL_EVENT_TRACK_MATCH_DISTANCE_PX", kDefaultThermalEventTrackMatchDistancePx));
    config.aspect_ratio_min = std::max(
        1.0, envDoubleOrDefault("THERMAL_EVENT_ASPECT_RATIO_MIN", kDefaultThermalEventAspectRatioMin));
    config.tip_x = envIntOrDefault("THERMAL_EVENT_TIP_X", kDefaultThermalEventTipX);
    config.tip_y = envIntOrDefault("THERMAL_EVENT_TIP_Y", kDefaultThermalEventTipY);
    if (config.tip_x >= 0) {
        config.tip_x = clampCoordinate(config.tip_x, kThermalWidth - 1);
    }
    if (config.tip_y >= 0) {
        config.tip_y = clampCoordinate(config.tip_y, kThermalHeight - 1);
    }
    config.tip_distance_max_px = std::max(
        0, envIntOrDefault("THERMAL_EVENT_TIP_DISTANCE_MAX_PX", kDefaultThermalEventTipDistanceMaxPx));
    config.score_min = std::max(
        1, envIntOrDefault("THERMAL_EVENT_SCORE_MIN", kDefaultThermalEventScoreMin));
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

// 웹소켓으로 내보낼 표준 페이로드 헤더를 빅엔디언 순서로 기록한다.
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

// 들어온 UDP 조각을 서버 내부 공통 형식으로 정규화한다.
// 이 단계에서 레거시 포맷은 임시 프레임 식별자를 부여하고, 웹소켓 전송용 본문도 함께 만든다.
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

size_t thermalRoiArea(const ThermalEventRoi& roi)
{
    return static_cast<size_t>(std::max(0, roi.width) * std::max(0, roi.height));
}

int thermalRoiIndex(const ThermalEventRoi& roi, int localX, int localY)
{
    return localY * roi.width + localX;
}

bool sameThermalEventRoi(const ThermalEventRoi& lhs, const ThermalEventRoi& rhs)
{
    return lhs.x == rhs.x
        && lhs.y == rhs.y
        && lhs.width == rhs.width
        && lhs.height == rhs.height;
}

int thermalPercentileValue(std::vector<int> values, int percentile)
{
    if (values.empty()) {
        return 0;
    }

    const int normalizedPercentile = clampPercentile(percentile);
    size_t rankIndex = 0;
    if (values.size() > 1) {
        const size_t rankCount =
            (static_cast<size_t>(normalizedPercentile) * values.size() + 99U) / 100U;
        rankIndex = rankCount > 0 ? std::min(values.size() - 1, rankCount - 1) : 0;
    }
    std::nth_element(values.begin(), values.begin() + rankIndex, values.end());
    return values[rankIndex];
}

void resetThermalEventTrackerState(ThermalEventTrackerState& tracker)
{
    tracker.persistFrames = 0;
    tracker.missFrames = 0;
    tracker.lastCenterX = -1;
    tracker.lastCenterY = -1;
    tracker.roi = {};
    tracker.prevMask.clear();
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

// 완성된 열화상 프레임에서 관심 영역 통계치를 계산해 이벤트 판정의 입력으로 쓴다.
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
    out.roiRawValues.assign(thermalRoiArea(out.roi), -1);

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

            out.roiRawValues[thermalRoiIndex(out.roi, x - out.roi.x, y - out.roi.y)] = static_cast<int>(rawValue);
            roiValues.push_back(rawValue);
            roiMin = std::min(roiMin, rawValue);
            roiMax = std::max(roiMax, rawValue);
        }
    }

    if (roiValues.empty()) {
        return false;
    }

    std::vector<int> roiValuesInt;
    roiValuesInt.reserve(roiValues.size());
    for (const uint16_t value : roiValues) {
        roiValuesInt.push_back(static_cast<int>(value));
    }

    out.valid = true;
    out.validPixels = static_cast<int>(roiValues.size());
    out.roiMinValue = static_cast<int>(roiMin);
    out.roiMaxValue = static_cast<int>(roiMax);
    out.medianValue = thermalPercentileValue(roiValuesInt, 50);
    out.p95Value = thermalPercentileValue(roiValuesInt, 95);
    out.p99Value = thermalPercentileValue(roiValuesInt, 99);
    std::vector<int> absoluteDeviations;
    absoluteDeviations.reserve(roiValuesInt.size());
    for (const int value : roiValuesInt) {
        absoluteDeviations.push_back(std::abs(value - out.medianValue));
    }
    out.madValue = thermalPercentileValue(absoluteDeviations, 50);
    out.signalValue = thermalPercentileValue(roiValuesInt, out.percentile);
    return true;
}

void buildThermalThresholdMasks(const ThermalEventFrameAnalysis& analysis,
                                int seedThreshold,
                                int growThreshold,
                                std::vector<uint8_t>& seedMask,
                                std::vector<uint8_t>& growMask)
{
    const size_t area = analysis.roiRawValues.size();
    seedMask.assign(area, 0);
    growMask.assign(area, 0);
    for (size_t i = 0; i < area; ++i) {
        const int value = analysis.roiRawValues[i];
        if (value < 0) {
            continue;
        }
        if (value >= growThreshold) {
            growMask[i] = 1;
        }
        if (value >= seedThreshold) {
            seedMask[i] = 1;
        }
    }
}

int computeThermalComponentRingMedian(const ThermalEventFrameAnalysis& analysis,
                                      const std::vector<uint8_t>& mask,
                                      int minX,
                                      int minY,
                                      int maxX,
                                      int maxY)
{
    std::vector<int> ringValues;
    const int startX = std::max(analysis.roi.x, minX - kDefaultThermalEventRingRadius);
    const int startY = std::max(analysis.roi.y, minY - kDefaultThermalEventRingRadius);
    const int endX = std::min(analysis.roi.x + analysis.roi.width - 1, maxX + kDefaultThermalEventRingRadius);
    const int endY = std::min(analysis.roi.y + analysis.roi.height - 1, maxY + kDefaultThermalEventRingRadius);

    for (int y = startY; y <= endY; ++y) {
        for (int x = startX; x <= endX; ++x) {
            const int localX = x - analysis.roi.x;
            const int localY = y - analysis.roi.y;
            const int idx = thermalRoiIndex(analysis.roi, localX, localY);
            if (idx < 0 || static_cast<size_t>(idx) >= mask.size() || mask[idx]) {
                continue;
            }
            const int value = analysis.roiRawValues[idx];
            if (value < 0) {
                continue;
            }
            ringValues.push_back(value);
        }
    }

    if (ringValues.empty()) {
        return analysis.medianValue;
    }
    return thermalPercentileValue(ringValues, 50);
}

int countThermalNewPixels(const std::vector<uint8_t>& mask, const std::vector<uint8_t>& prevMask)
{
    int newPixels = 0;
    if (prevMask.size() != mask.size()) {
        for (const uint8_t bit : mask) {
            if (bit) {
                newPixels += 1;
            }
        }
        return newPixels;
    }

    for (size_t i = 0; i < mask.size(); ++i) {
        if (mask[i] && !prevMask[i]) {
            newPixels += 1;
        }
    }
    return newPixels;
}

bool hasThermalEventTipAnchor(const ThermalEventConfig& config)
{
    return config.tip_x >= 0 && config.tip_y >= 0;
}

double computeThermalComponentAspectRatio(const ThermalEventComponent& component)
{
    const int width = std::max(1, component.maxX - component.minX + 1);
    const int height = std::max(1, component.maxY - component.minY + 1);
    const int major = std::max(width, height);
    const int minor = std::max(1, std::min(width, height));
    return static_cast<double>(major) / static_cast<double>(minor);
}

int computeThermalDistanceToTipPx(const ThermalEventComponent& component, const ThermalEventConfig& config)
{
    if (!hasThermalEventTipAnchor(config) || component.centerX < 0 || component.centerY < 0) {
        return -1;
    }

    const long long dx = static_cast<long long>(component.centerX - config.tip_x);
    const long long dy = static_cast<long long>(component.centerY - config.tip_y);
    return static_cast<int>(std::llround(std::sqrt(static_cast<double>(dx * dx + dy * dy))));
}

int computeThermalComponentScore(const ThermalEventComponent& component,
                                 const ThermalEventFrameAnalysis& analysis,
                                 const ThermalEventConfig& config)
{
    int score = 0;
    const int deltaPeak = component.peakValue - analysis.medianValue;

    if (deltaPeak >= std::max(config.seed_delta, 80)) {
        score += 2;
    }
    if (component.localContrast >= config.local_contrast_min) {
        score += 2;
    }
    if (component.aspectRatio >= config.aspect_ratio_min) {
        score += 2;
    }
    if (component.seedArea >= 2) {
        score += 1;
    }
    if (component.newPixels >= config.new_pixels_min) {
        score += 1;
    }
    if (component.area <= std::max(config.component_area_min * 4, 24)) {
        score += 1;
    }

    if (component.distanceToTipPx >= 0) {
        if (component.distanceToTipPx <= config.tip_distance_max_px) {
            score += 2;
        } else {
            score -= 2;
        }
    }

    return score;
}

bool selectDiagnosticThermalComponent(const std::vector<ThermalEventComponent>& components,
                                      ThermalEventComponent& out)
{
    bool found = false;
    long long bestScore = std::numeric_limits<long long>::min();
    for (const auto& component : components) {
        long long weightedScore = static_cast<long long>(component.score) * 1000LL
            + static_cast<long long>(component.localContrast) * 6LL
            + static_cast<long long>(component.seedArea) * 180LL
            + static_cast<long long>(component.newPixels) * 80LL
            + static_cast<long long>(component.peakValue)
            - static_cast<long long>(component.area) * 4LL;
        if (component.distanceToTipPx >= 0) {
            weightedScore -= static_cast<long long>(component.distanceToTipPx) * 12LL;
        }

        if (!found || weightedScore > bestScore) {
            bestScore = weightedScore;
            out = component;
            found = true;
        }
    }
    return found;
}

// 시작 임계값과 확장 임계값을 기준으로 고온 지점 후보 덩어리를 분리하고 각 후보의 품질 점수를 계산한다.
std::vector<ThermalEventComponent> extractThermalEventComponents(const ThermalEventFrameAnalysis& analysis,
                                                                 int seedThreshold,
                                                                 int growThreshold,
                                                                 const std::vector<uint8_t>& prevMask,
                                                                 const ThermalEventConfig& config)
{
    std::vector<uint8_t> seedMask;
    std::vector<uint8_t> growMask;
    buildThermalThresholdMasks(analysis, seedThreshold, growThreshold, seedMask, growMask);

    const int roiWidth = analysis.roi.width;
    const int roiHeight = analysis.roi.height;
    const size_t area = seedMask.size();
    std::vector<uint8_t> visited(area, 0);
    std::vector<ThermalEventComponent> components;

    for (size_t startIdx = 0; startIdx < area; ++startIdx) {
        if (!seedMask[startIdx] || visited[startIdx]) {
            continue;
        }

        ThermalEventComponent component;
        component.mask.assign(area, 0);
        component.minX = analysis.roi.x + roiWidth;
        component.minY = analysis.roi.y + roiHeight;
        component.maxX = analysis.roi.x;
        component.maxY = analysis.roi.y;
        long long sumX = 0;
        long long sumY = 0;
        std::vector<int> values;
        std::vector<int> stack;
        stack.push_back(static_cast<int>(startIdx));
        visited[startIdx] = 1;

        while (!stack.empty()) {
            const int idx = stack.back();
            stack.pop_back();
            if (!growMask[idx] || component.mask[idx]) {
                continue;
            }

            component.mask[idx] = 1;
            component.area += 1;
            if (seedMask[idx]) {
                component.seedArea += 1;
            }

            const int localX = idx % roiWidth;
            const int localY = idx / roiWidth;
            const int frameX = analysis.roi.x + localX;
            const int frameY = analysis.roi.y + localY;
            component.minX = std::min(component.minX, frameX);
            component.minY = std::min(component.minY, frameY);
            component.maxX = std::max(component.maxX, frameX);
            component.maxY = std::max(component.maxY, frameY);
            sumX += frameX;
            sumY += frameY;

            const int value = analysis.roiRawValues[idx];
            if (value >= 0) {
                component.peakValue = std::max(component.peakValue, value);
                values.push_back(value);
            }

            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) {
                        continue;
                    }
                    const int nextX = localX + dx;
                    const int nextY = localY + dy;
                    if (nextX < 0 || nextY < 0 || nextX >= roiWidth || nextY >= roiHeight) {
                        continue;
                    }
                    const int nextIdx = thermalRoiIndex(analysis.roi, nextX, nextY);
                    if (visited[nextIdx] || !growMask[nextIdx]) {
                        continue;
                    }
                    visited[nextIdx] = 1;
                    stack.push_back(nextIdx);
                }
            }
        }

        if (component.area <= 0 || values.empty()) {
            continue;
        }

        component.p90Value = thermalPercentileValue(values, 90);
        component.localRingMedian = computeThermalComponentRingMedian(
            analysis, component.mask, component.minX, component.minY, component.maxX, component.maxY);
        component.localContrast = component.peakValue - component.localRingMedian;
        component.newPixels = countThermalNewPixels(component.mask, prevMask);
        component.centerX = static_cast<int>(sumX / component.area);
        component.centerY = static_cast<int>(sumY / component.area);
        component.aspectRatio = computeThermalComponentAspectRatio(component);
        component.distanceToTipPx = computeThermalDistanceToTipPx(component, config);
        component.score = computeThermalComponentScore(component, analysis, config);
        const bool areaTooSmall = component.area < config.component_area_min;
        const bool areaTooLarge = component.area > config.component_area_max;
        const bool lowContrast = component.localContrast < config.local_contrast_min;
        const bool lowAspectRatio = component.aspectRatio < config.aspect_ratio_min;
        const bool hasTipAnchor = hasThermalEventTipAnchor(config);
        const bool missingTipAnchor = !hasTipAnchor;
        const bool tipDistanceUnknown = hasTipAnchor && component.distanceToTipPx < 0;
        const bool tooFarFromTip =
            hasTipAnchor
            && component.distanceToTipPx >= 0
            && component.distanceToTipPx > config.tip_distance_max_px;
        // 기준 팁 위치가 없으면 관심 영역 기반 탐지만 수행하고 거리 검사는 건너뛴다.
        component.valid =
            !areaTooSmall
            && !areaTooLarge
            && !lowContrast
            && !lowAspectRatio
            && (!hasTipAnchor || (!tipDistanceUnknown && !tooFarFromTip));
        if (component.valid) {
            component.rejectionReason = "ok";
        } else if (areaTooSmall) {
            component.rejectionReason = "area_too_small";
        } else if (areaTooLarge) {
            component.rejectionReason = "area_too_large";
        } else if (lowContrast) {
            component.rejectionReason = "low_contrast";
        } else if (lowAspectRatio) {
            component.rejectionReason = "low_aspect_ratio";
        } else if (missingTipAnchor) {
            component.rejectionReason = "tip_anchor_disabled";
        } else if (tipDistanceUnknown) {
            component.rejectionReason = "tip_distance_unknown";
        } else if (tooFarFromTip) {
            component.rejectionReason = "too_far_from_tip";
        } else {
            component.rejectionReason = "rejected";
        }
        components.push_back(std::move(component));
    }

    return components;
}

// 여러 고온 지점 후보 중 현재 프레임에서 가장 신뢰할 수 있는 후보 하나를 고른다.
bool selectBestThermalEventComponent(const std::vector<ThermalEventComponent>& components,
                                     const ThermalEventConfig& config,
                                     ThermalEventComponent& out)
{
    bool found = false;
    long long bestScore = std::numeric_limits<long long>::min();
    for (const auto& component : components) {
        if (!component.valid || component.score < config.score_min) {
            continue;
        }

        long long weightedScore = static_cast<long long>(component.score) * 1000LL
            + static_cast<long long>(component.localContrast) * 6LL
            + static_cast<long long>(component.seedArea) * 180LL
            + static_cast<long long>(component.newPixels) * 80LL
            + static_cast<long long>(component.peakValue)
            - static_cast<long long>(component.area) * 4LL;
        if (component.distanceToTipPx >= 0) {
            weightedScore -= static_cast<long long>(component.distanceToTipPx) * 12LL;
        }

        if (!found || weightedScore > bestScore) {
            bestScore = weightedScore;
            out = component;
            found = true;
        }
    }
    return found;
}

void updateThermalEventTrackerState(ThermalEventTrackerState& tracker,
                                    const ThermalEventConfig& config,
                                    const ThermalEventRoi& roi,
                                    const ThermalEventComponent* bestComponent,
                                    bool* cleared)
{
    if (cleared) {
        *cleared = false;
    }

    if (!sameThermalEventRoi(tracker.roi, roi) && !tracker.prevMask.empty()) {
        resetThermalEventTrackerState(tracker);
    }

    if (bestComponent && bestComponent->valid) {
        bool matched = true;
        if (tracker.lastCenterX >= 0 && tracker.lastCenterY >= 0) {
            const long long dx = static_cast<long long>(bestComponent->centerX - tracker.lastCenterX);
            const long long dy = static_cast<long long>(bestComponent->centerY - tracker.lastCenterY);
            const long long maxDistance = static_cast<long long>(config.track_match_distance_px);
            matched = (dx * dx + dy * dy) <= (maxDistance * maxDistance);
        }

        tracker.persistFrames = matched ? (tracker.persistFrames + 1) : 1;
        tracker.missFrames = 0;
        tracker.lastCenterX = bestComponent->centerX;
        tracker.lastCenterY = bestComponent->centerY;
        tracker.roi = roi;
        tracker.prevMask = bestComponent->mask;
        return;
    }

    if (tracker.persistFrames > 0 || !tracker.prevMask.empty()) {
        tracker.missFrames += 1;
        if (tracker.missFrames >= config.clear_frames) {
            resetThermalEventTrackerState(tracker);
            if (cleared) {
                *cleared = true;
            }
        }
    }
}

// 추적기가 비워져도 활성 해제 누적은 별도로 유지한다.
void updateThermalHotspotClearState(ThermalEventStats& stats,
                                    const ThermalEventConfig& config,
                                    bool analysisValid,
                                    bool hit,
                                    bool hasBestComponent,
                                    int candidateMissFrames,
                                    int observedSignalValue,
                                    int clearThreshold)
{
    if (!stats.hotspot_active) {
        stats.active_clear_miss_frames = 0;
        return;
    }

    const bool clearCandidateFrame =
        !hit
        && observedSignalValue < clearThreshold
        && (!analysisValid || !hasBestComponent || candidateMissFrames > 0);
    if (!clearCandidateFrame) {
        stats.active_clear_miss_frames = 0;
        return;
    }

    const int requiredClearFrames = std::max(1, config.clear_frames);
    // 추적기 누락 횟수와 별도로 활성 해제용 프레임 수를 누적한다.
    stats.active_clear_miss_frames = std::max(
        stats.active_clear_miss_frames + 1,
        std::max(candidateMissFrames, 0));
    if (stats.active_clear_miss_frames >= requiredClearFrames) {
        stats.hotspot_active = false;
        stats.active_clear_miss_frames = 0;
    }
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

void refreshThermalEventStatusLocked()
{
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

// 이벤트 발행이나 자동 제어가 켜져 있을 때만 MQTT 클라이언트를 지연 초기화한다.
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

// 감지 결과를 이벤트 로그와 MQTT에서 함께 소비할 수 있는 JSON 본문으로 직렬화한다.
std::string makeThermalEventPayload(const ThermalEventConfig& config,
                                    const ThermalPacketHeader& header,
                                    int active_threshold,
                                    int baseline_normal_max,
                                    int consecutive_hits,
                                    const ThermalEventFrameAnalysis& analysis,
                                    int hot_area_pixels,
                                    int hot_area_threshold,
                                    int seed_threshold,
                                    int grow_threshold,
                                    const ThermalEventComponent* best_component,
                                    int candidate_persist_frames,
                                    int candidate_miss_frames)
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
    payload["thermal"]["medianValue"] = analysis.medianValue;
    payload["thermal"]["p95Value"] = analysis.p95Value;
    payload["thermal"]["p99Value"] = analysis.p99Value;
    payload["thermal"]["madValue"] = analysis.madValue;
    payload["thermal"]["seedThreshold"] = seed_threshold;
    payload["thermal"]["growThreshold"] = grow_threshold;
    payload["thermal"]["roi"]["x"] = analysis.roi.x;
    payload["thermal"]["roi"]["y"] = analysis.roi.y;
    payload["thermal"]["roi"]["width"] = analysis.roi.width;
    payload["thermal"]["roi"]["height"] = analysis.roi.height;
    payload["thermal"]["candidate"]["persistFrames"] = candidate_persist_frames;
    payload["thermal"]["candidate"]["missFrames"] = candidate_miss_frames;
    payload["thermal"]["candidate"]["valid"] = best_component && best_component->valid;
    if (best_component) {
        payload["thermal"]["candidate"]["area"] = best_component->area;
        payload["thermal"]["candidate"]["seedArea"] = best_component->seedArea;
        payload["thermal"]["candidate"]["peakValue"] = best_component->peakValue;
        payload["thermal"]["candidate"]["p90Value"] = best_component->p90Value;
        payload["thermal"]["candidate"]["localRingMedian"] = best_component->localRingMedian;
        payload["thermal"]["candidate"]["localContrast"] = best_component->localContrast;
        payload["thermal"]["candidate"]["newPixels"] = best_component->newPixels;
        payload["thermal"]["candidate"]["aspectRatio"] = best_component->aspectRatio;
        payload["thermal"]["candidate"]["distanceToTipPx"] = best_component->distanceToTipPx;
        payload["thermal"]["candidate"]["score"] = best_component->score;
        payload["thermal"]["candidate"]["centerX"] = best_component->centerX;
        payload["thermal"]["candidate"]["centerY"] = best_component->centerY;
        payload["thermal"]["candidate"]["bbox"]["minX"] = best_component->minX;
        payload["thermal"]["candidate"]["bbox"]["minY"] = best_component->minY;
        payload["thermal"]["candidate"]["bbox"]["maxX"] = best_component->maxX;
        payload["thermal"]["candidate"]["bbox"]["maxY"] = best_component->maxY;
    }
    if (config.actuation_enabled) {
        payload["control"]["motor1Angle"] = config.motor1_angle;
        payload["control"]["motor2Angle"] = config.motor2_angle;
        payload["control"]["motor3Angle"] = config.motor3_angle;
        payload["control"]["laserEnabled"] = config.laser_enabled;
    }
    return payload.dump();
}

// 이벤트 발생 후 필요한 경우 모터와 레이저 제어 명령을 순차적으로 발행한다.
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

// 프레임 하나를 기준으로 고온 지점 이벤트를 판정하고,
// 조건을 만족하면 MQTT 이벤트 발행과 자동 제어까지 이어서 처리한다.
void maybePublishThermalEvent(const ThermalCompletedFrame& frame)
{
    if (!frame.ready) {
        return;
    }

    const ThermalPacketHeader& header = frame.header;
    const ThermalEventConfig config = loadThermalEventConfig();
    const long long now_ms = currentTimeMs();
    ThermalEventFrameAnalysis analysis{};
    (void)analyzeThermalEventFrame(frame, config, analysis);
    const int observed_signal_value = analysis.valid ? analysis.signalValue : static_cast<int>(header.maxValue);
    int baseline_normal_max = 0;
    int active_threshold = config.hotspot_threshold_max_value;
    int seed_threshold = config.hotspot_threshold_max_value;
    int grow_threshold = config.hotspot_threshold_max_value;
    int hot_area_pixels = 0;
    int consecutive_hits = 0;
    int candidate_persist_frames = 0;
    int candidate_miss_frames = 0;
    int component_count = 0;
    ThermalEventComponent best_component{};
    ThermalEventComponent diagnostic_component{};
    bool diagnostic_component_available = false;
    std::string candidate_rejection_reason = "analysis_unavailable";
    bool should_dispatch = false;

    if (!config.event_enabled && !config.actuation_enabled) {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked();
        if (frame.hasRangeData) {
            resetThermalEventTrackerState(g_thermal.event_tracker);
            g_thermal.event_stats.last_frame_max_value = header.maxValue;
            g_thermal.event_stats.last_signal_value = observed_signal_value;
            g_thermal.event_stats.last_hot_area_pixels = 0;
            g_thermal.event_stats.last_hot_area_threshold = active_threshold;
            g_thermal.event_stats.last_valid_pixels = analysis.valid ? analysis.validPixels : 0;
            g_thermal.event_stats.last_roi_min_value = analysis.valid ? analysis.roiMinValue : 0;
            g_thermal.event_stats.last_roi_max_value = analysis.valid ? analysis.roiMaxValue : 0;
            g_thermal.event_stats.last_median_value = analysis.valid ? analysis.medianValue : 0;
            g_thermal.event_stats.last_p95_value = analysis.valid ? analysis.p95Value : 0;
            g_thermal.event_stats.last_p99_value = analysis.valid ? analysis.p99Value : 0;
            g_thermal.event_stats.last_mad_value = analysis.valid ? analysis.madValue : 0;
            g_thermal.event_stats.last_seed_threshold = 0;
            g_thermal.event_stats.last_grow_threshold = 0;
            g_thermal.event_stats.last_candidate_area = 0;
            g_thermal.event_stats.last_candidate_seed_area = 0;
            g_thermal.event_stats.last_candidate_peak_value = 0;
            g_thermal.event_stats.last_candidate_p90_value = 0;
            g_thermal.event_stats.last_candidate_local_contrast = 0;
            g_thermal.event_stats.last_candidate_new_pixels = 0;
            g_thermal.event_stats.last_candidate_aspect_ratio = 0.0;
            g_thermal.event_stats.last_candidate_distance_to_tip_px = -1;
            g_thermal.event_stats.last_candidate_score = 0;
            g_thermal.event_stats.last_component_count = 0;
            g_thermal.event_stats.last_candidate_rejection_reason = "event_disabled";
            g_thermal.event_stats.last_candidate_center_x = -1;
            g_thermal.event_stats.last_candidate_center_y = -1;
            g_thermal.event_stats.last_candidate_persist_frames = 0;
            g_thermal.event_stats.last_candidate_miss_frames = 0;
            g_thermal.event_stats.active_clear_miss_frames = 0;
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
        refreshThermalEventStatusLocked();
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked();

        g_thermal.event_stats.last_frame_max_value = header.maxValue;
        g_thermal.event_stats.last_signal_value = observed_signal_value;
        g_thermal.event_stats.last_valid_pixels = analysis.valid ? analysis.validPixels : 0;
        g_thermal.event_stats.last_roi_min_value = analysis.valid ? analysis.roiMinValue : 0;
        g_thermal.event_stats.last_roi_max_value = analysis.valid ? analysis.roiMaxValue : 0;
        g_thermal.event_stats.last_median_value = analysis.valid ? analysis.medianValue : 0;
        g_thermal.event_stats.last_p95_value = analysis.valid ? analysis.p95Value : 0;
        g_thermal.event_stats.last_p99_value = analysis.valid ? analysis.p99Value : 0;
        g_thermal.event_stats.last_mad_value = analysis.valid ? analysis.madValue : 0;

        pruneThermalBaselineSamplesLocked(now_ms, config.baseline_window_ms);
        baseline_normal_max = computeThermalBaselineNormalMaxLocked();
        active_threshold = computeThermalAdaptiveThresholdLocked(config, baseline_normal_max);

        if (analysis.valid) {
            const int robustMad = std::max(1, analysis.madValue);
            grow_threshold = std::max({
                active_threshold + config.grow_delta,
                analysis.medianValue + (3 * robustMad),
                active_threshold
            });
            seed_threshold = std::max({
                active_threshold + config.seed_delta,
                analysis.p99Value,
                analysis.medianValue + (5 * robustMad)
            });
            if (grow_threshold > seed_threshold) {
                grow_threshold = seed_threshold;
            }
            hot_area_pixels = countThermalPixelsAtOrAboveThreshold(frame, analysis, grow_threshold);

            const std::vector<uint8_t> prevMask =
                sameThermalEventRoi(g_thermal.event_tracker.roi, analysis.roi)
                    ? g_thermal.event_tracker.prevMask
                    : std::vector<uint8_t>{};
            const auto components = extractThermalEventComponents(
                analysis, seed_threshold, grow_threshold, prevMask, config);
            component_count = static_cast<int>(components.size());
            diagnostic_component_available = selectDiagnosticThermalComponent(components, diagnostic_component);
            (void)selectBestThermalEventComponent(components, config, best_component);
            if (best_component.valid) {
                candidate_rejection_reason = "ok";
            } else if (!diagnostic_component_available) {
                candidate_rejection_reason = "no_components";
            } else if (!diagnostic_component.valid) {
                candidate_rejection_reason = diagnostic_component.rejectionReason;
            } else if (diagnostic_component.score < config.score_min) {
                candidate_rejection_reason = "score_below_min";
            } else {
                candidate_rejection_reason = "not_selected";
            }
            bool tracker_cleared = false;
            updateThermalEventTrackerState(
                g_thermal.event_tracker,
                config,
                analysis.roi,
                best_component.valid ? &best_component : nullptr,
                &tracker_cleared);
            candidate_persist_frames = g_thermal.event_tracker.persistFrames;
            // 해제 임계값에 도달한 프레임은 상태 초기화 뒤에도 해제 조건 판정에 반영되게 유지한다.
            candidate_miss_frames = tracker_cleared ? config.clear_frames : g_thermal.event_tracker.missFrames;
        } else {
            hot_area_pixels = 0;
            candidate_persist_frames = 0;
            candidate_miss_frames = g_thermal.event_tracker.missFrames;
            candidate_rejection_reason = "analysis_unavailable";
        }

        const int clear_threshold = computeThermalClearThreshold(config, active_threshold);
        const bool area_below_threshold =
            !analysis.valid || config.hot_area_min_pixels <= 0 || hot_area_pixels < config.hot_area_min_pixels;
        const bool can_learn_baseline =
            config.baseline_enabled
            && !g_thermal.event_stats.hotspot_active
            && observed_signal_value < clear_threshold
            && area_below_threshold
            && !best_component.valid;

        if (can_learn_baseline) {
            g_thermal.baseline_normal_samples.push_back({now_ms, observed_signal_value});
            pruneThermalBaselineSamplesLocked(now_ms, config.baseline_window_ms);
            baseline_normal_max = computeThermalBaselineNormalMaxLocked();
            active_threshold = computeThermalAdaptiveThresholdLocked(config, baseline_normal_max);
            if (analysis.valid) {
                const int robustMad = std::max(1, analysis.madValue);
                grow_threshold = std::max({
                    active_threshold + config.grow_delta,
                    analysis.medianValue + (3 * robustMad),
                    active_threshold
                });
                seed_threshold = std::max({
                    active_threshold + config.seed_delta,
                    analysis.p99Value,
                    analysis.medianValue + (5 * robustMad)
                });
                if (grow_threshold > seed_threshold) {
                    grow_threshold = seed_threshold;
                }
                hot_area_pixels = countThermalPixelsAtOrAboveThreshold(frame, analysis, grow_threshold);
            }
        }

        updateThermalBaselineStatsLocked(config, baseline_normal_max, active_threshold);
        const ThermalEventComponent& reported_component =
            best_component.valid ? best_component : diagnostic_component;
        g_thermal.event_stats.last_hot_area_pixels = hot_area_pixels;
        g_thermal.event_stats.last_hot_area_threshold = analysis.valid ? grow_threshold : active_threshold;
        g_thermal.event_stats.last_seed_threshold = analysis.valid ? seed_threshold : 0;
        g_thermal.event_stats.last_grow_threshold = analysis.valid ? grow_threshold : 0;
        g_thermal.event_stats.last_candidate_area =
            (best_component.valid || diagnostic_component_available) ? reported_component.area : 0;
        g_thermal.event_stats.last_candidate_seed_area =
            (best_component.valid || diagnostic_component_available) ? reported_component.seedArea : 0;
        g_thermal.event_stats.last_candidate_peak_value =
            (best_component.valid || diagnostic_component_available) ? reported_component.peakValue : 0;
        g_thermal.event_stats.last_candidate_p90_value =
            (best_component.valid || diagnostic_component_available) ? reported_component.p90Value : 0;
        g_thermal.event_stats.last_candidate_local_contrast =
            (best_component.valid || diagnostic_component_available) ? reported_component.localContrast : 0;
        g_thermal.event_stats.last_candidate_new_pixels =
            (best_component.valid || diagnostic_component_available) ? reported_component.newPixels : 0;
        g_thermal.event_stats.last_candidate_aspect_ratio =
            (best_component.valid || diagnostic_component_available) ? reported_component.aspectRatio : 0.0;
        g_thermal.event_stats.last_candidate_distance_to_tip_px =
            (best_component.valid || diagnostic_component_available) ? reported_component.distanceToTipPx : -1;
        g_thermal.event_stats.last_candidate_score =
            (best_component.valid || diagnostic_component_available) ? reported_component.score : 0;
        g_thermal.event_stats.last_component_count = component_count;
        g_thermal.event_stats.last_candidate_rejection_reason = candidate_rejection_reason;
        g_thermal.event_stats.last_candidate_center_x =
            (best_component.valid || diagnostic_component_available) ? reported_component.centerX : -1;
        g_thermal.event_stats.last_candidate_center_y =
            (best_component.valid || diagnostic_component_available) ? reported_component.centerY : -1;
        g_thermal.event_stats.last_candidate_persist_frames = candidate_persist_frames;
        g_thermal.event_stats.last_candidate_miss_frames = candidate_miss_frames;
        if (can_learn_baseline) {
            g_thermal.event_stats.baseline_updates += 1;
        }

        const int updated_clear_threshold = g_thermal.event_stats.current_clear_threshold_max_value;
        bool hit = false;
        if (analysis.valid) {
            const bool signal_hit = observed_signal_value >= active_threshold;
            const bool area_hit =
                config.hot_area_min_pixels <= 0 || hot_area_pixels >= config.hot_area_min_pixels;
            const bool component_hit =
                best_component.valid
                && (best_component.newPixels >= config.new_pixels_min || candidate_persist_frames > 1);
            hit = signal_hit && area_hit && component_hit;
            g_thermal.event_stats.consecutive_hits = hit ? candidate_persist_frames : 0;
            if (config.debug_log && (signal_hit || area_hit || diagnostic_component_available)) {
                const ThermalEventComponent& debug_component =
                    best_component.valid ? best_component : diagnostic_component;
                std::cout << "[THERMAL][EVENT][DEBUG] frame=" << header.frameId
                          << " signal_hit=" << signal_hit
                          << " area_hit=" << area_hit
                          << " component_hit=" << component_hit
                          << " reason=" << candidate_rejection_reason
                          << " area=" << ((best_component.valid || diagnostic_component_available) ? debug_component.area : 0)
                          << " contrast=" << ((best_component.valid || diagnostic_component_available) ? debug_component.localContrast : 0)
                          << " aspect=" << ((best_component.valid || diagnostic_component_available) ? debug_component.aspectRatio : 0.0)
                          << " tip_dist=" << ((best_component.valid || diagnostic_component_available) ? debug_component.distanceToTipPx : -1)
                          << " score=" << ((best_component.valid || diagnostic_component_available) ? debug_component.score : 0)
                          << " persist=" << candidate_persist_frames
                          << std::endl;
            }
        } else {
            const bool signal_hit = observed_signal_value >= active_threshold;
            hit = signal_hit;
            if (hit) {
                g_thermal.event_stats.consecutive_hits += 1;
            } else {
                g_thermal.event_stats.consecutive_hits = 0;
            }
        }

        updateThermalHotspotClearState(g_thermal.event_stats,
                                       config,
                                       analysis.valid,
                                       hit,
                                       best_component.valid,
                                       candidate_miss_frames,
                                       observed_signal_value,
                                       updated_clear_threshold);
        consecutive_hits = g_thermal.event_stats.consecutive_hits;
        // 기준선을 사용하는 모드에서는 준비가 끝난 뒤에만 이벤트를 발행한다.
        const bool baseline_ready_for_dispatch =
            !config.baseline_enabled || g_thermal.event_stats.baseline_ready;

        if (!g_thermal.event_stats.hotspot_active
            && hit
            && baseline_ready_for_dispatch
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
    if (!analysis.valid) {
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
                                                        analysis.valid ? grow_threshold : active_threshold,
                                                        seed_threshold,
                                                        grow_threshold,
                                                        best_component.valid ? &best_component : nullptr,
                                                        candidate_persist_frames,
                                                        candidate_miss_frames);
    const bool publish_ok = config.event_enabled && mqtt && mqtt->publishMessage(config.event_topic, payload);
    const bool actuation_ok = config.actuation_enabled && mqtt && publishThermalActuation(mqtt, config);
    const bool mark_hotspot_active =
        (config.event_enabled && publish_ok) || (config.actuation_enabled && actuation_ok);

    if (publish_ok) {
        std::string event_title = config.title;
        std::string event_message = payload;
        const auto payload_json = crow::json::load(payload);
        if (payload_json) {
            if (payload_json.has("title")) {
                event_title = payload_json["title"].s();
            }
            if (payload_json.has("message")) {
                event_message = payload_json["message"].s();
            }
        }

        EventLogInsertParams log_params;
        log_params.source = config.source;
        log_params.event_type = "thermal_hotspot";
        log_params.severity = config.severity;
        log_params.title = event_title;
        log_params.message = event_message;
        log_params.frame_id = static_cast<int>(header.frameId);
        log_params.signal_value = observed_signal_value;
        log_params.threshold_value = active_threshold;
        log_params.hot_area_pixels = hot_area_pixels;
        log_params.candidate_area = best_component.valid ? std::optional<int>(best_component.area) : std::nullopt;
        log_params.center_x = best_component.valid ? std::optional<int>(best_component.centerX) : std::nullopt;
        log_params.center_y = best_component.valid ? std::optional<int>(best_component.centerY) : std::nullopt;
        log_params.action_requested = config.actuation_enabled;
        if (config.actuation_enabled) {
            // 서버 자동 제어 사용 여부도 이벤트 메타데이터에 함께 남긴다.
            log_params.action_type = std::string("server_auto_control");
            log_params.action_result = actuation_ok ? std::optional<std::string>("success")
                                                    : std::optional<std::string>("failed");
            log_params.action_message = actuation_ok ? std::optional<std::string>("Thermal auto control executed")
                                                     : std::optional<std::string>("Thermal auto control failed");
        }
        log_params.payload_json = payload;

        if (!insertEventLog(log_params)) {
            std::cerr << "[THERMAL][EVENT] failed to persist event log for frame=" << header.frameId << std::endl;
        }
    }

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
            g_thermal.event_stats.last_event_candidate_area = best_component.valid ? best_component.area : 0;
            g_thermal.event_stats.last_event_candidate_local_contrast =
                best_component.valid ? best_component.localContrast : 0;
            g_thermal.event_stats.last_event_candidate_aspect_ratio =
                best_component.valid ? best_component.aspectRatio : 0.0;
            g_thermal.event_stats.last_event_candidate_distance_to_tip_px =
                best_component.valid ? best_component.distanceToTipPx : -1;
            g_thermal.event_stats.last_event_candidate_score =
                best_component.valid ? best_component.score : 0;
            g_thermal.event_stats.last_event_candidate_persist_frames = candidate_persist_frames;
            g_thermal.event_stats.last_event_threshold_max_value = active_threshold;
            g_thermal.event_stats.last_event_at_ms = now_ms;
        }
        if (config.actuation_enabled) {
            g_thermal.event_stats.actuation_requests += 1;
        }
        if (mark_hotspot_active) {
            g_thermal.event_stats.hotspot_active = true;
            g_thermal.event_stats.active_clear_miss_frames = 0;
        }
    }

    if (config.event_enabled && !publish_ok) {
        std::cerr << "[THERMAL][EVENT] failed to publish MQTT event for frame=" << header.frameId
                  << " max=" << header.maxValue
                  << " signal=" << observed_signal_value
                  << " hot_area=" << hot_area_pixels
                  << " candidate_area=" << (best_component.valid ? best_component.area : 0)
                  << " contrast=" << (best_component.valid ? best_component.localContrast : 0)
                  << " aspect=" << (best_component.valid ? best_component.aspectRatio : 0.0)
                  << " tip_dist=" << (best_component.valid ? best_component.distanceToTipPx : -1)
                  << " score=" << (best_component.valid ? best_component.score : 0)
                  << std::endl;
    }

    if (publish_ok) {
        std::cout << "[THERMAL][EVENT] published frame=" << header.frameId
                  << " max=" << header.maxValue
                  << " signal=" << observed_signal_value
                  << " baseline=" << baseline_normal_max
                  << " threshold=" << active_threshold
                  << " hot_area=" << hot_area_pixels
                  << " candidate_area=" << (best_component.valid ? best_component.area : 0)
                  << " contrast=" << (best_component.valid ? best_component.localContrast : 0)
                  << " aspect=" << (best_component.valid ? best_component.aspectRatio : 0.0)
                  << " tip_dist=" << (best_component.valid ? best_component.distanceToTipPx : -1)
                  << " score=" << (best_component.valid ? best_component.score : 0)
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
    resetThermalEventTrackerState(g_thermal.event_tracker);
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
    refreshThermalEventStatusLocked();
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
    g_thermal.event_stats.last_median_value = 0;
    g_thermal.event_stats.last_p95_value = 0;
    g_thermal.event_stats.last_p99_value = 0;
    g_thermal.event_stats.last_mad_value = 0;
    g_thermal.event_stats.last_seed_threshold = 0;
    g_thermal.event_stats.last_grow_threshold = 0;
    g_thermal.event_stats.last_candidate_area = 0;
    g_thermal.event_stats.last_candidate_seed_area = 0;
    g_thermal.event_stats.last_candidate_peak_value = 0;
    g_thermal.event_stats.last_candidate_p90_value = 0;
    g_thermal.event_stats.last_candidate_local_contrast = 0;
    g_thermal.event_stats.last_candidate_new_pixels = 0;
    g_thermal.event_stats.last_candidate_aspect_ratio = 0.0;
    g_thermal.event_stats.last_candidate_distance_to_tip_px = -1;
    g_thermal.event_stats.last_candidate_score = 0;
    g_thermal.event_stats.last_component_count = 0;
    g_thermal.event_stats.last_candidate_rejection_reason.clear();
    g_thermal.event_stats.last_candidate_center_x = -1;
    g_thermal.event_stats.last_candidate_center_y = -1;
    g_thermal.event_stats.last_candidate_persist_frames = 0;
    g_thermal.event_stats.last_candidate_miss_frames = 0;
    g_thermal.event_stats.active_clear_miss_frames = 0;
    g_thermal.event_stats.event_attempts = 0;
    g_thermal.event_stats.events_published = 0;
    g_thermal.event_stats.actuation_requests = 0;
    g_thermal.event_stats.last_event_attempt_frame_id = 0;
    g_thermal.event_stats.last_event_attempt_at_ms = 0;
    g_thermal.event_stats.last_event_frame_id = 0;
    g_thermal.event_stats.last_event_max_value = 0;
    g_thermal.event_stats.last_event_signal_value = 0;
    g_thermal.event_stats.last_event_hot_area_pixels = 0;
    g_thermal.event_stats.last_event_candidate_area = 0;
    g_thermal.event_stats.last_event_candidate_local_contrast = 0;
    g_thermal.event_stats.last_event_candidate_aspect_ratio = 0.0;
    g_thermal.event_stats.last_event_candidate_distance_to_tip_px = -1;
    g_thermal.event_stats.last_event_candidate_score = 0;
    g_thermal.event_stats.last_event_candidate_persist_frames = 0;
    g_thermal.event_stats.last_event_threshold_max_value = 0;
    g_thermal.event_stats.last_event_at_ms = 0;
    g_thermal.event_stats.consecutive_hits = 0;
    g_thermal.event_stats.baseline_ready = false;
    g_thermal.event_stats.hotspot_active = false;
    g_thermal.event_stats.last_publish_ok = false;
    g_thermal.event_stats.last_actuation_ok = false;
    g_thermal.event_stats.last_event_message.clear();
}

// 현재 연결된 모든 웹소켓 클라이언트에게 정규화된 열화상 조각을 전송한다.
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

// 디버깅과 운영 관찰을 위해 현재 열화상 파이프라인 상태를 JSON으로 노출한다.
crow::json::wvalue makeThermalStatusJson()
{
    const ThermalEventConfig config = loadThermalEventConfig();
    const ThermalEventRoi roi = normalizeThermalEventRoi(config);
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
    response["monitor_always_on"] = config.monitor_always_on;
    response["event"]["enabled"] = config.event_enabled;
    response["event"]["actuation_enabled"] = config.actuation_enabled;
    response["event"]["baseline_enabled"] = config.baseline_enabled;
    response["event"]["debug_log"] = config.debug_log;
    response["event"]["broker_connected"] = g_thermal.event_stats.broker_connected;
    response["event"]["topic"] = config.event_topic;
    response["event"]["hotspot_threshold_max_value"] = config.hotspot_threshold_max_value;
    response["event"]["cooldown_ms"] = config.cooldown_ms;
    response["event"]["baseline_margin"] = config.baseline_margin;
    response["event"]["baseline_window_ms"] = config.baseline_window_ms;
    response["event"]["baseline_min_samples"] = config.baseline_min_samples;
    response["event"]["baseline_guard_delta"] = config.baseline_guard_delta;
    response["event"]["consecutive_frames_required"] = config.consecutive_frames_required;
    response["event"]["signal_percentile"] = config.signal_percentile;
    response["event"]["hot_area_min_pixels"] = config.hot_area_min_pixels;
    response["event"]["seed_delta"] = config.seed_delta;
    response["event"]["grow_delta"] = config.grow_delta;
    response["event"]["component_area_min"] = config.component_area_min;
    response["event"]["component_area_max"] = config.component_area_max;
    response["event"]["local_contrast_min"] = config.local_contrast_min;
    response["event"]["new_pixels_min"] = config.new_pixels_min;
    response["event"]["clear_frames"] = config.clear_frames;
    response["event"]["track_match_distance_px"] = config.track_match_distance_px;
    response["event"]["aspect_ratio_min"] = config.aspect_ratio_min;
    response["event"]["tip"]["x"] = config.tip_x;
    response["event"]["tip"]["y"] = config.tip_y;
    response["event"]["tip"]["distance_max_px"] = config.tip_distance_max_px;
    response["event"]["score_min"] = config.score_min;
    response["event"]["roi"]["x"] = roi.x;
    response["event"]["roi"]["y"] = roi.y;
    response["event"]["roi"]["width"] = roi.width;
    response["event"]["roi"]["height"] = roi.height;
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
    response["event"]["last_median_value"] = g_thermal.event_stats.last_median_value;
    response["event"]["last_p95_value"] = g_thermal.event_stats.last_p95_value;
    response["event"]["last_p99_value"] = g_thermal.event_stats.last_p99_value;
    response["event"]["last_mad_value"] = g_thermal.event_stats.last_mad_value;
    response["event"]["last_seed_threshold"] = g_thermal.event_stats.last_seed_threshold;
    response["event"]["last_grow_threshold"] = g_thermal.event_stats.last_grow_threshold;
    response["event"]["last_candidate_area"] = g_thermal.event_stats.last_candidate_area;
    response["event"]["last_candidate_seed_area"] = g_thermal.event_stats.last_candidate_seed_area;
    response["event"]["last_candidate_peak_value"] = g_thermal.event_stats.last_candidate_peak_value;
    response["event"]["last_candidate_p90_value"] = g_thermal.event_stats.last_candidate_p90_value;
    response["event"]["last_candidate_local_contrast"] = g_thermal.event_stats.last_candidate_local_contrast;
    response["event"]["last_candidate_new_pixels"] = g_thermal.event_stats.last_candidate_new_pixels;
    response["event"]["last_candidate_aspect_ratio"] = g_thermal.event_stats.last_candidate_aspect_ratio;
    response["event"]["last_candidate_distance_to_tip_px"] = g_thermal.event_stats.last_candidate_distance_to_tip_px;
    response["event"]["last_candidate_score"] = g_thermal.event_stats.last_candidate_score;
    response["event"]["last_component_count"] = g_thermal.event_stats.last_component_count;
    response["event"]["last_candidate_rejection_reason"] = g_thermal.event_stats.last_candidate_rejection_reason;
    response["event"]["last_candidate_center_x"] = g_thermal.event_stats.last_candidate_center_x;
    response["event"]["last_candidate_center_y"] = g_thermal.event_stats.last_candidate_center_y;
    response["event"]["last_candidate_persist_frames"] = g_thermal.event_stats.last_candidate_persist_frames;
    response["event"]["last_candidate_miss_frames"] = g_thermal.event_stats.last_candidate_miss_frames;
    response["event"]["active_clear_miss_frames"] = g_thermal.event_stats.active_clear_miss_frames;
    response["event"]["last_event_attempt_frame_id"] = static_cast<int>(g_thermal.event_stats.last_event_attempt_frame_id);
    response["event"]["last_event_attempt_at_ms"] = g_thermal.event_stats.last_event_attempt_at_ms;
    response["event"]["last_event_frame_id"] = static_cast<int>(g_thermal.event_stats.last_event_frame_id);
    response["event"]["last_event_max_value"] = static_cast<int>(g_thermal.event_stats.last_event_max_value);
    response["event"]["last_event_signal_value"] = g_thermal.event_stats.last_event_signal_value;
    response["event"]["last_event_hot_area_pixels"] = g_thermal.event_stats.last_event_hot_area_pixels;
    response["event"]["last_event_candidate_area"] = g_thermal.event_stats.last_event_candidate_area;
    response["event"]["last_event_candidate_local_contrast"] = g_thermal.event_stats.last_event_candidate_local_contrast;
    response["event"]["last_event_candidate_aspect_ratio"] = g_thermal.event_stats.last_event_candidate_aspect_ratio;
    response["event"]["last_event_candidate_distance_to_tip_px"] = g_thermal.event_stats.last_event_candidate_distance_to_tip_px;
    response["event"]["last_event_candidate_score"] = g_thermal.event_stats.last_event_candidate_score;
    response["event"]["last_event_candidate_persist_frames"] = g_thermal.event_stats.last_event_candidate_persist_frames;
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

// 백그라운드 UDP 수신 스레드의 본체다.
// 수신 -> 정규화 -> 프레임 재조립/통계 갱신 -> 이벤트 판정 -> 웹소켓 브로드캐스트 순서로 동작한다.
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

        // 네트워크에서 받은 원본 UDP 본문을 내부 공통 형식으로 먼저 맞춘다.
        ThermalNormalizedPacket normalized{};
        if (!normalizeThermalPayload(payload, sender_key, now_ms, frame_timeout_ms, normalizer, normalized)) {
            noteInvalidPacketStats(payload.size(), sender_text, enable_frame_stats, stats_log_interval_ms);
            continue;
        }

        // 프레임이 완성되면 이벤트 판정과 웹소켓 브로드캐스트가 같은 루프 안에서 이어진다.
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

// 수신 스레드가 꺼져 있으면 한 번만 시작한다.
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

// 상시 모니터링이 꺼져 있고 클라이언트도 없을 때만 수신 스레드 종료를 요청한다.
void requestThermalReceiverStopIfIdle()
{
    const ThermalEventConfig config = loadThermalEventConfig();
    std::lock_guard<std::mutex> receiver_lock(g_thermal.receiver_mutex);
    std::lock_guard<std::mutex> state_lock(g_thermal.state_mutex);
    if (config.monitor_always_on) {
        return;
    }
    if (!g_thermal.clients.empty()) {
        return;
    }
    g_thermal.stop_requested.store(true);
}
}  // 익명 네임스페이스

// 열화상 제어용 REST와 실시간 스트리밍용 웹소켓 라우트를 등록한다.
void registerThermalProxyRoutes(crow::SimpleApp& app)
{
    const ThermalEventConfig config = loadThermalEventConfig();
    {
        std::lock_guard<std::mutex> lock(g_thermal.state_mutex);
        refreshThermalEventStatusLocked();
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
            // 열화상 스트림은 서버 -> 클라이언트 단방향 전송만 허용한다.
        });
}

// 프로세스 종료 시 수신 스레드를 join하고 MQTT 리소스를 함께 해제한다.
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
