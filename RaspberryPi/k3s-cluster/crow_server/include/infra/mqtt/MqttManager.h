#pragma once

#include <mosquittopp.h>

#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Keeps the process-wide mosquitto library initialization paired with process shutdown.
class MqttLibraryGuard {
public:
    // Initializes the global mosquitto library state once for the process.
    MqttLibraryGuard();
    // Releases the global mosquitto library state when the process exits.
    ~MqttLibraryGuard();

    MqttLibraryGuard(const MqttLibraryGuard&) = delete;
    MqttLibraryGuard& operator=(const MqttLibraryGuard&) = delete;
};

// Wraps a mosquittopp client with reconnect-aware subscriptions and message callbacks.
class MqttManager : public mosqpp::mosquittopp {
public:
    using MessageCallback = std::function<void(const std::string&, const std::string&)>;

    // Connects to the configured broker and starts the background network loop.
    MqttManager(const char* id, const char* host, int port);
    // Stops the network loop and disconnects from the broker.
    ~MqttManager();

    // Publishes a payload immediately if the broker connection is available.
    bool publishMessage(const std::string& topic, const std::string& payload);
    // Records and subscribes to a topic so it can be re-subscribed after reconnects.
    bool subscribeTopic(const std::string& topic, int qos = 0);

    // Registers the callback invoked when a subscribed MQTT message arrives.
    void set_message_callback(MessageCallback cb) { message_cb_ = std::move(cb); }
    // Reports whether the underlying broker connection is currently established.
    bool isConnected() const { return connected_.load(); }

    // Re-subscribes to cached topics after the broker connection is established.
    void on_connect(int rc) override;
    // Marks the client as disconnected when the broker connection closes.
    void on_disconnect(int rc) override;
    // Forwards subscribed messages to the registered callback.
    void on_message(const struct mosquitto_message* message) override;

private:
    MessageCallback message_cb_;
    std::vector<std::pair<std::string, int>> subscriptions_;
    mutable std::mutex subscriptions_mutex_;
    std::atomic<bool> connected_{false};
};
