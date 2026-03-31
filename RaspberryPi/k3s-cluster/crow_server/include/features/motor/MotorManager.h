#pragma once

#include "crow.h"

#include "infra/mqtt/MqttManager.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// REST 요청 하나가 MQTT 명령 발행과 응답 수신을 거쳐 끝난 결과를 담는다.
// 상위 API 계층은 성공 여부, 시간 초과 여부, 마지막 응답 문자열을 이 구조체로 전달받는다.
struct MotorCommandResult {
    bool ok = false;
    bool published = false;
    bool timed_out = false;
    bool broker_connected = false;
    std::string command;
    std::string response;
};

// 최근 모터 제어 상태를 외부 진단용으로 노출하기 위한 스냅샷이다.
// 마지막 명령, 마지막 응답, 브로커 연결 여부, 응답 지연 시간 등을 함께 담는다.
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

// 모터 제어 명령을 MQTT로 보내고, 대응 응답이 도착할 때까지 기다리는 서비스 객체다.
// REST 라우트는 입력 검증에 집중하고, 실제 요청-응답 동기화와 상태 기록은 이 클래스가 맡는다.
class MotorManager {
public:
    // 모터 제어 토픽과 응답 토픽을 감시할 전용 MQTT 클라이언트를 준비한다.
    MotorManager(const std::string& broker_host,
                 int broker_port,
                 const std::string& client_id,
                 const std::string& control_topic,
                 const std::string& response_topic,
                 int timeout_ms);
    // 내부 소유 객체가 자동 정리되므로 기본 소멸자를 사용한다.
    ~MotorManager() = default;

    MotorManager(const MotorManager&) = delete;
    MotorManager& operator=(const MotorManager&) = delete;

    // 기본 제어 토픽으로 원시 모터 명령 문자열을 보내고 응답을 기다린다.
    MotorCommandResult sendCommand(const std::string& command);
    // 호출자가 토픽을 직접 지정해 원시 명령을 보내고 응답을 기다린다.
    MotorCommandResult sendCommandToTopic(const std::string& control_topic, const std::string& command);
    // 지정한 모터를 누르는(press) 제어 명령을 보낸다.
    MotorCommandResult press(int motor, const std::string& direction);
    // 지정한 모터의 누름 상태를 해제하는(release) 명령을 보낸다.
    MotorCommandResult release(int motor);
    // 지정한 모터를 즉시 정지시키는 명령을 보낸다.
    MotorCommandResult stop(int motor);
    // 지정한 모터를 절대 각도로 이동시키는 명령을 보낸다.
    MotorCommandResult setAngle(int motor, int angle);
    // 지정한 모터의 속도를 설정하는 명령을 보낸다.
    MotorCommandResult setSpeed(int motor, int speed);
    // 지정한 모터를 상대 각도 기준으로 이동시키는 명령을 보낸다.
    MotorCommandResult moveRelative(int motor, const std::string& direction, int degrees);
    // 현재 각도 값을 읽어오라는 명령을 보낸다.
    MotorCommandResult readAngles();
    // 제어 보드가 살아 있는지 확인하기 위한 ping 명령을 보낸다.
    MotorCommandResult ping();
    // 모든 모터를 긴급 정지시키는 명령을 보낸다.
    MotorCommandResult stopAll();
    // 가장 최근의 요청/응답 상태를 진단용 스냅샷으로 반환한다.
    MotorStatusSnapshot getStatus() const;

private:
    // 응답 토픽에서 도착한 MQTT 메시지를 내부 상태와 대기 중 요청에 반영한다.
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

// 모터 제어와 상태 확인용 REST 라우트를 등록한다.
void registerMotorRoutes(crow::SimpleApp& app, MotorManager& motor_mgr);
// 레이저 켜기/끄기 명령을 모터 제어 파이프라인과 동일한 MQTT 흐름으로 처리하는 라우트를 등록한다.
void registerLaserRoutes(crow::SimpleApp& app, MotorManager& motor_mgr, const std::string& laser_control_topic);
