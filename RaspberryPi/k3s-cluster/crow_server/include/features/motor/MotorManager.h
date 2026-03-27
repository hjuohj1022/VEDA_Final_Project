#pragma once

#include "crow.h"

#include "infra/mqtt/MqttManager.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Captures the result of one REST-to-MQTT motor command exchange.
struct MotorCommandResult {
    bool ok = false;
    bool published = false;
    bool timed_out = false;
    bool broker_connected = false;
    std::string command;
    std::string response;
};

// Captures the most recent motor control state exposed through diagnostics endpoints.
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

// Publishes motor-control commands over MQTT and waits for correlated responses.
class MotorManager {
public:
    // Connects a dedicated MQTT client to the motor control and response topics.
    MotorManager(const std::string& broker_host,
                 int broker_port,
                 const std::string& client_id,
                 const std::string& control_topic,
                 const std::string& response_topic,
                 int timeout_ms);
    // Uses the default cleanup behavior because owned members already manage their lifetime.
    ~MotorManager() = default;

    MotorManager(const MotorManager&) = delete;
    MotorManager& operator=(const MotorManager&) = delete;

    // Publishes a raw motor command to the default control topic and waits for the response.
    MotorCommandResult sendCommand(const std::string& command);
    // Publishes a raw motor command to an explicit topic and waits for the response.
    MotorCommandResult sendCommandToTopic(const std::string& control_topic, const std::string& command);
    // Sends a press command for the selected motor and direction.
    MotorCommandResult press(int motor, const std::string& direction);
    // Sends a release command for the selected motor.
    MotorCommandResult release(int motor);
    // Sends a stop command for the selected motor.
    MotorCommandResult stop(int motor);
    // Sends an absolute angle set command for the selected motor.
    MotorCommandResult setAngle(int motor, int angle);
    // Sends a speed set command for the selected motor.
    MotorCommandResult setSpeed(int motor, int speed);
    // Sends a relative movement command for the selected motor.
    MotorCommandResult moveRelative(int motor, const std::string& direction, int degrees);
    // Requests the current angle values from the motor controller.
    MotorCommandResult readAngles();
    // Sends a health-check ping to the motor controller.
    MotorCommandResult ping();
    // Sends the emergency stop-all command.
    MotorCommandResult stopAll();
    // Returns the latest request/response state used by the diagnostics routes.
    MotorStatusSnapshot getStatus() const;

private:
    // Handles MQTT responses arriving from the motor controller.
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

// Registers REST routes that expose motor control operations and diagnostics.
void registerMotorRoutes(crow::SimpleApp& app, MotorManager& motor_mgr);
// Registers REST routes that reuse the motor MQTT pipeline for laser on/off commands.
void registerLaserRoutes(crow::SimpleApp& app, MotorManager& motor_mgr, const std::string& laser_control_topic);
