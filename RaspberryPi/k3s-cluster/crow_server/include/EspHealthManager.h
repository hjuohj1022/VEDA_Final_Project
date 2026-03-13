#pragma once

#include "crow.h"
#include "MqttManager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// watchdog 제어 요청 결과의 API 응답 표현용 구조체.
struct EspHealthCommandResult {
    bool published = false;
    bool broker_connected = false;
    std::string request_id;
    std::string control_topic;
    std::string payload;
    std::string message;
};

// 최신 watchdog 상태 메시지의 API 노출용 스냅샷.
struct EspHealthStatusSnapshot {
    bool broker_connected = false;
    bool has_status = false;
    bool latest_status_valid_json = false;
    std::string control_topic;
    std::string status_topic;
    std::string last_request_id;
    std::string last_request_payload;
    std::string latest_status_json;
    std::uint64_t status_sequence = 0;
    long long last_status_age_ms = -1;
};

// 역할:
// - ESP32 watchdog 제어 명령의 MQTT 발행
// - status topic 최신 payload의 메모리 보관
// - REST 라우트 사용을 위한 스냅샷 형태 상태 정리
class EspHealthManager {
public:
    EspHealthManager(const std::string& broker_host,
                     int broker_port,
                     const std::string& client_id,
                     const std::string& control_topic,
                     const std::string& status_topic);
    ~EspHealthManager() = default;

    EspHealthManager(const EspHealthManager&) = delete;
    EspHealthManager& operator=(const EspHealthManager&) = delete;

    // ESP32 즉시 상태 발행 요청용 MQTT 명령 전송.
    EspHealthCommandResult requestPublishNow();
    // 최근 요청/상태 정보의 읽기 전용 스냅샷 반환.
    EspHealthStatusSnapshot getStatusSnapshot() const;

private:
    void handleMessage(const std::string& topic, const std::string& payload);
    std::string generateRequestId() const;

    std::unique_ptr<MqttManager> mqtt_;
    std::string control_topic_;
    std::string status_topic_;

    mutable std::mutex state_mutex_;
    std::string last_request_id_;
    std::string last_request_payload_;
    std::string latest_status_json_;
    bool latest_status_valid_json_ = false;
    std::uint64_t status_sequence_ = 0;
    std::chrono::steady_clock::time_point last_status_time_{};
};

// EspHealthManager 기반 watchdog 상태 조회/요청 REST 라우트 등록.
void registerEspHealthRoutes(crow::SimpleApp& app, EspHealthManager& esp_health_mgr);
