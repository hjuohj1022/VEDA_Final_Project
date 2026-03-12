#pragma once

#include "crow.h"
#include "MqttManager.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

struct MotorCommandResult {
    bool ok = false;
    bool published = false;
    bool timed_out = false;
    bool broker_connected = false;
    std::string command;
    std::string response;
};

struct MotorStatusSnapshot {
    bool broker_connected = false;
    bool awaiting_response = false;
    std::string control_topic;
    std::string response_topic;
    int timeout_ms = 0;
    std::string last_command;
    std::string last_response;
    bool last_response_is_error = false;
    std::uint64_t response_sequence = 0;
    long long last_response_age_ms = -1;
};

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

    MotorCommandResult sendCommand(const std::string& command);
    MotorCommandResult press(int motor, const std::string& direction);
    MotorCommandResult release(int motor);
    MotorCommandResult stop(int motor);
    MotorCommandResult setAngle(int motor, int angle);
    MotorCommandResult moveRelative(int motor, const std::string& direction, int degrees);
    MotorCommandResult readAngles();
    MotorCommandResult ping();
    MotorCommandResult stopAll();
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
    std::string last_response_;
    bool last_response_is_error_ = false;
    std::uint64_t response_sequence_ = 0;
    std::chrono::steady_clock::time_point last_response_time_{};
};

void registerMotorRoutes(crow::SimpleApp& app, MotorManager& motor_mgr);
