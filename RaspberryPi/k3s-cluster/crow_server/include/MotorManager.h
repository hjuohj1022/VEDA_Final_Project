#pragma once

#include "crow.h"
#include "MqttManager.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// 모터 명령 1회 실행 결과의 REST 응답 표현용 구조체.
struct MotorCommandResult {
    bool ok = false;
    bool published = false;
    bool timed_out = false;
    bool broker_connected = false;
    std::string command;
    std::string response;
};

// 최근 모터 제어 상태의 진단용 API 노출 스냅샷.
struct MotorStatusSnapshot {
    bool broker_connected = false;
    bool awaiting_response = false;
    std::string control_topic;
    std::string response_topic;
    int timeout_ms = 0;
    std::string last_command;
    std::string last_command_topic;
    std::string last_response;
    bool last_response_is_error = false;
    std::uint64_t response_sequence = 0;
    long long last_response_age_ms = -1;
};

// 역할:
// - REST 요청의 MQTT 제어 명령 변환
// - 응답 topic 메시지 대기 후 동기식 결과 형태 반환
// - 최근 명령/응답 상태의 진단용 보관
//
// 동시성:
// - request_mutex_ 기반 단일 요청 직렬화
// - state_mutex_ 기반 응답 도착 여부와 마지막 상태 보호
class MotorManager {
public:
    MotorManager(const std::string& broker_host,
                 int broker_port,
                 const std::string& client_id,
                 const std::string& control_topic,
                 const std::string& response_topic,
                 int timeout_ms);
    ~MotorManager() = default;

    MotorManager(const MotorManager&) = delete;
    MotorManager& operator=(const MotorManager&) = delete;

    // MQTT publish 후 response topic 응답 또는 timeout까지 대기.
    MotorCommandResult sendCommand(const std::string& command);
    MotorCommandResult sendCommandToTopic(const std::string& control_topic, const std::string& command);
    MotorCommandResult press(int motor, const std::string& direction);
    MotorCommandResult release(int motor);
    MotorCommandResult stop(int motor);
    MotorCommandResult setAngle(int motor, int angle);
    MotorCommandResult moveRelative(int motor, const std::string& direction, int degrees);
    MotorCommandResult readAngles();
    MotorCommandResult ping();
    MotorCommandResult stopAll();
    // 최근 상태 복사본의 라우트 전달용 반환.
    MotorStatusSnapshot getStatus() const;

private:
    void handleMessage(const std::string& topic, const std::string& payload);

    std::unique_ptr<MqttManager> mqtt_;
    std::string control_topic_;
    std::string response_topic_;
    int timeout_ms_ = 3000;

    mutable std::mutex state_mutex_;
    std::mutex request_mutex_;
    std::condition_variable response_cv_;

    bool awaiting_response_ = false;
    std::string last_command_;
    std::string last_command_topic_;
    std::string last_response_;
    bool last_response_is_error_ = false;
    std::uint64_t response_sequence_ = 0;
    std::chrono::steady_clock::time_point last_response_time_{};
};

// MotorManager 기반 모터 제어/상태 REST 라우트 등록.
void registerMotorRoutes(crow::SimpleApp& app, MotorManager& motor_mgr);
// MotorManager 기반 레이저 on/off 테스트 REST 라우트 등록.
void registerLaserRoutes(crow::SimpleApp& app, MotorManager& motor_mgr, const std::string& laser_control_topic);
