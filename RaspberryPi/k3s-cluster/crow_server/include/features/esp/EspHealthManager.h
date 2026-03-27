#pragma once

#include "crow.h"

#include "infra/mqtt/MqttManager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// ESP32 워치독 제어 명령을 MQTT로 발행한 결과를 담는다.
// REST 응답 계층은 이 구조체를 그대로 직렬화해 호출자에게 전달한다.
struct EspHealthCommandResult {
    bool published = false;
    bool broker_connected = false;
    std::string request_id;
    std::string control_topic;
    std::string payload;
    std::string message;
};

// 최근 워치독 요청/응답 상태를 한 번에 조회하기 위한 진단용 스냅샷이다.
// 운영 중 브로커 연결 상태, 마지막 요청, 마지막 상태 메시지를 한 응답에 묶어 보여준다.
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

// ESP32 워치독 관련 MQTT 흐름을 감싸는 서비스 객체다.
// 제어 메시지 발행과 상태 토픽 구독을 동시에 관리하며, REST 라우트가 재사용할 상태를 메모리에 보관한다.
class EspHealthManager {
public:
    // 워치독 제어 토픽과 상태 토픽에 연결할 전용 MQTT 클라이언트를 준비한다.
    EspHealthManager(const std::string& broker_host,
                     int broker_port,
                     const std::string& client_id,
                     const std::string& control_topic,
                     const std::string& status_topic);
    // 내부에서 unique_ptr 등 소유 객체가 자동 정리되므로 기본 소멸자를 사용한다.
    ~EspHealthManager() = default;

    EspHealthManager(const EspHealthManager&) = delete;
    EspHealthManager& operator=(const EspHealthManager&) = delete;

    // ESP32에게 "지금 즉시 상태를 다시 발행하라"는 제어 메시지를 보낸다.
    EspHealthCommandResult requestPublishNow();
    // 최근 요청 정보와 마지막 상태 메시지를 하나의 스냅샷으로 반환한다.
    EspHealthStatusSnapshot getStatusSnapshot() const;

private:
    // 구독 중인 상태/제어 토픽 메시지를 받아 내부 최신 상태를 갱신한다.
    void handleMessage(const std::string& topic, const std::string& payload);
    // 요청과 로그를 연동하기 위한 증가형 요청 식별자를 생성한다.
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

// ESP32 워치독 상태 조회와 강제 상태 발행 요청을 위한 REST 라우트를 등록한다.
void registerEspHealthRoutes(crow::SimpleApp& app, EspHealthManager& esp_health_mgr);
