#pragma once

#include "crow.h"

#include "infra/mqtt/MqttManager.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

// Captures the result of publishing a watchdog command to ESP32 over MQTT.
struct EspHealthCommandResult {
    bool published = false;
    bool broker_connected = false;
    std::string request_id;
    std::string control_topic;
    std::string payload;
    std::string message;
};

// Captures the latest watchdog command and status data exposed through REST.
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

// Tracks ESP32 watchdog command/status state on top of a dedicated MQTT client.
class EspHealthManager {
public:
    // Connects an MQTT client to the ESP32 watchdog topics.
    EspHealthManager(const std::string& broker_host,
                     int broker_port,
                     const std::string& client_id,
                     const std::string& control_topic,
                     const std::string& status_topic);
    // Uses the default cleanup behavior because owned members already manage their lifetime.
    ~EspHealthManager() = default;

    EspHealthManager(const EspHealthManager&) = delete;
    EspHealthManager& operator=(const EspHealthManager&) = delete;

    // Publishes a "report status now" command to the watchdog control topic.
    EspHealthCommandResult requestPublishNow();
    // Returns a snapshot of the last command and most recent status message.
    EspHealthStatusSnapshot getStatusSnapshot() const;

private:
    // Handles messages arriving on the subscribed watchdog topics.
    void handleMessage(const std::string& topic, const std::string& payload);
    // Generates a request identifier for correlating outgoing commands with later logs.
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

// Registers REST routes that expose ESP32 watchdog state and trigger commands.
void registerEspHealthRoutes(crow::SimpleApp& app, EspHealthManager& esp_health_mgr);
